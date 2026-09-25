UNAME_S := $(shell uname -s)

NASM = nasm
GENISO = genisoimage

ifeq ($(UNAME_S),Darwin)
    CC   = i686-elf-gcc
    LD   = i686-elf-ld
    GRUB_MKRESCUE = $(shell command -v i686-elf-grub-mkrescue 2>/dev/null || echo grub-mkrescue)
    QEMU_AUDIO   = -audiodev coreaudio,id=snd0
    QEMU_DISPLAY = -display cocoa,zoom-to-fit=on,full-screen=on
	QEMU_ACCEL = -cpu max -smp cpus=4,cores=4
else
    CC   = gcc
    LD   = ld
    GRUB_MKRESCUE = grub-mkrescue
    QEMU_ACCEL   = -enable-kvm
    QEMU_AUDIO   = -audiodev pa,id=snd0
    QEMU_DISPLAY = -display gtk,zoom-to-fit=on,full-screen=on
endif

NASM_FLAGS = -f elf32
GCC_INC := $(shell $(CC) -m32 -print-file-name=include)
CFLAGS = -m32 -ffreestanding -c -fno-builtin -nostdinc -isystem $(GCC_INC) -I.
LD_FLAGS = -m elf_i386 -T link.ld

C_SRC := $(wildcard *.c)
ASM_SRC := $(wildcard *.s)
OBJ := $(patsubst %.c,%.o,$(C_SRC)) $(patsubst %.s,%.o,$(ASM_SRC))
ISO_DIR = iso
GRUB_DIR = $(ISO_DIR)/boot/grub
ISO = os.iso
KERNEL = ./kernel.elf

all: $(ISO)

%.o: %.c
	$(CC) $(CFLAGS) $< -o $@

%.o: %.s
	$(NASM) $(NASM_FLAGS) $< -o $@

# ---------------------------------------------------------------------------
# TinyCC: in-OS C compiler (see tccport/ and the `cc` shell command)
# Needs a HOST compiler for two tiny build-time generators (tcc's c2str and
# tccport/mkembed).  Override with e.g. `make HOSTCC=gcc`.
# ---------------------------------------------------------------------------
HOSTCC ?= cc
TCC_DIR = tinycc
ifneq ($(wildcard $(TCC_DIR)/config.h),)
$(error $(TCC_DIR)/config.h exists (left by tinycc's ./configure?) and would override tccport/config.h - delete it)
endif
TCCPORT = tccport
TCC_GEN = $(TCCPORT)/gen
TCC_CFLAGS = -m32 -ffreestanding -c -fno-builtin -nostdinc -isystem $(GCC_INC) \
             -I$(TCCPORT)/include -I. -I$(TCC_GEN) -I$(TCCPORT) -I$(TCC_DIR) \
             -O2 -w -fno-strict-aliasing -fno-stack-protector -fno-pie -fcf-protection=none \
             -mno-sse -mno-mmx -fno-tree-loop-distribute-patterns
TCC_OBJ = $(TCC_DIR)/libtcc.o $(TCCPORT)/tcc1.o $(TCCPORT)/tcc_libc.o \
          $(TCCPORT)/tcc_vfs.o $(TCCPORT)/tcc_kernel.o
TCC_EMBED = $(addprefix $(TCC_DIR)/include/,stdarg.h stddef.h stdbool.h float.h stdalign.h \
                                            stdnoreturn.h tgmath.h varargs.h stdatomic.h) \
            $(wildcard $(TCCPORT)/guest/include/*.h)
OBJ += $(TCC_OBJ)

$(TCC_GEN)/c2str: $(TCC_DIR)/conftest.c
	mkdir -p $(TCC_GEN)
	$(HOSTCC) -DC2STR $< -o $@
$(TCC_GEN)/tccdefs_.h: $(TCC_DIR)/include/tccdefs.h $(TCC_GEN)/c2str
	$(TCC_GEN)/c2str $< $@
$(TCC_GEN)/mkembed: $(TCCPORT)/mkembed.c
	mkdir -p $(TCC_GEN)
	$(HOSTCC) $< -o $@
$(TCC_GEN)/tcc_embedded.h: $(TCC_GEN)/mkembed $(TCC_EMBED)
	$(TCC_GEN)/mkembed $@ $(TCC_EMBED)

# libtcc is built as ONE_SOURCE (libtcc.c #includes the rest of the compiler)
$(TCC_DIR)/libtcc.o: $(TCC_DIR)/libtcc.c $(wildcard $(TCC_DIR)/*.c $(TCC_DIR)/*.h) \
                     $(TCCPORT)/config.h $(TCCPORT)/tcc_io.h $(TCC_GEN)/tccdefs_.h
	$(CC) $(TCC_CFLAGS) -DONE_SOURCE=1 -include $(TCCPORT)/tcc_io.h $< -o $@
$(TCCPORT)/tcc1.o $(TCCPORT)/tcc_libc.o $(TCCPORT)/tcc_vfs.o $(TCCPORT)/tcc_kernel.o: $(TCCPORT)/%.o: $(TCCPORT)/%.c $(TCC_GEN)/tcc_embedded.h $(TCCPORT)/tcc_kernel.h
	$(CC) $(TCC_CFLAGS) $< -o $@

kernel.elf: $(OBJ)
	$(LD) $(LD_FLAGS) $(OBJ) -o $(KERNEL)

$(ISO): $(KERNEL)
	rm -rf $(ISO_DIR)
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL) $(ISO_DIR)/boot/
	echo "set timeout=5" > $(ISO_DIR)/boot/grub/grub.cfg
	echo "insmod all_video" >> $(ISO_DIR)/boot/grub/grub.cfg
	echo "set default=1" >> $(ISO_DIR)/boot/grub/grub.cfg

	#GRAPHICAL MODE
	echo "menuentry 'Crusader OS' {" >> $(ISO_DIR)/boot/grub/grub.cfg
	echo "  set gfxpayload1920x1080x32" >> $(ISO_DIR)/boot/grub/grub.cfg
	echo "  multiboot /boot/kernel.elf" >> $(ISO_DIR)/boot/grub/grub.cfg
	echo "  boot" >> $(ISO_DIR)/boot/grub/grub.cfg
	echo "}" >> $(ISO_DIR)/boot/grub/grub.cfg
	$(GRUB_MKRESCUE) -o $(ISO) $(ISO_DIR)

clean:
	rm -f *.o $(KERNEL) $(ISO)
	rm -f $(TCC_DIR)/*.o $(TCCPORT)/*.o
	rm -rf $(TCC_GEN)
	rm -rf $(ISO_DIR)
	-rm -f disk.img
	-rm -f disk2.img

run:
	qemu-system-i386 -cdrom os.iso -boot d \
		-drive file=disk.img,format=raw,bus=0,unit=0,media=disk \
		-drive file=disk2.img,format=raw,bus=0,unit=1,media=disk \
		$(QEMU_AUDIO) -device ac97,audiodev=snd0 \
		-device pci-ohci,id=ohci \
		-device usb-ehci,id=ehci \
		-device qemu-xhci,id=xhci \
		-device usb-kbd,bus=xhci.0 \
		-device usb-mouse,bus=ohci.0 \
		-m 8G -vga std -serial stdio $(QEMU_ACCEL) \
		-device usb-host,vendorid=0x067b,productid=0x23a3,bus=ohci.0 \
		$(QEMU_DISPLAY) \
		-d int,cpu_reset,guest_errors,unimp -D /tmp/qemu-debug.log
		

dd_second:
	dd if=/dev/zero of=disk2.img bs=1M count=64 status=progress
dd32:
	dd if=/dev/zero of=disk.img bs=1M count=32 status=progress
dd128:
	dd if=/dev/zero of=disk.img bs=1M count=128 status=progress
dd4:
	dd if=/dev/zero of=disk.img bs=1M count=4 status=progress
dd1:
	dd if=/dev/zero of=disk.img bs=1GB count=1 status=progress
hd:
	hexdump -C disk.img | less
a:
	make clean
	make dd128
	make dd_second
	make 
	make run
	FINISHED
d: 	
	qemu-system-i386 -cdrom os.iso -no-reboot -d int,cpu_reset