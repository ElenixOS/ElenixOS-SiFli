#include "sifli_storage.h"
#include <stdio.h>
#include <rtthread.h>
#include "bf0_hal.h"
#include "drv_io.h"
#include "dfs_file.h"
#include "drv_flash.h"

#define LOG_TAG "Storage"
#include "log.h"

#ifndef FS_REGION_START_ADDR
#error "Need to define file system start address"
#endif

#define FS_ROOT "filesystem"

int sifli_storage_init(void)
{
    LOG_I("FS region: 0x%x size: %d\n", FS_REGION_START_ADDR, FS_REGION_SIZE);

    register_mtd_device(FS_REGION_START_ADDR, FS_REGION_SIZE, FS_ROOT);

    rt_device_t dev = rt_device_find(FS_ROOT);
    if (!dev)
    {
        LOG_E("MTD device '%s' not found\n", FS_ROOT);
        return RT_ERROR;
    }
    LOG_I("MTD device '%s' registered\n", FS_ROOT);

    if (dfs_mount(FS_ROOT, "/", "elm", 0, 0) == 0)
    {
        LOG_I("Mount FAT on flash to root success\n");
        return RT_EOK;
    }

    LOG_I("Mount failed, formatting...\n");
    if (dfs_mkfs("elm", FS_ROOT) == 0)
    {
        LOG_I("Format success, mounting again\n");
        if (dfs_mount(FS_ROOT, "/", "elm", 0, 0) == 0)
        {
            LOG_I("Mount success\n");
            return RT_EOK;
        }
    }
    LOG_E("Failed to init file system\n");
    return RT_ERROR;
}
INIT_ENV_EXPORT(sifli_storage_init);
