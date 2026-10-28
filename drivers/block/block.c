#include <block/block.h>
#include <kernel/error.h>

static struct block_device *block_devices[BLOCK_DEVICE_COUNT];
static uint64_t next_generation;

/* Registry operations never wait: callers retry EBUSY. References pin module
 * storage across preemption, mount attempts and live VFS handles. */
static unsigned int registry_gate;
static bool lock_registry(void){return !__atomic_exchange_n(&registry_gate,1U,__ATOMIC_ACQUIRE);}
static void unlock_registry(void){__atomic_store_n(&registry_gate,0U,__ATOMIC_RELEASE);}
int block_register(struct block_device *device)
{
    if(!device||!device->name||!device->ops)return -K_EINVAL;
    if(!lock_registry())return -K_EBUSY;
    int rc=-K_ENOSPC;unsigned int empty=BLOCK_DEVICE_COUNT;
    for(unsigned int i=0;i<BLOCK_DEVICE_COUNT;++i){
        if(block_devices[i]==device){rc=0;goto out;}
        if(!block_devices[i]&&empty==BLOCK_DEVICE_COUNT)empty=i;
    }
    if(empty<BLOCK_DEVICE_COUNT){device->generation=++next_generation;block_devices[empty]=device;rc=0;}
out:unlock_registry();return rc;
}
int block_unregister(struct block_device *device)
{
    if(!device)return -K_EINVAL;
    if(!lock_registry())return -K_EBUSY;
    int rc=-K_ENOENT;
    if(__atomic_load_n(&device->references,__ATOMIC_ACQUIRE)){rc=-K_EBUSY;goto out;}
    for(unsigned int i=0;i<BLOCK_DEVICE_COUNT;++i)if(block_devices[i]==device){block_devices[i]=NULL;rc=0;break;}
out:unlock_registry();return rc;
}
int block_hold(struct block_device *device)
{
    if(!device)return -K_EINVAL;
    if(!lock_registry())return -K_EBUSY;
    bool found=!device->removable;
    for(unsigned int i=0;i<BLOCK_DEVICE_COUNT;++i)if(block_devices[i]==device)found=true;
    if(found)__atomic_fetch_add(&device->references,1U,__ATOMIC_ACQ_REL);
    unlock_registry();return found?0:-K_ENODEV;
}
void block_put(struct block_device *device)
{if(device)__atomic_fetch_sub(&device->references,1U,__ATOMIC_ACQ_REL);}
struct block_device *block_get_device(unsigned int index)
{return index<BLOCK_DEVICE_COUNT?block_devices[index]:NULL;}
struct block_device *block_get_ref(unsigned int index)
{
    if(!lock_registry())return NULL;
    struct block_device *d=block_get_device(index);
    if(d)__atomic_fetch_add(&d->references,1U,__ATOMIC_ACQ_REL);
    unlock_registry();return d;
}
int block_sync(struct block_device *device)
{
    if(!device||device->offline)return -K_ENODEV;
    return device->ops&&device->ops->sync?device->ops->sync(device):0;
}

static int block_request_valid(struct block_device *device, uint64_t lba,
                               uint32_t count, const void *buffer)
{
    uint64_t end;

    if (device == NULL || device->ops == NULL || buffer == NULL ||
        count == 0U || !device->initialized) {
        return -K_EINVAL;
    }

    end = lba + (uint64_t)count;
    if (end < lba) {
        return -K_EINVAL;
    }
    if (!device->block_size || !device->block_count ||
        (uint64_t)count > (~(size_t)0) / device->block_size || end > device->block_count) {
        return -K_EINVAL;
    }
    return 0;
}

int block_init(struct block_device *device)
{
    int result;

    if (device == NULL || device->ops == NULL || device->ops->init == NULL) {
        return -K_EINVAL;
    }
    if (device->initialized) {
        return device->offline?-K_ENODEV:0;
    }

    result = device->ops->init(device);
    if (result == 0 && (!device->block_size || !device->block_count))
        return -K_ENODEV;
    if (result == 0) {
        device->initialized = true;
    }
    return result;
}

int block_read(struct block_device *device, uint64_t lba,
               uint32_t count, void *buffer)
{
    if(device&&device->offline)return -K_ENODEV;
    if (block_request_valid(device, lba, count, buffer) != 0 ||
        device->ops->read == NULL) {
        return -K_EINVAL;
    }
    return device->ops->read(device, lba, count, buffer);
}

int block_write(struct block_device *device, uint64_t lba,
                uint32_t count, const void *buffer)
{
    if(device&&device->offline)return -K_ENODEV;
    if(device&&device->read_only)return -K_EROFS;
    if (block_request_valid(device, lba, count, buffer) != 0 ||
        device->ops->write == NULL) {
        return -K_EINVAL;
    }
    return device->ops->write(device, lba, count, buffer);
}
