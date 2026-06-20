#include "eos_port.h"
#include "eos_log.h"
#include "eos_service_cache.h"
#include <rtthread.h>
#include <rthw.h>
#include "sifli_ble.h"
#include "mem_mgr.h"

void eos_delay(uint32_t ms)
{
    rt_thread_mdelay(ms);
}

void eos_cpu_reset(void)
{
    rt_hw_cpu_reset();
}

void *eos_cache_buf_alloc(size_t size)
{
    return mem_mgr_alloc(size);
}

void eos_cache_buf_free(void *ptr)
{
    mem_mgr_free(ptr);
}

void eos_bluetooth_enable(void)
{
    EOS_LOG_D("Bluetooth enable");
    sifli_ble_send(SIFLI_BLE_MSG_ON);
}

void eos_bluetooth_disable(void)
{
    EOS_LOG_D("Bluetooth disable");
    sifli_ble_send(SIFLI_BLE_MSG_OFF);
}

void eos_locate_phone(void)
{
}
