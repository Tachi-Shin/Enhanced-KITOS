#include <kernel/elf.h>
#include <kernel/error.h>
#include <lib/string.h>
struct elf_header {
    uint8_t ident[16]; uint16_t type, machine; uint32_t version;
    uint64_t entry, phoff, shoff; uint32_t flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};
struct program_header {
    uint32_t type, flags;
    uint64_t offset, vaddr, paddr, filesz, memsz, align;
};
_Static_assert(sizeof(struct elf_header) == 64, "ELF64 header");
_Static_assert(sizeof(struct program_header) == 56, "ELF64 program header");
static bool extent(uint64_t start, uint64_t size, uint64_t end)
{ return start <= end && size <= end - start; }
int elf_validate(const struct image_reader *reader, uint16_t machine, struct elf_plan *out)
{
    if (!reader || !reader->read || !out) return -K_EINVAL;
    struct elf_header h;
    if (reader->size < sizeof(h)) return -K_ENOEXEC;
    int rc = reader->read(reader->context, 0, &h, sizeof(h));
    if (rc) return rc;
    if (memcmp(h.ident, "\177ELF", 4) || h.ident[4] != 2 || h.ident[5] != 1 ||
        h.ident[6] != 1 || (h.ident[7] != 0 && h.ident[7] != 255) || h.ident[8] ||
        h.type != 2 || h.machine != machine || h.version != 1 || h.flags ||
        h.ehsize != sizeof(h) || h.phentsize != sizeof(struct program_header) ||
        !h.phnum || h.phnum > ELF_SEGMENTS_MAX || h.phoff < sizeof(h) || (h.phoff & 7) ||
        !extent(h.phoff, (uint64_t)h.phnum * h.phentsize, reader->size) || (h.entry & 3)) return -K_ENOEXEC;
    *out = (struct elf_plan){.entry = h.entry};
    uint64_t pages = 0;
    bool entry_valid = false;
    for (unsigned int i = 0; i < h.phnum; ++i) {
        struct program_header ph;
        rc = reader->read(reader->context, h.phoff + (uint64_t)i * h.phentsize, &ph, sizeof(ph));
        if (rc) return rc;
        if (ph.type == 2 || ph.type == 3 || ph.type == 7) return -K_ENOTSUP; /* dynamic/interp/TLS */
        if (ph.type == 0x6474e551U && (ph.flags & 1)) return -K_ENOEXEC; /* executable stack */
        if (ph.type != 1) continue;
        if (ph.filesz > ph.memsz || !extent(ph.offset, ph.filesz, reader->size) ||
            (ph.flags & ~7U) || !(ph.flags & 4) || (ph.flags & 3) == 3 ||
            (ph.align > 1 && ((ph.align & (ph.align - 1)) || ((ph.vaddr - ph.offset) & (ph.align - 1)))) ||
            ((ph.vaddr ^ ph.offset) & (PAGE_SIZE - 1))) return -K_ENOEXEC;
        if (!ph.memsz) continue;
        if (ph.vaddr < PROCESS_IMAGE_MIN || !extent(ph.vaddr, ph.memsz, PROCESS_IMAGE_END)) return -K_ENOEXEC;
        uint64_t start = ph.vaddr & ~(PAGE_SIZE - 1), end = (ph.vaddr + ph.memsz + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        pages += (end - start) / PAGE_SIZE;
        if (pages > PROCESS_IMAGE_BYTES_MAX / PAGE_SIZE) return -K_ENOMEM;
        for (unsigned int j = 0; j < out->count; ++j) {
            struct elf_segment *s = &out->segments[j];
            uint64_t other_start = s->address & ~(PAGE_SIZE - 1);
            uint64_t other_end = (s->address + s->memory_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            if (start < other_end && end > other_start) return -K_ENOEXEC;
        }
        unsigned int flags = VM_READ | ((ph.flags & 2) ? VM_WRITE : 0) | ((ph.flags & 1) ? VM_EXEC : 0);
        out->segments[out->count++] = (struct elf_segment){ph.vaddr, ph.offset, ph.filesz, ph.memsz, flags};
        if (end > out->end) out->end = end;
        if ((flags & VM_EXEC) && h.entry >= ph.vaddr && h.entry - ph.vaddr < ph.filesz) entry_valid = true;
    }
    return out->count && entry_valid ? 0 : -K_ENOEXEC;
}
