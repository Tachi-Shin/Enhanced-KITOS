#ifndef DOS_BLOCK_BLOCK_H
#define DOS_BLOCK_BLOCK_H

#include <dos/type.h>

#define BLOCK_DEVICE_COUNT 8U
#define BLOCK_SECTOR_SIZE  512U

struct block_device;

struct block_device_ops {
    int (*init)(struct block_device *device);
    int (*read)(struct block_device *device, uint64_t lba,
                uint32_t count, void *buffer);
    int (*write)(struct block_device *device, uint64_t lba,
                 uint32_t count, const void *buffer);
    int (*sync)(struct block_device *device);
};

struct block_device {
    const char *name;
    uint32_t block_size;
    uint64_t block_count;
    const struct block_device_ops *ops;
    void *private_data;
    bool initialized;
    bool removable, offline, read_only;
    unsigned int references;
    uint64_t generation;
};

/* Registered objects live until unregister succeeds. VFS references pin
 * removable devices and their owning module even after physical removal. */
int block_register(struct block_device *device);
int block_unregister(struct block_device *device);
int block_sync(struct block_device *device);
struct block_device *block_get_device(unsigned int index);
/* get_ref/hold pins the device and its module; every success needs block_put. */
struct block_device *block_get_ref(unsigned int index);
int block_hold(struct block_device *);
void block_put(struct block_device *);

int block_init(struct block_device *device);
int block_read(struct block_device *device, uint64_t lba,
               uint32_t count, void *buffer);
int block_write(struct block_device *device, uint64_t lba,
                uint32_t count, const void *buffer);

#endif
