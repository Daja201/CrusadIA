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
CRT0_OBJ = crt0.o syscall.o

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

SQLITE_DIR = thirdparty/sqlite
SQLITE_CFLAGS = $(CFLAGS) -I $(SQLITE_DIR) -include $(SQLITE_DIR)/sqlite_cfg.h \
                -DNDEBUG -O1 -w

$(SQLITE_DIR)/sqlite3.o: $(SQLITE_DIR)/sqlite3.c $(SQLITE_DIR)/sqlite3.h $(SQLITE_DIR)/sqlite_cfg.h
	$(CC) $(SQLITE_CFLAGS) $< -o $@

$(SQLITE_DIR)/os_crusadia.o: $(SQLITE_DIR)/os_crusadia.c $(SQLITE_DIR)/sqlite3.h
	$(CC) $(SQLITE_CFLAGS) $< -o $@

$(SQLITE_DIR)/sqlite_shell.o: $(SQLITE_DIR)/sqlite_shell.c $(SQLITE_DIR)/sqlite3.h
	$(CC) $(SQLITE_CFLAGS) $< -o $@

sqlite.elf: $(SQLITE_DIR)/sqlite_shell.o $(SQLITE_DIR)/os_crusadia.o $(SQLITE_DIR)/sqlite3.o libc.a $(CRT0_OBJ)
	$(LD) $(LDFLAGS) $(CRT0_OBJ) $(SQLITE_DIR)/sqlite_shell.o $(SQLITE_DIR)/os_crusadia.o \
	      $(SQLITE_DIR)/sqlite3.o libc.a -o $@

clean:
	rm -f libc.a $(LIBC_OBJ) $(CRT0_OBJ) examples/*.o examples/*.elf
	rm -f $(SQLITE_DIR)/*.o sqlite.elf

.PHONY: clean hello sqlite
