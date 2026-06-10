#include "eos_dev_display.h"
#include <rtthread.h>

#define LOG_TAG "Display"
#include "eos_log.h"

static void _set_brightness(uint8_t brightness)
{
    rt_device_t lcd = rt_device_find("lcd");
    if (lcd)
    {
        rt_device_control(lcd, RTGRAPHIC_CTRL_SET_BRIGHTNESS, &brightness);
    }
}

static void _power_on(void)
{
}

static void _power_off(void)
{
}

const eos_dev_display_ops_t sifli_display_ops = {
    .set_brightness = _set_brightness,
    .power_on       = _power_on,
    .power_off      = _power_off,
};
