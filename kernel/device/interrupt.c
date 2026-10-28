#include <kernel/device.h>
#include <platform/irq.h>
void irq_controller_init(void)
{ const struct ek_irq_ops *o = device_backend(EK_DEVICE_IRQ); if (o) o->init(); }
void irq_controller_enable(uint32_t id, uint32_t priority)
{ const struct ek_irq_ops *o = device_backend(EK_DEVICE_IRQ); if (o) o->enable(id, priority); }
void irq_controller_disable(uint32_t id)
{ const struct ek_irq_ops *o = device_backend(EK_DEVICE_IRQ); if (o) o->disable(id); }
uint32_t irq_controller_ack(uint32_t *id)
{ const struct ek_irq_ops *o = device_backend(EK_DEVICE_IRQ); if (o) return o->ack(id); *id = 1023; return 1023; }
void irq_controller_eoi(uint32_t token)
{ const struct ek_irq_ops *o = device_backend(EK_DEVICE_IRQ); if (o) o->eoi(token); }
unsigned int platform_timer_irq(void) { return 30U; }
