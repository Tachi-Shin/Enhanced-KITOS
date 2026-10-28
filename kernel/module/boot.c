#include <kernel/module.h>
#include <kernel/printk.h>
#include <lib/string.h>
#include <kernel/error.h>
#define DECLARE(n) extern const unsigned char boot_##n##_start[], boot_##n##_end[]
DECLARE(TIMER); DECLARE(GIC); DECLARE(GPIO); DECLARE(MAILBOX); DECLARE(UART); DECLARE(SD);
DECLARE(VIDEO); DECLARE(DWC2); DECLARE(HID); DECLARE(STORAGE); DECLARE(PI4USB);
struct boot_image { const char *name; const unsigned char *start, *end; };
#define IMAGE(n) {#n, boot_##n##_start, boot_##n##_end}
static const struct boot_image images[] = {
    IMAGE(TIMER), IMAGE(GIC), IMAGE(GPIO), IMAGE(MAILBOX), IMAGE(UART), IMAGE(SD),
    IMAGE(VIDEO), IMAGE(DWC2), IMAGE(HID), IMAGE(STORAGE), IMAGE(PI4USB)};
int module_embedded(const char *name)
{
    for (unsigned int i = 0; i < sizeof(images) / sizeof(images[0]); ++i)
        if (!strcmp(name, images[i].name))
            return module_load_memory(images[i].start, (size_t)(images[i].end - images[i].start));
    return -K_ENOENT;
}
int module_bootstrap(void)
{
    for (unsigned int i = 0; i < 6; ++i) {
        int rc = module_embedded(images[i].name);
        if (rc) { printk("[boot] %s.EKM failed: %d\n", images[i].name, rc); return rc; }
    }
    return 0;
}
