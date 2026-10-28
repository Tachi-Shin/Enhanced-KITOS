#include <kernel/device.h>
#include <kernel/error.h>
#include <drivers/mailbox/mailbox.h>
uint32_t mailbox_clock_rate(uint32_t id, uint32_t hz)
{
    const struct ek_firmware_ops *ops = device_backend(EK_DEVICE_FIRMWARE);
    return ops ? ops->clock_rate(id, hz) : 0;
}
int mailbox_arm_memory(uint32_t *base, uint32_t *size)
{
    const struct ek_firmware_ops *ops = device_backend(EK_DEVICE_FIRMWARE);
    return ops ? ops->arm_memory(base, size) : -K_ENODEV;
}
int mailbox_notify_xhci_reset(uint32_t bdf)
{
    const struct ek_firmware_ops *ops = device_backend(EK_DEVICE_FIRMWARE);
    return ops ? ops->xhci_reset(bdf) : -K_ENODEV;
}
int mailbox_property_list(void *tags,uint32_t bytes)
{
    const struct ek_firmware_ops *ops=device_backend(EK_DEVICE_FIRMWARE);
    return ops?ops->property_list(tags,bytes):-K_ENODEV;
}
