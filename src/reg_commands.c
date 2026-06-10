#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <rtthread.h>
#include <rtdbg.h>
#include <ymodem.h>
#include "board.h"
#include "lvgl.h"
#include "eos_core.h"
#include "eos_log.h"
#include "eos_error.h"
#include "eos_mem.h"
#include "eos_pkg_mgr.h"
#include "eos_app.h"
#include "eos_watchface.h"
#include "eos_service_storage.h"
#include "eos_fs_port.h"
#include "script_engine_core.h"
#include "lv_tiny_ttf.h"
#include "mem_mgr.h"

#define YMODEM_UART_NAME "uart1"

#define INSTALL_THREAD_STACK_SIZE 16384
#define INSTALL_THREAD_PRIORITY 20
#define INSTALL_THREAD_TIMESLICE 20

static int file_fd = -1;
static rt_size_t file_size = 0;
static rt_size_t received_size = 0;

static char *read_file_to_buffer(const char *filename)
{
    eos_file_t file = eos_fs_open_read(filename);
    if (file < 0)
    {
        printf("Cant open file: %s\n", filename);
        return NULL;
    }

    uint32_t fsize = 0;
    eos_fs_size(file, &fsize);
    if (fsize <= 0)
    {
        printf("invalid file sz: %u\n", fsize);
        eos_fs_close(file);
        return NULL;
    }

    size_t alloc_size = fsize + 64;
    char *buffer = (char *)mem_mgr_alloc(alloc_size);
    if (!buffer)
    {
        printf("mem alloc fail! sz: %zu\n", alloc_size);
        eos_fs_close(file);
        return NULL;
    }

    char *aligned_buffer = (char *)(((uintptr_t)buffer + 63) & ~63);

    int bytes_read = eos_fs_read(file, aligned_buffer, fsize);
    eos_fs_close(file);

    if (bytes_read != (int)fsize)
    {
        printf("Incomplete file, read: %d/%u\n", bytes_read, fsize);
        mem_mgr_free(buffer);
        return NULL;
    }

    return aligned_buffer;
}

/* YMODEM callbacks */
static enum rym_code on_begin(struct rym_ctx *ctx, rt_uint8_t *buf, rt_size_t len)
{
    char filename[64] = {0};
    rt_snprintf(filename, sizeof(filename), "/%s", buf);
    printf("Receiving file: %s\n", filename);

    const char *size_str = (const char *)(buf + rt_strlen((const char *)buf) + 1);
    file_size = atoi(size_str);
    received_size = 0;

    if (file_size <= 0 || file_size > (16 * 1024 * 1024))
    {
        printf("Invalid file size: %ld\n", (long)file_size);
        return RYM_CODE_CAN;
    }

    file_fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC);
    if (file_fd < 0)
    {
        printf("Open file %s failed!\n", filename);
        return RYM_CODE_CAN;
    }
    return RYM_CODE_ACK;
}

static enum rym_code on_data(struct rym_ctx *ctx, rt_uint8_t *buf, rt_size_t len)
{
    if (file_fd < 0 || file_size <= 0)
        return RYM_CODE_CAN;

    rt_size_t data_len = len;
    if (received_size + len > file_size)
    {
        data_len = file_size - received_size;
        if (data_len <= 0)
        {
            printf("Data overflow\n");
            return RYM_CODE_CAN;
        }
    }

    if (write(file_fd, buf, data_len) != data_len)
    {
        printf("Write file failed!\n");
        return RYM_CODE_CAN;
    }
    received_size += data_len;
    return RYM_CODE_ACK;
}

static enum rym_code on_end(struct rym_ctx *ctx, rt_uint8_t *buf, rt_size_t len)
{
    if (file_fd >= 0)
    {
        close(file_fd);
        file_fd = -1;
        if (received_size != file_size)
        {
            printf("File size mismatch! received: %u, expected: %u\n",
                   (unsigned)received_size, (unsigned)file_size);
            return RYM_CODE_CAN;
        }
        printf("File transfer complete! (%u bytes)\n", received_size);
    }
    return RYM_CODE_ACK;
}

void yrcv(void)
{
    struct rym_ctx ctx;
    rt_device_t serial = rt_device_find(YMODEM_UART_NAME);
    if (!serial)
    {
        printf("Cant find device: %s\n", YMODEM_UART_NAME);
        return;
    }

    /* 先打开串口，排空 RX 缓冲中的残留数据 */
    if (rt_device_open(serial, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX) != RT_EOK)
    {
        printf("Open device failed\n");
        return;
    }
    /* 丢弃 RX 缓冲中可能残留的旧数据（回显、提示符等） */
    rt_uint8_t trash[64];
    while (rt_device_read(serial, 0, trash, sizeof(trash)) > 0)
    {
        rt_thread_mdelay(10);
    }
    rt_device_close(serial);
    rt_thread_mdelay(50);

    rt_err_t ret = rym_recv_on_device(&ctx, serial,
                                       RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX,
                                       on_begin, on_data, on_end, 20);
    if (ret != RT_EOK)
    {
        printf("\nReceive Error: %d Stage:%d\n", ret, ctx.stage);
    }
}
MSH_CMD_EXPORT(yrcv, YMODEM file receive);

static void app_run_thread_entry(void *parameter)
{
    char *filepath = (char *)parameter;
    char *code_str = read_file_to_buffer(filepath);
    if (!code_str)
    {
        printf("Failed to read file.\n");
        eos_free(filepath);
        return;
    }

    script_pkg_t script = {
        .id = "cn.sab1e.app",
        .name = "AppName",
        .type = SCRIPT_TYPE_APPLICATION,
        .version = "1.0.2",
        .author = "Sab1e",
        .description = "Desc",
        .script_str = code_str,
    };

    script_engine_result_t ret = script_engine_run(&script);
    if (ret == SE_ERR_ALREADY_RUNNING)
    {
        printf("Another application is running!\n");
    }
    eos_free(filepath);
}

void js(int argc, char **argv)
{
    const char *file_name = NULL;
    rt_uint32_t app_stack = 8192;
    rt_uint8_t app_prio = 20;

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--stop") == 0)
        {
            if (script_engine_request_stop() == SE_ERR_SCRIPT_NOT_RUNNING)
                printf("No running application.\n");
            return;
        }
        else if (strcmp(argv[i], "--stack") == 0 && i + 1 < argc)
        {
            app_stack = (rt_uint32_t)atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--atp") == 0 && i + 1 < argc)
        {
            app_prio = (rt_uint8_t)atoi(argv[++i]);
        }
        else if (argv[i][0] != '-')
        {
            file_name = argv[i];
        }
        else
        {
            printf("Unknown option: %s\n", argv[i]);
            return;
        }
    }

    if (!file_name)
    {
        printf("Usage: js <file_name> [--stack N] [--atp P] [--stop]\n");
        return;
    }

    char *file_copy = eos_strdup(file_name);
    if (!file_copy)
    {
        printf("Out of memory\n");
        return;
    }
    rt_thread_t thread = rt_thread_create("app_run",
                                          app_run_thread_entry,
                                          file_copy,
                                          app_stack,
                                          app_prio,
                                          10);
    if (thread)
        rt_thread_startup(thread);
    else
    {
        printf("Failed to create app thread.\n");
        eos_free(file_copy);
    }
}
MSH_CMD_EXPORT(js, Run JavaScript Code in new thread);

void img(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("Usage: img <file_path>\n");
        return;
    }
    lv_obj_t *image = lv_image_create(lv_scr_act());
    lv_image_set_src(image, argv[1]);
    lv_obj_center(image);
    lv_obj_move_foreground(image);
}
MSH_CMD_EXPORT(img, Create a image on active screen);

void pma(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("Usage: pma <size>\n");
        return;
    }
    mem_mgr_alloc(atoi(argv[1]));
}
MSH_CMD_EXPORT(pma, PSRAM Memory Alloc);

void hex(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("Usage: hex <file_path>\n");
        return;
    }
    const char *file_path = argv[1];
    char *data = read_file_to_buffer(file_path);
    if (!data)
    {
        printf("file read fail: %s\n", file_path);
        return;
    }

    eos_file_t f = eos_fs_open_read(file_path);
    uint32_t fsize = 0;
    eos_fs_size(f, &fsize);
    eos_fs_close(f);

    printf("File hex (%u):\n", fsize);
    for (uint32_t i = 0; i < fsize; ++i)
    {
        printf("%02X ", (unsigned char)data[i]);
        if ((i + 1) % 16 == 0)
            printf("\n");
    }
    printf("\n");
    mem_mgr_free((void *)data);
}
MSH_CMD_EXPORT(hex, Print file hex code);

void upk(int argc, char **argv)
{
    if (argc < 4)
    {
        printf("Usage: upk <pkg_path> <output_path> <type>\n"
               "type: App=0 WatchFace=1\n");
        return;
    }
    script_pkg_type_t t = SCRIPT_TYPE_UNKNOWN;
    if (atoi(argv[3]) == 0)
        t = SCRIPT_TYPE_APPLICATION;
    else if (atoi(argv[3]) == 1)
        t = SCRIPT_TYPE_WATCHFACE;
    else
    {
        printf("Error: unknown type\n");
        return;
    }
    eos_result_t ret = eos_pkg_mgr_unpack(argv[1], argv[2], t);
    if (ret != EOS_OK)
        printf("Unpack error. Code: %d\n", ret);
}
MSH_CMD_EXPORT(upk, Unpack EAPK/EWPK package file);

static void eapki_install_thread_entry(void *parameter)
{
    char *pkg_path = (char *)parameter;
    eos_result_t ret = eos_app_install(pkg_path);
    if (ret != EOS_OK)
        rt_kprintf("Install error. Code: %d\n", ret);
    else
        rt_kprintf("App installed: %s\n", pkg_path);
    rt_free(pkg_path);
}

void eapki(int argc, char **argv)
{
    if (argc < 2)
    {
        rt_kprintf("Usage: eapki <pkg_path>\n");
        return;
    }
    char *path_copy = rt_malloc(strlen(argv[1]) + 1);
    if (!path_copy)
    {
        rt_kprintf("malloc failed\n");
        return;
    }
    strcpy(path_copy, argv[1]);
    rt_thread_t tid = rt_thread_create(
        "eapk_install", eapki_install_thread_entry, path_copy,
        INSTALL_THREAD_STACK_SIZE, INSTALL_THREAD_PRIORITY, INSTALL_THREAD_TIMESLICE);
    if (tid)
        rt_thread_startup(tid);
    else
    {
        rt_kprintf("Failed to create install thread\n");
        rt_free(path_copy);
    }
}
MSH_CMD_EXPORT(eapki, Install app package in background thread);

static void ewpki_install_thread_entry(void *parameter)
{
    char *pkg_path = (char *)parameter;
    eos_result_t ret = eos_watchface_install(pkg_path);
    if (ret != EOS_OK)
        rt_kprintf("Install error. Code: %d\n", ret);
    else
        rt_kprintf("Watchface installed: %s\n", pkg_path);
    rt_free(pkg_path);
}

void ewpki(int argc, char **argv)
{
    if (argc < 2)
    {
        rt_kprintf("Usage: ewpki <pkg_path>\n");
        return;
    }
    char *path_copy = rt_malloc(strlen(argv[1]) + 1);
    if (!path_copy)
    {
        rt_kprintf("malloc failed\n");
        return;
    }
    strcpy(path_copy, argv[1]);
    rt_thread_t tid = rt_thread_create(
        "ewpk_install", ewpki_install_thread_entry, path_copy,
        INSTALL_THREAD_STACK_SIZE, INSTALL_THREAD_PRIORITY, INSTALL_THREAD_TIMESLICE);
    if (tid)
        rt_thread_startup(tid);
    else
    {
        rt_kprintf("Failed to create install thread\n");
        rt_free(path_copy);
    }
}
MSH_CMD_EXPORT(ewpki, Install watchface package in background thread);

static void eapk_uninstall_thread_entry(void *parameter)
{
    char *app_id = (char *)parameter;
    eos_result_t ret = eos_app_uninstall(app_id);
    if (ret != EOS_OK)
        rt_kprintf("Uninstall error. Code: %d\n", ret);
    else
        rt_kprintf("App uninstalled: %s\n", app_id);
    rt_free(app_id);
}

void eapku(int argc, char **argv)
{
    if (argc < 2)
    {
        rt_kprintf("Usage: eapku <app_id>\n");
        return;
    }
    char *id_copy = rt_malloc(strlen(argv[1]) + 1);
    if (!id_copy)
    {
        rt_kprintf("malloc failed\n");
        return;
    }
    strcpy(id_copy, argv[1]);
    rt_thread_t tid = rt_thread_create(
        "eapk_uninstall", eapk_uninstall_thread_entry, id_copy,
        INSTALL_THREAD_STACK_SIZE, INSTALL_THREAD_PRIORITY, INSTALL_THREAD_TIMESLICE);
    if (tid)
        rt_thread_startup(tid);
    else
    {
        rt_kprintf("Failed to create uninstall thread\n");
        rt_free(id_copy);
    }
}
MSH_CMD_EXPORT(eapku, Uninstall app in background thread);

static void ewpk_uninstall_thread_entry(void *parameter)
{
    char *wf_id = (char *)parameter;
    eos_result_t ret = eos_watchface_uninstall(wf_id);
    if (ret != EOS_OK)
        rt_kprintf("Uninstall error. Code: %d\n", ret);
    else
        rt_kprintf("Watchface uninstalled: %s\n", wf_id);
    rt_free(wf_id);
}

void ewpku(int argc, char **argv)
{
    if (argc < 2)
    {
        rt_kprintf("Usage: ewpku <watchface_id>\n");
        return;
    }
    char *id_copy = rt_malloc(strlen(argv[1]) + 1);
    if (!id_copy)
    {
        rt_kprintf("malloc failed\n");
        return;
    }
    strcpy(id_copy, argv[1]);
    rt_thread_t tid = rt_thread_create(
        "ewpk_uninstall", ewpk_uninstall_thread_entry, id_copy,
        INSTALL_THREAD_STACK_SIZE, INSTALL_THREAD_PRIORITY, INSTALL_THREAD_TIMESLICE);
    if (tid)
        rt_thread_startup(tid);
    else
    {
        rt_kprintf("Failed to create uninstall thread\n");
        rt_free(id_copy);
    }
}
MSH_CMD_EXPORT(ewpku, Uninstall watchface in background thread);

static void rm_r(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("Usage: rm_r <path>\n");
        return;
    }
    for (int i = 1; i < argc; i++)
    {
        if (eos_storage_rm_recursive(argv[i]) != EOS_OK)
            printf("Failed to remove: %s\n", argv[i]);
    }
}
MSH_CMD_EXPORT(rm_r, Recursively remove files or directories);

static lv_font_t *font_ttf;

static void ttf(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("Usage: ttf <path>\n");
        return;
    }
    font_ttf = lv_tiny_ttf_create_file(argv[1], 24);
    if (font_ttf == NULL)
    {
        LV_LOG_ERROR("Failed to load TTF font!");
        return;
    }
    lv_obj_t *label = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_font(label, font_ttf, 0);
    lv_label_set_text(label, "Hello World!");
}
MSH_CMD_EXPORT(ttf, Load TTF font from file);

static void app_move(int argc, char **argv)
{
    if (argc < 3)
    {
        printf("Usage: app_move <id> <index>\n");
        return;
    }
    printf("Move %s To %d\n", argv[1], atoi(argv[2]));
    eos_app_order_move(argv[1], (size_t)atoi(argv[2]));
}
MSH_CMD_EXPORT(app_move, Move specified app to target index);
