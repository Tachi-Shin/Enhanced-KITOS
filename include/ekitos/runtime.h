#ifndef EKITOS_RUNTIME_H
#define EKITOS_RUNTIME_H
#include <ekitos/driver.h>
/* Linked only into hardware modules, never into the kernel. */
extern const struct ek_driver_api *ek_api;
#endif
