#include "eos_port.h"
#include "eos_log.h"
#include <rtthread.h>
#include <rthw.h>
#include "sifli_ble.h"

void eos_delay(uint32_t ms)
{
    rt_thread_mdelay(ms);
}

void eos_cpu_reset(void)
{
    rt_hw_cpu_reset();
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

void eos_speaker_set_volume(uint8_t volume)
{
    (void)volume;
}

bool eos_speaker_detect(void)
{
    return false;
}

bool eos_microphone_detect(void)
{
    return false;
}

eos_audio_state_t eos_audio_get_state(void)
{
    return EOS_AUDIO_STATE_UNAVAILABLE;
}

int eos_audio_play_file(const char *file_path)
{
    (void)file_path;
    return -1;
}

int eos_audio_stop(void)
{
    return -1;
}
