#include "mem_mgr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rtthread.h>
#include "mem_section.h"

#define LOG_TAG "PSRAM"
#include "log.h"

#define PSRAM_SIZE (6 * 1024 * 1024)

static struct rt_memheap psram_heap;

static uint8_t psram_cache[PSRAM_SIZE] L2_RET_BSS_SECT(mem_mgr_psram_ret_cache);

static int sifli_psram_init(void)
{
    LOG_I("PSRAM cache address: %p, size: %d\n", psram_cache, PSRAM_SIZE);

    rt_err_t result = rt_memheap_init(&psram_heap, "psram", psram_cache, PSRAM_SIZE);
    if (result != RT_EOK)
    {
        LOG_E("PSRAM heap init failed: %d\n", result);
        return -1;
    }
    LOG_I("PSRAM heap init success\n");
    return 0;
}
INIT_PREV_EXPORT(sifli_psram_init);

void *mem_mgr_alloc(size_t size)
{
    void *ptr = rt_memheap_alloc(&psram_heap, size);
    if (!ptr)
    {
        LOG_E("PSRAM alloc failed (size=%d)\n", size);
    }
    return ptr;
}

void mem_mgr_free(void *ptr)
{
    if (ptr)
    {
        rt_memheap_free(ptr);
    }
}

void *mem_mgr_realloc(void *ptr, size_t new_size)
{
    return rt_memheap_realloc(&psram_heap, ptr, new_size);
}

void *mem_mgr_calloc(size_t num, size_t size)
{
    size_t total = num * size;
    void *ptr = mem_mgr_alloc(total);
    if (ptr)
    {
        memset(ptr, 0, total);
    }
    return ptr;
}
