CROSS_COMPILE ?= aarch64-elf-
CC := $(CROSS_COMPILE)gcc
NEWLIB ?= ../build/newlib
BUILD := ../build/usr
CFLAGS := -O2 -g -Wall -Wextra -Werror -ffunction-sections -fdata-sections -fno-pic -fno-builtin -fno-stack-protector -mno-outline-atomics -MMD -MP -I../include -Iruntime -Iruntime/arm64 -isystem $(NEWLIB)/aarch64-elf/include
ASFLAGS := -g -I../include -MMD -MP
LDFLAGS := -nostdlib -static -Wl,-T,runtime/user.ld -Wl,--gc-sections -L$(NEWLIB)/aarch64-elf/lib
COMMON := $(BUILD)/crt0.o $(BUILD)/newlib.o
OBJECTS := $(COMMON) $(BUILD)/probe.o $(BUILD)/fp.o $(BUILD)/process_limits.o
BIN_COMMANDS := ls cat echo pwd touch mkdir rm rmdir mv cp write append truncate help env uname clear sleep true false
SBIN_COMMANDS := display time ps procs mem uart usb input disks lsmod insmod rmmod mount umount selftest memtest usertest oomtest gpio stop resume kill tsleep
COMMAND_FILES := $(addprefix $(BUILD)/bin/,$(BIN_COMMANDS)) $(addprefix $(BUILD)/sbin/,$(SBIN_COMMANDS))
.PHONY: all
all: $(BUILD)/probe.elf $(BUILD)/bin/sh $(COMMAND_FILES)
all: $(BUILD)/sbin/unix-probe
$(BUILD):
	mkdir -p $@
$(BUILD)/crt0.o: runtime/arm64/crt0.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@
$(BUILD)/newlib.o: runtime/newlib.c runtime/kitos.h runtime/arm64/arch_syscall.h ../include/uapi/syscall.h ../include/uapi/process.h ../include/uapi/error.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/probe.o: programs/probe.c runtime/kitos.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/fp.o: programs/fp.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@
$(BUILD)/process_limits.o: programs/process_limits.c runtime/kitos.h runtime/arm64/arch_syscall.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(OBJECTS): Makefile
$(BUILD)/probe.elf: $(OBJECTS) runtime/user.ld Makefile $(NEWLIB)/aarch64-elf/lib/libc.a $(NEWLIB)/aarch64-elf/lib/libm.a
	$(CC) $(LDFLAGS) $(OBJECTS) -Wl,--start-group -lc -lm -lgcc -Wl,--end-group -o $@
$(BUILD)/commands.o: programs/commands.c runtime/kitos.h ../include/uapi/system.h Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/sh.o: programs/sh.c runtime/kitos.h Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/lineedit.o: runtime/lineedit.c runtime/lineedit.h Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/sh_support.o: programs/sh_support.c programs/sh_support.h runtime/lineedit.h runtime/kitos.h Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(COMMAND_FILES): $(BUILD)/commands.o $(COMMON) runtime/user.ld Makefile
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $(COMMON) $(BUILD)/commands.o -Wl,--start-group -lc -lm -lgcc -Wl,--end-group -o $@
$(BUILD)/bin/sh: $(BUILD)/sh.o $(BUILD)/lineedit.o $(BUILD)/sh_support.o $(COMMON) runtime/user.ld Makefile
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $(COMMON) $(BUILD)/sh.o $(BUILD)/lineedit.o $(BUILD)/sh_support.o -Wl,--start-group -lc -lm -lgcc -Wl,--end-group -o $@
$(BUILD)/unix_probe.o: programs/unix_probe.c runtime/kitos.h Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/sbin/unix-probe: $(BUILD)/unix_probe.o $(COMMON) runtime/user.ld Makefile
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $(COMMON) $(BUILD)/unix_probe.o -Wl,--start-group -lc -lm -lgcc -Wl,--end-group -o $@
-include $(OBJECTS:.o=.d)
-include $(BUILD)/commands.d $(BUILD)/sh.d $(BUILD)/unix_probe.d
-include $(BUILD)/lineedit.d $(BUILD)/sh_support.d

all: $(BUILD)/sbin/ipc-probe
$(BUILD)/ipc_probe.o: programs/ipc_probe.c runtime/kitos.h Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/sbin/ipc-probe: $(BUILD)/ipc_probe.o $(COMMON) runtime/user.ld Makefile
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $(COMMON) $(BUILD)/ipc_probe.o -Wl,--start-group -lc -lm -lgcc -Wl,--end-group -o $@
-include $(BUILD)/ipc_probe.d

all: $(BUILD)/sbin/display-probe
$(BUILD)/display_probe.o: programs/display_probe.c runtime/kitos.h Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/sbin/display-probe: $(BUILD)/display_probe.o $(COMMON) runtime/user.ld Makefile
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $(COMMON) $(BUILD)/display_probe.o -Wl,--start-group -lc -lm -lgcc -Wl,--end-group -o $@
-include $(BUILD)/display_probe.d
