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
#include "bf0_hal.h"
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

static void nand_scan(int argc, char **argv)
{
    rt_device_t dev = RT_NULL;
    /* Try common flash device names */
    const char *names[] = {"flash4", "flash5", "nand0", "nand1", 0};
    for (int i = 0; names[i]; i++)
    {
        dev = rt_device_find(names[i]);
        if (dev)
            printf("Found: %s  type=%d  user_data=0x%p\n",
                   names[i], dev->type, dev->user_data);
        else
            printf("Not found: %s\n", names[i]);
    }
    dev = rt_device_find("filesyst");
    if (dev)
        printf("filesyst: type=%d flags=0x%x user_data=0x%p\n",
               dev->type, dev->flag, dev->user_data);
}
MSH_CMD_EXPORT(nand_scan, List NAND devices and user_data);

#include "drv_flash.h"

static void _nand_erase_and_format(uint32_t fs_start, uint32_t fs_size, const char *dev_name, const char *mount_point)
{
    extern int dfs_unmount(const char *mount_point);
    extern int dfs_mkfs(const char *type, const char *dev);

    dfs_unmount(mount_point);
    rt_thread_mdelay(200);

    void *h = rt_nand_get_handle(fs_start);
    if (!h) { printf("Not a NAND address\n"); return; }

    uint32_t blk_size = HAL_NAND_BLOCK_SIZE((FLASH_HandleTypeDef *)h);
    uint32_t num_blks = fs_size / blk_size;

    printf("Erasing %s: block size %uKB, total %u blocks, range [0x%x, 0x%x]\n",
           dev_name, blk_size / 1024, num_blks, fs_start, fs_start + fs_size);

    int ok = 0, fail = 0;
    for (uint32_t i = 0; i < num_blks; i++)
    {
        int r = rt_nand_erase_block(fs_start + i * blk_size);
        if (r == 0) ok++;
        else { fail++; printf("  block %u @0x%x FAIL (%d)\n", i, fs_start + i * blk_size, r); }
        if (i % 8 == 0) printf("  %u/%u OK\n", i, num_blks);
    }
    printf("Done: %d OK, %d FAIL\n", ok, fail);

    if (fail == 0)
    {
        printf("Formatting as elm...\n");
        if (dfs_mkfs("elm", dev_name) == 0)
            printf("Format OK. Mount: %s -> %s\n", dev_name, mount_point);
        else
            printf("Format FAILED\n");
    }
}

static void nand_erase_fs(int argc, char **argv)
{
    _nand_erase_and_format(FS_REGION_START_ADDR, FS_REGION_SIZE, "filesyst", "/");
}
MSH_CMD_EXPORT(nand_erase_fs, Erase+format root NAND FS (FAT));

#ifdef FS_DATA_REGION_START_ADDR
static void nand_erase_data(int argc, char **argv)
{
    _nand_erase_and_format(FS_DATA_REGION_START_ADDR, FS_DATA_REGION_SIZE, "fsdata", "/data");
}
MSH_CMD_EXPORT(nand_erase_data, Erase+format /data NAND FS (FAT));
#endif

/* ==================== Flash I/O 诊断命令 ==================== */

static uint32_t _ms(void)
{
    return lv_tick_get();
}

static int _find_test_file(char *out, int maxlen)
{
    /* 扫描常见路径, 找到第一个可用文件 */
    const char *candidates[] = {
        "/.sys/res/img/flash_light.bin",
        "/.sys/res/img/settings.bin",
        "/.sys/res/img/calculator.bin",
        "/.sys/res/img/logo.bin",
        "/.sys/res/img/app.bin",
        NULL
    };
    for (int i = 0; candidates[i]; i++) {
        struct stat st;
        if (stat(candidates[i], &st) == 0 && S_ISREG(st.st_mode)) {
            strncpy(out, candidates[i], maxlen - 1);
            out[maxlen - 1] = '\0';
            return 0;
        }
    }
    /* 还找不到就搜整个 /.sys/res/img 取第一个 */
    eos_dir_t d = eos_fs_opendir("/.sys/res/img");
    if (d) {
        while (eos_fs_readdir(d, out, maxlen) == 0) {
            if (out[0] != '.') {
                char tmp[256];
                snprintf(tmp, sizeof(tmp), "/.sys/res/img/%s", out);
                struct stat st;
                if (stat(tmp, &st) == 0 && S_ISREG(st.st_mode)) {
                    strncpy(out, tmp, maxlen - 1);
                    out[maxlen - 1] = '\0';
                    eos_fs_closedir(d);
                    return 0;
                }
            }
        }
        eos_fs_closedir(d);
    }
    /* 搜 /.sys/** 递归找一个文件 */
    d = eos_fs_opendir("/.sys");
    if (d) {
        while (eos_fs_readdir(d, out, maxlen) == 0) {
            if (out[0] != '.') {
                char sub[256];
                snprintf(sub, sizeof(sub), "/.sys/%s", out);
                struct stat st;
                if (stat(sub, &st) == 0 && S_ISREG(st.st_mode)) {
                    strncpy(out, sub, maxlen - 1);
                    out[maxlen - 1] = '\0';
                    eos_fs_closedir(d);
                    return 0;
                }
            }
        }
        eos_fs_closedir(d);
    }
    return -1;
}

void diag(int argc, char **argv)
{
    static uint8_t s_buf[4096] __attribute__((aligned(64)));
    uint8_t *buf = s_buf;
    static uint8_t s_dst[2048];
    char name[128];
    char test_img[256];
    uint32_t t0, t1;

    /*━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
      D0. 系统时钟与延迟精度诊断
      ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━*/
    printf("\n=== D0. 时钟与延迟诊断 ===\n");
    uint32_t hclk = HAL_RCC_GetHCLKFreq(CORE_ID_DEFAULT);
    printf("HCLK=%u Hz\n", hclk);
    printf("DWT init=%d\n", HAL_DBG_DWT_IsInit());

    uint32_t cyc0 = HAL_DBG_DWT_GetCycles();
    HAL_Delay_us_(1000);
    uint32_t cyc1 = HAL_DBG_DWT_GetCycles();
    printf("HAL_Delay_us_(1000): %u cycles (expect ~%u)\n",
           cyc1 - cyc0, hclk / 1000 * 1000);
    printf("  ≈ %u μs\n", hclk > 0 ? (cyc1 - cyc0) * 1000000 / hclk : 0);

    cyc0 = HAL_DBG_DWT_GetCycles();
    HAL_Delay_us_(20);
    cyc1 = HAL_DBG_DWT_GetCycles();
    printf("HAL_Delay_us_(20): %u cycles (expect ~%u)\n",
           cyc1 - cyc0, hclk / 1000000 * 20);
    printf("  ≈ %u μs\n", hclk > 0 ? (cyc1 - cyc0) * 1000000 / hclk : 0);

    volatile uint32_t sum = 0;
    cyc0 = HAL_DBG_DWT_GetCycles();
    for (int i = 0; i < 100; i++) {
        memcpy(s_dst, s_buf, 2048);
        for (int j = 0; j < 2048 / 4; j++) sum += ((uint32_t *)s_dst)[j];
    }
    cyc1 = HAL_DBG_DWT_GetCycles();
    printf("memcpy(2KB)+sum x100: %u cycles (%u μs avg)  sum=%u\n",
           cyc1 - cyc0, (cyc1 - cyc0) * 1000000 / hclk / 100, sum);

    /* NAND 控制器频率与命令表 (通过 rt_nand_get_handle 访问) */
    FLASH_HandleTypeDef *hflash = (FLASH_HandleTypeDef *)rt_nand_get_handle(FS_REGION_START_ADDR);
    if (hflash) {
        printf("NAND SPI freq: %u Hz\n", hflash->freq);
        if (hflash->ctable) {
            printf("NAND flash_mode=%d manuf=0x%02x dev_id=0x%02x\n",
                   hflash->ctable->flash_mode,
                   hflash->ctable->manuf_id,
                   hflash->ctable->dev_id);
            printf("  PREAD(0x13): data_mode=%d ins_mode=%d\n",
                   hflash->ctable->cmd_cfg[4].data_mode,
                   hflash->ctable->cmd_cfg[4].ins_mode);
            printf("  4READ(0x%02x): data_mode=%d ins_mode=%d dummy=%d\n",
                   hflash->ctable->cmd_cfg[10].cmd,
                   hflash->ctable->cmd_cfg[10].data_mode,
                   hflash->ctable->cmd_cfg[10].ins_mode,
                   hflash->ctable->cmd_cfg[10].dummy_cycle);
        }
    }

    /*━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
      0. 文件系统基本信息
      ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━*/
    printf("\n=== 0. 文件系统基本信息 ===\n");
    printf("FS region: 0x%x size: %d\n", FS_REGION_START_ADDR, FS_REGION_SIZE);

    printf("\n--- 0a. 各目录完整遍历 (含 dot 文件) ---\n");
    const char *scan_dirs[] = {"/", "/.sys", "/.sys/res", "/.sys/res/img", "/data", NULL};
    for (int i = 0; scan_dirs[i]; i++) {
        int all = 0, hide = 0;
        eos_dir_t d = eos_fs_opendir(scan_dirs[i]);
        if (d) {
            while (eos_fs_readdir(d, name, sizeof(name)) == 0) {
                all++;
                if (name[0] == '.') hide++;
            }
            eos_fs_closedir(d);
            printf("  %-20s: %d entries (%d hidden, %d visible)\n",
                   scan_dirs[i], all, hide, all - hide);
        } else {
            printf("  %-20s: FAIL (opendir failed)\n", scan_dirs[i]);
        }
    }

    printf("\n--- 0b. 寻找测试文件 ---\n");
    int has_file = (_find_test_file(test_img, sizeof(test_img)) == 0);
    if (has_file) {
        struct stat st;
        stat(test_img, &st);
        printf("  found: %s (%ld bytes)\n", test_img, (long)st.st_size);
    } else {
        printf("  no image file found, 部分测试将跳过\n");
    }

    /*━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
      1. 裸 NAND 读 (绕过整个 FS 栈, 硬件延迟基线)
      ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━*/
    printf("\n=== 1. 裸 NAND 读 ===\n");

    int total_size = rt_nand_get_total_size(FS_REGION_START_ADDR);
    printf("  NAND total size: %d bytes\n", total_size);

    uint32_t nand_id = rt_nand_read_id(FS_REGION_START_ADDR);
    printf("  NAND ID: 0x%x\n", nand_id);

    /* 1a: 连续地址 bulk read */
    t0 = _ms();
    for (int i = 0; i < 100; i++) {
        rt_nand_read(FS_REGION_START_ADDR, buf, 64);
    }
    t1 = _ms();
    printf("  1a rt_nand_read(64B) x100: %d ms (%d us avg)\n",
           t1 - t0, ((t1 - t0) * 1000) / 100);

    t0 = _ms();
    for (int i = 0; i < 100; i++) {
        rt_nand_read(FS_REGION_START_ADDR, buf, 512);
    }
    t1 = _ms();
    printf("  1b rt_nand_read(512B) x100: %d ms (%d us avg)\n",
           t1 - t0, ((t1 - t0) * 1000) / 100);

    t0 = _ms();
    for (int i = 0; i < 100; i++) {
        rt_nand_read(FS_REGION_START_ADDR, buf, 2048);
    }
    t1 = _ms();
    printf("  1c rt_nand_read(2KB) x100: %d ms (%d us avg)\n",
           t1 - t0, ((t1 - t0) * 1000) / 100);

    /* 1d: 测试不同位置 (随机地址) */
    t0 = _ms();
    for (int i = 0; i < 100; i++) {
        uint32_t off = (i * 7919) % (1024 * 1024);  /* 素数步长, 覆盖不同位置 */
        rt_nand_read(FS_REGION_START_ADDR + off, buf, 64);
    }
    t1 = _ms();
    printf("  1d rt_nand_read(64B,随机偏移) x100: %d ms (%d us avg)\n",
           t1 - t0, ((t1 - t0) * 1000) / 100);

    /* 1e: read page (带 spare 区域) */
    t0 = _ms();
    for (int i = 0; i < 100; i++) {
        rt_nand_read_page(FS_REGION_START_ADDR, buf, 2048, NULL, 0);
    }
    t1 = _ms();
    printf("  1e rt_nand_read_page(2KB) x100: %d ms (%d us avg)\n",
           t1 - t0, ((t1 - t0) * 1000) / 100);

    /*━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
      2. MTD 块设备读 (经 Dhara FTL, 保留 FS 感知)
      ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━*/
    printf("\n=== 2. MTD 块设备读 (512B 扇区) ===\n");

    const char *mtd_names[] = {"filesyst", "filesystem", "fsdata"};
    rt_device_t mtd = NULL;
    for (int i = 0; mtd_names[i]; i++) {
        mtd = rt_device_find(mtd_names[i]);
        if (mtd) {
            printf("  Found MTD: %s\n", mtd_names[i]);
            break;
        }
    }

    if (mtd) {
        /* 2a: sector 0 (通常是 FAT 引导区) */
        t0 = _ms();
        for (int i = 0; i < 100; i++) {
            rt_device_read(mtd, 0, buf, 1);
        }
        t1 = _ms();
        printf("  2a sector 0 x100: %d ms (%d us avg)\n",
               t1 - t0, ((t1 - t0) * 1000) / 100);

        /* 2b: sector 1024 (隔一段距离, 映射到不同 NAND 页) */
        t0 = _ms();
        for (int i = 0; i < 100; i++) {
            rt_device_read(mtd, 1024, buf, 1);
        }
        t1 = _ms();
        printf("  2b sector 1024 x100: %d ms (%d us avg)\n",
               t1 - t0, ((t1 - t0) * 1000) / 100);

        /* 2c: 随机扇区 (评估 FTL 映射查询开销) */
        t0 = _ms();
        for (int i = 0; i < 100; i++) {
            uint32_t sec = (i * 7919) % 4096;
            rt_device_read(mtd, sec, buf, 1);
        }
        t1 = _ms();
        printf("  2c 随机 sector x100: %d ms (%d us avg)\n",
               t1 - t0, ((t1 - t0) * 1000) / 100);

        /* 2d: 连续多扇区 (大块读) */
        t0 = _ms();
        for (int i = 0; i < 20; i++) {
            rt_device_read(mtd, i * 8, buf, 1); /* 读 1 扇区, 连续 */
        }
        t1 = _ms();
        printf("  2d 连续 sector (0,8,16...) x20: %d ms (%d us avg)\n",
               t1 - t0, ((t1 - t0) * 1000) / 20);
    } else {
        printf("  MTD device not found (tried: filesyst, filesystem, fsdata)\n");
    }

    /*━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
      3. 目录操作基准 (不依赖文件存在)
      ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━*/
    printf("\n=== 3. 目录操作基准 ===\n");

    /* 3a: stat 根目录 */
    t0 = _ms();
    for (int i = 0; i < 100; i++) {
        struct stat st;
        stat("/", &st);
    }
    t1 = _ms();
    printf("  3a stat(\"/\") x100: %d ms (%d us avg)\n",
           t1 - t0, ((t1 - t0) * 1000) / 100);

    /* 3b: opendir + 遍历根目录所有条目 */
    t0 = _ms();
    for (int j = 0; j < 20; j++) {
        eos_dir_t d = eos_fs_opendir("/");
        if (d) {
            while (eos_fs_readdir(d, name, sizeof(name)) == 0);
            eos_fs_closedir(d);
        }
    }
    t1 = _ms();
    printf("  3b opendir+遍历+closedir(\"/\") x20: %d ms (%d us avg)\n",
           t1 - t0, ((t1 - t0) * 1000) / 20);

    /* 3c: stat 各层级目录 */
    const char *stat_dirs[] = {"/.sys", "/.sys/res", "/.sys/res/img"};
    for (int i = 0; i < 3; i++) {
        t0 = _ms();
        for (int j = 0; j < 50; j++) {
            struct stat st;
            stat(stat_dirs[i], &st);
        }
        t1 = _ms();
        printf("  3c stat(\"%s\") x50: %d ms (%d us avg)\n",
               stat_dirs[i], t1 - t0, ((t1 - t0) * 1000) / 50);
    }

    /* 3d: opendir+遍历 各层级目录 */
    for (int i = 0; i < 3; i++) {
        t0 = _ms();
        for (int j = 0; j < 20; j++) {
            eos_dir_t d = eos_fs_opendir(stat_dirs[i]);
            if (d) {
                while (eos_fs_readdir(d, name, sizeof(name)) == 0);
                eos_fs_closedir(d);
            }
        }
        t1 = _ms();
        printf("  3d opendir+遍历+closedir(\"%s\") x20: %d ms (%d us avg)\n",
               stat_dirs[i], t1 - t0, ((t1 - t0) * 1000) / 20);
    }

    /*━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
      4. 文件操作 (仅当找到测试文件)
      ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━*/
    printf("\n=== 4. 文件操作 ===\n");
    if (has_file) {
        /* 4a: 完整的 open → close */
        t0 = _ms();
        for (int i = 0; i < 10; i++) {
            int f = open(test_img, O_RDONLY);
            if (f >= 0) close(f);
        }
        t1 = _ms();
        printf("  4a open+close x10: %d ms total, %d ms avg\n",
               t1 - t0, (t1 - t0) / 10);

        /* 4b: 已 open 后 read(64B) + lseek */
        int fd = open(test_img, O_RDONLY);
        if (fd >= 0) {
            t0 = _ms();
            for (int i = 0; i < 100; i++) {
                lseek(fd, 0, SEEK_SET);
                read(fd, buf, 64);
            }
            t1 = _ms();
            printf("  4b open后 read(64B)+lseek x100: %d ms (%d us avg)\n",
                   t1 - t0, ((t1 - t0) * 1000) / 100);
            close(fd);
        }

        /* 4c: 完整读文件 */
        t0 = _ms();
        for (int i = 0; i < 10; i++) {
            int f = open(test_img, O_RDONLY);
            if (f >= 0) {
                uint32_t sz;
                eos_fs_size(f, &sz);
                read(f, buf, sz < 4096 ? sz : 4096);
                close(f);
            }
        }
        t1 = _ms();
        printf("  4c open+read全部+close x10: %d ms (%d ms avg)\n",
               t1 - t0, (t1 - t0) / 10);

        /* 4d: stat 文件 vs 目录 */
        t0 = _ms();
        for (int j = 0; j < 50; j++) {
            struct stat st;
            stat(test_img, &st);
        }
        t1 = _ms();
        printf("  4d stat(文件) x50: %d ms (%d us avg)\n",
               t1 - t0, ((t1 - t0) * 1000) / 50);

        /* 4e: 文件名长度影响 */
        printf("  4e 文件路径: '%s' (%zu 字符)\n",
               test_img, strlen(test_img));
    } else {
        printf("  未找到测试文件, 跳过文件操作测试\n");
        printf("  可先上传任意 .bin 图片到 /.sys/res/img/ 目录\n");
    }

    /*━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
      5. 信息汇总
      ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━*/
    printf("\n=== 5. 信息汇总 ===\n");
    char cwd[128];
    if (getcwd(cwd, sizeof(cwd))) printf("  CWD: %s\n", cwd);

    printf("  NAND 裸读 64B:    (见 1a)\n");
    printf("  NAND 裸读 512B:   (见 1b)\n");
    printf("  NAND 裸读 2KB:    (见 1c)\n");
    printf("  MTD 读 512B:      (见 2a)\n");
    printf("  stat 根目录:       (见 3a)\n");
    printf("  opendir 根目录:    (见 3b)\n");
    if (has_file) {
        printf("  open+close:        (见 4a)\n");
        printf("  read(64B)+lseek:   (见 4b)\n");
    } else {
        printf("  文件测试:         未找到文件已跳过\n");
    }
    printf("\n=== 诊断完成 ===\n");
}
MSH_CMD_EXPORT(diag, Flash I/O diagnostic tests);
