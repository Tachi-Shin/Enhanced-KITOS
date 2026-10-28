#include <lib/string.h>
size_t strlen(const char *s) { size_t n = 0; while (s[n]) ++n; return n; }
int strcmp(const char *a, const char *b)
{ while (*a && *a == *b) { ++a; ++b; } return (unsigned char)*a - (unsigned char)*b; }
int strncmp(const char *a, const char *b, size_t n)
{ while (n && *a && *a == *b) { --n; ++a; ++b; } return n ? (unsigned char)*a - (unsigned char)*b : 0; }
char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)) {} return r; }
char *strchr(const char *s, int c)
{ do { if (*s == (char)c) return (char *)s; } while (*s++); return NULL; }
