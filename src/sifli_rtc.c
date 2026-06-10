#include "sifli_rtc.h"
#include <stdio.h>
#include <string.h>
#include <rtthread.h>
#include "bf0_hal.h"
#include "bf0_hal_rtc.h"

#define LOG_TAG "RTC"
#include "log.h"

static RTC_HandleTypeDef rtc_handler;

void sifli_rtc_init(void)
{
    rtc_handler.Instance = (RTC_TypeDef *)RTC_BASE;

#ifndef LXT_DISABLE
    if (HAL_RTC_LXT_ENABLED() && HAL_PMU_LXTReady() != HAL_OK)
    {
        LOG_E("LXT not ready\n");
        return;
    }

    rtc_handler.Init.DivAInt = 0x80;
    rtc_handler.Init.DivAFrac = 0x0;
    rtc_handler.Init.DivB = 0x100;
#else
#error "LXT is required"
#endif

    if (HAL_RTC_Init(&rtc_handler, RTC_INIT_NORMAL) != HAL_OK)
    {
        LOG_E("RTC init failed\n");
        return;
    }
    LOG_I("RTC init success\n");
}

void sifli_rtc_set(eos_datetime_t dt)
{
    RTC_TimeTypeDef time = {0};
    RTC_DateTypeDef date = {0};

    time.Seconds = dt.sec;
    time.Minutes = dt.min;
    time.Hours   = dt.hour;
    date.Date    = dt.day;
    date.Month   = dt.month;
    date.Year    = dt.year - 2000;
    date.WeekDay = dt.day_of_week == 0 ? RTC_WEEKDAY_SUNDAY : dt.day_of_week;

    if (HAL_RTC_SetTime(&rtc_handler, &time, RTC_FORMAT_BIN) != HAL_OK)
    {
        LOG_E("RTC set time failed\n");
    }
    if (HAL_RTC_SetDate(&rtc_handler, &date, RTC_FORMAT_BIN) != HAL_OK)
    {
        LOG_E("RTC set date failed\n");
    }
}

eos_datetime_t sifli_rtc_get(void)
{
    RTC_TimeTypeDef time = {0};
    RTC_DateTypeDef date = {0};
    eos_datetime_t dt = {0};

    HAL_RTC_GetTime(&rtc_handler, &time, RTC_FORMAT_BIN);
    while (HAL_RTC_GetDate(&rtc_handler, &date, RTC_FORMAT_BIN) == HAL_ERROR)
    {
        HAL_RTC_GetTime(&rtc_handler, &time, RTC_FORMAT_BIN);
    }

    dt.sec  = time.Seconds;
    dt.min  = time.Minutes;
    dt.hour = time.Hours;
    dt.day  = date.Date;
    dt.month = date.Month;
    dt.year  = date.Year + 2000;
    dt.day_of_week = date.WeekDay;

    return dt;
}
