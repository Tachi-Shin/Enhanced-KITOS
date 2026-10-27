#ifndef KERNEL_SYNC_H
#define KERNEL_SYNC_H
#include <dos/type.h>
struct kernel_gate { unsigned int locked; };
bool kernel_gate_try(struct kernel_gate *gate);
void kernel_gate_release(struct kernel_gate *gate);
#endif
