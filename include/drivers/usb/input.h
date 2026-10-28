#ifndef USB_INPUT_H
#define USB_INPUT_H
#include <dos/type.h>
#include <uapi/input.h>
struct usb_interface;
struct input_event {
    uint64_t time_ms;
    uint8_t device, type, modifiers; /* 1=key, 2=relative pointer */
    bool caps_lock;
    uint16_t code;       /* keyboard usage or pointer button mask */
    int32_t value, x, y, wheel;
};
typedef void (*input_emit)(const struct input_event *, void *);
int usb_hid_decode(struct usb_interface *, uint8_t device, const uint8_t *, size_t,
                   input_emit, void *);
char usb_key_ascii(uint16_t usage, uint8_t modifiers, bool caps_lock);
int usb_input_getc(char *);
void usb_input_emit(const struct input_event *, void *);
void usb_input_print(void);
unsigned int usb_input_read(struct kitos_input_event *, unsigned int capacity);
#endif
