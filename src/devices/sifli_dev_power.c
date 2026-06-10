#include "eos_dev_power.h"
#include <rtthread.h>

#define LOG_TAG "Power"
#include "eos_log.h"

static int _set_power(dev_power_state_t state)
{
    (void)state;
    return 0;
}

const eos_dev_power_ops_t sifli_power_ops = {
    .set_power = _set_power,
};
