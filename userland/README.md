Userland libc for CrusadIA
===========================

This is a real, statically-linked libc for building ring3 ELF binaries that
`elf_exec` can load and run on CrusadIA. It is separate from the kernel's
internal `string.c`/`heap.c` and from `tccport/tcc_libc.c` (the JIT shim used
by the in-kernel `cc` command) — this one is for offline cross-compiled apps.

What it provides
-----------------
- `crt0.s`: entry point. Calls `main()`, then `exit()` with its return value.
- `syscall.c`: raw `int $0x80` trap wrapper.
- `libc/unistd.c`: `open/close/read/write/lseek/unlink/mkdir/sbrk/_exit`,
  wired to the kernel's `SYS_OPEN..SYS_MKDIR` and `SYS_BRK` syscalls. fd 0/1/2
  are the terminal, matching the kernel's stdio fd convention.
- `libc/malloc.c`: a free-list allocator backed by `sbrk`/`SYS_BRK`.
- `libc/string.c`, `libc/stdlib.c`: the usual mem/str/convert functions.
- `libc/vsnprintf.c`, `libc/stdio.c`: `printf`/`fprintf`/`snprintf` and a
  `FILE*` layer (`fopen/fread/fwrite/fgets/...`) on top of the raw fds.
- `include/`: freestanding `stdio.h stdlib.h string.h unistd.h fcntl.h
  stdint.h stddef.h stdarg.h` for use with `-nostdinc`.
- `link_user.ld`: places the binary at `0x08048000` (below `USER_HEAP_BASE`
  at `0x40000000` and `USER_STACK_TOP` at `0x90000000`), static, no dynamic
  linking (`elf_exec` only understands flat `PT_LOAD` segments).

Building an app
----------------
    cd userland
    make hello
    # -> examples/hello.elf

That expands to, for any `program.c`:

    i686-elf-gcc -m32 -ffreestanding -fno-builtin -fno-stack-protector \
        -fno-pie -nostdinc -isystem <gcc include> -I include -c crt0.s -o crt0.o
    i686-elf-gcc <same flags> -c program.c -o program.o
    ar rcs libc.a libc/*.o syscall.o
    i686-elf-ld -m elf_i386 -T link_user.ld crt0.o program.o libc.a -o program.elf

Drop `program.elf` onto a disk image CrusadIA can mount, then from the
in-kernel shell:

    elf program.elf

(`commands.c` already calls `elf_exec()` for that command — no kernel change
needed to run a binary built against this libc.)

argv/envp
---------
`elf_exec_argv(path, argv, argc, priority)` (in `elf.c`) now builds a real
argv block at the top of the new task's user stack before starting it:
strings, then a null-terminated `argv[]` pointer array, then a null `envp`,
then `argc`, in that order, written via the new `task_write_user_mem()`
helper in `task.c` (which walks the (possibly non-contiguous) physical
pages behind the stack's virtual range). `task_start_user()` takes the
resulting `esp` instead of always using `USER_STACK_TOP`. `crt0.s` reads
`argc`/`argv` off the stack at entry and passes them to `main(argc, argv)`.
`elf_exec(path, priority)` still exists as a one-arg convenience wrapper
(`argv = {path}`, `argc = 1`). The shell's `exec`/`elf` command
(`cmd_exec` in `commands.c`) now forwards any extra words typed after the
filename as `argv[1..]` to the program it launches. No `envp` entries are
populated yet (the `envp` pointer written is always `NULL`-terminated empty).
There's a 4 KB cap (`ELF_MAX_ARGV_BYTES`) and a 32-argument cap
(`ELF_MAX_ARGV`) on what fits in the argv block; both are enforced in
`push_argv()` in `elf.c` and return `ELF_E_MEM` if exceeded, well inside the
16 KB stack.

envp
----
Real environment variables now exist, kernel-side, in `env.c`/`env.h`: a
flat table of up to `ENV_MAX_VARS` (16) `"KEY=VALUE"` strings, seeded at
boot (`env_init()`, called from `kmain()` in `kernel.c`) with `PATH`,
`HOME`, `SHELL`, `TERM`. The shell gained `export KEY=VALUE`, `unset KEY`
and `printenv` (`commands.c`). `push_argv()` in `elf.c` now writes the
current environment into every new process's stack alongside argv, in the
standard order (`argc`, `argv[]`, NULL, `envp[]`, NULL, strings), capped at
`ELF_MAX_ENV` (16) entries. `crt0.s` computes `envp` from `argv`'s NULL
terminator, hands it to `main(argc, argv, envp)` as a third (optional, GNU
extension) argument, and also calls `__init_environ(envp)` before `main()`
so `environ` and `getenv()` (`libc/stdlib.c`) work without a program having
to take the third `main` argument at all.

spawn
-----
`SYS_SPAWN` (syscall number 19, right after `SYS_BRK` at 18) lets a
*running ring3 program*
launch another ELF binary, not just the kernel shell. `syscall.c`'s handler
copies the path and up to `ELF_SPAWN_MAX_ARGV` (16) argv strings out of the
caller's address space (safe to dereference directly — a syscall runs with
the caller's page directory still active, same as the existing `SYS_WRITE`
handler already did) into bounded kernel stack buffers, then calls the same
`elf_exec_argv()` used by the shell. It does not fork or share memory with
the caller — more `posix_spawn()` than `fork()`+`exec()` — and does not
block: the spawned task becomes runnable independently and the caller's
syscall returns immediately with `0` on success or `-1` on failure.
`userland/libc/unistd.c` exposes this as `int spawn(const char* path,
char* const argv[], int argc)`; there's no `wait()` yet, so a caller has no
way to block for the child's exit status.

Current limits
--------------
- `free()` never returns memory to the kernel (no `sbrk` shrink, no
  coalescing) — fine for short-lived utilities, not for long-running
  servers.
- `spawn()` has no `wait()`/exit-status channel back to the caller, and no
  true `fork()` (no copy-on-write address-space duplication) — only
  spawn-a-fresh-binary.
- No signals, no threads — only the kernel's seven file syscalls
  (`open/close/read/write/lseek/unlink/mkdir`) plus `brk` and `spawn`.
- Static linking only, by design — matches `elf_exec`, which has no
  relocation or symbol-resolution logic for `.so`s.
- Environment and argv are each capped (16 vars / 16 args respectively,
  4 KB total on the stack) — generous for a hobby OS's shell utilities, not
  for arbitrary ported software.

Next steps (not done here)
---------------------------
1. `wait(pid)` / exit-status reporting so a caller of `spawn()` can block on
   and collect its child's result, turning it into a real `fork+exec+wait`
   story.
2. Consolidate the *kernel's* three separate string/mem implementations
   (kernel `string.c`, `tccport/tcc_libc.c`, this `userland/libc/string.c`)
   behind one shared freestanding source if binary size / maintenance ever
   becomes a problem — they're kept separate for now because each target
   (kernel, TCC JIT, ring3 static binary) has a different ABI and build
   flow, and merging them is a refactor with no new capability by itself.
