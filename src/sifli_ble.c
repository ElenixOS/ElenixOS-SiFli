#include "sifli_ble.h"
#include <stdio.h>
#include <string.h>
#include <rtthread.h>

#define LOG_TAG "BLE"
#include "log.h"

static sifli_ble_state_t ble_state = SIFLI_BLE_STATE_OFF;

void sifli_ble_send(sifli_ble_msg_t msg)
{
    LOG_I("BLE msg: %d\n", msg);
}

void sifli_ble_early_init(void)
{
}

int sifli_ble_start(void)
{
    ble_state = SIFLI_BLE_STATE_ADVERTISING;
    LOG_I("BLE started\n");
    return RT_EOK;
}

int sifli_ble_stop(void)
{
    ble_state = SIFLI_BLE_STATE_OFF;
    LOG_I("BLE stopped\n");
    return RT_EOK;
}

int sifli_ble_full_stop(void)
{
    return sifli_ble_stop();
}

sifli_ble_state_t sifli_ble_get_state(void)
{
    return ble_state;
}

bool sifli_ble_is_connected(void)
{
    return ble_state == SIFLI_BLE_STATE_CONNECTED;
}

eos_datetime_t sifli_ble_get_current_time(void)
{
    eos_datetime_t dt = {0};
    return dt;
}

void sifli_ble_request_time(uint8_t conn_idx)
{
    (void)conn_idx;
}
