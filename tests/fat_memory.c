#include <stdlib.h>
unsigned int fat_allocations;
int fat_alloc_fail;
void *ff_memalloc(unsigned int n)
{if(fat_alloc_fail||n>4096)return NULL;void *p=malloc(n);if(p)++fat_allocations;return p;}
void ff_memfree(void *p){if(p){--fat_allocations;free(p);}}
