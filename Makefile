.DEFAULT_GOAL := all
PI4USB_SOURCES := drivers/modules/pi4usb.c drivers/pci/bcm2711.c drivers/usb/xhci.c drivers/usb/xhci_protocol.c
HID_SOURCES := drivers/modules/hid.c drivers/usb/hid_report.c
STORAGE_SOURCES := drivers/modules/storage.c drivers/usb/msc.c
VIDEO_SOURCES := drivers/modules/video.c drivers/video/edid.c drivers/video/terminal.c drivers/video/font6x8.c
HELLO_SOURCES := drivers/modules/hello.c
GPIO_SOURCES := drivers/modules/gpio.c drivers/gpio/bcm2711/bcm2711_gpio.c
UART_SOURCES := drivers/modules/uart.c drivers/uart/pl011/pl011.c drivers/uart/mini_uart/mini_uart.c
GIC_SOURCES := drivers/modules/gic.c platform/rpi4/common/gic.c
TIMER_SOURCES := drivers/modules/timer.c drivers/timer/arm.c
MAILBOX_SOURCES := drivers/modules/mailbox.c drivers/mailbox/bcm2711_mailbox.c
SD_SOURCES := drivers/modules/sd.c drivers/modules/sd_io.c drivers/mmc/sd.c drivers/mmc/sdhci.c
DWC2_SOURCES := drivers/modules/dwc2.c drivers/usb/dwc2.c
BOOT_DRIVER_NAMES := TIMER GIC GPIO MAILBOX UART SD DWC2
BOOT_DRIVER_FILES := $(addprefix build/drivers/,$(addsuffix .EKM,$(BOOT_DRIVER_NAMES)))
BOOT_DRIVER_SOURCES := $(foreach n,$(BOOT_DRIVER_NAMES),$($(n)_SOURCES))
.PHONY: boot-drivers
boot-drivers: $(BOOT_DRIVER_FILES)
$(BOOT_DRIVER_FILES): build/drivers/%.EKM: $(BOOT_DRIVER_SOURCES) sdk/device_runtime.c sdk/driver.mk $(wildcard include/ekitos/*.h include/*/*.h include/*/*/*.h) Makefile
	$(MAKE) -f sdk/driver.mk DRIVER=$* SOURCES="$($*_SOURCES) sdk/device_runtime.c"
build/drivers/IRQTEST.EKM: tests/irq_module.c sdk/driver.mk $(wildcard include/ekitos/*.h include/uapi/*.h)
	$(MAKE) -f sdk/driver.mk DRIVER=IRQTEST SOURCES=tests/irq_module.c
.PHONY: drivers drivers-native rootfs
rootfs: image users drivers
	$(PYTHON) tools/rootfs.py --out build/rootfs --kernel $(IMG)
drivers: boot-drivers build/drivers/VIDEO.EKM build/drivers/HID.EKM build/drivers/STORAGE.EKM build/drivers/PI4USB.EKM build/drivers/HELLO.EKM
drivers-native:
	$(MAKE) -f sdk/driver.mk native DRIVER=VIDEO SOURCES="$(VIDEO_SOURCES)"
	$(MAKE) -f sdk/driver.mk native DRIVER=TIMER SOURCES="$(TIMER_SOURCES) sdk/device_runtime.c"
	$(MAKE) -f sdk/driver.mk native DRIVER=GIC SOURCES="$(GIC_SOURCES) sdk/device_runtime.c"
	$(MAKE) -f sdk/driver.mk native DRIVER=GPIO SOURCES="$(GPIO_SOURCES) sdk/device_runtime.c"
	$(MAKE) -f sdk/driver.mk native DRIVER=MAILBOX SOURCES="$(MAILBOX_SOURCES) sdk/device_runtime.c"
	$(MAKE) -f sdk/driver.mk native DRIVER=UART SOURCES="$(UART_SOURCES) sdk/device_runtime.c"
	$(MAKE) -f sdk/driver.mk native DRIVER=SD SOURCES="$(SD_SOURCES) sdk/device_runtime.c"
	$(MAKE) -f sdk/driver.mk native DRIVER=DWC2 SOURCES="$(DWC2_SOURCES) sdk/device_runtime.c"
	$(MAKE) -f sdk/driver.mk native DRIVER=STORAGE SOURCES="drivers/modules/storage.c drivers/usb/msc.c"
	$(MAKE) -f sdk/driver.mk native DRIVER=PI4USB SOURCES="$(PI4USB_SOURCES)"
	$(MAKE) -f sdk/driver.mk native DRIVER=HID SOURCES="drivers/modules/hid.c drivers/usb/hid_report.c"
	$(MAKE) -f sdk/driver.mk native DRIVER=HELLO SOURCES="drivers/modules/hello.c"
PLATFORM ?= rpi4
TARGET ?= hardware
ARCH := arm64
ifeq ($(filter $(PLATFORM),rpi4),)
$(error Unsupported PLATFORM=$(PLATFORM))
endif
ifeq ($(filter $(TARGET),hardware qemu),)
$(error TARGET must be hardware or qemu)
endif
CROSS_COMPILE ?= aarch64-elf-
CC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)ld
OBJCOPY := $(CROSS_COMPILE)objcopy
OBJDUMP := $(CROSS_COMPILE)objdump
NM := $(CROSS_COMPILE)nm
HOSTCC ?= cc
QEMU ?= qemu-system-aarch64
PYTHON ?= python3
BUILD_DIR := build/$(PLATFORM)/$(TARGET)
IMAGE_DIR := image/$(PLATFORM)/$(TARGET)
# Target fragments choose sources, boot code and linker script.
include platform/$(PLATFORM)/$(TARGET)/build.mk
CFLAGS := -std=gnu11 -Wall -Wextra -Werror -ffreestanding -fno-builtin \
 -fno-stack-protector -fno-pic -mgeneral-regs-only -mstrict-align -mno-outline-atomics \
 -fno-asynchronous-unwind-tables -O2 -g -DEKITOS_FATFS_TYPES -Iinclude -Iarch/$(ARCH)/include
ASFLAGS := -g -Iinclude
C_SRCS := $(filter-out drivers/usb/xhci.c drivers/usb/xhci_protocol.c drivers/usb/msc.c drivers/usb/dwc2.c drivers/usb/hid.c drivers/usb/hid_report.c drivers/gpio/bcm2711/bcm2711_gpio.c drivers/mmc/sd.c drivers/mmc/sdhci.c drivers/mailbox/bcm2711_mailbox.c platform/rpi4/common/gic.c,$(sort $(wildcard arch/arm64/kernel/*.c kernel/*/*.c \
 drivers/block/*.c drivers/char/*.c drivers/gpio/*.c drivers/gpio/bcm2711/*.c \
 drivers/mmc/*.c drivers/mailbox/*.c drivers/uart/uart.c drivers/usb/*.c fs/*/*.c lib/*.c \
 platform/$(PLATFORM)/common/*.c) $(PLATFORM_SRCS) third_party/fatfs/ff.c third_party/fatfs/ffunicode.c tests/kernel_regression.c tests/memory_regression.c))
S_SRCS := $(BOOT_SOURCE) arch/arm64/kernel/context_switch.S arch/arm64/kernel/exception.S tests/user/probe.S
OBJS := $(addprefix $(BUILD_DIR)/,$(C_SRCS:.c=.c.o) $(S_SRCS:.S=.S.o))
OBJS += $(BUILD_DIR)/boot_modules.o
EMBEDDED_DRIVERS := $(BOOT_DRIVER_FILES) build/drivers/VIDEO.EKM build/drivers/HID.EKM build/drivers/STORAGE.EKM build/drivers/PI4USB.EKM
build/drivers/VIDEO.EKM build/drivers/HID.EKM build/drivers/STORAGE.EKM build/drivers/PI4USB.EKM build/drivers/HELLO.EKM: build/drivers/%.EKM: sdk/driver.mk $(wildcard include/ekitos/*.h include/uapi/*.h include/drivers/usb/*.h include/drivers/video/*.h) $(VIDEO_SOURCES) $(PI4USB_SOURCES) drivers/modules/hid.c drivers/usb/hid.c drivers/usb/hid_report.c drivers/modules/storage.c drivers/usb/msc.c drivers/modules/hello.c
	$(MAKE) -f sdk/driver.mk DRIVER=$* SOURCES="$($*_SOURCES)"
$(BUILD_DIR)/boot_modules.S: tools/embed_modules.py $(EMBEDDED_DRIVERS)
	@mkdir -p $(dir $@)
	$(PYTHON) tools/embed_modules.py $@ $(EMBEDDED_DRIVERS)
$(BUILD_DIR)/boot_modules.o: $(BUILD_DIR)/boot_modules.S
	$(CC) $(ASFLAGS) -c $< -o $@
ELF := $(BUILD_DIR)/kernel8.elf
IMG := $(BUILD_DIR)/kernel8.img
.PHONY: all image clean run dump symbols test test-block test-fat32 test-qemu
all: image rootfs
image: $(IMG)
	@mkdir -p $(IMAGE_DIR)
	cp $< $(IMAGE_DIR)/kernel8.img
$(IMG): $(ELF)
	$(OBJCOPY) -O binary $< $@
$(ELF): $(OBJS) $(LINKER_SCRIPT) Makefile
	$(LD) -T $(LINKER_SCRIPT) -nostdlib -Map $(BUILD_DIR)/kernel8.map -o $@ $(OBJS)
$(BUILD_DIR)/%.c.o: %.c Makefile platform/$(PLATFORM)/$(TARGET)/build.mk
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
$(BUILD_DIR)/%.S.o: %.S Makefile platform/$(PLATFORM)/$(TARGET)/build.mk
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -MMD -MP -c $< -o $@
dump: $(ELF)
	$(OBJDUMP) -D $< > $(BUILD_DIR)/kernel8.dump
symbols: $(ELF)
	$(NM) -n $< > $(BUILD_DIR)/kernel8.sym
# No user SD image is selected implicitly. run uses a disposable test image.
SD_IMAGE ?= build/tests/system.img
.PHONY: update-system-image
update-system-image: users drivers
	$(PYTHON) tools/run_image.py --update "$(SD_IMAGE)"
run:
	$(MAKE) PLATFORM=$(PLATFORM) TARGET=qemu all
	$(PYTHON) tools/run_image.py "$(SD_IMAGE)"
	$(QEMU) -M raspi4b -m 2g \
	 -kernel build/$(PLATFORM)/qemu/kernel8.img \
	 -dtb $(IMAGE_DIR)/bcm2711-rpi-4-b.dtb \
	 -drive "if=sd,file=$(SD_IMAGE),format=raw" \
	 -bios build/$(PLATFORM)/qemu/start4.elf \
	 -serial null -serial stdio
clean:
	rm -rf build/$(PLATFORM)/$(TARGET)
HOST_TEST_CFLAGS := -std=c11 -Wall -Wextra -Werror -g -Iinclude -Iarch/$(ARCH)/include
build/host/block_test: tests/block_test.c drivers/block/block.c include/block/block.h
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_TEST_CFLAGS) tests/block_test.c drivers/block/block.c -o $@
test-block: build/host/block_test
	$<
build/host/fat32_test: tests/fat32_test.c drivers/block/block.c fs/fat32/fat32.c lib/unicode.c include/fs/fat32.h
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_TEST_CFLAGS) tests/fat32_test.c drivers/block/block.c fs/fat32/fat32.c lib/unicode.c -o $@
test-fat32: build/host/fat32_test
	$(PYTHON) tools/test_images.py --suite build/tests
	$< build/tests/direct.img build/tests/mbr.img
test: test-block test-fat32 test-vfs test-timer test-sdhci test-sd test-geometry test-page test-boot-memory test-elf test-usb test-drivers test-fat-family test-msc test-superspeed test-terminal test-uart
test-qemu: users
	$(MAKE) TARGET=qemu all
	$(PYTHON) tools/qemu_test.py --qemu $(QEMU) --kernel build/$(PLATFORM)/qemu/kernel8.img
-include $(OBJS:.o=.d)

HOST_FS_SRCS := lib/unicode.c drivers/block/block.c fs/fat32/fat32.c fs/fat32/vfs.c fs/vfs/vfs.c kernel/sync/gate.c tests/support/runtime.c
build/host/fs_test: Makefile tests/fs_test.c $(HOST_FS_SRCS) $(wildcard include/fs/*.h) tests/support/file_block.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -Itests/support $(HOST_TEST_CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer tests/fs_test.c $(HOST_FS_SRCS) -o $@
.PHONY: test-vfs
test-vfs: build/host/fs_test
	$(PYTHON) tools/test_images.py --suite build/tests/vfs
	$< build/tests/vfs/direct.img build/tests/vfs/mbr.img

build/host/timer_test: tests/timer_test.c kernel/time/timer.c kernel/time/time.c tests/support/runtime.c include/kernel/timer.h
	@mkdir -p $(dir $@)
	$(HOSTCC) -Itests/support $(HOST_TEST_CFLAGS) -fsanitize=address,undefined tests/timer_test.c kernel/time/timer.c kernel/time/time.c tests/support/runtime.c -o $@
.PHONY: test-timer
test-timer: build/host/timer_test
	$<

build/host/sdhci_test: tests/sdhci_test.c drivers/mmc/sdhci.c platform/rpi4/hardware/sdhci_io.c include/drivers/mmc/sdhci.h
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_TEST_CFLAGS) -fsanitize=address,undefined tests/sdhci_test.c drivers/mmc/sdhci.c platform/rpi4/hardware/sdhci_io.c -o $@
.PHONY: test-sdhci
test-sdhci: build/host/sdhci_test
	$<

.PHONY: test-geometry
test-geometry: build/host/fs_test
	$(PYTHON) tools/test_images.py --suite --sector-size 1024 build/tests/sector1k
	$< build/tests/sector1k/direct.img build/tests/sector1k/mbr.img
	$(PYTHON) tools/test_images.py --suite --sector-size 2048 build/tests/sector2k
	$< build/tests/sector2k/direct.img build/tests/sector2k/mbr.img
	$(PYTHON) tools/test_images.py --suite --sector-size 4096 build/tests/sector4k
	$< build/tests/sector4k/direct.img build/tests/sector4k/mbr.img

build/host/sd_test: tests/sd_test.c drivers/mmc/sd.c include/drivers/mmc/sd.h
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_TEST_CFLAGS) -fsanitize=address,undefined tests/sd_test.c drivers/mmc/sd.c -o $@
.PHONY: test-sd
test-sd: build/host/sd_test
	$<

# Host checks also rebuild when public contracts or the mock runtime change.
HOST_HEADERS := $(wildcard include/*/*.h include/*/*/*.h include/*/*/*/*.h tests/support/*.h tests/support/arch/*.h arch/$(ARCH)/include/arch/*.h lib/unicode_data.h)
build/host/block_test build/host/fat32_test build/host/fs_test build/host/timer_test build/host/sdhci_test build/host/sd_test: Makefile $(HOST_HEADERS)

build/host/page_test: tests/page_test.c kernel/mm/page.c tests/support/runtime.c Makefile $(HOST_HEADERS)
	@mkdir -p $(dir $@)
	$(HOSTCC) -Itests/support $(HOST_TEST_CFLAGS) -fsanitize=address,undefined tests/page_test.c kernel/mm/page.c tests/support/runtime.c -o $@
.PHONY: test-page
test-page: build/host/page_test
	$<

build/host/boot_memory_test: tests/boot_memory_test.c kernel/mm/boot_memory.c Makefile $(HOST_HEADERS)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_TEST_CFLAGS) -fsanitize=address,undefined tests/boot_memory_test.c kernel/mm/boot_memory.c -o $@
.PHONY: test-boot-memory
test-boot-memory: build/host/boot_memory_test
	$<

NEWLIB_PREFIX := $(abspath build/newlib)
NEWLIB_BUILD_DIR := $(abspath build/newlib-build)
.PHONY: newlib users test-elf
newlib:
	NEWLIB_PREFIX="$(NEWLIB_PREFIX)" NEWLIB_BUILD_DIR="$(NEWLIB_BUILD_DIR)" CROSS_COMPILE="$(CROSS_COMPILE)" sh tools/build_newlib.sh
$(NEWLIB_PREFIX)/aarch64-elf/lib/libc.a:
	NEWLIB_PREFIX="$(NEWLIB_PREFIX)" NEWLIB_BUILD_DIR="$(NEWLIB_BUILD_DIR)" CROSS_COMPILE="$(CROSS_COMPILE)" sh tools/build_newlib.sh
users: $(NEWLIB_PREFIX)/aarch64-elf/lib/libc.a
	$(MAKE) -C usr NEWLIB="$(NEWLIB_PREFIX)" CROSS_COMPILE="$(CROSS_COMPILE)"
build/host/elf_test: tests/elf_test.c kernel/process/elf.c kernel/process/elf_load.c Makefile $(HOST_HEADERS)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_TEST_CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all tests/elf_test.c kernel/process/elf.c kernel/process/elf_load.c -o $@
test-elf: build/host/elf_test
	$<

.PHONY: test-usb test-usb-qemu
build/host/usb_test: tests/usb_test.c drivers/usb/descriptor.c drivers/usb/hid_report.c drivers/usb/keymap.c Makefile $(HOST_HEADERS)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_TEST_CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all tests/usb_test.c drivers/usb/descriptor.c drivers/usb/hid_report.c drivers/usb/keymap.c -o $@
test-usb: build/host/usb_test
	$<
test-usb-qemu: users drivers
	$(MAKE) TARGET=qemu all
	$(PYTHON) tools/usb_qemu_test.py --qemu $(QEMU) --kernel build/$(PLATFORM)/qemu/kernel8.img

.PHONY: test-drivers test-xhci-qemu test-modules-qemu
build/host/pcie_test: tests/pcie_test.c drivers/pci/bcm2711.c Makefile $(HOST_HEADERS)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_TEST_CFLAGS) -fsanitize=address,undefined tests/pcie_test.c drivers/pci/bcm2711.c -o $@
build/host/module_test: tests/module_test.c kernel/module/image.c Makefile $(HOST_HEADERS)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_TEST_CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all tests/module_test.c kernel/module/image.c -o $@
test-drivers: drivers build/host/pcie_test build/host/module_test
	build/host/pcie_test
	build/host/module_test $(BOOT_DRIVER_FILES) build/drivers/VIDEO.EKM build/drivers/HID.EKM build/drivers/HELLO.EKM build/drivers/PI4USB.EKM build/drivers/STORAGE.EKM
ifeq ($(shell uname -s),Darwin)
HOST_SHARED := -dynamiclib
else
HOST_SHARED := -shared
endif
build/host/xhci.so: tests/xhci_bridge.c drivers/usb/xhci.c drivers/usb/xhci_protocol.c drivers/usb/enumerate.c drivers/usb/descriptor.c Makefile $(HOST_HEADERS)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_TEST_CFLAGS) -O2 -ffreestanding -fno-builtin -fPIC $(HOST_SHARED) tests/xhci_bridge.c drivers/usb/xhci.c drivers/usb/xhci_protocol.c drivers/usb/enumerate.c drivers/usb/descriptor.c -o $@
test-xhci-qemu: build/host/xhci.so
	$(PYTHON) tools/xhci_qtest.py --qemu $(QEMU)
test-modules-qemu: drivers
	$(MAKE) TARGET=qemu all
	$(PYTHON) tools/module_qemu_test.py --qemu $(QEMU) --kernel build/$(PLATFORM)/qemu/kernel8.img

FAT_HOST_SRCS := fs/fat/vfs.c third_party/fatfs/ff.c third_party/fatfs/ffunicode.c tests/fat_memory.c drivers/block/block.c fs/vfs/vfs.c kernel/sync/gate.c tests/support/runtime.c
FAT_HOST_HEADERS := $(wildcard third_party/fatfs/*.h)
.PHONY: test-fat-family
build/host/fat_family_test: tests/fat_family_test.c $(FAT_HOST_SRCS) Makefile $(HOST_HEADERS) $(FAT_HOST_HEADERS)
	@mkdir -p $(dir $@)
	$(HOSTCC) -Itests/support $(HOST_TEST_CFLAGS) -DEKITOS_FATFS_TYPES -DEKITOS_FATFS_FORMAT -fsanitize=address,undefined -fno-sanitize-recover=all tests/fat_family_test.c $(FAT_HOST_SRCS) -o $@
test-fat-family: build/host/fat_family_test
	$(PYTHON) tools/fat_images.py build/tests/fat-family
	build/host/fat_family_test build/tests/fat-family/fat12.img build/tests/fat-family/fat16.img build/tests/fat-family/fat32.img build/tests/fat-family/exfat.img build/tests/fat-family/exfat4k.img build/tests/fat-family/exfat-mbr.img build/tests/fat-family/exfat-gpt.img
build/host/xhci_storage.so: tests/xhci_bridge.c drivers/usb/xhci.c drivers/usb/xhci_protocol.c drivers/usb/msc.c drivers/usb/enumerate.c drivers/usb/descriptor.c $(FAT_HOST_SRCS) Makefile $(HOST_HEADERS) $(FAT_HOST_HEADERS)
	@mkdir -p $(dir $@)
	$(HOSTCC) -Itests/support $(HOST_TEST_CFLAGS) -DEKITOS_FATFS_TYPES -DEKITOS_STORAGE_TEST -O2 -ffreestanding -fno-builtin -fPIC $(HOST_SHARED) tests/xhci_bridge.c drivers/usb/xhci.c drivers/usb/xhci_protocol.c drivers/usb/msc.c drivers/usb/enumerate.c drivers/usb/descriptor.c $(FAT_HOST_SRCS) -o $@
.PHONY: test-storage-qemu test-storage-xhci
# Fixture generation is explicit and only touches ignored build/tests images.
test-storage-qemu: users drivers test-fat-family
	$(MAKE) TARGET=qemu all
	$(PYTHON) tools/storage_qemu_test.py --qemu $(QEMU) --kernel build/$(PLATFORM)/qemu/kernel8.img
test-storage-xhci: build/host/xhci_storage.so test-fat-family
	$(PYTHON) tools/storage_xhci_test.py --qemu $(QEMU)
.PHONY: test-msc
build/host/msc_test: tests/msc_test.c drivers/usb/msc.c Makefile $(HOST_HEADERS)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_TEST_CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all tests/msc_test.c drivers/usb/msc.c -o $@
test-msc: build/host/msc_test
	$<
.PHONY: test-exfat-large
test-exfat-large: users drivers build/host/fat_family_test
	$(MAKE) TARGET=qemu all
	$(PYTHON) tools/test_large_exfat.py --qemu $(QEMU) --kernel build/$(PLATFORM)/qemu/kernel8.img
.PHONY: test-superspeed test-superspeed-qemu
build/host/superspeed_test: tests/superspeed_test.c drivers/usb/descriptor.c drivers/usb/enumerate.c drivers/usb/xhci_protocol.c Makefile $(HOST_HEADERS)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_TEST_CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all tests/superspeed_test.c drivers/usb/descriptor.c drivers/usb/enumerate.c drivers/usb/xhci_protocol.c -o $@
test-superspeed: build/host/superspeed_test
	$<
test-superspeed-qemu: build/host/xhci_storage.so test-fat-family
	$(PYTHON) tools/storage_xhci_test.py --super --qemu $(QEMU)
.PHONY: test-unix-qemu
test-unix-qemu: users drivers build/drivers/IRQTEST.EKM
	$(MAKE) TARGET=qemu all
	$(PYTHON) tools/unix_qemu_test.py --qemu $(QEMU) --kernel build/$(PLATFORM)/qemu/kernel8.img

.PHONY: test-terminal
build/host/lineedit_test: tests/lineedit_test.c usr/runtime/lineedit.c usr/runtime/lineedit.h Makefile
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_TEST_CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all tests/lineedit_test.c usr/runtime/lineedit.c -o $@
build/host/video_test: tests/video_test.c $(VIDEO_SOURCES) $(HOST_HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_TEST_CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all tests/video_test.c $(VIDEO_SOURCES) -o $@
test-terminal: build/host/lineedit_test build/host/video_test
	build/host/lineedit_test
	build/host/video_test

.PHONY: test-terminal-qemu run-video
test-terminal-qemu: users drivers
	$(MAKE) TARGET=qemu all
	$(PYTHON) tools/terminal_qemu_test.py --qemu $(QEMU) --kernel build/$(PLATFORM)/qemu/kernel8.img
# Use cocoa on macOS, gtk/sdl on Linux. Keep run headless for UART-only use.
QEMU_DISPLAY ?= cocoa
run-video:
	$(MAKE) TARGET=qemu all
	$(PYTHON) tools/run_image.py "$(SD_IMAGE)"
	$(QEMU) -M raspi4b -m 2g -display $(QEMU_DISPLAY) -monitor none \
	 -kernel build/$(PLATFORM)/qemu/kernel8.img \
	 -drive "if=sd,file=$(SD_IMAGE),format=raw" -serial null -serial stdio \
	 -device usb-kbd,bus=usb-bus.0 -device usb-mouse,bus=usb-bus.0

.PHONY: test-uart test-uart-qemu
build/host/uart_test: tests/uart_test.c drivers/uart/uart.c tests/support/runtime.c Makefile $(HOST_HEADERS)
	@mkdir -p $(dir $@)
	$(HOSTCC) -Itests/support $(HOST_TEST_CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all tests/uart_test.c drivers/uart/uart.c tests/support/runtime.c -o $@
test-uart: build/host/uart_test
	$<
test-uart-qemu: drivers
	$(MAKE) TARGET=hardware all
	$(PYTHON) tools/uart_qemu_test.py --qemu $(QEMU) --kernel build/$(PLATFORM)/hardware/kernel8.img

.PHONY: test-recovery-qemu
test-recovery-qemu: users drivers
	$(MAKE) TARGET=qemu all
	$(PYTHON) tools/recovery_qemu_test.py --qemu $(QEMU) --kernel build/$(PLATFORM)/qemu/kernel8.img

.PHONY: test-ipc
test: test-ipc
build/host/ipc_test: tests/ipc_test.c kernel/ipc/objects.c tests/ipc_support/arch/memory.h Makefile $(HOST_HEADERS)
	@mkdir -p $(dir $@)
	$(HOSTCC) -Itests/ipc_support -Itests/support $(HOST_TEST_CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all tests/ipc_test.c kernel/ipc/objects.c -o $@
test-ipc: build/host/ipc_test
	$<

.PHONY: test-ipc-qemu test-display-qemu
test-ipc-qemu: users drivers
	$(MAKE) TARGET=qemu all
	$(PYTHON) tools/ipc_qemu_test.py --qemu $(QEMU) --kernel build/$(PLATFORM)/qemu/kernel8.img
test-display-qemu: users drivers
	$(MAKE) TARGET=qemu all
	$(PYTHON) tools/display_qemu_test.py --qemu $(QEMU) --kernel build/$(PLATFORM)/qemu/kernel8.img
	$(PYTHON) tools/boot_snapshot_test.py --qemu $(QEMU) --kernel build/$(PLATFORM)/qemu/kernel8.img
