#include "eos_dev_battery.h"
#include <rtthread.h>

#define LOG_TAG "Battery"
#include "eos_log.h"

static void _request_update(void)
{
}

const eos_battery_dev_ops_t sifli_battery_ops = {
    .request_update = _request_update,
};
