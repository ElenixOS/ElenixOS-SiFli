#include "sifli_device.h"
#include <stdio.h>
#include <stdlib.h>
#include <rtthread.h>
#include "sifli_ble.h"

#define LOG_TAG "Device"
#include "log.h"

#define DEVICE_MGR_MSG_MQ_MAX_MSG 32

static struct rt_messagequeue device_ctrl_mq;
static rt_uint8_t msg_pool[DEVICE_MGR_MSG_MQ_MAX_MSG];

void sifli_device_send(sifli_device_msg_t msg)
{
    rt_mq_send(&device_ctrl_mq, &msg, sizeof(sifli_device_msg_t));
}

static void device_mgr_thread(void *parameter)
{
    sifli_device_msg_t msg;
    while (1)
    {
        if (rt_mq_recv(&device_ctrl_mq, &msg, sizeof(sifli_device_msg_t), RT_WAITING_FOREVER) == RT_EOK)
        {
            switch (msg)
            {
            case SIFLI_DEVICE_MSG_BT_ON:
                LOG_I("BT on");
                sifli_ble_start();
                break;
            case SIFLI_DEVICE_MSG_BT_OFF:
                LOG_I("BT off");
                sifli_ble_stop();
                break;
            default:
                break;
            }
        }
    }
}

void sifli_device_init(void)
{
    sifli_ble_early_init();

    if (rt_mq_init(&device_ctrl_mq, "dev_ctrl_mq", msg_pool,
                   sizeof(sifli_device_msg_t), sizeof(msg_pool),
                   RT_IPC_FLAG_FIFO) != RT_EOK)
    {
        LOG_E("Failed to init message queue\n");
        return;
    }

    rt_thread_t thread = rt_thread_create(
        "device_mgr", device_mgr_thread, NULL, 2048, 25, 20);
    if (thread)
        rt_thread_startup(thread);
    else
        LOG_E("Failed to create device mgr thread\n");
}
