#ifndef KERNEL_PAGE_H
#define KERNEL_PAGE_H
#include <dos/type.h>

#define PAGE_SIZE 4096UL
/* Metadata capacity of the initial allocator; platforms supply usable ranges. */
#define PMM_LIMIT 0x100000000ULL

struct memory_range { uint64_t base, size; };
struct page_stats { uint64_t base, end, total, free; };

/* Boot-only, one initialization. Reservations include partially covered pages.
 * The caller supplies verified usable RAM, excluding platform-owned memory. */
int page_init(struct memory_range ram, const struct memory_range *reserved,
              unsigned int count);
/* Physical address; zero means exhausted/uninitialized. Contents unspecified. */
uint64_t page_alloc(void);
/* Only a page returned by page_alloc may be freed, exactly once. */
int page_free(uint64_t physical);
void page_get_stats(struct page_stats *out);
bool page_is_allocated(uint64_t physical);
#endif
