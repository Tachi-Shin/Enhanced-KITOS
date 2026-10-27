#ifndef LIB_UNICODE_H
#define LIB_UNICODE_H
#include <dos/type.h>
int utf8_next(const char **text, uint32_t *codepoint);
uint32_t unicode_upper(uint32_t codepoint);
uint32_t unicode_cp437(uint8_t byte);
#endif
