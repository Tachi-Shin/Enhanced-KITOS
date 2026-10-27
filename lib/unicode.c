#include <lib/unicode.h>
#include "unicode_data.h"
int utf8_next(const char **text, uint32_t *cp)
{
    const uint8_t *p = (const uint8_t *)*text;
    if (!*p) return 0;
    uint8_t first = *p++;
    uint32_t value, minimum; unsigned int more;
    if (first < 0x80) { value = first; minimum = 0; more = 0; }
    else if (first >= 0xC2 && first <= 0xDF) { value = first & 31; minimum = 0x80; more = 1; }
    else if (first >= 0xE0 && first <= 0xEF) { value = first & 15; minimum = 0x800; more = 2; }
    else if (first >= 0xF0 && first <= 0xF4) { value = first & 7; minimum = 0x10000; more = 3; }
    else return -1;
    while (more--) {
        if ((*p & 0xC0) != 0x80) return -1;
        value = (value << 6) | (*p++ & 63);
    }
    if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) return -1;
    *cp = value; *text = (const char *)p; return 1;
}
uint32_t unicode_upper(uint32_t cp)
{
    for (unsigned int i = 0; i < sizeof(upper_ranges) / sizeof(upper_ranges[0]); ++i)
        if (cp >= upper_ranges[i].first && cp <= upper_ranges[i].last) return cp + upper_ranges[i].delta;
    unsigned int lo = 0, hi = sizeof(upper_pairs) / sizeof(upper_pairs[0]);
    while (lo < hi) {
        unsigned int mid = lo + (hi - lo) / 2;
        if (upper_pairs[mid].lower < cp) lo = mid + 1; else hi = mid;
    }
    return lo < sizeof(upper_pairs) / sizeof(upper_pairs[0]) && upper_pairs[lo].lower == cp ? upper_pairs[lo].upper : cp;
}
uint32_t unicode_cp437(uint8_t byte) { return byte < 128 ? byte : cp437_high[byte - 128]; }
