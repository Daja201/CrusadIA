# Crusader-OS-v03
Open source operating system build for industrial automatization. Build for x86 processors. Build on Crusader-OS-v03 which was build on Crusader-OS-v02.... Made for SOČ - Czech highschool competition.
## Introduction
This operating system is build by me, a IT high school student. It IS NOT a fully functioning operating system like windows or debian. It is more like a learning engine build to help understanding how low-level programming in C and Assembly works. First version of this operating system started as my first C project ever so you shouldn't expect much, even thought later it did grow up to something i thought i'd never achieve in this area. Please note that I AM NOT responsible for any harm my OS does to your hardware if you are (excuse my dictionary) dumb enough to trey to boot it on hardware. I did that and yeah, it can work but I highly recommend using a virtual machine.
## Sources
[OSDEV wiki, the best thing on whole internet](URL "https://wiki.osdev.org/")

[Little OS book](URL "https://littleosbook.github.io/")

My friend L.M.<3 with her crazy ideas

## How to compile and run
I use **make** which helps you with the whole compilation process. As compiler and assembler, I chose **NASM** and **gcc**. By running **"make a"**, you should get the whole code recompiled and it should run automatically. Since this isn't something hard to do, you should be able to kinda debug this part alone if some error occurs. You also have to have all needed software programs installed like compilers and other. 

## How does it boot?
Well, first of all, I use GRUB as my bootloader. Makefile builds bootable .iso with grub-mkrescue. BIOS hands off to GRUB which sets video mode set from my **grub.cfg** and loads built **kernel.elf** to RAM.Then jumps to its entry point on adress 0x00100000 (1MB). Entry point is located in **loader.s** as "loader" function. The "multiboot" section in **loader.s** holds other information like magic number standart for GRUB's Multiboot 1 (0x1BADB002), also other flags like request for linear framebuffer and video mode data (1920x1080x32, full HD 32bits color depth). **Loader.s** sets 64KB stack "kernel_stack" and pushes multiboot info pointer there. Then it calls kmain() from **kernel.c**.  If it ever returns, it genuely stops CPU. **Kernel.c** validates magic number. Reads memory map (marks usable) and pulls out frame buffer data set by GRUB. Physical memory manager (pmm_init()) sets usable RAM. Interrupt descriptor table starts. Then **vesa.c** starts framebuffer. **Paging.c** enmables paging and maps the framebuffer for vesa. Some GUI thing start. Then the filesystem gets initialized. PIT timerr gets set for 1000Hz tick. Multitasking starts and it runs "system_main_task" which manages redrawing screen and managing USB devices etc. AC97 audio driver starts. **Usb.c** and **pci.c** scans PIC for USB controllers (OHCI/EHCI). **"Asm volatile("sti");"** enables interrupts and an infinite loop ends kmain().

## Memory management
RAM is divided to 4KB blocks. Availability is tracked via bitmap (pmm_bitmap). While initializing RAM, pmm_init automatically marks everything as used (1 in bitmap). When it gets info from **bootinfo.c**, it marks actual usable ram with 0's. Then it again puts 1's to places like first MB where BIOS is and next 4MB where kernel is to provide safety for those importatnt software parts. **Paging.c** implements virtual memory management which acts like a cover for real memory adresses, maps them to a virtual one which can be called by programs. 

## Graphics (VESA / framebuffer)
There's no real "driver" for graphics. **Kernel.c** just reads the framebuffer info out of the multiboot info structure and hands it to vesa_init_from_params() in **vesa.c**. From there I have a linear framebuffer pointer (lfb) which I basically just poke pixels into with put_pixel_32(). Because writing directly to the LFB for every single pixel is painfully slow (lots of flickering too), I made a second buffer in RAM called back (sitting at a fixed physical address, 0x800000) which acts as a backbuffer. Text rendering is done with my  tiny bitmap font (font.c/font.h).

## Drivers
I wrote (or at least tried to) a bunch of drivers.

**pci.c** - scans the PCI bus by bruteforcing all 16 busses, 32 devices and 8 functions, using outl/inl on ports 0xCF8/0xCFC (the classic PCI configuration mechanism), and lets other drivers find hardware by class/subclass code (pci_find_class()). Basically everything else, USB and audio included, depends on this file to even find the hardware in the first place.
**usb.c / usbinit.c / usbhub.c / usbhid.c** - a small USB stack. usb.c manages a table of generic usb_device_t structures and does control transfers, ehci.c and ohci.c are the actual host controller drivers (found via PCI class 0x0C/0x03) which get scanned for during boot, usbhub.c handles hub enumeration (because USB devices love hiding behind hubs) and usbhid.c parses HID reports so keyboards and mice actually produce something useful. bioskbd.c exists as a fallback so you're not completely lost before USB gets initialized.
**ac97.c** - driver for the AC97 audio codec, again found through pci.c. It sets up a Buffer Descriptor List (BDL) of 32 entries, DMAs raw PCM chunks read straight off disk into those buffers and pushes them out through the NAM/NABM registers. There's basic volume control and mute support too (AC97_NAM_PCM_OUT_VOL). It's not fancy, no mixing multiple sounds, but it plays audio, which is more than I expected when I started this.
**rtc.c** - reads date/time off the CMOS real time clock so the OS actually knows what time it is instead of just counting ticks since boot.
reboot.c / speaker.c - small utility drivers, one triggers a reset through the keyboard controller, the other beeps the PC speaker (yes, the actual old-school one, I couldn't resist adding it).
**idt.c / interrupts.s** - not a "driver" exactly but this is where all the ISRs (isr0-isr47) live, sets up the Interrupt Descriptor Table and hooks the PIT (Programmable Interval Timer) at 1000Hz which drives both system_ticks and the task scheduler in task.c.

## Filesystem
I have my own COS filesystem in **fs.c**, very unix-inspired, built around a superblock, an inode bitmap, an inode table and indirect block pointers (PTRS_PER_BLOCK) for bigger files, talking to disk through ATA PIO on ports 0x1F0/0x170 (ATA_PRIMARY/ATA_SECONDARY), block size fixed at 512 bytes. It supports multiple drives (g_drives[MAX_DRIVES]), directories, and basic read/write through fs_read()/fs_write(). Also I implemented real FAT32 driver. 


## Multitasking
Multitasking is very simple round robin, task.c keeps a static array of MAX_TASKS (16) tasks, and schedule_handler() gets called on every PIT tick, saves the current task's stack pointer and just moves to the next one in line.

## Shell 
**Terminal.c** is a shell that reads keystrokes, keeps a small command history and tokenizes the typed line into argv/argc, then looks the first word up in a table of registered commands (commands.c/commands.h) and calls the matching function.

## Issues
- shit documentation
- No real memory protection (so ring 0)
- many things work but with small bugs