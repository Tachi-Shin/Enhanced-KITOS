#include <kernel/module.h>
#include <kernel/error.h>
#include <lib/string.h>
struct header {
    uint8_t ident[16]; uint16_t type, machine; uint32_t version;
    uint64_t entry, phoff, shoff; uint32_t flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};
struct phdr { uint32_t type, flags; uint64_t offset, vaddr, paddr, filesz, memsz, align; };
struct dynamic { int64_t tag; uint64_t value; };
struct rela { uint64_t offset, info; int64_t addend; };
_Static_assert(sizeof(struct header) == 64 && sizeof(struct phdr) == 56 && sizeof(struct rela) == 24, "ELF64 layout");
static bool extent(uint64_t start, uint64_t size, uint64_t end)
{ return start <= end && size <= end - start; }
static const struct module_segment *segment_at(const struct module_plan *plan, uint64_t va, uint64_t length, unsigned int perms, bool file)
{
    for (unsigned int i = 0; i < plan->count; ++i) {
        const struct module_segment *s = &plan->segments[i];
        if (va >= s->address && extent(va - s->address, length, file ? s->file_size : s->memory_size) &&
            (s->permissions & perms) == perms) return s;
    }
    return NULL;
}
static int read_rela(const struct image_reader *reader, const struct module_plan *plan, uint64_t index, struct rela *r)
{
    int rc = reader->read(reader->context, plan->rela_offset + index * sizeof(*r), r, sizeof(*r));
    if (rc) return rc;
    if (r->info != 1027 || (r->offset & 7) || !segment_at(plan, r->offset, 8, VM_WRITE, false) ||
        r->addend < 0 || !segment_at(plan, (uint64_t)r->addend, 1, VM_READ, false)) return -K_ENOEXEC;
    return 0;
}
int module_validate(const struct image_reader *reader, struct module_plan *plan)
{
    if (!reader || !reader->read || !plan) return -K_EINVAL;
    if (reader->size < sizeof(struct header) || reader->size > MODULE_FILE_MAX) return -K_ENOEXEC;
    struct header h; int rc = reader->read(reader->context, 0, &h, sizeof(h));
    if (rc) return rc;
    if (memcmp(h.ident,"\177ELF\2\1\1",7) || h.type != 3 || h.machine != 183 || h.version != 1 || h.flags ||
        h.ehsize != 64 || h.phentsize != 56 || !h.phnum || h.phnum > MODULE_SEGMENTS_MAX ||
        h.phoff < 64 || (h.phoff & 7) || !extent(h.phoff, (uint64_t)h.phnum * 56, reader->size) || (h.entry & 3)) return -K_ENOEXEC;
    *plan = (struct module_plan){.entry=h.entry};
    uint64_t dynamic_offset = 0, dynamic_size = 0, dynamic_va = 0;
    for (unsigned int i = 0; i < h.phnum; ++i) {
        struct phdr p;
        rc = reader->read(reader->context, h.phoff + (uint64_t)i * 56, &p, sizeof(p));
        if (rc) return rc;
        if (p.type == 3 || p.type == 7 || (p.type == 0x6474E551 && (p.flags & 1))) return -K_ENOTSUP;
        if (p.type == 2) {
            if (dynamic_size || !p.filesz || p.filesz % 16 || p.filesz > 4096 || !extent(p.offset,p.filesz,reader->size)) return -K_ENOEXEC;
            dynamic_offset=p.offset; dynamic_size=p.filesz; dynamic_va=p.vaddr;
        }
        if (p.type != 1 || !p.memsz) continue;
        if (p.filesz > p.memsz || !extent(p.offset,p.filesz,reader->size) || !extent(p.vaddr,p.memsz,MODULE_IMAGE_MAX) ||
            !(p.flags & 4) || (p.flags & ~7U) || (p.flags & 3) == 3 ||
            ((p.vaddr ^ p.offset) & 4095) || (p.align > 1 && ((p.align & (p.align - 1)) || ((p.vaddr - p.offset) & (p.align - 1))))) return -K_ENOEXEC;
        uint64_t first=p.vaddr & ~4095ULL, last=(p.vaddr+p.memsz+4095)&~4095ULL;
        for (unsigned int j = 0; j < plan->count; ++j) {
            const struct module_segment *s = &plan->segments[j];
            if (first < ((s->address+s->memory_size+4095)&~4095ULL) && last > (s->address&~4095ULL)) return -K_ENOEXEC;
        }
        unsigned int perms = VM_READ | ((p.flags & 2) ? VM_WRITE : 0) | ((p.flags & 1) ? VM_EXEC : 0);
        plan->segments[plan->count++] = (struct module_segment){p.vaddr,p.offset,p.filesz,p.memsz,perms};
        if (last > plan->end) plan->end=last;
    }
    if (!plan->count || !segment_at(plan,h.entry,64,VM_READ,true) || !dynamic_size) return -K_ENOEXEC;
    const struct module_segment *ds=segment_at(plan,dynamic_va,dynamic_size,VM_READ,true);
    if (!ds || ds->offset+dynamic_va-ds->address != dynamic_offset) return -K_ENOEXEC;
    uint64_t rela_va=0,rela_size=0,rela_entry=0; bool terminated=false;
    unsigned int seen=0;
    for (uint64_t off=0; off<dynamic_size; off+=16) {
        struct dynamic d; rc=reader->read(reader->context,dynamic_offset+off,&d,sizeof(d));
        if (rc) return rc;
        if (!d.tag) { terminated=true; break; }
        if (d.tag==1 || d.tag==2 || d.tag==17 || d.tag==22 || d.tag==23) return -K_ENOTSUP;
        if (d.value && (d.tag==12 || d.tag==13 || (d.tag>=25 && d.tag<=28) ||
                       d.tag==32 || d.tag==33 || (d.tag>=35 && d.tag<=37))) return -K_ENOTSUP;
        if (d.tag>=7 && d.tag<=9) {unsigned int bit=1U<<(d.tag-7);if(seen&bit)return -K_ENOEXEC;seen|=bit;}
        if (d.tag==7) { if (rela_va) return -K_ENOEXEC; rela_va=d.value; }
        if (d.tag==8) { if (rela_size) return -K_ENOEXEC; rela_size=d.value; }
        if (d.tag==9) { if (rela_entry) return -K_ENOEXEC; rela_entry=d.value; }
    }
    if (!terminated || rela_size%24 || rela_size > 4096*24 || (rela_size && (!rela_va || rela_entry!=24))) return -K_ENOEXEC;
    if (rela_size) {
        const struct module_segment *s=segment_at(plan,rela_va,rela_size,VM_READ,true);
        if (!s) return -K_ENOEXEC;
        plan->rela_offset=s->offset+rela_va-s->address; plan->rela_count=rela_size/24;
        for (uint64_t i=0;i<plan->rela_count;++i) { struct rela r; rc=read_rela(reader,plan,i,&r); if (rc) return rc; }
    }
    return 0;
}
int module_image_load(const struct image_reader *reader, const struct module_plan *plan, uint64_t base,
                       const struct module_memory_ops *ops, void *context)
{
    if (!reader || !plan || !ops || !ops->page || !ops->relocate || base > ~0ULL - plan->end) return -K_EINVAL;
    uint8_t page[PAGE_SIZE];
    for (unsigned int i=0;i<plan->count;++i) {
        const struct module_segment *s=&plan->segments[i];
        for (uint64_t va=s->address&~4095ULL; va<((s->address+s->memory_size+4095)&~4095ULL); va+=PAGE_SIZE) {
            memset(page,0,sizeof(page));
            uint64_t begin=va>s->address?va:s->address;
            uint64_t end=va+PAGE_SIZE<s->address+s->file_size?va+PAGE_SIZE:s->address+s->file_size;
            if (end>begin) { int rc=reader->read(reader->context,s->offset+begin-s->address,page+(begin-va),(uint32_t)(end-begin)); if(rc)return rc; }
            int rc=ops->page(context,va,s->permissions,page); if(rc)return rc;
        }
    }
    for (uint64_t i=0;i<plan->rela_count;++i) {
        struct rela r; int rc=read_rela(reader,plan,i,&r); if(rc)return rc;
        rc=ops->relocate(context,r.offset,base+(uint64_t)r.addend); if(rc)return rc;
    }
    return 0;
}
