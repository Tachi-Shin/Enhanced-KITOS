#include <kernel/elf.h>
#include <lib/string.h>
int elf_load(const struct image_reader *reader, const struct elf_plan *plan, struct vm_space *space)
{
    /* Validate first; caller owns an initially empty space and destroys it on
     * any failure. Permissions are final when each initialized page is published. */
    uint8_t page[PAGE_SIZE];
    for (unsigned int i = 0; i < plan->count; ++i) {
        const struct elf_segment *s = &plan->segments[i];
        uint64_t end = (s->address + s->memory_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        for (uint64_t va = s->address & ~(PAGE_SIZE - 1); va < end; va += PAGE_SIZE) {
            memset(page, 0, sizeof(page));
            uint64_t begin = va > s->address ? va : s->address;
            uint64_t last = va + PAGE_SIZE < s->address + s->file_size ? va + PAGE_SIZE : s->address + s->file_size;
            if (begin < last) {
                int rc = reader->read(reader->context, s->offset + begin - s->address,
                                      page + (begin - va), (uint32_t)(last - begin));
                if (rc) return rc;
            }
            int rc = vm_map_new(space, va, s->permissions, page, sizeof(page));
            if (rc) return rc;
        }
    }
    return 0;
}
