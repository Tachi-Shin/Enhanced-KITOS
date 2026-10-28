#include <drivers/usb/usb.h>
#include <drivers/usb/input.h>
#include <kernel/error.h>
static struct usb_endpoint *input_endpoint(struct usb_interface *iface)
{
    for (unsigned int i = 0; i < iface->endpoint_count; ++i) {
        struct usb_endpoint *ep = &iface->endpoints[i];
        if ((ep->address & 0x80) && ep->type == 3 && ep->interval && ep->max_packet <= 64 &&
            ep->max_packet >= (iface->protocol == 1 ? 8 : 3)) return ep;
    }
    return NULL;
}
static int bind(struct usb_device *dev, struct usb_interface *iface)
{
    if (iface->subclass != 1 || (iface->protocol != 1 && iface->protocol != 2) || !input_endpoint(iface)) return -K_ENOTSUP;
    uint8_t interval = input_endpoint(iface)->interval;
    if (dev->speed >= USB_HIGH && interval > 16) return -K_EINVAL;
    iface->poll_interval_ms = dev->speed >= USB_HIGH ? (uint16_t)(((1U << (interval - 1)) + 7) / 8) : interval;
    unsigned int done;
    int rc = usb_control(dev, 0x21, 11, 0, iface->number, NULL, 0, &done); /* boot protocol */
    if (!rc) (void)usb_control(dev, 0x21, 10, 0, iface->number, NULL, 0, &done); /* idle on change */
    return rc;
}
static void emit(const struct input_event *event, void *context)
{
    (void)context;
    usb_input_emit(event, NULL);
}
static void poll(struct usb_device *dev, struct usb_interface *iface)
{
    uint8_t report[64]; unsigned int done;
    int rc = usb_interrupt_in(dev, input_endpoint(iface), report, &done);
    if (!rc) (void)usb_hid_decode(iface, dev->address, report, done, emit, iface);
}
static void disconnect(struct usb_device *dev, struct usb_interface *iface)
{
    uint8_t released[8] = {0};
    (void)usb_hid_decode(iface, dev->address, released, iface->protocol == 1 ? 8 : 3, emit, iface);
}
const struct usb_class_driver usb_boot_hid_driver = {3, "boot-hid", bind, poll, disconnect};
