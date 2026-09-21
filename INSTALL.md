# TinyCC in CrusadIA - install

From the CrusadIA repo root (the folder with Makefile and the `tinycc/` checkout inside it):

    cp -r <this>/tccport .
    patch -p1 < <this>/crusadia-tinycc.patch
    make a            # or: make && make run

Then, in the OS shell (write source with `app scriber` or `wr`):

    cc hello.c
    cc -e "#include <os.h>\nint main(){klogf(\"hi %d\\n\", 42); return 0;}"

Requirements: tinycc checked out at ./tinycc (0.9.28rc, "mob"), a HOST compiler named `cc`
(`make HOSTCC=gcc` to change) for two tiny build-time generators.

Test without booting: `sh tccport/tests/run_host_test.sh` (needs gcc -m32, as, ld).

What the patch changes in your files:
  Makefile     TinyCC build block, objects added to OBJ, clean target
  setjmp.s     fixes saved esp (longjmp resumed 4 bytes low; crashes at -O2)
  string.c     fuller vsnprintf (width/precision/%lld/%X/%i, fixes negative %x, basic %f)
  bootinfo.c   PMM reserves 1MB..6MB (kernel+bss now ends at ~0x502000, was reserved to 0x500000)
  commands.c   `cc` command;  library.c  help entry;  .gitignore
