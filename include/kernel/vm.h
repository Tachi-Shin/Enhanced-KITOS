#ifndef KERNEL_VM_H
#define KERNEL_VM_H
#include <kernel/page.h>

enum vm_permission { VM_READ = 1, VM_WRITE = 2, VM_EXEC = 4 };
/* Owned translation root. No sharing/refcounts until Phase 3. Not copyable.
 * One executing thread per space; destroy only after switching away. */
struct vm_space { uint64_t root; };
struct vm_mapping { uint64_t physical; unsigned int permissions; };
int vm_space_create(struct vm_space *space);
void vm_space_destroy(struct vm_space *space);
/* Fresh zeroed page, optionally initialized from a kernel buffer. W^X required.
 * No user-facing map syscall yet. Caller serializes mutations with execution. */
int vm_map_new(struct vm_space *space, uint64_t address, unsigned int permissions,
               const void *initial, size_t length);
int vm_unmap(struct vm_space *space, uint64_t address);
/* Borrowed pages remain owned by the IPC object; unmap/destroy only remove
 * translations. Mapping lifetime must hold an IPC reference. Never executable. */
int vm_map_shared(struct vm_space *,uint64_t address,uint64_t physical,bool writable);
int vm_query(const struct vm_space *space, uint64_t address, struct vm_mapping *out);
/* Scheduler-only: IRQ masked, switch before changing the kernel context. */
void vm_activate(const struct vm_space *space);
bool vm_user_range(uint64_t address, size_t length);
int vm_user_check(const struct vm_space *space, uint64_t address, size_t length,
                  unsigned int permissions);
int copy_from_user(const struct vm_space *space, void *to, uint64_t from, size_t length);
int copy_to_user(const struct vm_space *space, uint64_t to, const void *from, size_t length);
#endif
