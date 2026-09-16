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
CFLAGS = -m32 -ffreestanding -c -fno-builtin
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
		-d guest_errors,unimp,int -D /tmp/qemu-debug.log
		

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