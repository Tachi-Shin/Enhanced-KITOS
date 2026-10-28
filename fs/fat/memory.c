#include <arch/memory.h>
void *ff_memalloc(unsigned int size)
{if(size>PAGE_SIZE)return NULL;uint64_t p=page_alloc();return p?arch_phys_to_virt(p):NULL;}
void ff_memfree(void *p)
{if(p)(void)page_free(arch_virt_to_phys(p));}
