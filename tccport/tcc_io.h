/* Force-included (-include) ONLY while compiling tinycc/libtcc.c.
 * Redirects TinyCC's file I/O to tcc_vfs.c, which
 *   - serves /tcc/include/<hdr> from memory (your FS has no path support), and
 *   - renumbers real fds so they are never 0 (TinyCC treats fd 0 as stdin and
 *     would never close it -> the file slot would leak on every run).
 * Function-like macros: only calls are rewritten, struct fields are untouched. */
#ifndef TCC_IO_H
#define TCC_IO_H
#define open(...)  tcc_os_open(__VA_ARGS__)
#define read(...)  tcc_os_read(__VA_ARGS__)
#define close(...) tcc_os_close(__VA_ARGS__)
#define lseek(...) tcc_os_lseek(__VA_ARGS__)
#endif
