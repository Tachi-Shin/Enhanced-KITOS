#ifndef KITOS_DRIVERS_GPIO_GPIO_H
#define KITOS_DRIVERS_GPIO_GPIO_H
#include <dos/type.h>

enum gpio_level {
    LOW = 0,
    HIGH = 1
};

enum gpio_mode {
    INPUT = 0,
    OUTPUT = 1
};

enum gpio_result {
    GPIO_OK = 0,
    GPIO_EINVAL = -1,
    GPIO_ENODEV = -2,
    GPIO_EMODE = -3
};

/* pin_numはBCM GPIO番号。コネクタの物理端子番号ではない。 */

/* INPUT/OUTPUTの現在の端子レベルを返す。失敗時は負数。 */
int gpio_read(unsigned int pin_num);

/* OUTPUT設定済みのピンへ出力する。成功時GPIO_OK、失敗時は負数。 */
int gpio_write(unsigned int pin_num, enum gpio_level level);

/*
 * 入出力方向を設定する。代替機能のピンもGPIOへ切り替える。
 * OUTPUTへの切り替え時はLOWで開始する。
 * 既にOUTPUTの場合は現在の出力を維持する。
 * プルアップ/プルダウン設定は変更しない。
 */
int gpio_pinMode(unsigned int pin_num, enum gpio_mode mode);

enum gpio_function { GPIO_FUNCTION_INPUT, GPIO_FUNCTION_OUTPUT,
    GPIO_ALT0, GPIO_ALT1, GPIO_ALT2, GPIO_ALT3, GPIO_ALT4, GPIO_ALT5 };
enum gpio_pull { GPIO_PULL_NONE, GPIO_PULL_UP, GPIO_PULL_DOWN };
struct gpio_backend_ops;
struct gpio_device {
    /*
     * CPUからアクセスできるGPIOレジスタの先頭アドレス。
     * Pi 4Bの通常の物理アドレス配置では0xFE200000。
     * MMU有効時はDeviceメモリとしてマップした仮想アドレスを渡す。
     */
    const struct gpio_backend_ops *ops;
    uintptr_t base;

    /*
     * 有効なGPIO番号は0..pin_count-1。BCM2711では最大58。
     * GPIO0..27に限定する場合は28を指定する。
     */
    unsigned int pin_count;
};

struct gpio_backend_ops {
    int (*init)(const struct gpio_device *gpio_device);
    int (*read)(unsigned int pin_num);
    int (*write)(unsigned int pin_num, enum gpio_level level);
    int (*set_function)(unsigned int pin, enum gpio_function function);
    int (*set_pull)(unsigned int pin_num, enum gpio_pull pull);
    int (*pin_mode)(unsigned int pin_num, enum gpio_mode mode);
};

int gpio_init(const struct gpio_device *device);
/* Alternate functions are mapped by the controller backend. */
int gpio_set_function(unsigned int pin, enum gpio_function function);
int gpio_set_pull(unsigned int pin_num, enum gpio_pull pull);
#endif