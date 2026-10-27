CROSS_COMPILE ?= aarch64-elf-
CC := $(CROSS_COMPILE)gcc
NEWLIB ?= ../build/newlib
BUILD := ../build/usr
CFLAGS := -O2 -g -Wall -Wextra -Werror -ffunction-sections -fdata-sections -fno-pic -fno-builtin -fno-stack-protector -mno-outline-atomics -I../include -Iruntime -Iruntime/arm64 -isystem $(NEWLIB)/aarch64-elf/include
LDFLAGS := -nostdlib -static -Wl,-T,runtime/user.ld -Wl,--gc-sections -L$(NEWLIB)/aarch64-elf/lib
COMMON := $(BUILD)/crt0.o $(BUILD)/newlib.o
.PHONY: all
all: $(BUILD)/probe.elf
$(BUILD):
	mkdir -p $@
$(BUILD)/crt0.o: runtime/arm64/crt0.S | $(BUILD)
	$(CC) -g -I../include -c $< -o $@
$(BUILD)/newlib.o: runtime/newlib.c runtime/kitos.h runtime/arm64/arch_syscall.h ../include/uapi/syscall.h ../include/uapi/process.h ../include/uapi/error.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/probe.o: programs/probe.c runtime/kitos.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/fp.o: programs/fp.S | $(BUILD)
	$(CC) -g -I../include -c $< -o $@
$(BUILD)/probe.elf: $(COMMON) $(BUILD)/probe.o $(BUILD)/fp.o runtime/user.ld
	$(CC) $(LDFLAGS) $(COMMON) $(BUILD)/probe.o $(BUILD)/fp.o -Wl,--start-group -lc -lm -lgcc -Wl,--end-group -o $@
