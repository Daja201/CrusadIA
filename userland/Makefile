UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
    CC = i686-elf-gcc
    LD = i686-elf-ld
    NASM = nasm
else
    CC = gcc
    LD = ld
    NASM = nasm
endif

GCC_INC := $(shell $(CC) -m32 -print-file-name=include)

CFLAGS = -m32 -ffreestanding -fno-builtin -fno-stack-protector -fno-pie -nostdinc \
         -isystem $(GCC_INC) -I include -c
LDFLAGS = -m elf_i386 -T link_user.ld

LIBC_SRC = $(wildcard libc/*.c)
LIBC_OBJ = $(LIBC_SRC:.c=.o)
CRT0_OBJ = crt0.o

libc.a: $(LIBC_OBJ)
	ar rcs libc.a $(LIBC_OBJ)

%.o: %.c
	$(CC) $(CFLAGS) $< -o $@

%.o: %.s
	$(NASM) -f elf32 $< -o $@

examples/%.elf: examples/%.c libc.a $(CRT0_OBJ)
	$(CC) $(CFLAGS) $< -o $(basename $@).o
	$(LD) $(LDFLAGS) $(CRT0_OBJ) $(basename $@).o libc.a -o $@

hello: examples/hello.elf

clean:
	rm -f libc.a $(LIBC_OBJ) $(CRT0_OBJ) examples/*.o examples/*.elf

.PHONY: clean hello
