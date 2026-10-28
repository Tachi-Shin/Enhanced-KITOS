# Invoke from the repository root, or provide SDK_ROOT, BUILD_DIR and absolute
# SOURCES. No Newlib/OS libc is required by a freestanding driver.
ifndef SDK_ROOT
SDK_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
endif
CROSS_COMPILE ?= aarch64-elf-
DRIVER ?= HELLO
SOURCES ?= drivers/modules/hello.c
BUILD_DIR ?= $(SDK_ROOT)/build/drivers
OBJ_DIR := $(BUILD_DIR)/obj/$(DRIVER)
COMMON := $(SDK_ROOT)/lib/memcpy.c $(SDK_ROOT)/lib/memset.c $(SDK_ROOT)/lib/memcmp.c $(SDK_ROOT)/lib/string.c
ALL_SOURCES := $(SOURCES) $(COMMON)
OBJECTS := $(addprefix $(OBJ_DIR)/,$(ALL_SOURCES:.c=.o))
FLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector \
 -fPIC -fvisibility=hidden -fno-asynchronous-unwind-tables -fno-unwind-tables -DEKITOS_MODULE -I$(SDK_ROOT)/include -I$(SDK_ROOT)/arch/arm64/include
TARGET_FLAGS := -mgeneral-regs-only -mstrict-align -mno-outline-atomics
.DEFAULT_GOAL := all
.PHONY: all native
all: $(BUILD_DIR)/$(DRIVER).EKM
$(OBJ_DIR)/%.o: %.c $(SDK_ROOT)/sdk/driver.mk
	@mkdir -p $(dir $@)
	$(CROSS_COMPILE)gcc $(FLAGS) $(TARGET_FLAGS) -MMD -MP -c $< -o $@
$(BUILD_DIR)/$(DRIVER).EKM: $(OBJECTS) $(SDK_ROOT)/sdk/driver.mk
	$(CROSS_COMPILE)gcc -shared -nostdlib -Wl,--no-undefined,-Bsymbolic,-z,text,-z,max-page-size=4096,-e,ek_driver_manifest $(OBJECTS) -o $@
NATIVE_CC ?= cc
ifeq ($(shell uname -s),Darwin)
NATIVE_LINK := -dynamiclib
else
NATIVE_LINK := -shared -Wl,--no-undefined
endif
native:
	@mkdir -p $(BUILD_DIR)/native
	$(NATIVE_CC) $(FLAGS) $(NATIVE_LINK) $(ALL_SOURCES) -o $(BUILD_DIR)/native/$(DRIVER).so
-include $(OBJECTS:.o=.d)
