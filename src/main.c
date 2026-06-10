#include <rtthread.h>
#include <rtdevice.h>
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

    eos_init();

    while (1)
    {
        uint32_t ms = eos_main_loop();
        rt_thread_mdelay(ms);
    }
}
#endif
