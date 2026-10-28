/* FatFs R0.16 adapter. VFS owns serialization and persistent handle lifetimes;
 * FatFs handles are short-lived so each successful mutation is synchronized. */
#include <fs/fat.h>
#include <block/block.h>
#include <kernel/error.h>
#include <lib/string.h>
#include "../../third_party/fatfs/ff.h"
#include "../../third_party/fatfs/diskio.h"
struct fat_volume {FATFS fs;struct block_device *device;bool used,write_failed;int error;};
static struct fat_volume volumes[FF_VOLUMES];
static struct fat_volume *volume(BYTE d){return d<FF_VOLUMES?&volumes[d]:NULL;}
DSTATUS disk_status(BYTE d)
{
    struct fat_volume *v=volume(d);
    if(!v||!v->device||v->device->offline||!v->device->initialized)return STA_NOINIT|STA_NODISK;
    return v->device->read_only?STA_PROTECT:0;
}
DSTATUS disk_initialize(BYTE d){return disk_status(d);}
static DRESULT disk_result(struct fat_volume *v,int rc)
{if(!rc)return RES_OK;v->error=rc;return rc==-K_EROFS?RES_WRPRT:rc==-K_ENODEV?RES_NOTRDY:RES_ERROR;}
DRESULT disk_read(BYTE d,BYTE *buf,LBA_t lba,UINT count)
{struct fat_volume *v=volume(d);return !v||!v->device?RES_NOTRDY:disk_result(v,block_read(v->device,lba,count,buf));}
DRESULT disk_write(BYTE d,const BYTE *buf,LBA_t lba,UINT count)
{
    struct fat_volume *v=volume(d);if(!v||!v->device)return RES_NOTRDY;
    if(v->write_failed)return RES_WRPRT;
    int rc=block_write(v->device,lba,count,buf);
    if(rc&&rc!=-K_EBUSY&&rc!=-K_EROFS)v->write_failed=true;
    return disk_result(v,rc);
}
DRESULT disk_ioctl(BYTE d,BYTE cmd,void *buf)
{
    struct fat_volume *v=volume(d);if(!v||!v->device||v->device->offline)return RES_NOTRDY;
    if(cmd==CTRL_SYNC)return disk_result(v,block_sync(v->device));
    if(!buf)return RES_PARERR;
    if(cmd==GET_SECTOR_COUNT)*(LBA_t *)buf=v->device->block_count;
    else if(cmd==GET_SECTOR_SIZE)*(WORD *)buf=(WORD)v->device->block_size;
    else if(cmd==GET_BLOCK_SIZE)*(DWORD *)buf=1;
    else return RES_PARERR;
    return RES_OK;
}
static int result(struct fat_volume *v,FRESULT r)
{
    if(!r)return 0;
    if((r==FR_DISK_ERR||r==FR_NOT_READY)&&v->error)return v->error;
    switch(r){
    case FR_NO_FILE:case FR_NO_PATH:return -K_ENOENT;
    case FR_EXIST:return -K_EEXIST;
    case FR_WRITE_PROTECTED:return -K_EROFS;
    case FR_DENIED:return -K_ENOSPC;
    case FR_INVALID_NAME:case FR_INVALID_PARAMETER:return -K_EINVAL;
    case FR_NOT_ENOUGH_CORE:return -K_ENOMEM;
    case FR_TOO_MANY_OPEN_FILES:return -K_ENFILE;
    case FR_LOCKED:case FR_TIMEOUT:return -K_EBUSY;
    case FR_NO_FILESYSTEM:return -K_ENODEV;
    default:return -K_EIO;
    }
}
static int ready(struct super_block *sb,bool write)
{
    struct fat_volume *v=sb->private_data;v->error=0;
    if(!v->device||v->device->offline)return -K_ENODEV;
    if(write&&(v->device->read_only||v->write_failed))return -K_EROFS;
    return 0;
}
static int fullpath(struct super_block *sb,const char *path,char *out)
{
    if(strlen(path)+3>=VFS_PATH_MAX)return -K_ENAMETOOLONG;
    struct fat_volume *v=sb->private_data;
    out[0]='0'+(char)(v-volumes);out[1]=':';strcpy(out+2,path);return 0;
}
static int lookup(struct super_block *sb,const char *path,struct inode *node)
{
    int rc=ready(sb,false);if(rc)return rc;char name[VFS_PATH_MAX];if((rc=fullpath(sb,path,name)))return rc;
    FILINFO info={0};
    if(strcmp(path,"/")){rc=result(sb->private_data,f_stat(name,&info));if(rc)return rc;}else info.fattrib=AM_DIR;
    uint64_t identity=1469598103934665603ULL;
    for(const unsigned char *p=(const void *)path;*p;++p){unsigned char c=*p;if(c>='a'&&c<='z')c-=32;identity=(identity^c)*1099511628211ULL;}
    if(path!=node->path)strcpy(node->path,path);
    node->sb=sb;node->number=identity;node->size=info.fsize;node->attributes=info.fattrib;
    node->kind=(info.fattrib&AM_DIR)?VFS_DIRECTORY:VFS_REGULAR;return 0;
}
static int close_file(struct fat_volume *v,FIL *f,int rc)
{int closed=result(v,f_close(f));return rc?rc:closed;}
static int file_open(struct inode *node,BYTE flags,FIL *f)
{
    int rc=ready(node->sb,(flags&FA_WRITE)!=0);if(rc)return rc;
    char path[VFS_PATH_MAX];if((rc=fullpath(node->sb,node->path,path)))return rc;
    return result(node->sb->private_data,f_open(f,path,flags));
}
static int extend_zero(struct fat_volume *v,FIL *f,uint64_t end)
{
    /* FatFs f_lseek extensions are undefined data. Explicitly zero the gap. */
    if(end<=f_size(f))return 0;
    if(v->fs.fs_type!=FS_EXFAT&&end>0xFFFFFFFFULL)return -K_EOVERFLOW;
    int rc=result(v,f_lseek(f,f_size(f)));if(rc)return rc;
    uint8_t zeros[512]={0};
    while(f_tell(f)<end){UINT n=(UINT)(end-f_tell(f)>sizeof(zeros)?sizeof(zeros):end-f_tell(f)),done=0;
        rc=result(v,f_write(f,zeros,n,&done));if(rc||done!=n)return rc?rc:-K_ENOSPC;
    }
    return 0;
}
static int read_file(struct inode *node,uint64_t offset,void *buf,uint32_t n,uint32_t *done)
{
    *done=0;FIL f;struct fat_volume *v=node->sb->private_data;int rc=file_open(node,FA_READ,&f);if(rc)return rc;
    if(offset<f_size(&f)){rc=result(v,f_lseek(&f,offset));if(!rc){UINT got=0;rc=result(v,f_read(&f,buf,n,&got));*done=got;}}
    return close_file(v,&f,rc);
}
static int write_file(struct inode *node,uint64_t offset,const void *buf,uint32_t n,uint32_t *done)
{
    *done=0;if(offset>0x7FFFFFFFFFFFFFFFULL-n)return -K_EOVERFLOW;
    FIL f;struct fat_volume *v=node->sb->private_data;int rc=file_open(node,FA_WRITE,&f);if(rc)return rc;
    if(v->fs.fs_type!=FS_EXFAT&&offset+n>0xFFFFFFFFULL)rc=-K_EOVERFLOW;
    if(!rc&&n)rc=extend_zero(v,&f,offset);
    if(!rc&&n)rc=result(v,f_lseek(&f,offset));
    if(!rc&&n){UINT put=0;rc=result(v,f_write(&f,buf,n,&put));*done=put;if(!rc&&put<n)rc=-K_ENOSPC;}
    return close_file(v,&f,rc);
}
static int truncate_file(struct inode *node,uint64_t size)
{
    FIL f;struct fat_volume *v=node->sb->private_data;int rc=file_open(node,FA_WRITE,&f);if(rc)return rc;
    if(size>0x7FFFFFFFFFFFFFFFULL)rc=-K_EOVERFLOW;
    if(!rc&&size>f_size(&f))rc=extend_zero(v,&f,size);
    else if(!rc){rc=result(v,f_lseek(&f,size));if(!rc)rc=result(v,f_truncate(&f));}
    return close_file(v,&f,rc);
}
static int create(struct super_block *sb,const char *path,bool directory)
{
    int rc=ready(sb,true);if(rc)return rc;char name[VFS_PATH_MAX];if((rc=fullpath(sb,path,name)))return rc;
    if(directory)return result(sb->private_data,f_mkdir(name));
    FIL f;rc=result(sb->private_data,f_open(&f,name,FA_WRITE|FA_CREATE_NEW));return rc?rc:close_file(sb->private_data,&f,0);
}
static int remove_entry(struct super_block *sb,const char *path,bool directory)
{
    struct inode node;int rc=ready(sb,true);if(!rc)rc=lookup(sb,path,&node);if(rc)return rc;
    if((node.kind==VFS_DIRECTORY)!=directory)return directory?-K_ENOTDIR:-K_EISDIR;
    if(node.attributes&AM_RDO)return -K_EROFS;
    char name[VFS_PATH_MAX];if((rc=fullpath(sb,path,name)))return rc;
    FRESULT r=f_unlink(name);if(r==FR_DENIED&&directory)return -K_ENOTEMPTY;return result(sb->private_data,r);
}
static int rename_entry(struct super_block *sb,const char *old,const char *new)
{
    int rc=ready(sb,true);if(rc)return rc;char a[VFS_PATH_MAX],b[VFS_PATH_MAX];
    if((rc=fullpath(sb,old,a))||(rc=fullpath(sb,new,b)))return rc;
    FILINFO info;rc=result(sb->private_data,f_stat(a,&info));if(rc)return rc;
    if(info.fattrib&AM_DIR){
        /* FatFs does not reject directory cycles. Compare cluster identities
         * of every destination ancestor, including case/Unicode aliases. */
        DIR source;rc=result(sb->private_data,f_opendir(&source,a));if(rc)return rc;
        DWORD cluster=source.obj.sclust;(void)f_closedir(&source);
        for(size_t i=3;b[i];++i)if(b[i]=='/'){
            b[i]=0;DIR ancestor;rc=result(sb->private_data,f_opendir(&ancestor,b));b[i]='/';if(rc)return rc;
            bool cycle=ancestor.obj.sclust==cluster;(void)f_closedir(&ancestor);
            if(cycle)return -K_EINVAL;
        }
    }
    /* Preserve existing destination until callers remove it explicitly. */
    return result(sb->private_data,f_rename(a,b));
}
static int list(struct super_block *sb,const char *path,vfs_dir_callback callback,void *arg)
{
    int rc=ready(sb,false);if(rc)return rc;char name[VFS_PATH_MAX];if((rc=fullpath(sb,path,name)))return rc;
    DIR dir;rc=result(sb->private_data,f_opendir(&dir,name));if(rc)return rc;
    for(;;){FILINFO info;FRESULT r=f_readdir(&dir,&info);if(r){rc=result(sb->private_data,r);break;}if(!info.fname[0])break;
        struct vfs_dirent entry;strcpy(entry.name,info.fname);entry.size=info.fsize;entry.kind=(info.fattrib&AM_DIR)?VFS_DIRECTORY:VFS_REGULAR;
        rc=callback(arg,&entry);if(rc)break;
    }
    int end=result(sb->private_data,f_closedir(&dir));return rc?rc:end;
}
static void release(struct super_block *sb)
{
    struct fat_volume *v=sb->private_data;char path[3]={(char)('0'+(v-volumes)),':',0};
    (void)f_mount(NULL,path,0);memset(v,0,sizeof(*v));sb->private_data=NULL;
}
static const struct super_operations super_ops={lookup,create,remove_entry,rename_entry,list,release};
static const struct file_operations file_ops={read_file,write_file,truncate_file};
static int mount(struct super_block *sb,struct block_device *device,const void *options)
{
    (void)options;if(!device||device->offline||!device->initialized||device->block_size<512||device->block_size>4096||(device->block_size&(device->block_size-1)))return -K_ENODEV;
    for(unsigned int i=0;i<FF_VOLUMES;++i)if(!volumes[i].used){
        struct fat_volume *v=&volumes[i];memset(v,0,sizeof(*v));v->device=device;
        char path[3]={(char)('0'+i),':',0};int rc=result(v,f_mount(&v->fs,path,1));
        if(rc){(void)f_mount(NULL,path,0);memset(v,0,sizeof(*v));return rc;}
        v->used=true;sb->private_data=v;sb->ops=&super_ops;sb->file_ops=&file_ops;return 0;
    }
    return -K_ENOSPC;
}
const struct filesystem_type fat_type={"fat",mount};
#ifdef EKITOS_FATFS_FORMAT
/* Host fixture formatter only; not present in the EKITOS kernel. */
int fat_test_format(struct block_device *device,unsigned int flags)
{
    struct fat_volume *v=&volumes[0];if(v->used)return -K_EBUSY;
    v->device=device;uint8_t work[4096];MKFS_PARM options={.fmt=(BYTE)(flags|FM_SFD)};
    int rc=result(v,f_mkfs("0:",&options,work,sizeof(work)));memset(v,0,sizeof(*v));return rc;
}
#endif
