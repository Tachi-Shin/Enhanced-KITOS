#ifndef UAPI_TERMINAL_H
#define UAPI_TERMINAL_H
#define KITOS_DISPLAY_CONNECTED 1U
#define KITOS_DISPLAY_EDID 2U
#define KITOS_DISPLAY_TIMING_VERIFIED 4U
#define KITOS_DISPLAY_VIRTUAL 8U
#define KITOS_DISPLAY_RGB_LIMITED 16U
#define KITOS_DISPLAY_DVI 32U
#define KITOS_DISPLAY_LINK_KNOWN 64U
#define KITOS_DISPLAY_DOUBLE_BUFFER 128U
#define KITOS_DISPLAY_ACQUIRE 1U
#define KITOS_DISPLAY_PRESENT 2U
#define KITOS_DISPLAY_RELEASE 3U
/* Frame format: little-endian XRGB8888 (bytes B,G,R,X). */
struct kitos_terminal_info {
    unsigned int columns, rows, width, height, depth, pitch;
    unsigned int refresh_millihz, flags, pixel_order, display_id;
    unsigned int advertised_bpc, dropped_bytes;
};
_Static_assert(sizeof(struct kitos_terminal_info)==48,"terminal info wire layout");
#endif
