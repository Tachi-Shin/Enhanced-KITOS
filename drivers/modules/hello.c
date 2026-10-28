#include <ekitos/driver.h>
static const struct ek_driver_api *api;
static int init(const struct ek_driver_api *provided)
{
    if(!provided || provided->abi!=EK_DRIVER_ABI || provided->size<sizeof(*provided))return -EK_EINVAL;
    api=provided; api->log("hello C driver",EK_DRIVER_ABI,0);return 0;
}
static int fini(void) {api->log("hello C driver stopped",0,0);return 0;}
EK_DRIVER_EXPORT const struct ek_driver_manifest ek_driver_manifest={
    EK_DRIVER_MAGIC,EK_DRIVER_ABI,sizeof(struct ek_driver_manifest),"hello",init,fini};
