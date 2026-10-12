# P5OS build.  Uses the host GCC in 32-bit freestanding mode (no cross
# compiler needed), GNU ld/objcopy, NASM and Python 3.

CC      := gcc
LD      := ld
OBJCOPY := objcopy
OBJDUMP := objdump
NASM    := nasm
PYTHON  := python3
QEMU    := qemu-system-i386

BUILD   := build
IMAGES  := images
STAGE2_SECTORS := 16
HDD_MB  := 8
DATA_MB := 4

# -march=pentium: never emit i686+ instructions (CMOV, FCOMI, SSE ...).
# -mgeneral-regs-only: no FPU/MMX/SSE code in the kernel at all.
CFLAGS  := -m32 -march=pentium -mtune=pentium -std=gnu11 -O2 -g \
           -ffreestanding -fno-builtin -nostdinc -nostdlib \
           -fno-pic -fno-pie -fno-stack-protector -fno-omit-frame-pointer \
           -fno-asynchronous-unwind-tables -fno-strict-aliasing \
           -fno-delete-null-pointer-checks -fno-isolate-erroneous-paths-dereference \
           -mgeneral-regs-only -mno-red-zone \
           -Wall -Wextra -Wshadow -Wstrict-prototypes -Wmissing-prototypes \
           -Wno-unused-parameter -Iinclude
ifeq ($(WERROR),1)
CFLAGS  += -Werror
endif
LDFLAGS := -m elf_i386 -nostdlib -static --no-dynamic-linker --no-warn-rwx-segments
NASMFLAGS_K := -f elf32 -g

C_SRCS   := $(shell find kernel -name '*.c' | sort)
ASM_SRCS := $(shell find kernel -name '*.asm' | sort)
OBJS     := $(C_SRCS:%.c=$(BUILD)/%.o) $(ASM_SRCS:%.asm=$(BUILD)/%.asm.o)
DEPS     := $(C_SRCS:%.c=$(BUILD)/%.d)

FLOPPY_IMG := $(IMAGES)/p5os-floppy.img
HDD_IMG    := $(IMAGES)/p5os-hdd.img
DATA_IMG   := $(BUILD)/data.img

.DELETE_ON_ERROR:

.PHONY: all clean distclean images check-isa run run-hdd run-floppy run-isapc debug test data-disk size

all: images check-isa size

images: $(FLOPPY_IMG) $(HDD_IMG)

$(BUILD)/%.o: %.c Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.asm.o: %.asm
	@mkdir -p $(dir $@)
	$(NASM) $(NASMFLAGS_K) $< -o $@

$(BUILD)/kernel.elf: $(OBJS) kernel/linker.ld
	$(LD) $(LDFLAGS) -T kernel/linker.ld -Map $(BUILD)/kernel.map -o $@ $(OBJS)

$(BUILD)/kernel.bin: $(BUILD)/kernel.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/stage1.bin: boot/stage1.asm
	@mkdir -p $(BUILD)
	$(NASM) -f bin -DSTAGE2_SECTORS=$(STAGE2_SECTORS) $< -o $@

# stage 2 needs the kernel size (in sectors) baked in
$(BUILD)/stage2.bin: boot/stage2.asm $(BUILD)/kernel.bin
	$(NASM) -f bin -DSTAGE2_SECTORS=$(STAGE2_SECTORS) \
	  -DKERNEL_SECTORS=$$(( ($$(stat -c %s $(BUILD)/kernel.bin) + 511) / 512 )) $< -o $@

MKIMAGE_DEPS := $(BUILD)/stage1.bin $(BUILD)/stage2.bin $(BUILD)/kernel.bin \
                tools/mkimage.py tools/p5fs.py $(wildcard rootfs/*)

$(FLOPPY_IMG): $(MKIMAGE_DEPS)
	@mkdir -p $(IMAGES)
	$(PYTHON) tools/mkimage.py floppy --out $@ --stage1 $(BUILD)/stage1.bin \
	  --stage2 $(BUILD)/stage2.bin --kernel $(BUILD)/kernel.bin

$(HDD_IMG): $(MKIMAGE_DEPS)
	@mkdir -p $(IMAGES)
	$(PYTHON) tools/mkimage.py hdd --out $@ --size-mb $(HDD_MB) --stage1 $(BUILD)/stage1.bin \
	  --stage2 $(BUILD)/stage2.bin --kernel $(BUILD)/kernel.bin --rootfs rootfs

# Separate scratch disk for write tests (whole-disk P5FS, no boot code).
data-disk: $(DATA_IMG)
$(DATA_IMG): tools/p5fs.py
	@mkdir -p $(BUILD)
	$(PYTHON) tools/p5fs.py mkfs $@ --size-mb $(DATA_MB) --label DATA

# Fail the build if the kernel contains instructions the P5 lacks.
check-isa: $(BUILD)/kernel.elf
	@$(PYTHON) tools/check_isa.py $(BUILD)/kernel.elf

size: $(BUILD)/kernel.elf
	@$(PYTHON) tools/kernel_size.py $(BUILD)/kernel.elf $(BUILD)/kernel.bin

QEMU_BASE := $(QEMU) -cpu pentium -m 16
run: run-hdd
run-hdd: $(HDD_IMG) $(DATA_IMG)
	$(QEMU_BASE) -drive file=$(HDD_IMG),format=raw,index=0,media=disk \
	  -drive file=$(DATA_IMG),format=raw,index=1,media=disk -boot c -serial stdio
run-floppy: $(FLOPPY_IMG) $(DATA_IMG)
	$(QEMU_BASE) -drive file=$(FLOPPY_IMG),format=raw,if=floppy \
	  -drive file=$(DATA_IMG),format=raw,index=0,media=disk -boot a -serial stdio
run-isapc: $(FLOPPY_IMG)
	$(QEMU_BASE) -M isapc -drive file=$(FLOPPY_IMG),format=raw,if=floppy -boot a -serial stdio
debug: $(HDD_IMG)
	@echo "Connect with: gdb $(BUILD)/kernel.elf -ex 'target remote :1234'"
	$(QEMU_BASE) -drive file=$(HDD_IMG),format=raw,index=0,media=disk -boot c \
	  -serial stdio -s -S

test: images
	$(PYTHON) tests/run_tests.py

clean:
	rm -rf $(BUILD)

distclean: clean
	rm -f $(FLOPPY_IMG) $(HDD_IMG)

-include $(DEPS)
