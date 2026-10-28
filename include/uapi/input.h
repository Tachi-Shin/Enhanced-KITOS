#ifndef UAPI_INPUT_H
#define UAPI_INPUT_H
#define KITOS_INPUT_KEY 1U
#define KITOS_INPUT_POINTER 2U
#define KITOS_INPUT_CAPS_LOCK 1U
#define KITOS_INPUT_BATCH_MAX 16U
/* Nonblocking, shared event queue. INPUT_READ returns the number of events,
 * 0 when empty, negative public errno on failure. Console bytes are separate. */
struct kitos_input_event {
    unsigned long long time_ms;
    unsigned int device, type, code, modifiers;
    int value, x, y, wheel;
    unsigned int flags, reserved;
};
_Static_assert(sizeof(struct kitos_input_event) == 48, "input wire layout");
#endif
