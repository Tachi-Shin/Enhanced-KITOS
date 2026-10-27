#include <block/block.h>
#include <kernel/error.h>

static struct block_device *block_devices[BLOCK_DEVICE_COUNT];

int block_register(struct block_device *device)
{
    if (!device || !device->name || !device->ops) return -K_EINVAL;
    for (unsigned int i = 0; i < BLOCK_DEVICE_COUNT; ++i) {
        if (block_devices[i] == device) return 0;
        if (!block_devices[i]) { block_devices[i] = device; return 0; }
    }
    return -K_ENOSPC;
}

struct block_device *block_get_device(unsigned int index)
{
    if (index >= BLOCK_DEVICE_COUNT) {
        return NULL;
    }
    return block_devices[index];
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
        return 0;
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
    if (block_request_valid(device, lba, count, buffer) != 0 ||
        device->ops->read == NULL) {
        return -K_EINVAL;
    }
    return device->ops->read(device, lba, count, buffer);
}

int block_write(struct block_device *device, uint64_t lba,
                uint32_t count, const void *buffer)
{
    if (block_request_valid(device, lba, count, buffer) != 0 ||
        device->ops->write == NULL) {
        return -K_EINVAL;
    }
    return device->ops->write(device, lba, count, buffer);
}
