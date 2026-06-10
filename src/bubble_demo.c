#include <rtthread.h>
#include "littlevgl2rtt.h"
#include "eos_bubble_grid.h"
#include "eos_config.h"

#define DEMO_BUBBLE_COUNT 20

void bubble_demo_entry(void)
{
    littlevgl2rtt_init("lcd");

    lv_obj_t *grid = eos_bubble_create(lv_scr_act());
    lv_obj_set_size(grid, EOS_DISPLAY_WIDTH, EOS_DISPLAY_HEIGHT);
    lv_obj_center(grid);

    static const uint8_t colors[][3] = {
        {255, 68, 68},    {68, 255, 68},    {68, 68, 255},
        {255, 255, 68},   {255, 68, 255},   {68, 255, 255},
        {255, 136, 68},   {136, 255, 68},   {68, 136, 255},
        {255, 68, 136},   {136, 68, 255},   {68, 255, 136},
        {255, 200, 68},   {68, 200, 255},   {200, 68, 255},
        {200, 255, 68},   {68, 200, 200},   {200, 200, 68},
        {255, 136, 200},  {136, 200, 255},
    };

    for (int i = 0; i < DEMO_BUBBLE_COUNT; i++)
    {
        int ci = i % 20;
        lv_color_t c = lv_color_make(colors[ci][0], colors[ci][1], colors[ci][2]);
        eos_bubble_set_icon_color(grid, i, c);
    }

    while (1)
    {
        uint32_t sleep_ms = lv_timer_handler();
        rt_thread_mdelay(sleep_ms);
    }
}
