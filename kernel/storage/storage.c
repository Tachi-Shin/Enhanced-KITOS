#include <kernel/storage.h>
#include <block/block.h>
#include <fs/vfs.h>
#include <kernel/clock.h>
#include <kernel/error.h>
#include <kernel/printk.h>
#include <lib/string.h>
static struct {uint64_t generation;bool attempted;} states[BLOCK_DEVICE_COUNT];
static uint64_t next_scan;
int storage_mount(unsigned int index,const char *path)
{
    struct block_device *d=block_get_ref(index);if(!d)return -K_ENODEV;
    int rc=block_init(d);if(!rc)rc=vfs_mount_virtual("fat",path,d);
    if(!rc){states[index].generation=d->generation;states[index].attempted=true;}
    block_put(d);return rc;
}
void storage_poll(void)
{
    if(clock_ms()<next_scan)return;
    next_scan=clock_ms()+100;
    for(unsigned int i=0;i<BLOCK_DEVICE_COUNT;++i){
        struct block_device *d=block_get_ref(i);if(!d)continue;
        if(!d->removable){block_put(d);continue;}
        if(states[i].generation!=d->generation){states[i].generation=d->generation;states[i].attempted=false;}
        if(d->offline){
            int rc=vfs_unmount_device(d);if(!rc)printk("[storage] removed disk=%u\n",i);
        }else if(!states[i].attempted){
            char path[16]="/media/usb0";path[10]=(char)('0'+(i?i-1:0));struct vfs_stat stat;
            if(vfs_stat("/",&stat)==-K_ENODEV)strcpy(path,"/");
            int rc=block_init(d);if(!rc)rc=vfs_mount_virtual("fat",path,d);
            if(rc!=-K_EBUSY){states[i].attempted=true;printk("[storage] disk=%u mount=%s status=%d\n",i,path,rc);}
        }
        block_put(d);
    }
}
void storage_print(void)
{
    for(unsigned int i=0;i<BLOCK_DEVICE_COUNT;++i){
        struct block_device *d=block_get_ref(i);if(!d)continue;
        char path[VFS_PATH_MAX];int rc=vfs_mount_path(d,path);
        printk("[disk] id=%u blocks=%llu sector=%u ro=%u offline=%u mount=%s name=%s\n",i,d->block_count,d->block_size,d->read_only,d->offline,rc?"-":path,d->name);
        block_put(d);
    }
}
