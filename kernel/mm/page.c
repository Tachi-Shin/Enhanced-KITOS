#include <kernel/page.h>
#include <kernel/error.h>
#include <arch/irq.h>

#define MAX_PAGES (PMM_LIMIT / PAGE_SIZE)
#define WORDS ((MAX_PAGES + 63) / 64)
/* eligible never changes after boot; used distinguishes allocations from
 * permanently reserved pages, so page_free cannot release firmware/kernel RAM. */
static uint64_t eligible[WORDS], used[WORDS];
static struct page_stats stats;
static uint64_t first_page, last_page, cursor;
static bool initialized;

static bool bit(const uint64_t *map, uint64_t n)
{ return (map[n / 64] & (1ULL << (n % 64))) != 0; }
static void set(uint64_t *map, uint64_t n)
{ map[n / 64] |= 1ULL << (n % 64); }
static void clear(uint64_t *map, uint64_t n)
{ map[n / 64] &= ~(1ULL << (n % 64)); }

int page_init(struct memory_range ram, const struct memory_range *reserved,
              unsigned int count)
{
    if (initialized) return -K_EBUSY;
    if (!ram.size || ram.base >= PMM_LIMIT || ram.size > PMM_LIMIT - ram.base ||
        (count && !reserved)) return -K_EINVAL;
    for (unsigned int r = 0; r < count; ++r)
        if (reserved[r].size > ~0ULL - reserved[r].base) return -K_EINVAL;
    first_page = (ram.base + PAGE_SIZE - 1) / PAGE_SIZE;
    if (!first_page) first_page = 1; /* NULL is never allocatable. */
    last_page = (ram.base + ram.size) / PAGE_SIZE;
    if (first_page >= last_page) return -K_EINVAL;
    for (uint64_t p = first_page; p < last_page; ++p) {
        bool keep = false;
        uint64_t address = p * PAGE_SIZE;
        for (unsigned int r = 0; r < count; ++r)
            if (reserved[r].size && address < reserved[r].base + reserved[r].size &&
                address + PAGE_SIZE > reserved[r].base) { keep = true; break; }
        if (!keep) { set(eligible, p); ++stats.total; }
    }
    stats.base = first_page * PAGE_SIZE;
    stats.end = last_page * PAGE_SIZE;
    stats.free = stats.total;
    cursor = first_page;
    initialized = true;
    return 0;
}

uint64_t page_alloc(void)
{
    uint64_t flags = arch_irq_save(), result = 0;
    if (initialized && stats.free) {
        for (uint64_t n = 0; n < last_page - first_page; ++n) {
            uint64_t p = cursor++;
            if (cursor == last_page) cursor = first_page;
            if (bit(eligible, p) && !bit(used, p)) {
                set(used, p); --stats.free; result = p * PAGE_SIZE; break;
            }
        }
    }
    arch_irq_restore(flags);
    return result;
}

int page_free(uint64_t physical)
{
    uint64_t flags = arch_irq_save();
    uint64_t p = physical / PAGE_SIZE;
    int rc = -K_EINVAL;
    if (initialized && !(physical % PAGE_SIZE) && p >= first_page && p < last_page &&
        bit(eligible, p) && bit(used, p)) {
        clear(used, p); ++stats.free; cursor = p; rc = 0;
    }
    arch_irq_restore(flags);
    return rc;
}

bool page_is_allocated(uint64_t physical)
{
    uint64_t flags = arch_irq_save(), p = physical / PAGE_SIZE;
    bool result = initialized && !(physical % PAGE_SIZE) && p >= first_page &&
                  p < last_page && bit(eligible, p) && bit(used, p);
    arch_irq_restore(flags);
    return result;
}

void page_get_stats(struct page_stats *out)
{
    if (!out) return;
    uint64_t flags = arch_irq_save();
    *out = stats;
    arch_irq_restore(flags);
}
