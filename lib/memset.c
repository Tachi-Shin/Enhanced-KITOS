#include <lib/string.h>
void *memset(void *destination, int value, size_t count)
{
    unsigned char *bytes = destination;
    for (size_t i = 0; i < count; ++i) bytes[i] = (unsigned char)value;
    return destination;
}
