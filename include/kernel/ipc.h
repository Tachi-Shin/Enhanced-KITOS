#ifndef KERNEL_IPC_H
#define KERNEL_IPC_H
#include <dos/type.h>
#include <uapi/ipc.h>
#define IPC_PIPE 1U
#define IPC_CHANNEL 2U
#define IPC_LISTENER 3U
#define IPC_EVENT 4U
#define IPC_SHM 5U
/* Generation-tagged capabilities, never userspace pointers. Operations are
 * nonblocking; the process layer implements interruptible waits on ipc_epoch. */
extern volatile uint64_t ipc_epoch;
int ipc_pair(unsigned int kind,unsigned int flags,uint32_t handles[2]);
int ipc_retain(uint32_t);
int ipc_close(uint32_t);
int ipc_read(uint32_t,void *,unsigned int,unsigned int *);
int ipc_write(uint32_t,const void *,unsigned int,unsigned int *);
unsigned int ipc_poll(uint32_t);
int ipc_get_flags(uint32_t);
int ipc_set_flags(uint32_t,unsigned int);
int ipc_kind(uint32_t);
uint64_t ipc_size(uint32_t);
int ipc_listen(const char *,unsigned int,uint32_t *);
int ipc_connect(const char *,unsigned int,uint32_t *);
int ipc_accept(uint32_t,unsigned int,uint32_t *);
int ipc_event(uint64_t,unsigned int,uint32_t *);
int ipc_shm_open(const char *,size_t,unsigned int,uint32_t *);
int ipc_shm_unlink(const char *);
uint64_t ipc_shm_page(uint32_t,unsigned int);
int ipc_shm_read(uint32_t,uint64_t,void *,unsigned int);
void ipc_notify(void);
#endif
