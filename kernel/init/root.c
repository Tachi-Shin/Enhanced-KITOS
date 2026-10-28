#include <kernel/root.h>
#include <fs/vfs.h>
#include <kernel/printk.h>
#include <kernel/error.h>
#include <lib/string.h>
static int initial_file(const char *path,const char *text)
{
    struct file *f;int rc=vfs_open(path,VFS_WRITE|VFS_CREATE|VFS_EXCL,&f);
    if(rc==-K_EEXIST)return 0;
    if(rc)return rc;
    uint32_t done;rc=vfs_write(f,text,(uint32_t)strlen(text),&done);
    int closed=vfs_close(f);return rc?rc:closed;
}
void root_layout_init(void)
{
    struct vfs_stat stat;if(vfs_stat("/",&stat))return;
    static const char *dirs[]={"/bin","/sbin","/etc","/lib","/lib/modules","/boot","/dev","/proc",
        "/tmp","/var","/var/log","/var/tmp","/home","/root","/mnt","/media","/usr","/usr/bin","/usr/sbin","/usr/lib","/usr/share"};
    for(unsigned int i=0;i<sizeof(dirs)/sizeof(dirs[0]);++i){
        int rc=vfs_mkdir(dirs[i]);
        if(rc&&rc!=-K_EEXIST){printk("[init] root layout %s: %d\n",dirs[i],rc);return;}
        if(rc==-K_EEXIST&&(vfs_stat(dirs[i],&stat)||stat.kind!=VFS_DIRECTORY)){
            printk("[init] root layout %s: not a directory\n",dirs[i]);return;
        }
    }
    (void)initial_file("/etc/hostname","ekitos\n");
    (void)initial_file("/etc/os-release","NAME=\"Enhanced KITOS\"\nID=ekitos\nPRETTY_NAME=\"Enhanced KITOS\"\n");
    (void)initial_file("/etc/profile","export PATH=/bin:/sbin:/usr/bin:/usr/sbin\nexport HOME=/root\nexport TERM=xterm\n");
    (void)initial_file("/etc/motd","Enhanced KITOS\nType help for commands.\n");
}
