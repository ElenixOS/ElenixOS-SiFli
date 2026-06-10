#include "eos_dev_time.h"
#include "sifli_rtc.h"

#define LOG_TAG "Time"
#include "eos_log.h"

static eos_datetime_t _get_datetime(void)
{
    return sifli_rtc_get();
}

const eos_dev_time_ops_t sifli_time_ops = {
    .get_datetime = _get_datetime,
};
