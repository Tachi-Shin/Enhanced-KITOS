#include <kernel/boot_memory.h>
#include <kernel/error.h>
#include <lib/string.h>

static uint32_t be32(const uint8_t *p)
{ return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint64_t cells(const uint8_t *p, unsigned int n)
{ return n == 2 ? (uint64_t)be32(p) << 32 | be32(p + 4) : be32(p); }
static bool within(uint32_t offset, uint32_t length, uint32_t total)
{ return offset <= total && length <= total - offset; }
static int reserve(struct boot_memory *out, uint64_t base, uint64_t size)
{
    if (!size) return 0;
    if (size > ~0ULL - base) return -K_EINVAL;
    if (out->count == BOOT_RESERVED_MAX) return -K_ENOSPC;
    out->reserved[out->count++] = (struct memory_range){base, size};
    return 0;
}
static bool string_end(const uint8_t *p, uint32_t size, uint32_t *length)
{
    for (uint32_t n = 0; n < size; ++n)
        if (!p[n]) { *length = n; return true; }
    return false;
}

struct node {
    unsigned int address_cells, size_cells;
    const uint8_t *reg;
    uint32_t reg_size;
    bool reserved_root, reserved_child, memory, chosen, disabled, children;
};

static uint32_t le32(const uint8_t *p)
{ return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0]; }

int boot_memory_read_atags(const void *tags, size_t accessible, struct boot_memory *out)
{
    if (!tags || !out || accessible < 8) return -K_EINVAL;
    const uint8_t *b = tags;
    if (le32(b + 4) != 0x54410001U) return -K_EINVAL;
    *out = (struct boot_memory){0};
    size_t pos = 0;
    while (pos <= accessible && accessible - pos >= 8) {
        uint32_t words = le32(b + pos), tag = le32(b + pos + 4);
        if (!words && !tag) return 0;
        if (words < 2 || words > (accessible - pos) / 4) return -K_EINVAL;
        if (tag == 0x54420005U) { /* ATAG_INITRD2 */
            if (words != 4) return -K_EINVAL;
            int rc = reserve(out, le32(b + pos + 8), le32(b + pos + 12));
            if (rc) return rc;
        }
        if (tag == 0x54410002U) { /* ATAG_MEM: size, start */
            if (words != 4) return -K_EINVAL;
            if (!le32(b + pos + 12) && le32(b + pos + 8))
                out->ram = (struct memory_range){0, le32(b + pos + 8)};
        }
        pos += (size_t)words * 4;
    }
    return -K_EINVAL;
}

int boot_memory_read(const void *dtb, size_t accessible, struct boot_memory *out)
{
    if (!dtb || !out || accessible < 40) return -K_EINVAL;
    const uint8_t *b = dtb;
    uint32_t total = be32(b + 4), structure = be32(b + 8), strings = be32(b + 12);
    uint32_t reservations = be32(b + 16), string_size = be32(b + 32), struct_size = be32(b + 36);
    if (be32(b) != 0xD00DFEEDU || total > accessible || total < 40 ||
        be32(b + 20) < 17 || be32(b + 24) > 17 ||
        reservations < 40 || (reservations & 7) || structure < reservations ||
        (structure & 3) || (struct_size & 3) || !within(structure, struct_size, total) ||
        strings < structure + struct_size || !within(strings, string_size, total)) return -K_EINVAL;
    *out = (struct boot_memory){0};
    out->dtb_size = total;
    for (;;) {
        if (!within(reservations, 16, structure)) return -K_EINVAL;
        uint64_t base = cells(b + reservations, 2), size = cells(b + reservations + 8, 2);
        reservations += 16;
        if (!base && !size) break;
        int rc = reserve(out, base, size);
        if (rc) return rc;
    }
    struct node stack[32];
    unsigned int depth = 0;
    bool root_seen = false, root_closed = false, initrd_start_seen = false, initrd_end_seen = false;
    uint64_t initrd_start = 0, initrd_end = 0;
    uint32_t p = structure, end = structure + struct_size;
    while (within(p, 4, end)) {
        uint32_t token = be32(b + p); p += 4;
        if (token == 1) { /* FDT_BEGIN_NODE */
            uint32_t n;
            if (root_closed || depth == 32 || !string_end(b + p, end - p, &n)) return -K_EINVAL;
            if (!depth && (root_seen || n)) return -K_EINVAL;
            struct node node = { .address_cells = 2, .size_cells = 1 };
            if (depth) {
                stack[depth - 1].children = true;
                node.disabled = stack[depth - 1].disabled;
                node.reserved_child = stack[depth - 1].reserved_root;
            }
            if (depth == 1) {
                node.reserved_root = !strcmp((const char *)b + p, "reserved-memory");
                node.chosen = !strcmp((const char *)b + p, "chosen");
                node.memory = !strcmp((const char *)b + p, "memory") ||
                              !strncmp((const char *)b + p, "memory@", 7);
                if (node.reserved_root) {
                    node.address_cells = stack[0].address_cells;
                    node.size_cells = stack[0].size_cells;
                }
            }
            root_seen = true;
            stack[depth++] = node;
            uint32_t skip = (n + 4U) & ~3U;
            if (!within(p, skip, end)) return -K_EINVAL;
            p += skip;
        } else if (token == 2) { /* FDT_END_NODE */
            if (!depth) return -K_EINVAL;
            struct node *node = &stack[depth - 1];
            if ((node->reserved_child || node->memory) && !node->disabled && node->reg) {
                struct node *parent = &stack[depth - 2];
                unsigned int a = parent->address_cells, s = parent->size_cells;
                if ((a != 1 && a != 2) || (s != 1 && s != 2) ||
                    !node->reg_size || node->reg_size % (4U * (a + s))) return -K_EINVAL;
                for (uint32_t offset = 0; offset < node->reg_size; offset += 4U * (a + s)) {
                    uint64_t base = cells(node->reg + offset, a), size = cells(node->reg + offset + 4U * a, s);
                    if (size > ~0ULL - base) return -K_EINVAL;
                    if (node->memory) {
                        if (!base && size) out->ram = (struct memory_range){base, size};
                    } else {
                        int rc = reserve(out, base, size);
                        if (rc) return rc;
                    }
                }
            }
            if (!--depth) root_closed = true;
        } else if (token == 3) { /* FDT_PROP */
            if (!depth || stack[depth - 1].children || !within(p, 8, end)) return -K_EINVAL;
            uint32_t size = be32(b + p), name_offset = be32(b + p + 4), n;
            p += 8;
            if (!within(p, size, end) || size > 0xFFFFFFFCU || name_offset >= string_size ||
                !string_end(b + strings + name_offset, string_size - name_offset, &n)) return -K_EINVAL;
            const char *name = (const char *)b + strings + name_offset;
            struct node *node = &stack[depth - 1];
            if (!strcmp(name, "#address-cells") || !strcmp(name, "#size-cells")) {
                if (size != 4) return -K_EINVAL;
                if (!strcmp(name, "#address-cells")) node->address_cells = be32(b + p);
                else node->size_cells = be32(b + p);
            } else if (!strcmp(name, "status")) {
                if (!string_end(b + p, size, &n)) return -K_EINVAL;
                if (strcmp((const char *)b + p, "okay") && strcmp((const char *)b + p, "ok")) node->disabled = true;
            } else if (node->reserved_root && !strcmp(name, "ranges")) {
                if (size) return -K_ENOTSUP; /* Only physical, untranslated reservations. */
            } else if ((node->reserved_child || node->memory) && !strcmp(name, "reg")) {
                if (node->reg) return -K_EINVAL;
                node->reg = b + p; node->reg_size = size;
            } else if (node->chosen && (!strcmp(name, "linux,initrd-start") || !strcmp(name, "linux,initrd-end"))) {
                if (size != 4 && size != 8) return -K_EINVAL;
                if (!strcmp(name, "linux,initrd-start")) {
                    if (initrd_start_seen) return -K_EINVAL;
                    initrd_start = cells(b + p, size / 4); initrd_start_seen = true;
                } else {
                    if (initrd_end_seen) return -K_EINVAL;
                    initrd_end = cells(b + p, size / 4); initrd_end_seen = true;
                }
            }
            uint32_t skip = (size + 3U) & ~3U;
            if (!within(p, skip, end)) return -K_EINVAL;
            p += skip;
        } else if (token == 4) { /* FDT_NOP */
            continue;
        } else if (token == 9) { /* FDT_END */
            if (depth || !root_closed || initrd_start_seen != initrd_end_seen || initrd_end < initrd_start) return -K_EINVAL;
            return reserve(out, initrd_start, initrd_end - initrd_start);
        } else return -K_EINVAL;
    }
    return -K_EINVAL;
}
