#ifndef EKITOS_DEVICE_H
#define EKITOS_DEVICE_H
#include <dos/type.h>
#include <uapi/terminal.h>
/* Hardware backends are supplied by ELF modules. Registries, queues and
 * scheduling remain kernel services. Active boot backends are pinned. */
enum ek_device_kind { EK_DEVICE_GPIO, EK_DEVICE_UART, EK_DEVICE_FIRMWARE,
    EK_DEVICE_IRQ, EK_DEVICE_TIMER, EK_DEVICE_VIDEO, EK_DEVICE_COUNT };
struct ek_firmware_ops {
    uint32_t (*clock_rate)(uint32_t, uint32_t);
    int (*arm_memory)(uint32_t *, uint32_t *);
    int (*xhci_reset)(uint32_t);
    int (*property_list)(void *, uint32_t);
};
struct ek_irq_ops {
    void (*init)(void);
    void (*enable)(uint32_t, uint32_t);
    void (*disable)(uint32_t);
    uint32_t (*ack)(uint32_t *);
    void (*eoi)(uint32_t);
};
struct ek_timer_ops { void (*program)(uint64_t); };
/* Only the protected console worker invokes poll/write. Info is a snapshot. */
struct ek_video_frame {
    void *context;
    int (*read)(void *,uint64_t,void *,unsigned int);
    unsigned int width,height,pitch;
};
struct ek_video_ops {
    void (*write)(char);
    void (*poll)(void);
    void (*info)(struct kitos_terminal_info *);
    int (*frame)(const struct ek_video_frame *);
    void (*text_mode)(void);
};
#endif
