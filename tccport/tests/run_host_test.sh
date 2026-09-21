#!/bin/sh
# Runs the REAL port objects (libtcc, tcc_kernel, tcc_vfs, tcc_libc, string.c)
# inside a 32-bit Linux process, with a tiny stand-in for kernel services.
# Verifies the whole compile -> relocate -> run path without booting the OS.
# (your own files are built with your normal flags, i.e. -O0.)
# Needs: gcc -m32 (freestanding only), as, ld.   Run from the repo root:
#   sh tccport/tests/run_host_test.sh
set -e
T=tccport/tests; O=/tmp/crusadia_hosttest; mkdir -p $O
GI=$(gcc -m32 -print-file-name=include)
make tinycc/libtcc.o tccport/tcc1.o tccport/tcc_libc.o tccport/tcc_vfs.o >/dev/null
FL="-m32 -ffreestanding -fno-builtin -nostdinc -isystem $GI -Itccport/include -I. -Itccport/gen -Itccport -Itinycc -O2 -w -fno-pie -fno-stack-protector -fcf-protection=none -mno-sse -mno-mmx -fno-tree-loop-distribute-patterns"
gcc $FL -c -DTCC_HOST_TEST tccport/tcc_kernel.c -o $O/tcc_kernel.o
gcc -m32 -ffreestanding -fno-builtin -nostdinc -isystem $GI -I. -c string.c -o $O/string.o
gcc -m32 -ffreestanding -fno-builtin -nostdinc -isystem $GI -I. -c libdiv.c -o $O/libdiv.o
gcc $FL -c $T/host_harness.c -o $O/harness.o
as --32 -o $O/setjmp32.o $T/setjmp32.S 2>/dev/null
ld -m elf_i386 -r -o $O/port.o tinycc/libtcc.o tccport/tcc1.o tccport/tcc_libc.o tccport/tcc_vfs.o $O/tcc_kernel.o $O/string.o $O/setjmp32.o 2>&1 | grep -v "NOTE\|GNU-stack" || true
ld -m elf_i386 -static -e _start -o $O/harness $O/harness.o $O/port.o $O/libdiv.o 2>&1 | grep -v "NOTE\|GNU-stack" || true
echo "== hello.c";    $O/harness $T/hello.c
echo "== features.c"; $O/harness $T/features.c
echo "== 200 compiles in one process (fd leak check)"; $O/harness $T/hello.c x | tail -2
