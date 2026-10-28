#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <kernel/boot_memory.h>
#include <kernel/error.h>

static unsigned int checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static uint8_t blob[4096], names[1024];
static unsigned int pos, names_size;
static void put(unsigned int where, uint32_t value)
{ blob[where] = value >> 24; blob[where + 1] = value >> 16; blob[where + 2] = value >> 8; blob[where + 3] = value; }
static void word(uint32_t v) { put(pos, v); pos += 4; }
static void begin(const char *name)
{ word(1); unsigned int n = (unsigned int)strlen(name) + 1; memcpy(blob + pos, name, n); pos += (n + 3) & ~3U; }
static void prop(const char *name, const uint32_t *values, unsigned int count)
{
    word(3); word(count * 4); word(names_size);
    unsigned int n = (unsigned int)strlen(name) + 1;
    memcpy(names + names_size, name, n); names_size += n;
    for (unsigned int i = 0; i < count; ++i) word(values[i]);
}
static unsigned int fixture(unsigned int cell_count)
{
    memset(blob, 0, sizeof(blob)); names_size = 0; pos = 72;
    put(40, 0); put(44, 0x5000); put(48, 0); put(52, 0x1000);
    begin("");
    prop("#address-cells", &cell_count, 1); prop("#size-cells", &cell_count, 1);
    begin("memory@0");
    uint32_t ram[] = {0, 0, 0, 0x3c000000}, small_ram[] = {0, 0x3c000000};
    prop("reg", cell_count == 2 ? ram : small_ram, cell_count == 2 ? 4 : 2); word(2);
    begin("reserved-memory"); prop("ranges", NULL, 0);
    begin("firmware@8000");
    uint32_t regs[] = {0, 0x8000, 0, 0x2000, 0, 0xD000, 0, 0x1000};
    uint32_t small_regs[] = {0x8000, 0x2000, 0xD000, 0x1000};
    prop("reg", cell_count == 2 ? regs : small_regs, cell_count == 2 ? 8 : 4);
    word(2);
    begin("unused@e000");
    uint32_t disabled[] = {0x64697361, 0x626c6564, 0};
    prop("status", disabled, 3); prop("reg", regs, 8); word(2);
    /* Dynamic CMA request is not a fixed reservation and is not instantiated. */
    begin("linux,cma"); uint32_t size[] = {0, 0x100000}; prop("size", size, 2); word(2);
    word(2);
    begin("chosen"); uint32_t start[] = {0, 0x100000}, end[] = {0, 0x108000};
    prop("linux,initrd-start", start, 2); prop("linux,initrd-end", end, 2); word(2);
    word(2); word(9);
    unsigned int strings = pos;
    memcpy(blob + pos, names, names_size); pos += names_size;
    put(0, 0xD00DFEED); put(4, pos); put(8, 72); put(12, strings);
    put(16, 40); put(20, 17); put(24, 16); put(32, names_size); put(36, strings - 72);
    return pos;
}

int main(void)
{
    struct boot_memory out;
    for (unsigned int cells = 1; cells <= 2; ++cells) {
        unsigned int length = fixture(cells);
        CHECK(boot_memory_read(blob, length, &out) == 0);
        CHECK(out.count == 4 && out.dtb_size == length);
        CHECK(out.ram.base == 0 && out.ram.size == 0x3c000000);
        CHECK(out.reserved[0].base == 0x5000 && out.reserved[0].size == 0x1000);
        CHECK(out.reserved[1].base == 0x8000 && out.reserved[1].size == 0x2000);
        CHECK(out.reserved[2].base == 0xD000 && out.reserved[2].size == 0x1000);
        CHECK(out.reserved[3].base == 0x100000 && out.reserved[3].size == 0x8000);
        for (unsigned int n = 0; n < length; ++n) CHECK(boot_memory_read(blob, n, &out) < 0);
        /* Mutated headers/tokens/strings exercise bounded rejection under ASan. */
        for (unsigned int n = 0; n < length; ++n) {
            uint8_t saved = blob[n];
            for (unsigned int bit = 0; bit < 8; ++bit) {
                blob[n] = saved ^ (1U << bit);
                int rc = boot_memory_read(blob, length, &out);
                CHECK(rc <= 0);
                if (!rc) for (unsigned int r = 0; r < out.count; ++r)
                    CHECK(out.reserved[r].size <= ~0ULL - out.reserved[r].base);
            }
            blob[n] = saved;
        }
    }
    unsigned int length = fixture(2);
    CHECK(boot_memory_read(NULL, length, &out) < 0);
    CHECK(boot_memory_read(blob, length, NULL) < 0);
    put(44, 0xFFFFFFFF); put(40, 0xFFFFFFFF);
    CHECK(boot_memory_read(blob, length, &out) == -K_EINVAL);
    length = fixture(2); put(16, 41);
    CHECK(boot_memory_read(blob, length, &out) == -K_EINVAL);
    length = fixture(2); put(20, 16);
    CHECK(boot_memory_read(blob, length, &out) == -K_EINVAL);
    length = fixture(2); put(24, 18);
    CHECK(boot_memory_read(blob, length, &out) == -K_EINVAL);
    uint32_t tags[] = {5, 0x54410001, 0, 0, 0, 4, 0x54420005, 0x300000, 0x8000, 0, 0};
    CHECK(boot_memory_read_atags(tags, sizeof(tags), &out) == 0);
    CHECK(out.count == 1 && out.reserved[0].base == 0x300000 && out.reserved[0].size == 0x8000);
    for (unsigned int n = 0; n < sizeof(tags); ++n) CHECK(boot_memory_read_atags(tags, n, &out) < 0);
    tags[5] = 0xFFFFFFFF;
    CHECK(boot_memory_read_atags(tags, sizeof(tags), &out) == -K_EINVAL);
    tags[5] = 3;
    CHECK(boot_memory_read_atags(tags, sizeof(tags), &out) == -K_EINVAL);
    printf("boot memory: %u checks PASS\n", checks);
    return 0;
}
