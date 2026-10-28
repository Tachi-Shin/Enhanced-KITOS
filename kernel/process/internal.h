#ifndef PROCESS_INTERNAL_H
#define PROCESS_INTERNAL_H
#include <kernel/process.h>
#include <fs/vfs.h>
#include <uapi/ipc.h>
#define PROC_FREE 0
#define PROC_BUILDING 1
#define PROC_RUNNING 2
#define PROC_DEAD 3
#define PROC_ZOMBIE 4
#define FD_INPUT 1
#define FD_OUTPUT 2
#define FD_FILE 3
#define FD_IPC 4
struct process_fd { unsigned int kind, flags; struct file *file; uint32_t handle; bool readonly; };
#define PROCESS_MAPS_MAX 16U
struct process_mapping {uint64_t address,size;uint32_t handle;};
struct process {
    unsigned int state;
    uint32_t pid, parent;
    int status;
    uint64_t heap_base, heap_break, heap_limit;
    struct process_fd fd[PROCESS_FD_MAX];
    struct process_mapping mappings[PROCESS_MAPS_MAX];
    char cwd[VFS_PATH_MAX];
};
void process_pin(void);
void process_unpin(void);
int process_user_string(uint64_t address, char *out, size_t capacity);
int process_user_path(uint64_t address, char *out, size_t capacity);
int64_t process_fd_call(struct process *, uint64_t number, const uint64_t args[6]);
int64_t process_fs_call(struct process *, uint64_t number, const uint64_t args[6]);
int64_t process_control(const uint64_t args[6]);
int process_fd_close(struct process *,unsigned int);
int process_fd_copy(struct process_fd *,const struct process_fd *);
void process_ipc_cleanup(struct process *);
int64_t process_ipc_call(struct process *,uint64_t,const uint64_t[6]);
int64_t process_ipc_io(struct process_fd *,uint64_t,uint64_t,bool);
int process_spawn_mapped(const char *,unsigned int,const char *const [],unsigned int,const char *const [],const struct kitos_fd_map *,unsigned int);
#endif
