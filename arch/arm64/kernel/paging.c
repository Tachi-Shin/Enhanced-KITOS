#include <arch/memory.h>
#include <arch/irq.h>
#include <kernel/vm.h>
#include <kernel/error.h>
#include <lib/string.h>

#define PTE_VALID 1ULL
#define PTE_TABLE 3ULL
#define PTE_ADDRESS 0x0000FFFFFFFFF000ULL
#define PTE_USER (1ULL << 6)
#define PTE_RO (1ULL << 7)
#define PTE_SH_INNER (3ULL << 8)
#define PTE_AF (1ULL << 10)
#define PTE_NG (1ULL << 11)
#define PTE_PXN (1ULL << 53)
#define PTE_UXN (1ULL << 54)
#define PTE_DEVICE (1ULL << 2)

static uint64_t kernel_root, empty_root, active_user;
static bool final_tables;

void arch_tlb_flush(void)
{
    __asm__ volatile("dsb ishst; tlbi vmalle1; dsb ish; isb" ::: "memory");
}
void arch_user_table_switch(uint64_t root)
{
    if (!final_tables) return;
    uint64_t selected = root ? root : empty_root;
    /* Single CPU, ASID 0. Invalidate before any EL0 return or uaccess. Future
     * ASIDs/SMP must replace this with scoped invalidation and shootdowns. */
    __asm__ volatile("dsb ishst; msr ttbr0_el1, %0; isb" :: "r"(selected) : "memory");
    arch_tlb_flush();
    active_user = root;
}
void vm_activate(const struct vm_space *space)
{ arch_user_table_switch(space ? space->root : 0); }

void arch_sync_code(void *address, size_t length)
{
    uint64_t ctr;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    uintptr_t line = 4UL << ((ctr >> 16) & 15), start = (uintptr_t)address;
    for (uintptr_t p = start & ~(line - 1); p < start + length; p += line)
        __asm__ volatile("dc cvau, %0" :: "r"(p) : "memory");
    /* The user VA differs from the writable kernel alias. Invalidate all I-cache
     * entries on this CPU rather than assuming a VA-indexing arrangement. */
    __asm__ volatile("dsb ish; ic iallu; dsb ish; isb" ::: "memory");
}

static uint64_t new_table(void)
{
    uint64_t page = page_alloc();
    if (page) memset(arch_phys_to_virt(page), 0, PAGE_SIZE);
    return page;
}
static unsigned int index_at(uint64_t va, unsigned int level)
{ return (unsigned int)((va >> (39 - 9 * level)) & 511); }
static uint64_t *table(uint64_t physical)
{ return arch_phys_to_virt(physical & PTE_ADDRESS); }
static uint64_t *walk(uint64_t root, uint64_t address, bool create, int *error)
{
    uint64_t *entries = table(root);
    for (unsigned int level = 0; level < 3; ++level) {
        uint64_t *slot = &entries[index_at(address, level)];
        if (!(*slot & PTE_VALID)) {
            if (!create) { *error = -K_ENOENT; return NULL; }
            uint64_t page = new_table();
            if (!page) { *error = -K_ENOMEM; return NULL; }
            __asm__ volatile("dsb ishst" ::: "memory");
            *slot = page | PTE_TABLE;
        }
        if ((*slot & 3) != PTE_TABLE) { *error = -K_EINVAL; return NULL; }
        entries = table(*slot);
    }
    *error = 0;
    return &entries[index_at(address, 3)];
}
static bool table_empty(uint64_t physical)
{
    uint64_t *entries = table(physical);
    for (unsigned int i = 0; i < 512; ++i) if (entries[i]) return false;
    return true;
}
static void prune(uint64_t root, uint64_t address)
{
    uint64_t *slots[3]; unsigned int depth = 0;
    uint64_t *entries = table(root);
    for (unsigned int level = 0; level < 3; ++level) {
        uint64_t *slot = &entries[index_at(address, level)];
        if (!(*slot & PTE_VALID)) break;
        slots[depth++] = slot; entries = table(*slot);
    }
    while (depth) {
        uint64_t *slot = slots[--depth], page = *slot & PTE_ADDRESS;
        if (!table_empty(page)) break;
        *slot = 0;
        arch_tlb_flush(); /* Never free a page still reachable by a walker. */
        (void)page_free(page);
    }
}
static int map(uint64_t root, uint64_t va, uint64_t pa, uint64_t attributes)
{
    int rc;
    uint64_t *entry = walk(root, va, true, &rc);
    if (!entry) { prune(root, va); return rc; }
    if (*entry & PTE_VALID) return -K_EEXIST;
    *entry = pa | attributes | PTE_TABLE | PTE_AF;
    return 0;
}

int vm_space_create(struct vm_space *space)
{
    if (!space || space->root) return -K_EINVAL;
    space->root = new_table();
    return space->root ? 0 : -K_ENOMEM;
}
static void destroy_table(uint64_t physical, unsigned int level)
{
    uint64_t *entries = table(physical);
    for (unsigned int i = 0; i < 512; ++i) {
        if (!(entries[i] & PTE_VALID)) continue;
        uint64_t page = entries[i] & PTE_ADDRESS;
        if (level < 3) destroy_table(page, level + 1);
        else (void)page_free(page);
    }
    (void)page_free(physical);
}
void vm_space_destroy(struct vm_space *space)
{
    if (!space || !space->root || space->root == active_user) return;
    /* No task may activate this space while it is being destroyed. */
    destroy_table(space->root, 0);
    space->root = 0;
}
int vm_map_new(struct vm_space *space, uint64_t address, unsigned int permissions,
               const void *initial, size_t length)
{
    if (!space || !space->root || !vm_user_range(address, PAGE_SIZE) ||
        address % PAGE_SIZE || length > PAGE_SIZE || (length && !initial) ||
        !(permissions & VM_READ) || (permissions & ~(VM_READ | VM_WRITE | VM_EXEC)) ||
        ((permissions & (VM_WRITE | VM_EXEC)) == (VM_WRITE | VM_EXEC))) return -K_EINVAL;
    uint64_t page = page_alloc();
    if (!page) return -K_ENOMEM;
    void *memory = arch_phys_to_virt(page);
    memset(memory, 0, PAGE_SIZE);
    if (length) memcpy(memory, initial, length);
    if (permissions & VM_EXEC) arch_sync_code(memory, PAGE_SIZE);
    uint64_t attrs = PTE_USER | PTE_NG | PTE_SH_INNER | PTE_PXN;
    if (!(permissions & VM_WRITE)) attrs |= PTE_RO;
    if (!(permissions & VM_EXEC)) attrs |= PTE_UXN;
    int rc = map(space->root, address, page, attrs);
    if (rc) (void)page_free(page);
    arch_tlb_flush();
    return rc;
}
int vm_query(const struct vm_space *space, uint64_t address, struct vm_mapping *out)
{
    if (!space || !space->root || !out || !vm_user_range(address, 1)) return -K_EFAULT;
    int rc;
    uint64_t *entry = walk(space->root, address, false, &rc);
    if (!entry || !(*entry & PTE_VALID) || !(*entry & PTE_USER)) return -K_EFAULT;
    out->physical = (*entry & PTE_ADDRESS) + address % PAGE_SIZE;
    out->permissions = VM_READ;
    if (!(*entry & PTE_RO)) out->permissions |= VM_WRITE;
    if (!(*entry & PTE_UXN)) out->permissions |= VM_EXEC;
    return 0;
}
int vm_unmap(struct vm_space *space, uint64_t address)
{
    if (!space || !space->root || address % PAGE_SIZE || !vm_user_range(address, PAGE_SIZE)) return -K_EINVAL;
    int rc;
    uint64_t *entry = walk(space->root, address, false, &rc);
    if (!entry || !(*entry & PTE_VALID)) return -K_ENOENT;
    uint64_t page = *entry & PTE_ADDRESS;
    *entry = 0;
    arch_tlb_flush();
    (void)page_free(page);
    prune(space->root, address);
    return 0;
}
static bool intersects(uint64_t page, struct memory_range range)
{ return range.size && page < range.base + range.size && page + PAGE_SIZE > range.base; }

int arch_kernel_memory_init(struct memory_range ram,
    const struct memory_range *reserved, unsigned int count,
    const struct memory_range *mmio, unsigned int mmio_count)
{
    extern uint8_t __text_start[], __text_end[], __rodata_start[], __rodata_end[];
    extern uint8_t __kernel_start[], __kernel_end[], __boot_stack_guard[];
    if (final_tables || ram.base || ram.size > 0xC0000000ULL) return -K_EINVAL;
    kernel_root = new_table(); empty_root = new_table();
    if (!kernel_root || !empty_root) return -K_ENOMEM;
    uint64_t kernel_start = arch_virt_to_phys(__kernel_start);
    uint64_t kernel_end = arch_virt_to_phys(__kernel_end);
    for (uint64_t pa = kernel_start; pa + PAGE_SIZE <= ram.size; pa += PAGE_SIZE) {
        uintptr_t va = (uintptr_t)arch_phys_to_virt(pa);
        bool omit = false;
        for (unsigned int r = 0; r < count; ++r) if (intersects(pa, reserved[r])) {
            /* A firmware-owned region must never overlap the loaded kernel. */
            if (pa < kernel_end) return -K_EINVAL;
            omit = true; break;
        }
        if (omit || va == (uintptr_t)__boot_stack_guard) continue;
        uint64_t attrs = PTE_SH_INNER | PTE_UXN | PTE_PXN;
        if (va >= (uintptr_t)__text_start && va < (uintptr_t)__text_end)
            attrs = PTE_SH_INNER | PTE_UXN | PTE_RO;
        else if (va >= (uintptr_t)__rodata_start && va < (uintptr_t)__rodata_end)
            attrs |= PTE_RO;
        int rc = map(kernel_root, va, pa, attrs);
        if (rc) return rc;
    }
    for (unsigned int r = 0; r < mmio_count; ++r) {
        if ((mmio[r].base | mmio[r].size) % PAGE_SIZE) return -K_EINVAL;
        for (uint64_t offset = 0; offset < mmio[r].size; offset += PAGE_SIZE) {
            uint64_t pa = mmio[r].base + offset;
            int rc = map(kernel_root, (uintptr_t)arch_phys_to_virt(pa), pa,
                         PTE_DEVICE | PTE_PXN | PTE_UXN);
            if (rc) return rc;
        }
    }
    /* Already executing with high PC, high SP and high VBAR. Replace the shared
     * bootstrap tables, invalidate both regimes, then enable caches and WXN. */
    __asm__ volatile("dsb sy; msr ttbr1_el1, %0; msr ttbr0_el1, %1; isb"
                     :: "r"(kernel_root), "r"(empty_root) : "memory");
    arch_tlb_flush();
    uint64_t sctlr;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1ULL << 2) | (1ULL << 12) | (1ULL << 19);
    __asm__ volatile("ic iallu; dsb sy; isb; msr sctlr_el1, %0; isb"
                     :: "r"(sctlr) : "memory");
    final_tables = true;
    return 0;
}
int arch_kernel_guard(void *page)
{
    uintptr_t va = (uintptr_t)page;
    if (!final_tables || va < KERNEL_VA_BASE || va % PAGE_SIZE) return -K_EINVAL;
    int rc;
    uint64_t *entry = walk(kernel_root, va, false, &rc);
    if (!entry || !(*entry & PTE_VALID)) return -K_ENOENT;
    *entry = 0;
    arch_tlb_flush();
    return 0; /* Static stack reservation remains owned by the kernel. */
}
static bool translation_fault(uintptr_t va, bool write, bool user)
{
    uint64_t par;
    if (user) __asm__ volatile("at s1e0r, %0" :: "r"(va) : "memory");
    else if (write) __asm__ volatile("at s1e1w, %0" :: "r"(va) : "memory");
    else __asm__ volatile("at s1e1r, %0" :: "r"(va) : "memory");
    __asm__ volatile("isb; mrs %0, par_el1" : "=r"(par) :: "memory");
    return (par & 1) != 0;
}
int arch_memory_check(void)
{
    extern uint8_t __text_start[], __rodata_start[], __kernel_end[], __boot_stack_guard[];
    int rc;
    uint64_t sctlr, ttbr0, ttbr1;
    __asm__ volatile("mrs %0, sctlr_el1; mrs %1, ttbr0_el1; mrs %2, ttbr1_el1"
                     : "=r"(sctlr), "=r"(ttbr0), "=r"(ttbr1));
    if (!final_tables || !(sctlr & 1) || !(sctlr & (1ULL << 19)) ||
        ttbr1 != kernel_root || ttbr0 != empty_root) return -K_EINVAL;
    const void *addresses[] = {__text_start, __rodata_start, (void *)((uintptr_t)__kernel_end - PAGE_SIZE)};
    const uint64_t expected[] = {PTE_RO | PTE_UXN, PTE_RO | PTE_UXN | PTE_PXN, PTE_UXN | PTE_PXN};
    for (unsigned int i = 0; i < 3; ++i) {
        uint64_t *entry = walk(kernel_root, (uintptr_t)addresses[i], false, &rc);
        if (!entry || (*entry & (PTE_RO | PTE_USER | PTE_UXN | PTE_PXN)) != expected[i]) return -K_EINVAL;
    }
    uint64_t *guard = walk(kernel_root, (uintptr_t)__boot_stack_guard, false, &rc);
    if (guard && *guard) return -K_EINVAL;
    if (!table_empty(empty_root)) return -K_EINVAL;
    /* Exercise the CPU translation/permission machinery as well as descriptors. */
    if (translation_fault((uintptr_t)__text_start, false, false) ||
        !translation_fault((uintptr_t)__text_start, true, false) ||
        !translation_fault((uintptr_t)__text_start, false, true) ||
        !translation_fault((uintptr_t)__rodata_start, true, false) ||
        translation_fault((uintptr_t)&kernel_root, true, false) ||
        !translation_fault((uintptr_t)__boot_stack_guard, false, false) ||
        !translation_fault(0x80000, false, false) ||
        !translation_fault(0, false, false)) return -K_EINVAL;
    return 0;
}
