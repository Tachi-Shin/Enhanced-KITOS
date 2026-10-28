#ifndef DRIVERS_USB_H
#define DRIVERS_USB_H
#include <dos/type.h>
#define USB_DEVICES_MAX 12U
#define USB_INTERFACES_MAX 4U
#define USB_ENDPOINTS_MAX 4U
#define USB_CONFIG_MAX 1024U
enum usb_speed { USB_LOW, USB_FULL, USB_HIGH, USB_SUPER };
struct usb_device;
struct usb_interface;
struct usb_endpoint {
    uint8_t address, type, interval, toggle;
    uint16_t max_packet;
    uint8_t max_burst, companion_attributes;
    uint16_t bytes_per_interval;
    bool companion;
};
struct usb_host_ops {
    int (*start)(void *);
    int (*root_status)(void *, unsigned int port, bool *connected, bool *changed);
    int (*root_reset)(void *, unsigned int port, enum usb_speed *);
    /* One packet. PID: 0=DATA0, 2=DATA1, 3=SETUP. EAGAIN means NAK.
     * Synchronous, bounded, task context, serialized by the USB core. */
    int (*packet)(void *, const struct usb_device *, const struct usb_endpoint *,
                  unsigned int pid, void *, unsigned int length, unsigned int *done);
    /* xHCI owns device addresses and complete control TDs. Optional for DWC2. */
    int (*open)(void *, struct usb_device *);
    int (*address)(void *, struct usb_device *, uint8_t logical_address);
    int (*configured)(void *, struct usb_device *);
    int (*release)(void *, struct usb_device *);
    int (*control)(void *, struct usb_device *, uint8_t, uint8_t, uint16_t, uint16_t, void *, uint16_t, unsigned int *);
    int (*stop)(void *);
    int (*bulk)(void *,struct usb_device *,struct usb_endpoint *,void *,unsigned int,unsigned int *);
    int (*clear_halt)(void *,struct usb_device *,struct usb_endpoint *);
};
struct usb_host { const char *name; const struct usb_host_ops *ops; void *context; bool split_routes; uint8_t root_ports; };
struct usb_class_driver {
    uint8_t class_code;
    const char *name;
    int (*bind)(struct usb_device *, struct usb_interface *);
    void (*poll)(struct usb_device *, struct usb_interface *);
    void (*disconnect)(struct usb_device *, struct usb_interface *);
};
struct usb_interface {
    uint8_t number, class_code, subclass, protocol, endpoint_count;
    struct usb_endpoint endpoints[USB_ENDPOINTS_MAX];
    const struct usb_class_driver *driver;
    uint64_t next_poll;
    uint16_t poll_interval_ms;
    /* Boot HID state; future drivers can use their own bounded state pool. */
    uint8_t keys[8], buttons;
    bool caps_lock;
};
struct usb_device {
    bool used;
    uint8_t address, parent, port, depth, interface_count, hub_ports;
    uint16_t ep0_size;
    enum usb_speed speed;
    uint16_t vendor, product;
    uint16_t failed_ports;
    uint8_t host_id, device_protocol, tt_think_time;
    struct usb_interface interfaces[USB_INTERFACES_MAX];
};
/* Caller initializes dev->speed; parsing preserves the device's routing state. */
int usb_parse_config(const uint8_t *, size_t, struct usb_device *, uint8_t *configuration);
int usb_parse_device_prefix(const uint8_t *,size_t,enum usb_speed,uint16_t *ep0);
int usb_parse_bos(const uint8_t *,size_t);
typedef int (*usb_control_fn)(struct usb_device *,uint8_t,uint8_t,uint16_t,uint16_t,void *,uint16_t,unsigned int *);
/* Portable enumeration is shared by the kernel and native controller tests. */
int usb_enumerate_device(struct usb_host *,struct usb_device *,uint8_t,usb_control_fn,void (*sleep_ms)(uint32_t),bool *hub);
int usb_configure_hub(struct usb_host *,struct usb_device *,usb_control_fn,void (*sleep_ms)(uint32_t));
int usb_hub_port_speed(enum usb_speed,uint16_t,enum usb_speed *);
int usb_register_class(const struct usb_class_driver *);
int usb_unregister_class(const struct usb_class_driver *);
int usb_init(struct usb_host *);
int usb_attach_host(struct usb_host *);
int usb_detach_host(struct usb_host *);
void usb_task(void *);
/* Class callbacks run on the USB worker while its gate is held. Transfer APIs
 * below are for those callbacks only; one controller owns one DMA packet at a time. */
int usb_control(struct usb_device *, uint8_t type, uint8_t request,
                uint16_t value, uint16_t index, void *, uint16_t length, unsigned int *done);
int usb_interrupt_in(struct usb_device *, struct usb_endpoint *, void *, unsigned int *done);
int usb_bulk(struct usb_device *,struct usb_endpoint *,void *,unsigned int,unsigned int *);
int usb_clear_halt(struct usb_device *,struct usb_endpoint *);
/* Task I/O claims the USB gate across the entire BOT transaction. Class
 * callbacks already own it and must not call begin/end recursively. */
int usb_begin(struct usb_device *);
void usb_end(void);
void usb_print(void);
struct usb_host *dwc2_qemu_host(void);
extern const struct usb_class_driver usb_boot_hid_driver;
#endif
