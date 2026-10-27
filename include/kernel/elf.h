#ifndef KERNEL_ELF_H
#define KERNEL_ELF_H
#include <dos/type.h>
#include <kernel/vm.h>
#define ELF_SEGMENTS_MAX 16U
#define PROCESS_IMAGE_MIN 0x400000ULL
#define PROCESS_IMAGE_END 0x20000000ULL
#define PROCESS_IMAGE_BYTES_MAX (16U * 1024U * 1024U)
struct image_reader {
    void *context;
    uint64_t size;
    int (*read)(void *, uint64_t, void *, uint32_t); /* exact read or negative error */
};
struct elf_segment {
    uint64_t address, offset, file_size, memory_size;
    unsigned int permissions;
};
struct elf_plan {
    uint64_t entry, end;
    unsigned int count;
    struct elf_segment segments[ELF_SEGMENTS_MAX];
};
int elf_validate(const struct image_reader *reader, uint16_t machine, struct elf_plan *out);
int elf_load(const struct image_reader *reader, const struct elf_plan *plan, struct vm_space *space);
#endif
