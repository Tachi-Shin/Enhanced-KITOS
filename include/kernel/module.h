#ifndef KERNEL_MODULE_H
#define KERNEL_MODULE_H
#include <kernel/elf.h>
#define MODULE_IMAGE_MAX (1024U * 1024U)
#define MODULE_FILE_MAX (512U * 1024U)
#define MODULE_SEGMENTS_MAX 16U
#define MODULE_COUNT_MAX 32U
#define MODULE_VIRTUAL_BASE 0xFFFF000100000000ULL
#define MODULE_VIRTUAL_STRIDE 0x200000ULL
struct module_segment { uint64_t address, offset, file_size, memory_size; unsigned int permissions; };
struct module_plan {
    uint64_t entry, end, rela_offset, rela_count;
    unsigned int count;
    struct module_segment segments[MODULE_SEGMENTS_MAX];
};
struct module_memory_ops {
    int (*page)(void *, uint64_t offset, unsigned int permissions, const uint8_t *);
    int (*relocate)(void *, uint64_t offset, uint64_t value);
};
int module_validate(const struct image_reader *, struct module_plan *);
int module_image_load(const struct image_reader *, const struct module_plan *, uint64_t base,
                       const struct module_memory_ops *, void *context);
int module_load(const char *path);
int module_load_memory(const void *, size_t);
int module_bootstrap(void);
int module_embedded(const char *name);
int module_unload(const char *name);
void module_print(void);
void module_autoload(void *);
#endif
