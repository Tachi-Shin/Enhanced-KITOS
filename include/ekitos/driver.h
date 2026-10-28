#ifndef EKITOS_DRIVER_H
#define EKITOS_DRIVER_H
/* C driver SDK v6. Rebuild modules when the ABI version changes. Modules use
 * only this explicit API table; they do not import arbitrary kernel symbols. */
#include <ekitos/error.h>
#include <drivers/usb/usb.h>
#include <drivers/usb/input.h>
#include <block/block.h>
#include <ekitos/device.h>
struct kernel_gate;
struct uart_device;
#define EK_DRIVER_ABI 6U
#define EK_DRIVER_MAGIC 0x454B49544F53444DULL
#define EK_DMA_TO_DEVICE 1U
#define EK_DMA_FROM_DEVICE 2U
struct ek_driver_api {
    uint32_t abi, size;
    void *owner;
    const char *platform;
    uint64_t (*now_ms)(void);
    void (*sleep_ms)(uint32_t);
    void (*log)(const char *, uint64_t, uint64_t);
    void *(*dma_alloc)(void *owner, uint64_t *physical);
    int (*dma_free)(void *owner, void *);
    void (*dma_sync)(void *, size_t, unsigned int direction);
    uint32_t (*read32)(uint64_t physical);
    void (*write32)(uint64_t physical, uint32_t);
    int (*firmware_xhci_reset)(uint32_t bdf);
    int (*usb_attach_host)(void *owner, struct usb_host *);
    int (*usb_detach_host)(void *owner, struct usb_host *);
    int (*usb_register_class)(void *owner, const struct usb_class_driver *);
    int (*usb_unregister_class)(void *owner, const struct usb_class_driver *);
    int (*usb_control)(struct usb_device *, uint8_t, uint8_t, uint16_t, uint16_t, void *, uint16_t, unsigned int *);
    int (*usb_interrupt_in)(struct usb_device *, struct usb_endpoint *, void *, unsigned int *);
    void (*input_emit)(const struct input_event *, void *);
    int (*usb_bulk)(struct usb_device *,struct usb_endpoint *,void *,unsigned int,unsigned int *);
    int (*usb_clear_halt)(struct usb_device *,struct usb_endpoint *);
    int (*usb_begin)(struct usb_device *);
    void (*usb_end)(void);
    int (*block_register)(void *owner,struct block_device *);
    int (*block_unregister)(void *owner,struct block_device *);
    int (*device_publish)(void *owner, enum ek_device_kind, const void *);
    uint64_t (*counter_read)(void);
    uint64_t (*counter_frequency)(void);
    void (*delay_us)(uint32_t);
    uint8_t (*read8)(uint64_t);
    uint16_t (*read16)(uint64_t);
    void (*write8)(uint64_t, uint8_t);
    void (*write16)(uint64_t, uint16_t);
    bool (*gate_try)(struct kernel_gate *);
    void (*gate_release)(struct kernel_gate *);
    void (*uart_rx_push)(struct uart_device *, char);
    void (*uart_rx_overrun)(struct uart_device *);
    int (*gpio_function)(unsigned int, unsigned int);
    int (*gpio_pull)(unsigned int, unsigned int);
    uint32_t (*firmware_clock)(uint32_t, uint32_t);
    int (*irq_register)(void *owner, unsigned int, bool (*handler)(void *), void *context, unsigned int priority);
    int (*irq_unregister)(void *owner, unsigned int, bool (*handler)(void *), void *context);
    int (*firmware_request)(void *tags, uint32_t bytes);
    void *(*framebuffer_map)(void *owner, uint64_t physical, size_t bytes);
};
struct ek_driver_manifest {
    uint64_t magic;
    uint32_t abi, size;
    char name[32];
    int (*init)(const struct ek_driver_api *);
    /* Nonzero refuses unload; code/data/DMA ownership is retained. */
    int (*fini)(void);
};
_Static_assert(sizeof(struct ek_driver_manifest) == 64, "driver manifest ABI");
#define EK_DRIVER_EXPORT __attribute__((visibility("default")))
/* ELF e_entry identifies this data object, not executable code. ABI validation
 * therefore precedes execution of any module instruction. */
extern EK_DRIVER_EXPORT const struct ek_driver_manifest ek_driver_manifest;
#endif
