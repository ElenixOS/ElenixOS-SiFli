#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>
#include "board.h"
#include "drv_gpio.h"
#include "littlevgl2rtt.h"
#include "button.h"
#include "elenix_os.h"
#include "eos_crown.h"
#include "eos_side_button.h"
#include "eos_dev_display.h"
#include "eos_dev_battery.h"
#include "eos_dev_time.h"
#include "eos_dev_power.h"
#include "eos_dev_vibrator.h"
#include "sifli_rtc.h"
#include "sifli_device.h"
#include "bf0_hal.h"
#include "drv_flash.h"

#define LOG_TAG "main"
#include "log.h"

#define CROWN_UP_PIN   28
#define CROWN_BTN_PIN  150
#define CROWN_DOWN_PIN 30
#define SIDE_BTN_PIN   152

static lv_indev_t *indev_encoder;
static int32_t encoder_diff;
static lv_indev_state_t encoder_state;

extern const eos_dev_display_ops_t  sifli_display_ops;
extern const eos_battery_dev_ops_t  sifli_battery_ops;
extern const eos_dev_time_ops_t     sifli_time_ops;
extern const eos_dev_power_ops_t    sifli_power_ops;
extern const eos_dev_vibrator_ops_t sifli_vibrator_ops;

static void button_event_handler(int32_t pin, button_action_t action)
{
    if (action != BUTTON_CLICKED)
        return;

    LOG_I("Pin: %d clicked", pin);

    if (pin == CROWN_UP_PIN)
    {
        encoder_diff += 1;
    }
    else if (pin == CROWN_DOWN_PIN)
    {
        encoder_diff -= 1;
    }
    else if (pin == CROWN_BTN_PIN)
    {
        eos_crown_button_report(EOS_BUTTON_STATE_CLICKED);
    }
    else if (pin == SIDE_BTN_PIN)
    {
        eos_side_button_report(EOS_BUTTON_STATE_CLICKED);
    }
}

static void encoder_read(lv_indev_t *drv, lv_indev_data_t *data)
{
    data->enc_diff = encoder_diff;
    data->state = encoder_state;
    encoder_diff = 0;
}

static void crown_encoder_init(void)
{
    button_cfg_t cfg;
    int32_t id;

    cfg.pin = CROWN_UP_PIN;
    cfg.active_state = BUTTON_ACTIVE_HIGH;
    cfg.mode = PIN_MODE_INPUT;
    cfg.button_handler = button_event_handler;
    id = button_init(&cfg);
    if (id < 0) { LOG_E("crown_up init fail"); return; }
    button_enable(id);

    cfg.pin = CROWN_DOWN_PIN;
    id = button_init(&cfg);
    if (id < 0) { LOG_E("crown_down init fail"); return; }
    button_enable(id);

    LOG_I("Crown encoder init OK");
}

static void crown_button_init(void)
{
    button_cfg_t cfg;

    cfg.pin = CROWN_BTN_PIN;
    cfg.active_state = BUTTON_ACTIVE_HIGH;
    cfg.mode = PIN_MODE_INPUT;
    cfg.button_handler = button_event_handler;
    int32_t id = button_init(&cfg);
    if (id < 0) { LOG_E("crown_btn init fail"); return; }
    button_enable(id);

    LOG_I("Crown button init OK");
}

static void side_button_init(void)
{
    button_cfg_t cfg;

    cfg.pin = SIDE_BTN_PIN;
    cfg.active_state = BUTTON_ACTIVE_HIGH;
    cfg.mode = PIN_MODE_INPUT;
    cfg.button_handler = button_event_handler;
    int32_t id = button_init(&cfg);
    if (id < 0) { LOG_E("side_btn init fail"); return; }
    button_enable(id);

    LOG_I("Side button init OK");
}

static void lv_port_indev_init(void)
{
    crown_encoder_init();
    crown_button_init();
    side_button_init();

    indev_encoder = lv_indev_create();
    lv_indev_set_type(indev_encoder, LV_INDEV_TYPE_ENCODER);
    lv_indev_set_read_cb(indev_encoder, encoder_read);

    static lv_group_t *g = NULL;
    g = lv_group_create();
    lv_group_set_default(g);
    lv_indev_set_group(indev_encoder, g);
}
extern volatile uint32_t nand_diag_pread_cycles;
extern volatile uint32_t nand_diag_ahb_cycles;
extern volatile uint32_t nand_diag_call_count;
extern volatile uint32_t nand_diag_pread_max;
extern volatile uint32_t nand_diag_ahb_max;

static void nand_speed_test(void)
{
    uint32_t addr = FS_REGION_START_ADDR;
    FLASH_HandleTypeDef *handler;
    uint32_t page_size;
    int page_count = 64;
    uint8_t *buf;
    rt_tick_t start, end, elapsed;
    int i, res;

    nand_diag_pread_cycles = 0;
    nand_diag_ahb_cycles = 0;
    nand_diag_call_count = 0;
    nand_diag_pread_max = 0;
    nand_diag_ahb_max = 0;

    rt_kprintf("\n*** NAND Speed Test ***\n");
    rt_kprintf("Test address: 0x%x\n", addr);

    handler = (FLASH_HandleTypeDef *)rt_nand_get_handle(addr);
    if (handler == NULL)
    {
        rt_kprintf("FAIL: no NAND handler for 0x%x\n", addr);
        return;
    }

    page_size = HAL_NAND_PAGE_SIZE(handler);
    rt_kprintf("Page size: %u, base=0x%x\n", page_size, (unsigned int)handler->base);
    {
        volatile uint32_t *regs = (volatile uint32_t *)handler->Instance;
        rt_kprintf("REG: CR=0x%08x DCR=0x%08x PSCLR=0x%08x\n",
                   (unsigned int)regs[0x00/4], (unsigned int)regs[0x08/4], (unsigned int)regs[0x0C/4]);
        rt_kprintf("REG: SR=0x%08x HCMDR=0x%08x HRCCR=0x%08x\n",
                   (unsigned int)regs[0x10/4], (unsigned int)regs[0x40/4], (unsigned int)regs[0x48/4]);
    }

    buf = rt_malloc(page_size);
    if (buf == NULL)
    {
        rt_kprintf("FAIL: malloc %u bytes\n", page_size);
        return;
    }

    start = rt_tick_get();
    for (i = 0; i < page_count; i++)
    {
        res = rt_nand_read_page(addr + i * page_size, buf, page_size, NULL, 0);
        if (res != (int)page_size)
        {
            rt_kprintf("FAIL: page %d at 0x%x, res=%d\n", i, addr + i * page_size, res);
            rt_kprintf("*** Test Aborted ***\n\n");
            rt_free(buf);
            return;
        }
    }
    end = rt_tick_get();
    elapsed = end - start;

    if (elapsed > 0)
    {
        uint32_t total_bytes = page_count * page_size;
        float speed = (float)total_bytes / 1024.0f / ((float)elapsed / RT_TICK_PER_SECOND);
        rt_kprintf("Read %d pages (%u KB) in %lu ticks\n",
                   page_count, total_bytes / 1024, (unsigned long)elapsed);
        rt_kprintf("Read speed: %.2f KB/s\n", speed);
    }
    else
    {
        rt_kprintf("Read %d pages in < 1 tick\n", page_count);
    }

    if (nand_diag_call_count > 0)
    {
        uint32_t avg_pread = nand_diag_pread_cycles / nand_diag_call_count;
        uint32_t avg_ahb = nand_diag_ahb_cycles / nand_diag_call_count;
        uint32_t total_avg = avg_pread + avg_ahb;
        rt_kprintf("DWT cycles per page: total=%u, pread=%u (max=%u), ahb_copy=%u (max=%u)\n",
                   total_avg, avg_pread, nand_diag_pread_max, avg_ahb, nand_diag_ahb_max);
        rt_kprintf("  -> ~%u us for pread, ~%u us for ahb_copy (est @ 240MHz)\n",
                   avg_pread / 240, avg_ahb / 240);
    }
    {
        volatile uint32_t *dwt_cyccnt = (volatile uint32_t *)0xE0001004;
        volatile uint32_t *regs = (volatile uint32_t *)handler->Instance;
        uint32_t w;
        uint32_t t0, t1;

        rt_kprintf("\n--- Microbenchmark ---\n");

        /* 1. Single word read from AHB window */
        t0 = *dwt_cyccnt;
        w = *(volatile uint32_t *)(handler->base);
        t1 = *dwt_cyccnt;
        rt_kprintf("1x LDR from AHB(0x%x)=0x%08x: %u cycles\n",
                   (unsigned int)handler->base, (unsigned int)w, (unsigned int)(t1 - t0));

        /* 2. 32-word read loop from AHB window */
        t0 = *dwt_cyccnt;
        for (i = 0; i < 512; i++)
            w += *(volatile uint32_t *)(handler->base + (i * 4));
        t1 = *dwt_cyccnt;
        rt_kprintf("512x LDR from AHB (total): %u cycles = ~%u us\n",
                   (unsigned int)(t1 - t0), (unsigned int)((t1 - t0) / 240));
        (void)w;

        /* 3. Memcpy 2KB from SRAM->SRAM (reference) */
        {
            uint8_t sram_src[2048] __attribute__((aligned(4)));
            uint8_t sram_dst[2048] __attribute__((aligned(4)));
            memset(sram_src, 0xA5, sizeof(sram_src));
            t0 = *dwt_cyccnt;
            memcpy(sram_dst, sram_src, 2048);
            t1 = *dwt_cyccnt;
            rt_kprintf("memcpy 2KB SRAM->SRAM: %u cycles = ~%u us\n",
                       (unsigned int)(t1 - t0), (unsigned int)((t1 - t0) / 240));
        }
        rt_kprintf("--- End Microbenchmark ---\n\n");
    }

    rt_free(buf);
    rt_kprintf("*** NAND Test Done ***\n\n");
}

#ifdef BUBBLE_DEMO
#include "bubble_demo.h"

int main(void)
{
    sifli_rtc_init();
    bubble_demo_entry();
    return 0;
}
#else
int main(void)
{
    sifli_rtc_init();

    littlevgl2rtt_init("lcd");

    lv_port_indev_init();

    sifli_device_init();

    eos_dev_display_register(&sifli_display_ops);
    eos_dev_battery_register(&sifli_battery_ops, 300);
    eos_dev_time_register(&sifli_time_ops);
    eos_dev_power_register(&sifli_power_ops);
    eos_dev_vibrator_register(&sifli_vibrator_ops);

    nand_speed_test();

    eos_init();

    while (1)
    {
        uint32_t ms = eos_main_loop();
        rt_thread_mdelay(ms);
    }
}
#endif
