#include "eos_dev_vibrator.h"
#include <rtthread.h>

#define LOG_TAG "Vibrator"
#include "eos_log.h"

static void _on(uint8_t strength)  { (void)strength; }
static void _off(void)             {}

const eos_dev_vibrator_ops_t sifli_vibrator_ops = {
    .on  = _on,
    .off = _off,
};
