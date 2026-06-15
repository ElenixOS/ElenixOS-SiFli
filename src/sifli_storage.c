#include "sifli_storage.h"
#include <stdio.h>
#include <unistd.h>
#include <rtthread.h>
#include <sys/stat.h>
#include "bf0_hal.h"
#include "drv_io.h"
#include "dfs_file.h"
#include "drv_flash.h"

#define LOG_TAG "Storage"
#include "log.h"

#ifndef FS_REGION_START_ADDR
#error "Need to define file system start address"
#endif

#define FS_ROOT   "filesystem"
#define FS_DATA   "fsdata"

static int _mount_fs(const char *dev_name, const char *mount_point)
{
    rt_device_t dev = rt_device_find(dev_name);
    if (!dev)
    {
        LOG_E("MTD device '%s' not found\n", dev_name);
        return RT_ERROR;
    }
    LOG_I("MTD device '%s' registered\n", dev_name);

    if (dfs_mount(dev_name, mount_point, "elm", 0, 0) == 0)
    {
        LOG_I("Mount FAT on %s success\n", mount_point);
        return RT_EOK;
    }

    LOG_I("%s mount failed, formatting...\n", mount_point);
    if (dfs_mkfs("elm", dev_name) == 0)
    {
        LOG_I("Format success, mounting %s again\n", mount_point);
        if (dfs_mount(dev_name, mount_point, "elm", 0, 0) == 0)
        {
            LOG_I("Mount %s success\n", mount_point);
            return RT_EOK;
        }
    }
    LOG_E("Failed to init filesystem on %s\n", mount_point);
    return RT_ERROR;
}

int sifli_storage_init(void)
{
    LOG_I("FS region: 0x%x size: %d\n", FS_REGION_START_ADDR, FS_REGION_SIZE);

    register_mtd_device(FS_REGION_START_ADDR, FS_REGION_SIZE, FS_ROOT);

    if (_mount_fs(FS_ROOT, "/") != RT_EOK)
    {
        return RT_ERROR;
    }

    chdir("/");

#ifdef FS_DATA_REGION_START_ADDR
    LOG_I("FS data region: 0x%x size: %d\n", FS_DATA_REGION_START_ADDR, FS_DATA_REGION_SIZE);
    register_mtd_device(FS_DATA_REGION_START_ADDR, FS_DATA_REGION_SIZE, FS_DATA);

    mkdir("/data", 0);

    _mount_fs(FS_DATA, "/data");
#endif

    return RT_EOK;
}
INIT_ENV_EXPORT(sifli_storage_init);
