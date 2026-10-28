#include <drivers/usb/usb.h>
#include <drivers/usb/input.h>
#include <kernel/error.h>
#include <lib/string.h>
static bool has_key(const uint8_t *keys, uint8_t key)
{
    for (unsigned int i = 2; i < 8; ++i) if (keys[i] == key) return true;
    return false;
}
int usb_hid_decode(struct usb_interface *iface, uint8_t device, const uint8_t *report,
                   size_t length, input_emit emit, void *context)
{
    if (!iface || !report || !emit || iface->subclass != 1) return -K_EINVAL;
    if (iface->protocol == 1) {
        if (length != 8 || report[1]) return -K_EINVAL;
        /* ErrorRollOver/POSTFail/ErrorUndefined do not release held keys. */
        for (unsigned int i = 2; i < 8; ++i) if (report[i] && report[i] <= 3) return -K_EAGAIN;
        for (unsigned int i = 0; i < 8; ++i) if ((iface->keys[0] ^ report[0]) & (1U << i)) {
            struct input_event e = {.device=device, .type=1, .code=(uint16_t)(0xE0+i),
                .value=!!(report[0]&(1U<<i)), .modifiers=report[0], .caps_lock=iface->caps_lock};
            emit(&e, context);
        }
        for (unsigned int group = 0; group < 2; ++group) {
            const uint8_t *from = group ? report : iface->keys;
            const uint8_t *other = group ? iface->keys : report;
            for (unsigned int i = 2; i < 8; ++i) {
                uint8_t key = from[i];
                if (!key || has_key(other, key)) continue;
                bool duplicate = false;
                for (unsigned int j = 2; j < i; ++j) if (from[j] == key) duplicate = true;
                if (duplicate) continue;
                if (group && key == 0x39) iface->caps_lock = !iface->caps_lock;
                struct input_event e = {.device=device, .type=1, .code=key, .value=(int32_t)group,
                    .modifiers=report[0], .caps_lock=iface->caps_lock};
                emit(&e, context);
            }
        }
        memcpy(iface->keys, report, 8);
        return 0;
    }
    if (iface->protocol == 2) {
        if (length < 3 || length > 4) return -K_EINVAL;
        uint8_t buttons = report[0] & 7;
        struct input_event e = {.device=device, .type=2, .code=buttons,
            .x=(int8_t)report[1], .y=(int8_t)report[2], .wheel=length == 4 ? (int8_t)report[3] : 0};
        if (buttons != iface->buttons || e.x || e.y || e.wheel) emit(&e, context);
        iface->buttons = buttons;
        return 0;
    }
    return -K_ENOTSUP;
}
