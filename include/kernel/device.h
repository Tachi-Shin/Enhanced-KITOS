#ifndef KERNEL_DEVICE_H
#define KERNEL_DEVICE_H
#include <ekitos/device.h>
const void *device_backend(enum ek_device_kind);
int device_publish(enum ek_device_kind, const void *);
void device_unpublish(enum ek_device_kind, const void *);
#endif
