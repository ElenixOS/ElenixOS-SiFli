#include "eos_dev_sensor.h"
#include <rtthread.h>

#define LOG_TAG "Sensor"
#include "eos_log.h"

static void _init(eos_dev_sensor_t *dev)       { (void)dev; }
static void _deinit(eos_dev_sensor_t *dev)     { (void)dev; }
static void _enable(eos_dev_sensor_t *dev)     { (void)dev; }
static void _disable(eos_dev_sensor_t *dev)    { (void)dev; }
static void _set_sample_rate(eos_dev_sensor_t *dev, uint32_t hz) { (void)dev; (void)hz; }
static void _get_sample_rate(eos_dev_sensor_t *dev, uint32_t *hz) { (void)dev; *hz = 0; }

static const eos_dev_sensor_ops_t sifli_sensor_ops = {
    .init           = _init,
    .deinit         = _deinit,
    .enable         = _enable,
    .disable        = _disable,
    .set_sample_rate = _set_sample_rate,
    .get_sample_rate = _get_sample_rate,
};
