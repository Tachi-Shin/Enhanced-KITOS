#ifndef BCM2711_PCIE_H
#define BCM2711_PCIE_H
#include <ekitos/driver.h>
/* Polling VL805 setup. Returns the CPU physical BAR address. */
int bcm2711_vl805_start(const struct ek_driver_api *, uint64_t *bar);
#endif
