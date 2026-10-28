#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fs/fat.h>
#include <kernel/error.h>
#include "support/file_block.h"
static unsigned int checks;
extern unsigned int fat_allocations;
extern int fat_alloc_fail;
int fat_test_format(struct block_device *,unsigned int);
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned int listed;
static int listing(void *ctx,const struct vfs_dirent *e){(void)ctx;CHECK(e->name[0]);++listed;return 0;}
static void exercise(const char *path)
{
    struct file_context disk;struct block_device d;CHECK(!disk_open(path,&d,&disk));
    if(strstr(path,"4k")){d.block_size=4096;d.block_count/=8;}
    CHECK(!vfs_mount("fat","/",&d,NULL));CHECK(d.references==1);
    CHECK(!vfs_mkdir("/日本語 long directory"));struct file *f;uint32_t done;uint64_t position;
    CHECK(vfs_rename("/日本語 long directory","/日本語 LONG DIRECTORY/child")==-K_EINVAL);
    const char *name="/日本語 long directory/Mixed Case 日本語 😀.txt";
    CHECK(!vfs_open(name,VFS_READ|VFS_WRITE|VFS_CREATE|VFS_EXCL,&f));
    unsigned char written[17011],readback[20000];for(unsigned int i=0;i<sizeof(written);++i)written[i]=(unsigned char)(i*37+11);
    CHECK(!vfs_write(f,written,sizeof(written),&done)&&done==sizeof(written));
    CHECK(!vfs_seek(f,0,0,&position));CHECK(!vfs_read(f,readback,sizeof(readback),&done)&&done==sizeof(written));CHECK(!memcmp(written,readback,done));
    CHECK(vfs_unmount("/")==-K_EBUSY);
    CHECK(!vfs_ftruncate(f,513));CHECK(!vfs_ftruncate(f,9000));CHECK(!vfs_seek(f,513,0,&position));
    memset(readback,1,sizeof(readback));CHECK(!vfs_read(f,readback,8487,&done)&&done==8487);for(unsigned int i=0;i<done;++i)CHECK(readback[i]==0);
    CHECK(!vfs_seek(f,10003,0,&position));CHECK(!vfs_write(f,"tail",4,&done)&&done==4);
    CHECK(!vfs_seek(f,9000,0,&position));CHECK(!vfs_read(f,readback,sizeof(readback),&done)&&done==1007);
    for(unsigned int i=0;i<1003;++i)CHECK(readback[i]==0);CHECK(!memcmp(readback+1003,"tail",4));
    CHECK(!vfs_close(f));CHECK(!vfs_rename(name,"/renamed.bin"));CHECK(!vfs_rmdir("/日本語 long directory"));
    listed=0;CHECK(!vfs_list("/",listing,NULL)&&listed>=1);
    struct vfs_stat stat;CHECK(!vfs_stat("/RENAMED.BIN",&stat)&&stat.size==10007);
    fat_alloc_fail=1;CHECK(vfs_stat("/renamed.bin",&stat)==-K_ENOMEM);fat_alloc_fail=0;CHECK(!fat_allocations);
    CHECK(!vfs_unmount("/")&&d.references==0);CHECK(!vfs_mount("fat","/",&d,NULL));
    CHECK(!vfs_open("/renamed.bin",VFS_READ,&f));CHECK(!vfs_seek(f,-4,2,&position));CHECK(!vfs_read(f,readback,4,&done)&&done==4&&!memcmp(readback,"tail",4));CHECK(!vfs_close(f));
    d.read_only=true;CHECK(vfs_open("/readonly",VFS_WRITE|VFS_CREATE,&f)==-K_EROFS);d.read_only=false;
    /* Mount references keep an unplugged device alive; cached metadata must
     * still fail ENODEV, and open files must not silently switch media. */
    d.removable=true;CHECK(!block_register(&d));CHECK(!vfs_open("/renamed.bin",VFS_READ,&f));d.offline=true;
    CHECK(vfs_stat("/renamed.bin",&stat)==-K_ENODEV);CHECK(vfs_read(f,readback,4,&done)==-K_ENODEV);
    CHECK(block_unregister(&d)==-K_EBUSY);CHECK(vfs_unmount_device(&d)==-K_EBUSY);CHECK(!vfs_close(f));
    CHECK(!vfs_unmount("/"));CHECK(!block_unregister(&d));d.offline=false;
    CHECK(!block_register(&d));CHECK(!vfs_mount("fat","/",&d,NULL));CHECK(!vfs_unlink("/renamed.bin"));CHECK(!vfs_unmount("/"));CHECK(!block_unregister(&d));
    CHECK(!fat_allocations);close(disk.fd);
}
static void large_file(const char *path)
{
    struct file_context c;struct block_device d;CHECK(!disk_open(path,&d,&c));CHECK(!vfs_mount("fat","/",&d,NULL));
    struct vfs_stat st;CHECK(!vfs_stat("/large.bin",&st)&&st.size==0x100000011ULL+15);
    struct file *f;CHECK(!vfs_open("/large.bin",VFS_READ|VFS_WRITE,&f));uint64_t position;uint32_t done;char data[32]={0};
    CHECK(!vfs_seek(f,0x100000011LL,0,&position)&&position==0x100000011ULL);
    CHECK(!vfs_read(f,data,15,&done)&&done==15&&!memcmp(data,"beyond-four-gib",15));
    CHECK(!vfs_seek(f,0x100000011LL,0,&position));CHECK(!vfs_write(f,"UPDATED-4GIB!!",14,&done)&&done==14);CHECK(!vfs_close(f));
    CHECK(!vfs_unmount("/"));CHECK(!vfs_mount("fat","/",&d,NULL));CHECK(!vfs_open("/large.bin",VFS_READ,&f));
    CHECK(!vfs_seek(f,0x100000011LL,0,&position));CHECK(!vfs_read(f,data,14,&done)&&done==14&&!memcmp(data,"UPDATED-4GIB!!",14));
    CHECK(!vfs_close(f));CHECK(!vfs_unmount("/"));close(c.fd);
}
int main(int argc,char **argv)
{
    if(argc==5&&!strcmp(argv[1],"format")){
        struct file_context c;struct block_device d;CHECK(!disk_open(argv[2],&d,&c));d.block_size=(uint32_t)strtoul(argv[4],NULL,10);d.block_count=d.block_count*512/d.block_size;
        int rc=fat_test_format(&d,(unsigned int)strtoul(argv[3],NULL,10));if(rc)fprintf(stderr,"format: %d\n",rc);close(c.fd);return rc?1:0;
    }
    CHECK(argc>1);CHECK(!vfs_register(&fat_type));for(int i=1;i<argc;++i){if(strstr(argv[i],"large"))large_file(argv[i]);else exercise(argv[i]);}
    printf("FAT12/16/32/exFAT VFS read/write/Unicode/zero-fill/remount/hot-unplug: %u checks PASS\n",checks);
}
