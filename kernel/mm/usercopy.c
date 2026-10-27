#include <kernel/vm.h>
#include <kernel/error.h>
#include <arch/memory.h>
#include <lib/string.h>

bool vm_user_range(uint64_t address, size_t length)
{
    return address >= PAGE_SIZE && address < ARCH_USER_END &&
           length <= ARCH_USER_END - address;
}
int vm_user_check(const struct vm_space *space, uint64_t address, size_t length,
                  unsigned int permissions)
{
    if (!space || !space->root || !vm_user_range(address, length)) return -K_EFAULT;
    while (length) {
        struct vm_mapping mapping;
        if (vm_query(space, address, &mapping) ||
            (mapping.permissions & permissions) != permissions) return -K_EFAULT;
        size_t chunk = PAGE_SIZE - address % PAGE_SIZE;
        if (chunk > length) chunk = length;
        address += chunk; length -= chunk;
    }
    return 0;
}
static int user_copy(const struct vm_space *space, uint64_t user, void *buffer,
                     size_t length, bool to_user)
{
    int rc = vm_user_check(space, user, length, to_user ? VM_WRITE : VM_READ);
    if (rc) return rc;
    /* Validate the entire range before copying any bytes. Copy through the
     * kernel's physical mapping, never by dereferencing an EL0 address. */
    uint8_t *kernel = buffer;
    while (length) {
        struct vm_mapping mapping;
        rc = vm_query(space, user, &mapping);
        if (rc) return rc;
        size_t chunk = PAGE_SIZE - user % PAGE_SIZE;
        if (chunk > length) chunk = length;
        void *mapped = arch_phys_to_virt(mapping.physical);
        if (to_user) memcpy(mapped, kernel, chunk);
        else memcpy(kernel, mapped, chunk);
        user += chunk; kernel += chunk; length -= chunk;
    }
    return 0;
}
int copy_from_user(const struct vm_space *space, void *to, uint64_t from, size_t length)
{ return user_copy(space, from, to, length, false); }
int copy_to_user(const struct vm_space *space, uint64_t to, const void *from, size_t length)
{ return user_copy(space, to, (void *)from, length, true); }
