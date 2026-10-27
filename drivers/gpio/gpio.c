#include <drivers/gpio/gpio.h>
static const struct gpio_backend_ops *backend;
int gpio_init(const struct gpio_device *device)
{
    if (!device || !device->ops || !device->ops->init) return GPIO_EINVAL;
    int rc = device->ops->init(device);
    if (!rc) backend = device->ops;
    return rc;
}
int gpio_read(unsigned int pin) { return backend ? backend->read(pin) : GPIO_ENODEV; }
int gpio_write(unsigned int pin, enum gpio_level level)
{ return backend ? backend->write(pin, level) : GPIO_ENODEV; }
int gpio_pinMode(unsigned int pin, enum gpio_mode mode)
{ return backend ? backend->pin_mode(pin, mode) : GPIO_ENODEV; }
int gpio_set_pull(unsigned int pin, enum gpio_pull pull)
{ return backend && backend->set_pull ? backend->set_pull(pin, pull) : GPIO_ENODEV; }
int gpio_set_function(unsigned int pin, enum gpio_function function)
{ return backend && backend->set_function ? backend->set_function(pin, function) : GPIO_ENODEV; }
