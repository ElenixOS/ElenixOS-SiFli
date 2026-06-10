#ifndef SIFLI_DEVICE_H
#define SIFLI_DEVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "rtthread.h"

typedef enum
{
    SIFLI_DEVICE_MSG_NULL,
    SIFLI_DEVICE_MSG_BT_ON,
    SIFLI_DEVICE_MSG_BT_OFF,
} sifli_device_msg_t;

void sifli_device_send(sifli_device_msg_t msg);
void sifli_device_init(void);

#ifdef __cplusplus
}
#endif

#endif
