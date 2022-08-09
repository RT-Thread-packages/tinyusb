#include <tusb.h>

#include <string.h>

#include <rtthread.h>
#include <rtdevice.h>
#include <rthw.h>
#include <dfs_fs.h>

#define DBG_TAG "udisk"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

#define MIN(A, B) ((A) < (B) ? (A) : (B))

#define MAX_PARTITION_COUNT      1
#define SCSI_TIMEOUT             PKG_TINYUSB_HOST_MSC_SCSI_TIMEOUT
#define UDISK_MOUNTPOINT         PKG_TINYUSB_HOST_MSC_MOUNT_POINT

#define UDISK_EVENT_READ10_CPLT  0x01
#define UDISK_EVENT_WRITE10_CPLT 0x02

typedef struct upart
{
    uint32_t block_size;
    struct dfs_partition dfs_part;
    struct rt_device dev;
} upart_t;
static upart_t _upart[MAX_PARTITION_COUNT];
static uint8_t _dev_count;
static uint8_t _dev_addr;
static rt_mutex_t _lock;
static rt_event_t _udisk_event;

static CFG_TUSB_MEM_SECTION CFG_TUSB_MEM_ALIGN scsi_inquiry_resp_t inquiry_resp;
static CFG_TUSB_MEM_SECTION CFG_TUSB_MEM_ALIGN uint8_t sector_buf[PKG_TINYUSB_HOST_MSC_BUFSIZE];
static const rt_size_t MAX_PACKET_SIZE = sizeof(sector_buf) / SECTOR_SIZE;

static bool read10_complete_cb(uint8_t dev_addr, msc_cbw_t const *cbw, msc_csw_t const *csw)
{
    (void)cbw;

    if (csw->status != 0)
    {
        LOG_E("READ10 failed");
        return false;
    }

    rt_event_send(_udisk_event, UDISK_EVENT_READ10_CPLT);
    return true;
}

static bool write10_complete_cb(uint8_t dev_addr, msc_cbw_t const *cbw, msc_csw_t const *csw)
{
    (void)cbw;

    if (csw->status != 0)
    {
        LOG_E("WRITE10 failed");
        return false;
    }

    rt_event_send(_udisk_event, UDISK_EVENT_WRITE10_CPLT);
    return true;
}

static rt_err_t udisk_init(rt_device_t dev)
{
    return RT_EOK;
}

static rt_size_t udisk_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    rt_err_t ret;
    upart_t *part;
    rt_size_t read_size;

    /* check parameter */
    RT_ASSERT(dev != RT_NULL);
    RT_ASSERT(buffer != RT_NULL);

    rt_mutex_take(_lock, RT_WAITING_FOREVER);

    part = (upart_t *)dev->user_data;

    read_size = 0;
    while (size)
    {
        rt_size_t packet_size = MIN(size, MAX_PACKET_SIZE);
        tuh_msc_read10(_dev_addr, 0, sector_buf, pos + part->dfs_part.offset + read_size, packet_size, read10_complete_cb);
        ret = rt_event_recv(_udisk_event, UDISK_EVENT_READ10_CPLT, RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR, SCSI_TIMEOUT, RT_NULL);

        if (ret == RT_EOK)
        {
            rt_memcpy(buffer + read_size * SECTOR_SIZE, sector_buf, SECTOR_SIZE * packet_size);
            read_size += packet_size;
            size -= packet_size;
        }
        else
        {
            break;
        }
    }
    rt_mutex_release(_lock);
    return read_size;
}

static rt_size_t udisk_write(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size)
{
    rt_err_t ret;
    upart_t *part;
    rt_size_t sent_size;

    /* check parameter */
    RT_ASSERT(dev != RT_NULL);
    RT_ASSERT(buffer != RT_NULL);

    rt_mutex_take(_lock, RT_WAITING_FOREVER);

    part = (upart_t *)dev->user_data;
    sent_size = 0;
    while (size)
    {
        rt_size_t packet_size = MIN(size, MAX_PACKET_SIZE);
        rt_memcpy(sector_buf, buffer + sent_size * SECTOR_SIZE, packet_size * SECTOR_SIZE);
        tuh_msc_write10(_dev_addr, 0, sector_buf, pos + part->dfs_part.offset + sent_size, packet_size, write10_complete_cb);
        ret = rt_event_recv(_udisk_event, UDISK_EVENT_WRITE10_CPLT, RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR, SCSI_TIMEOUT, RT_NULL);

        if (ret == RT_EOK)
        {
            sent_size += packet_size;
            size -= packet_size;
        }
        else
        {
            break;
        }
    }

    rt_mutex_release(_lock);

    if (ret != RT_EOK)
    {
        return 0;
    }

    return sent_size;
}

static rt_err_t udisk_control(rt_device_t dev, int cmd, void *args)
{
    rt_err_t ret;
    upart_t *part;

    /* check parameter */
    RT_ASSERT(dev != RT_NULL);

    part = (upart_t *)dev->user_data;

    if (cmd == RT_DEVICE_CTRL_BLK_GETGEOME)
    {
        struct rt_device_blk_geometry *geometry;

        geometry = (struct rt_device_blk_geometry *)args;
        if (geometry == RT_NULL)
            return -RT_ERROR;

        geometry->bytes_per_sector = SECTOR_SIZE;
        geometry->block_size = part->block_size;
        geometry->sector_count = part->dfs_part.size;
    }

    return RT_EOK;
}

#ifdef RT_USING_DEVICE_OPS
const static struct rt_device_ops udisk_device_ops =
    {
        udisk_init,
        RT_NULL,
        RT_NULL,
        udisk_read,
        udisk_write,
        udisk_control};
#endif

static rt_err_t register_device(upart_t *upart, const char *device_name)
{
    upart->block_size = sizeof(sector_buf);

    /* register sd card device */
    upart->dev.type = RT_Device_Class_Block;
#ifdef RT_USING_DEVICE_OPS
    stor->dev[i].ops = &udisk_device_ops;
#else
    upart->dev.init = udisk_init;
    upart->dev.read = udisk_read;
    upart->dev.write = udisk_write;
    upart->dev.control = udisk_control;
#endif
    upart->dev.user_data = (void *)upart;

    return rt_device_register(&upart->dev, device_name, RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_REMOVABLE | RT_DEVICE_FLAG_STANDALONE);
}

/**
 * This function will run udisk driver when usb disk is detected.
 *
 * @param intf the usb interface instance.
 *
 * @return the error code, RT_EOK on successfully.
 */
static void udisk_run(void *parameter)
{
    rt_err_t ret;
    uint32_t block_count;
    uint32_t block_size;
    char dname[8];

    block_count = tuh_msc_get_block_count(_dev_addr, 0);
    block_size = tuh_msc_get_block_size(_dev_addr, 0);

    LOG_I("capacity %lu MB, block size %d",
          block_count / ((1024 * 1024) / block_size),
          block_size);

    LOG_D("read partition table");

    for (int i = 0; i < MAX_PARTITION_COUNT; i++)
    {
        /* get the partition table */
        tuh_msc_read10(_dev_addr, 0, sector_buf, 0, 1, read10_complete_cb);
        ret = rt_event_recv(_udisk_event, UDISK_EVENT_READ10_CPLT, RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, SCSI_TIMEOUT, RT_NULL);
        if (ret != RT_EOK)
        {
            LOG_E("read partition table error");
            break;
        }
        /* get the first partition */
        ret = dfs_filesystem_get_partition(&_upart[i].dfs_part, sector_buf, i);

        if (ret == RT_EOK)
        {
            rt_snprintf(dname, 7, "ud%d%d", _dev_addr, i);
            ret = register_device(_upart + i, dname);
            if (ret == RT_EOK)
            {
                LOG_I("udisk part %d register successfully", i);
            }
            else
            {
                LOG_E("udisk part %d registerfailed: %d", i, ret);
            }
            _dev_count++;
        }
        else
        {
            /* there is no partition */
            if (i == 0)
            {
                rt_snprintf(dname, 8, "ud%d", _dev_addr);
                _upart[0].dfs_part.offset = 0;
                _upart[0].dfs_part.size = 0;
                ret = register_device(_upart, dname);
                if (ret == RT_EOK)
                {
                    LOG_I("udisk register successfully", 0);
                }
                else
                {
                    LOG_E("udisk register failed: %d", 0, rt_get_errno());
                }
                _dev_count = 1;
            }
            break;
        }
    }

    return;
}

void tuh_msc_mount_cb(uint8_t dev_addr)
{
    rt_thread_t udisk_thread;

    LOG_I("A MassStorage device is mounted");

    _udisk_event = rt_event_create("udisk", RT_IPC_FLAG_PRIO);
    _lock = rt_mutex_create("udisk", RT_IPC_FLAG_PRIO);
    if (!_udisk_event || !_lock)
    {
        LOG_E("init failed: cannot create mutex or event");
        return;
    }

    _dev_addr = dev_addr;
    udisk_thread = rt_thread_create("udisk", udisk_run, &_dev_addr, 4096, PKG_TINYUSB_THREAD_PRIORITY, 10);
    rt_thread_startup(udisk_thread);
}

void tuh_msc_umount_cb(uint8_t dev_addr)
{
    (void)dev_addr;

    rt_base_t level = rt_hw_interrupt_disable();

    LOG_I("A MassStorage device is unmounted");

    if (_udisk_event)
    {
        rt_event_delete(_udisk_event);
        _udisk_event = RT_NULL;
    }
    if (_lock)
    {
        rt_mutex_delete(_lock);
        _lock = RT_NULL;
    }

    for (int i = 0; i < _dev_count; i++)
    {
        rt_device_unregister(&_upart[i].dev);
    }

    _dev_count = 0;

    rt_hw_interrupt_enable(level);
}
