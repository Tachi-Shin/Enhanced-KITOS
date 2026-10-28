/* Test adapter only: the production xHCI code runs unchanged on this PC. */
#include <drivers/usb/xhci.h>
#include <kernel/error.h>
#include <lib/string.h>
static struct usb_host *host;
static const struct ek_driver_api *api;
static struct usb_device devices[12];
#ifdef EKITOS_STORAGE_TEST
static const struct usb_class_driver *storage_class;
#endif
int test_start(const struct ek_driver_api *a,uint64_t bar)
{api=a;host=xhci_host(api,bar,0);return host?host->ops->start(host->context):-K_EBUSY;}
int test_status(unsigned int port)
{bool connected,changed;int rc=host->ops->root_status(host->context,port,&connected,&changed);return rc?rc:(int)connected;}
int test_control(unsigned int n,unsigned int type,unsigned int request,unsigned int value,unsigned int index,void *buf,unsigned int len)
{unsigned int done=0;int rc=host->ops->control(host->context,&devices[n],type,request,value,index,buf,len,&done);return rc?rc:(int)done;}
static int enumeration_control(struct usb_device *d,uint8_t type,uint8_t request,uint16_t value,uint16_t index,void *buf,uint16_t len,unsigned int *done)
{return host->ops->control(host->context,d,type,request,value,index,buf,len,done);}
int test_enumerate(unsigned int n,unsigned int port,unsigned int parent)
{
    if(n>=12||parent>12)return -K_EINVAL;
    struct usb_device *d=&devices[n];memset(d,0,sizeof(*d));d->port=(uint8_t)port;int rc;
    if(parent){
        d->parent=devices[parent-1].address;d->depth=devices[parent-1].depth+1;
        if((rc=test_control(parent-1,0x23,3,4,port,NULL,0)))return rc;
        api->sleep_ms(60);uint8_t status[4];rc=test_control(parent-1,0xa3,0,0,port,status,4);
        if(rc!=4||(status[0]&0x13)!=3)return -K_EIO;
        if((rc=usb_hub_port_speed(devices[parent-1].speed,(uint16_t)(status[0]|status[1]<<8),&d->speed)))return rc;
    }else if((rc=host->ops->root_reset(host->context,port,&d->speed)))return rc;
    bool hub;rc=usb_enumerate_device(host,d,(uint8_t)(n+1),enumeration_control,api->sleep_ms,&hub);if(rc)return rc;
    for(unsigned int i=0;i<d->interface_count;++i){struct usb_interface *f=&d->interfaces[i];
#ifdef EKITOS_STORAGE_TEST
        if(f->class_code==8&&storage_class){rc=storage_class->bind(d,f);if(rc)return rc;f->driver=storage_class;}
#endif
        if(f->class_code==3&&f->subclass==1){if((rc=test_control(n,0x21,11,0,f->number,NULL,0)))return rc;}
    }
    if(hub&&(rc=usb_configure_hub(host,d,enumeration_control,api->sleep_ms)))return rc;
    d->used=true;return 0;
}
unsigned int test_speed(unsigned int n){return n<12?devices[n].speed:99;}
unsigned int test_ep0(unsigned int n){return n<12?devices[n].ep0_size:0;}
unsigned int test_host_id(unsigned int n){return n<12?devices[n].host_id:0;}
unsigned int test_endpoint(unsigned int n,unsigned int e)
{if(n>=12||e>=devices[n].interfaces[0].endpoint_count)return 0;struct usb_endpoint *p=&devices[n].interfaces[0].endpoints[e];return p->max_packet|((unsigned int)p->max_burst<<16)|((unsigned int)p->companion<<24);}
int test_read(unsigned int n,void *buffer)
{
    struct usb_device *d=&devices[n];struct usb_endpoint *e=&d->interfaces[0].endpoints[0];unsigned int done;
    int rc=host->ops->packet(host->context,d,e,0,buffer,e->max_packet,&done);return rc?rc:(int)done;
}
int test_release(unsigned int n){
    for(unsigned int i=0;i<devices[n].interface_count;++i){struct usb_interface *f=&devices[n].interfaces[i];if(f->driver){f->driver->disconnect(&devices[n],f);f->driver=NULL;}}
    return host->ops->release(host->context,&devices[n]);
}
int test_stop(void){return host->ops->stop(host->context);}
unsigned int test_api_size(void){return sizeof(struct ek_driver_api);}

#ifdef EKITOS_STORAGE_TEST
#include <drivers/usb/msc.h>
#include <fs/fat.h>
static struct ek_driver_api storage_api;
static int register_storage(void *owner,const struct usb_class_driver *driver){(void)owner;storage_class=driver;return 0;}
static int unregister_storage(void *owner,const struct usb_class_driver *driver)
{(void)owner;for(unsigned int i=0;i<12;++i)for(unsigned int j=0;j<devices[i].interface_count;++j){struct usb_interface *f=&devices[i].interfaces[j];if(f->driver==driver){driver->disconnect(&devices[i],f);f->driver=NULL;}}storage_class=NULL;return 0;}
static int register_disk(void *owner,struct block_device *d){(void)owner;return block_register(d);}
static int unregister_disk(void *owner,struct block_device *d){(void)owner;return block_unregister(d);}
static int control_storage(struct usb_device *d,uint8_t type,uint8_t req,uint16_t val,uint16_t index,void *buf,uint16_t n,unsigned int *done)
{return host->ops->control(host->context,d,type,req,val,index,buf,n,done);}
static int bulk_storage(struct usb_device *d,struct usb_endpoint *e,void *buf,unsigned int n,unsigned int *done)
{return host->ops->bulk(host->context,d,e,buf,n,done);}
static int clear_storage(struct usb_device *d,struct usb_endpoint *e)
{unsigned int done;int rc=control_storage(d,2,1,0,e->address,NULL,0,&done);return rc?rc:host->ops->clear_halt(host->context,d,e);}
static int begin_storage(struct usb_device *d){return d->used?0:-K_ENODEV;}
static void end_storage(void){}
int test_storage_start(void)
{
    storage_api=*api;storage_api.usb_control=control_storage;storage_api.usb_bulk=bulk_storage;
    storage_api.usb_clear_halt=clear_storage;storage_api.usb_begin=begin_storage;storage_api.usb_end=end_storage;
    storage_api.block_register=register_disk;storage_api.block_unregister=unregister_disk;
    storage_api.usb_register_class=register_storage;storage_api.usb_unregister_class=unregister_storage;
    int rc=vfs_register(&fat_type);if(rc&&rc!=-K_EEXIST)return rc;return usb_msc_init(&storage_api);
}
int test_storage_mount(unsigned int index)
{struct block_device *d=block_get_ref(index);if(!d)return -K_ENODEV;int rc=vfs_mount("fat","/",d,NULL);block_put(d);return rc;}
int test_storage_unmount(void){return vfs_unmount("/");}
int test_file_write(const char *path,const void *buf,unsigned int size)
{struct file *f;int rc=vfs_open(path,VFS_CREATE|VFS_TRUNC|VFS_WRITE,&f);if(rc)return rc;uint32_t done;rc=vfs_write(f,buf,size,&done);int end=vfs_close(f);return rc?rc:end?end:done==size?0:-K_EIO;}
int test_file_read(const char *path,void *buf,unsigned int size)
{struct file *f;int rc=vfs_open(path,VFS_READ,&f);if(rc)return rc;uint32_t done;rc=vfs_read(f,buf,size,&done);int end=vfs_close(f);return rc?rc:end?end:(int)done;}
int test_storage_finish(void){return usb_msc_fini();}
#endif
