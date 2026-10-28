#include <ekitos/runtime.h>
#include <drivers/mmc/sd.h>
#include <lib/string.h>
static struct sd_card card;
static struct block_device disk;
static int init(const struct ek_driver_api *api)
{
    if (!api || api->abi != EK_DRIVER_ABI || api->size < sizeof(*api)) return -EK_EINVAL;
    ek_api = api; bool qemu = !strcmp(api->platform, "rpi4/qemu");
    if (!qemu && strcmp(api->platform, "rpi4/hardware")) return -EK_ENODEV;
    card.host.base_address = qemu ? 0xFE300000 : 0xFE340000;
    if (!qemu) {
        card.host.input_clock_hz = api->firmware_clock(12, 0);
        if (!card.host.input_clock_hz) return -EK_ENODEV;
    }
    sd_block_setup(&disk, &card, qemu ? "QEMU SDHCI (FE300000)" : "BCM2711 eMMC2 (FE340000)");
    return api->block_register(api->owner, &disk);
}
static int fini(void) { return ek_api->block_unregister(ek_api->owner, &disk); }
EK_DRIVER_EXPORT const struct ek_driver_manifest ek_driver_manifest = {
    EK_DRIVER_MAGIC, EK_DRIVER_ABI, sizeof(struct ek_driver_manifest), "bcm2711-sd", init, fini};
