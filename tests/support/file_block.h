#ifndef TEST_FILE_BLOCK_H
#define TEST_FILE_BLOCK_H
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <block/block.h>
struct file_context { int fd; int fail_read, fail_write; };
static int disk_init(struct block_device *device) { (void)device; return 0; }
static int disk_read(struct block_device *device, uint64_t lba, uint32_t count, void *buffer)
{
    struct file_context *c = device->private_data;
    if (c->fail_read == 0) return -5;
    if (c->fail_read > 0) --c->fail_read;
    size_t bytes = (size_t)count * device->block_size;
    return pread(c->fd, buffer, bytes, (off_t)(lba * device->block_size)) == (ssize_t)bytes ? 0 : -5;
}
static int disk_write(struct block_device *device, uint64_t lba, uint32_t count, const void *buffer)
{
    struct file_context *c = device->private_data;
    if (c->fail_write == 0) return -5;
    if (c->fail_write > 0) --c->fail_write;
    size_t bytes = (size_t)count * device->block_size;
    return pwrite(c->fd, buffer, bytes, (off_t)(lba * device->block_size)) == (ssize_t)bytes ? 0 : -5;
}
static const struct block_device_ops disk_ops = {disk_init, disk_read, disk_write};
static int disk_open(const char *path, struct block_device *device, struct file_context *context)
{
    struct stat st;
    context->fd = open(path, O_RDWR); context->fail_read = context->fail_write = -1;
    if (context->fd < 0 || fstat(context->fd, &st)) return -1;
    *device = (struct block_device){.name = path, .block_size = 512,
        .block_count = (uint64_t)st.st_size / 512, .ops = &disk_ops, .private_data = context};
    return block_init(device);
}
#endif
