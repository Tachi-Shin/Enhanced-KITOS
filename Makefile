.DEFAULT_GOAL := all
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
 -fno-asynchronous-unwind-tables -O2 -g -Iinclude -Iarch/$(ARCH)/include
ASFLAGS := -g -Iinclude
C_SRCS := $(sort $(wildcard arch/arm64/kernel/*.c kernel/*/*.c \
 drivers/block/*.c drivers/char/*.c drivers/gpio/*.c drivers/gpio/bcm2711/*.c \
 drivers/mmc/*.c drivers/mailbox/*.c drivers/uart/uart.c fs/*/*.c lib/*.c \
 platform/$(PLATFORM)/common/*.c) $(PLATFORM_SRCS) tests/kernel_regression.c tests/memory_regression.c)
S_SRCS := $(BOOT_SOURCE) arch/arm64/kernel/context_switch.S arch/arm64/kernel/exception.S tests/user/probe.S
OBJS := $(addprefix $(BUILD_DIR)/,$(C_SRCS:.c=.c.o) $(S_SRCS:.S=.S.o))
ELF := $(BUILD_DIR)/kernel8.elf
IMG := $(BUILD_DIR)/kernel8.img
.PHONY: all image clean run dump symbols test test-block test-fat32 test-qemu
all: image
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
SD_IMAGE ?= build/tests/qemu.img
run:
	$(MAKE) PLATFORM=$(PLATFORM) TARGET=qemu all
	@test -f "$(SD_IMAGE)" || $(PYTHON) tools/test_images.py "$(SD_IMAGE)"
	$(QEMU) -M raspi4b -m 2g -display none -monitor none \
	 -kernel build/$(PLATFORM)/qemu/kernel8.img \
	 -drive "if=sd,file=$(SD_IMAGE),format=raw" -serial null -serial stdio
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
test: test-block test-fat32 test-vfs test-timer test-sdhci test-sd test-geometry test-page test-boot-memory test-elf
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
.PHONY: newlib users test-elf
newlib:
	sh tools/build_newlib.sh
$(NEWLIB_PREFIX)/aarch64-elf/lib/libc.a:
	sh tools/build_newlib.sh
users: $(NEWLIB_PREFIX)/aarch64-elf/lib/libc.a
	$(MAKE) -C usr NEWLIB=$(NEWLIB_PREFIX) CROSS_COMPILE=$(CROSS_COMPILE)
build/host/elf_test: tests/elf_test.c kernel/process/elf.c kernel/process/elf_load.c Makefile $(HOST_HEADERS)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_TEST_CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all tests/elf_test.c kernel/process/elf.c kernel/process/elf_load.c -o $@
test-elf: build/host/elf_test
	$<
