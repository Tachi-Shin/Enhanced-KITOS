#include <stdio.h>
#include <stdlib.h>
#include <kernel/page.h>
#include <kernel/error.h>
#include <arch/irq.h>

static unsigned int checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

int main(void)
{
    CHECK(page_alloc() == 0);
    CHECK(page_free(4096) == -K_EINVAL);
    CHECK(page_init((struct memory_range){0, 0}, NULL, 0) == -K_EINVAL);
    CHECK(page_init((struct memory_range){PMM_LIMIT - 1, 2}, NULL, 0) == -K_EINVAL);
    CHECK(page_init((struct memory_range){~0ULL, 2}, NULL, 0) == -K_EINVAL);
    CHECK(page_init((struct memory_range){0, 4096}, NULL, 0) == -K_EINVAL);
    CHECK(page_init((struct memory_range){0, 8192}, NULL, 1) == -K_EINVAL);
    struct memory_range bad = {~0ULL, 2};
    CHECK(page_init((struct memory_range){0, 8192}, &bad, 1) == -K_EINVAL);
    struct memory_range reservations[] = {
        {0x2001, 4096}, /* Both partially covered pages reserved. */
        {0x3000, 4096}, /* Overlapping reservation. */
        {0x9000, 0}, {0x1000000, 400}, {0xF800, 0x1800}
    };
    CHECK(page_init((struct memory_range){1, 0x10FFE}, reservations, 5) == 0);
    CHECK(page_init((struct memory_range){0, 0x10000}, NULL, 0) == -K_EBUSY);
    struct page_stats before, after;
    page_get_stats(&before);
    CHECK(before.base == 0x1000 && before.end == 0x10000 && before.total == 12 && before.free == 12);
    uint64_t pages[12];
    for (unsigned int i = 0; i < 12; ++i) {
        test_irq_mask = (i & 1) ? 128 : 0;
        pages[i] = page_alloc();
        CHECK(test_irq_mask == ((i & 1) ? 128 : 0));
        CHECK(pages[i] >= 0x1000 && pages[i] < 0xF000 && pages[i] != 0x2000 && pages[i] != 0x3000);
        CHECK(page_is_allocated(pages[i]));
        for (unsigned int j = 0; j < i; ++j) CHECK(pages[j] != pages[i]);
    }
    CHECK(page_alloc() == 0);
    CHECK(page_free(0) == -K_EINVAL);
    CHECK(page_free(0x2000) == -K_EINVAL);
    CHECK(page_free(PMM_LIMIT) == -K_EINVAL);
    CHECK(page_free(~0ULL) == -K_EINVAL);
    CHECK(page_free(pages[0] + 1) == -K_EINVAL);
    CHECK(page_free(pages[5]) == 0);
    CHECK(!page_is_allocated(pages[5]));
    CHECK(page_free(pages[5]) == -K_EINVAL);
    CHECK(page_alloc() == pages[5]);
    for (unsigned int i = 0; i < 12; ++i) CHECK(page_free(pages[i]) == 0);
    page_get_stats(&after);
    CHECK(after.free == before.free && after.total == before.total);
    /* Repeated wraparound/reuse must neither leak nor allocate reservations. */
    for (unsigned int cycle = 0; cycle < 100; ++cycle) {
        for (unsigned int i = 0; i < 12; ++i) { pages[i] = page_alloc(); CHECK(pages[i] != 0); }
        CHECK(page_alloc() == 0);
        for (unsigned int i = 0; i < 12; ++i) CHECK(page_free(pages[11 - i]) == 0);
    }
    printf("page allocator: %u checks PASS\n", checks);
    return 0;
}
