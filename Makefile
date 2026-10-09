# Libai kernel and EFI loader.
# Usage: make && make qemu
# Debug: make qemu-gdb    then F5 in VSCode, or make gdb

BUILD := build
ESP := $(BUILD)/esp
OVMF ?= /usr/share/OVMF/OVMF_CODE_4M.fd
EFI_LDS ?= /usr/lib/elf_x86_64_efi.lds

KERNEL_SRCS := \
	kernel/main.c \
	mm/pmm.c \
	mm/vmm.c \
	mm/kmalloc.c \
	arch/x86_64/idt.c \
	arch/x86_64/irq.c \
	arch/x86_64/task.c \
	arch/x86_64/kbd.c \
	arch/x86_64/gdt.c \
	arch/x86_64/user.c

KERNEL_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(KERNEL_SRCS))

KERNEL_CFLAGS := -Iinclude -mcmodel=kernel -ffreestanding -fno-stack-protector \
	-fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables \
	-fno-exceptions -mno-red-zone -g -O0 -Wall -Wextra

LOADER_CFLAGS := -Iinclude -I/usr/include/efi -I/usr/include/efi/x86_64 \
	-fno-stack-protector -fpic -fshort-wchar -mno-red-zone \
	-DEFI_FUNCTION_WRAPPER -g -O0 -Wall -Wextra

.PHONY: all kernel loader esp qemu qemu-gdb gdb clean

all: esp

kernel: $(BUILD)/libai-kernel.elf

loader: $(BUILD)/BOOTX64.EFI

esp: $(ESP)/EFI/BOOT/BOOTX64.EFI $(ESP)/libai-kernel.elf

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	gcc $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD)/libai-kernel.elf: $(KERNEL_OBJS) kernel/linker.ld
	ld -nostdlib -T kernel/linker.ld $(KERNEL_OBJS) -o $@

$(BUILD)/loader.o: boot/efi/loader.c
	@mkdir -p $(dir $@)
	gcc $(LOADER_CFLAGS) -c $< -o $@

$(BUILD)/loader.so: $(BUILD)/loader.o
	ld -nostdlib -znocombreloc -T $(EFI_LDS) -shared -Bsymbolic \
		-L/usr/lib /usr/lib/crt0-efi-x86_64.o $< -o $@ -lefi -lgnuefi

$(BUILD)/BOOTX64.EFI: $(BUILD)/loader.so
	objcopy -j .text -j .sdata -j .data -j .dynamic \
		-j .dynsym -j .rel -j .rela -j .reloc \
		--target=efi-app-x86_64 $< $@

$(ESP)/EFI/BOOT/BOOTX64.EFI: $(BUILD)/BOOTX64.EFI
	@mkdir -p $(dir $@)
	cp $< $@

$(ESP)/libai-kernel.elf: $(BUILD)/libai-kernel.elf
	@mkdir -p $(dir $@)
	cp $< $@

qemu: esp
	qemu-system-x86_64 -machine q35 -m 512M -nographic \
		-serial mon:stdio \
		-monitor telnet:127.0.0.1:45454,server,nowait \
		-drive if=pflash,format=raw,readonly=on,file=$(OVMF) \
		-drive format=raw,file=fat:rw:$(ESP)

qemu-gdb: esp
	@echo "gdbstub ready"
	qemu-system-x86_64 -machine q35 -m 512M -nographic \
		-serial mon:stdio -s -S \
		-monitor telnet:127.0.0.1:45454,server,nowait \
		-drive if=pflash,format=raw,readonly=on,file=$(OVMF) \
		-drive format=raw,file=fat:rw:$(ESP)

gdb: kernel
	gdb -x scripts/gdbinit $(BUILD)/libai-kernel.elf

clean:
	rm -rf $(BUILD)