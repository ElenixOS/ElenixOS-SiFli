#ifndef SIFLI_RTC_H
#define SIFLI_RTC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "eos_dev_time.h"

void sifli_rtc_init(void);
void sifli_rtc_set(eos_datetime_t dt);
eos_datetime_t sifli_rtc_get(void);

#ifdef __cplusplus
}
#endif

#endif
