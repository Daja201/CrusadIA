/* User-space (32-bit Linux, no libc) stand-in for the CrusadIA kernel services,
 * used to run the REAL port objects (libtcc, tcc_kernel, tcc_vfs, tcc_libc,
 * string.c) and verify the whole compile-and-run path without booting QEMU. */
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include "tccport/tcc_kernel.h"

int vsnprintf(char*, size_t, const char*, va_list);
size_t strlen(const char*);
void *memcpy(void*, const void*, size_t);

static long sc3(int n, long a, long b, long c) { long r; __asm__ volatile("int $0x80":"=a"(r):"a"(n),"b"(a),"c"(b),"d"(c):"memory"); return r; }
static long sc6(int n, long a, long b, long c, long d, long e, long f) {
    long r; __asm__ volatile("push %%ebp; mov %7, %%ebp; int $0x80; pop %%ebp":"=a"(r):"a"(n),"b"(a),"c"(b),"d"(c),"S"(d),"D"(e),"m"(f):"memory"); return r; }
static void out(const char *s) { sc3(4, 1, (long)s, (long)strlen(s)); }

/* ---- heap: RWX mmap bump allocator (the kernel heap is RWX by construction) ---- */
static uint8_t *heap_cur, *heap_end;
void *malloc(size_t n) {
    n = (n + 15) & ~15u; n += 16;
    if (!heap_cur || heap_cur + n > heap_end) {
        size_t chunk = n > (32u<<20) ? n : (32u<<20);
        long p = sc6(192, 0, (long)chunk, 7 /*RWX*/, 0x22 /*PRIVATE|ANON*/, -1, 0);
        if (p < 0 && p > -4096) return 0;
        heap_cur = (uint8_t*)p; heap_end = heap_cur + chunk;
    }
    uint8_t *r = heap_cur; heap_cur += n; *(size_t*)r = n - 16; return r + 16;
}
void free(void *p) { (void)p; }
void *calloc(size_t a, size_t b) { void *p = malloc(a*b); if (p) { uint8_t *q = p; for (size_t i=0;i<a*b;i++) q[i]=0; } return p; }
void *realloc(void *p, size_t n) { if (!p) return malloc(n); size_t old = *(size_t*)((uint8_t*)p-16); if (n <= old) return p; void *q = malloc(n); if (q) memcpy(q, p, old); return q; }

/* ---- kernel console ---- */
void klog(const char *s) { out(s); }
void kklog(const char *s) { out(s); out("\n"); }
void klog_color(const char *s, uint32_t c) { (void)c; out(s); }
void klogf(const char *fmt, ...) { char b[512]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap); out(b); }
uint32_t Wwidth(void) { return 1920; } uint32_t Hheight(void) { return 1080; }
static uint32_t px_count; static int last_rect[4];
void vesa_putpixel(int x,int y,uint32_t c){ (void)x;(void)y;(void)c; px_count++; }
void vesa_draw_rec(int x,int y,int w,int h,uint32_t c){ (void)c; last_rect[0]=x;last_rect[1]=y;last_rect[2]=w;last_rect[3]=h; }
void vesa_clear(uint32_t c){(void)c;} void vesa_swap(void){}
volatile uint32_t system_ticks;
void rtc_get_datetime(int*Y,int*M,int*D,int*h,int*m,int*s){ *Y=2026;*M=9;*D=21;*h=12;*m=34;*s=56; }
void task_exit(void) { sc3(1, 99, 0, 0); for(;;); }

/* ---- files: real host files through Linux syscalls, standing in for fs.c ---- */
static int open_now, open_max;
int open(const char *p, int flags, ...) { open_now++; if(open_now>open_max) open_max=open_now; long r = sc3(5, (long)p, (flags & 0x0003) | ((flags&0x40)?0100:0) | ((flags&0x200)?01000:0), 0644); return r < 0 ? -1 : (int)r; }
long read(int fd, void *b, size_t n) { return sc3(3, fd, (long)b, (long)n); }
long write(int fd, const void *b, size_t n) { return sc3(4, fd, (long)b, (long)n); }
int close(int fd) { open_now--; return (int)sc3(6, fd, 0, 0); }
long lseek(int fd, long o, int w) { return sc3(19, fd, o, w); }
int unlink(const char *p) { return (int)sc3(10, (long)p, 0, 0); }
char *getcwd(char *b, size_t n) { if (n>1) { b[0]='/'; b[1]=0; } return b; }
void *fopen(const char*p,const char*m){(void)p;(void)m;return 0;} int fclose(void*f){(void)f;return -1;}
size_t fread(void*p,size_t s,size_t n,void*f){(void)p;(void)s;(void)n;(void)f;return 0;}
size_t fwrite(const void*p,size_t s,size_t n,void*f){(void)p;(void)s;(void)n;(void)f;return 0;}
int fseek(void*f,long o,int w){(void)f;(void)o;(void)w;return -1;} long ftell(void*f){(void)f;return -1;} int feof(void*f){(void)f;return 1;}


/* ---- SIGSEGV diagnostics ---- */
static void hex(const char *label, uint32_t v) { char b[40]; const char *d="0123456789abcdef"; int i=0; while(*label) b[i++]=*label++; b[i++]='0'; b[i++]='x'; for(int k=28;k>=0;k-=4) b[i++]=d[(v>>k)&15]; b[i++]='\n'; b[i]=0; out(b); }
static void segv(int sig, void *si, void *uc) { (void)sig;
    hex("SEGV fault addr = ", *(uint32_t*)((char*)si + 12));
    hex("       eip      = ", *(uint32_t*)((char*)uc + 20 + 14*4));
    hex("       esp      = ", *(uint32_t*)((char*)uc + 20 + 7*4));
    hex("       ebp      = ", *(uint32_t*)((char*)uc + 20 + 6*4));
    sc3(1, 139, 0, 0); }
__asm__(".globl restorer\nrestorer:\n movl $173,%eax\n int $0x80\n");
extern void restorer(void);
static uint8_t altstack[65536];
static void install_segv(void) {
    struct { long stack; int flags; long size; } ss = { (long)altstack, 0, sizeof altstack };
    sc3(186, (long)&ss, 0, 0);
    struct { void *h; unsigned long flags; void *rest; unsigned long long mask; } sa = { (void*)segv, 4|0x04000000|0x08000000|0x40000000, (void*)restorer, 0 };
    sc6(174, 11, (long)&sa, 0, 8, 0, 0);
}

/* ---- run: harness main = argv[1] .. programs compiled by the REAL tcc ---- */
void harness_main(int argc, char **argv) {
    if (argc < 2) { out("usage: harness file.c [args]\n"); return; }
    int code = -12345;
    int reps = argc > 2 && argv[2][0]=='x' ? 200 : 1; int rc = 0;
    for (int i = 0; i < reps; i++) rc = tcc_os_run_file(argv[1], argc - 1, argv + 1, &code);
    if (reps > 1) klogf("[harness] after %d runs: fds still open=%d (max %d)\n", reps, open_now, open_max);
    char b[96]; klogf("[harness] rc=%d exit_code=%d\n", rc, code);
    (void)b;
}
void __attribute__((used)) start_c(long *sp) {
    install_segv();
    int argc = (int)sp[0]; char **argv = (char**)(sp + 1);
    harness_main(argc, argv);
    sc3(1, 0, 0, 0);
}
__asm__(".globl _start\n_start:\n  movl %esp, %eax\n  andl $-16, %esp\n  subl $12, %esp\n  pushl %eax\n  call start_c\n");
