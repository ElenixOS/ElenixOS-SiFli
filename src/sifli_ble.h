#ifndef SIFLI_BLE_H
#define SIFLI_BLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "eos_dev_time.h"

typedef enum
{
    SIFLI_BLE_MSG_OFF,
    SIFLI_BLE_MSG_ON,
} sifli_ble_msg_t;

typedef enum
{
    SIFLI_BLE_STATE_OFF = 0,
    SIFLI_BLE_STATE_STANDBY,
    SIFLI_BLE_STATE_ADVERTISING,
    SIFLI_BLE_STATE_CONNECTED,
    SIFLI_BLE_STATE_SCANNING,
} sifli_ble_state_t;

void sifli_ble_send(sifli_ble_msg_t msg);
void sifli_ble_early_init(void);
int  sifli_ble_start(void);
int  sifli_ble_stop(void);
int  sifli_ble_full_stop(void);
sifli_ble_state_t sifli_ble_get_state(void);
bool sifli_ble_is_connected(void);
eos_datetime_t sifli_ble_get_current_time(void);
void sifli_ble_request_time(uint8_t conn_idx);

#ifdef __cplusplus
}
#endif

#endif
