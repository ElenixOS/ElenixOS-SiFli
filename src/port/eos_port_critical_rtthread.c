#include <rthw.h>
#include "eos_config.h"

#if EOS_RTOS_TYPE == EOS_RTOS_RTTHREAD

#include "eos_port_critical.h"
#include <rtthread.h>
#include "eos_port.h"

eos_critical_ctx_t eos_critical_enter(void)
{
    return (eos_critical_ctx_t)rt_hw_interrupt_disable();
}

void eos_critical_leave(eos_critical_ctx_t ctx)
{
    rt_hw_interrupt_enable((rt_base_t)ctx);
}

#endif
