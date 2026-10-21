# SQLite port for CrusadIA

This directory contains a full port of SQLite 3.54.1 (amalgamation) to
CrusadIA's userland, running on `/userland/libc`.

## Files

- `sqlite3.c`, `sqlite3.h` — unmodified upstream SQLite amalgamation.
- `sqlite_cfg.h` — compile-time configuration (`-include`d into every
  translation unit in this directory). Selects `SQLITE_OS_OTHER`,
  disables threads, WAL, localtime, UTF-16, load-extension, mmap.
- `os_crusadia.c` — the SQLite VFS, implemented directly on top of the
  OS's `open/close/read/write/lseek/unlink` syscalls. This is what gives
  SQLite access to any mounted drive (FAT32 or COS), since those
  syscalls are already filesystem-agnostic in the kernel.
- `sqlite_shell.c` — a small interactive SQL shell (`.open`, `.close`,
  `.tables`, `.schema`, `.headers`, `.exit`), the entry point of
  `sqlite.elf`.

## What had to be added elsewhere in the tree

- `SYS_GETTIME` syscall (`syscall.h`, `userland/syscall.h`, `syscall.c`)
  — reads the RTC and returns a Unix epoch, used for
  `CURRENT_TIMESTAMP`/`datetime('now')` and to seed the VFS's PRNG.
- `yield()` added to `userland/unistd.h`/`unistd.c`, used by the VFS's
  `xSleep`.
- New libc pieces: `ctype.c`, `libm.c` (just `fabs`), `divdi3.c`
  (software 64-bit divide/modulo — the i386 target has no hardware
  instruction for this and the project doesn't link libgcc), `time.c`,
  `errno.c`, `assert.c`, plus `strspn`/`strcspn`/`strpbrk` in
  `string.c`. New headers: `assert.h`, `ctype.h`, `inttypes.h`,
  `errno.h`, `math.h`, `time.h`. `stdint.h` gained the C99
  `INT*_MIN/MAX`, `UINT*_MAX` and `*_C()` macros SQLite's source needs.
  `vsnprintf.c` gained real `ll` (64-bit) handling.
- A `sqlite` built-in shell command in `commands.c` that execs
  `/sqlite.elf`, and `.db`/`.sqlite`/`.sqlite3` added to the `open`
  command's extension dispatch.
- `CRT0_OBJ` in `userland/Makefile` now also links `syscall.o` — this
  was previously missing from every userland link line, which would
  have failed to link even the existing `hello.elf` example.

No FTS5, RTREE, or JSON1-via-libm functions are compiled in — the
default amalgamation build needs none of them, and leaving them out
keeps the port free of any real libm dependency (the only math.h
symbol SQLite's core actually needs is `fabs`, used by the `SUM()`/
`AVG()` Kahan summation).

## Limitations

- No `fsync` syscall exists on this OS, so `xSync` is a no-op — writes
  are assumed durable as soon as `write()` returns. This is normal
  rollback-journal behavior for this kind of OS but means there is no
  real crash-safety guarantee beyond what the underlying filesystem
  driver itself provides.
- No `stat`/`access` syscall: `xAccess` and `xFileSize` are
  approximated by actually opening the file and seeking. This works
  but means `.open`ing a path you don't have permission to can leave a
  harmless failed `open()` attempt in the kernel's FS layer.
- No `getcwd`: `xFullPathname` does not resolve relative paths. Always
  pass absolute paths to `.open`, `ATTACH`, etc. (e.g. `/mydb.db` or
  `/d1/mydb.db`).
- `SQLITE_OMIT_LOCALTIME` is set, so the `localtime`/`utc` date
  modifiers are unavailable; all date/time functions operate in UTC.
- `SQLITE_OMIT_WAL` is set — there is no shared-memory primitive on
  this OS, so only the default rollback-journal mode is supported.
- `xRandomness` is a software xorshift32 PRNG seeded from the RTC, not
  a cryptographic source. Fine for SQLite's own internal use
  (`ROWID` spread, `VACUUM` temp names); do not rely on it for
  anything security-sensitive.
- The OS's allocator (`userland/libc/malloc.c`) is a simple
  non-splitting first-fit allocator. It is correct but can fragment
  under SQLite's page-cache churn on long-running sessions with a
  large `PRAGMA cache_size`. The default config here uses a modest
  cache (128 pages) to keep this manageable.
- `_syscall` is a 32-bit `int 0x80` trap, matching this OS's whole
  ABI, so `time()`'s epoch (and therefore all SQLite timestamps) will
  wrap in 2106. Not a practical concern for a hobby OS.

## Building

```
cd userland
make sqlite.elf
```

This produces `userland/sqlite.elf`, statically linked against
`libc.a`, `crt0.o`, `syscall.o`, the SQLite VFS, and the amalgamation
itself.

## Getting it onto a disk image

The kernel doesn't bake files into `disk.img`/`disk2.img` at build
time — drives are formatted/mounted at runtime by the OS itself. Use
the existing host-side `UTILS/driveutil` tool to inject the built
binary before booting:

```
cd UTILS
gcc -O2 -o driveutil driveutil.c
./driveutil disk.img put ../userland/sqlite.elf /sqlite.elf
```

(adjust the image name/path and the tool's exact subcommand syntax to
whatever `driveutil --help` reports for your checked-out revision).

## Using it

From the OS's own shell:

```
sqlite /mydb.db
sqlite> CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT);
sqlite> INSERT INTO t(name) VALUES ('hello');
sqlite> SELECT * FROM t;
id|name
1|hello
sqlite> .tables
sqlite> .exit
```

Typing `open mydb.db` (or any path ending in `.db`/`.sqlite`/
`.sqlite3`) in the shell also launches `sqlite.elf` on that file,
mirroring how `.elf` files are already dispatched to `exec`.
