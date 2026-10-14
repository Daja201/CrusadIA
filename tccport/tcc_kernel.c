/*
 * tcc_kernel.c - TinyCC as a kernel service ("in-OS compiler").
 *
 *   source (.c file or string)
 *        |  libtcc: tcc_add_file / tcc_compile_string
 *        v
 *   machine code in heap memory (TCC_OUTPUT_MEMORY, tcc_relocate)
 *        |  the kernel is identity mapped and has no NX, so it is executable
 *        v
 *   main(argc, argv)  -- runs in ring 0, directly calling kernel functions
 *                        that we export with tcc_add_symbol()
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include "libtcc.h"
#include "tcc_kernel.h"
#include "klog.h"
#include "vesa.h"
#include "rtc.h"
#include "fs.h"
#include "task.h"
#include "syscall.h"
#include "ring3fs.h"
#include "usbhid.h"
#include "serial.h"

/* Raw, unbuffered debug channel straight to COM1 (write_serial polls the
 * UART directly, byte by byte -- no heap, no stdio, no framebuffer, no
 * dependency on any interrupt to "flush" it later). Watch it with QEMU's
 * -serial stdio (or whatever your run target already wires COM1 to). If a
 * dbg_serial() line that is the literal first statement of a function does
 * not show up here, either that function was never reached/compiled in
 * (stale tcc_kernel.o -- do a clean rebuild: `rm tccport/tcc_kernel.o`
 * before `make`), or the CPU is wedged (interrupts off, tight loop, triple
 * fault) before even that single outb() can run. */
static void dbg_serial(const char *s) {
    while (*s) { write_serial(*s); s++; }
}
static void dbg_serial_hex(uint32_t v) {
    const char *hex = "0123456789ABCDEF";
    write_serial('0'); write_serial('x');
    for (int i = 28; i >= 0; i -= 4) write_serial(hex[(v >> i) & 0xF]);
}

extern volatile uint32_t system_ticks;   /* idt.c, 1000 Hz PIT tick */
extern void usb_poll_all(void);

volatile int tcc_guest_blocking = 0;

/* ------------------------------------------------ what programs may call */

/* TinyCC runtime helpers from tcc1.c (64-bit math, float<->int64) */
extern char tcc1___ashldi3[], tcc1___ashrdi3[], tcc1___divdi3[], tcc1___fixdfdi[],
            tcc1___fixsfdi[], tcc1___fixunsdfdi[], tcc1___fixunssfdi[], tcc1___fixunsxfdi[],
            tcc1___fixxfdi[], tcc1___floatundidf[], tcc1___floatundisf[], tcc1___floatundixf[],
            tcc1___lshrdi3[], tcc1___moddi3[], tcc1___udivdi3[], tcc1___umoddi3[];

static jmp_buf guest_exit_jb;
static int     guest_exit_code;
static int     guest_running;

static void guest_exit(int code) {
    if (guest_running) {
        guest_exit_code = code;
        longjmp(guest_exit_jb, 1);
    }
    for (;;) __asm__ volatile("hlt");
}
static void guest_abort(void) { guest_exit(134); }

void os_sleep_ms(uint32_t ms) {
    uint32_t target = system_ticks + ms;
    while ((int32_t)(system_ticks - target) < 0) __asm__ volatile("sti; hlt");
}

/* ring3-safe counterparts: "sti"/"hlt" are privileged and fault at CPL3, so
 * guest programs running as a ring3 task get these instead, bound over the
 * same symbol names in guest_syms_ring3[] below. */
static void ring3_exit_stub(int code) {
    __asm__ volatile ("int $0x80" : : "a"(SYS_EXIT), "b"(code) : "memory");
    for (;;) { }
}
static void ring3_abort_stub(void) { ring3_exit_stub(134); }

static void ring3_sleep_ms(uint32_t ms) {
    uint32_t target = system_ticks + ms;
    while ((int32_t)(system_ticks - target) < 0)
        __asm__ volatile ("int $0x80" : : "a"(SYS_YIELD) : "memory");
}

extern int current_task;

static void* r3_malloc(size_t size) {
    if (current_task < 0) return 0;
    return heap_arena_alloc(&tasks[current_task].heap, size, current_task);
}
static void r3_free(void *ptr) {
    if (current_task < 0) return;
    heap_arena_free(&tasks[current_task].heap, ptr);
}
static void* r3_calloc(size_t nmemb, size_t size) {
    if (current_task < 0) return 0;
    return heap_arena_calloc(&tasks[current_task].heap, nmemb, size, current_task);
}
static void* r3_realloc(void *ptr, size_t size) {
    if (current_task < 0) return 0;
    return heap_arena_realloc(&tasks[current_task].heap, ptr, size, current_task);
}

#define SYM(x)        { #x, (const void *)&x }
#define SYM_AS(n, x)  { n,  (const void *)&x }

static const struct { const char *name; const void *addr; } guest_syms[] = {
    /* --- console / screen (klog.h, vesa.h) --- */
    SYM(klog), SYM(kklog), SYM(klogf), SYM(klog_color),
    SYM(Wwidth), SYM(Hheight), SYM(vesa_putpixel), SYM(vesa_draw_rec),
    SYM(vesa_draw_hor), SYM(vesa_draw_ver), SYM(vesa_draw_char),
    SYM(vesa_clear), SYM(vesa_swap),
    SYM(vesa_draw_button), SYM(vesa_buttons_clear), SYM(vesa_buttons_poll),
    SYM(mouse_x), SYM(mouse_y), SYM(mouse_buttons), SYM(mouse_poll_click),
    /* --- time --- */
    SYM(system_ticks), SYM(os_sleep_ms), SYM(rtc_get_datetime), SYM(time), SYM(localtime),
    /* --- stdio --- */
    SYM(printf), SYM(fprintf), SYM(sprintf), SYM(snprintf), SYM(vsnprintf), SYM(vfprintf),
    SYM(puts), SYM(putchar), SYM(fputs), SYM(fputc), SYM(fflush),
    SYM(fopen), SYM(fclose), SYM(fread), SYM(fwrite), SYM(fseek), SYM(ftell), SYM(feof),
    SYM(remove), SYM(stdin), SYM(stdout), SYM(stderr),
    /* --- fd api --- */
    SYM(open), SYM(read), SYM(write), SYM(lseek), SYM(close), SYM(unlink),
    /* --- stdlib / string --- */
    SYM(malloc), SYM(calloc), SYM(realloc), SYM(free),
    SYM_AS("exit", guest_exit), SYM_AS("abort", guest_abort),
    SYM(abs), SYM(atoi), SYM(strtol), SYM(strtoul), SYM(strtod), SYM(qsort), SYM(bsearch), SYM(itoa),
    SYM(memcpy), SYM(memmove), SYM(memset), SYM(memcmp), SYM(memchr),
    SYM(strlen), SYM(strcmp), SYM(strncmp), SYM(strcasecmp), SYM(strcpy), SYM(strncpy),
    SYM(strcat), SYM(strncat), SYM(strchr), SYM(strrchr), SYM(strstr), SYM(strdup),
    /* --- compiler runtime (code generated by tcc calls these by name) --- */
    SYM_AS("__ashldi3", tcc1___ashldi3),       SYM_AS("__ashrdi3", tcc1___ashrdi3),
    SYM_AS("__divdi3", tcc1___divdi3),         SYM_AS("__moddi3", tcc1___moddi3),
    SYM_AS("__udivdi3", tcc1___udivdi3),       SYM_AS("__umoddi3", tcc1___umoddi3),
    SYM_AS("__lshrdi3", tcc1___lshrdi3),
    SYM_AS("__fixdfdi", tcc1___fixdfdi),       SYM_AS("__fixsfdi", tcc1___fixsfdi),
    SYM_AS("__fixxfdi", tcc1___fixxfdi),       SYM_AS("__fixunsdfdi", tcc1___fixunsdfdi),
    SYM_AS("__fixunssfdi", tcc1___fixunssfdi), SYM_AS("__fixunsxfdi", tcc1___fixunsxfdi),
    SYM_AS("__floatundidf", tcc1___floatundidf), SYM_AS("__floatundisf", tcc1___floatundisf),
    SYM_AS("__floatundixf", tcc1___floatundixf),
};
#define GUEST_SYM_COUNT (sizeof(guest_syms) / sizeof(guest_syms[0]))

/* Reduced symbol table for guest programs run as a ring3 task (see
 * tcc_os_run_file_ring3 / cmd_cc "-u"). Deliberately excludes anything that
 * reaches real hardware I/O ports directly (fs.c disk access via
 * open/read/write/fopen/fclose/etc, rtc.c time/date, the fd api) since those
 * execute "cli"/"outb"/"inb" and fault with #GP at CPL3. Everything listed
 * here only touches plain memory (console framebuffer, the heap, strings)
 * and is safe to run at ring3 under CrusadIA's current flat, user-accessible
 * address space. */
static const struct { const char *name; const void *addr; } guest_syms_ring3[] = {
    SYM(klog), SYM(kklog), SYM(klogf), SYM(klog_color),
    SYM(Wwidth), SYM(Hheight), SYM(vesa_putpixel), SYM(vesa_draw_rec),
    SYM(vesa_draw_hor), SYM(vesa_draw_ver), SYM(vesa_draw_char),
    SYM(vesa_clear), SYM(vesa_swap),
    SYM(vesa_draw_button), SYM(vesa_buttons_clear), SYM(vesa_buttons_poll),
    SYM(mouse_x), SYM(mouse_y), SYM(mouse_buttons), SYM(mouse_poll_click), SYM(mouse_draw),
    SYM(system_ticks), SYM_AS("os_sleep_ms", ring3_sleep_ms),
    SYM(printf), SYM(fprintf), SYM(sprintf), SYM(snprintf), SYM(vsnprintf), SYM(vfprintf),
    SYM(puts), SYM(putchar), SYM(fputs), SYM(fputc), SYM(fflush), SYM(fwrite),
    SYM(stdin), SYM(stdout), SYM(stderr),
    SYM_AS("malloc", r3_malloc), SYM_AS("calloc", r3_calloc),
    SYM_AS("realloc", r3_realloc), SYM_AS("free", r3_free),
    SYM_AS("exit", ring3_exit_stub), SYM_AS("abort", ring3_abort_stub),
    SYM(abs), SYM(atoi), SYM(strtol), SYM(strtoul), SYM(strtod), SYM(qsort), SYM(bsearch), SYM(itoa),
    SYM(memcpy), SYM(memmove), SYM(memset), SYM(memcmp), SYM(memchr),
    SYM(strlen), SYM(strcmp), SYM(strncmp), SYM(strcasecmp), SYM(strcpy), SYM(strncpy),
    SYM(strcat), SYM(strncat), SYM(strchr), SYM(strrchr), SYM(strstr), SYM(strdup),
    SYM_AS("__ashldi3", tcc1___ashldi3),       SYM_AS("__ashrdi3", tcc1___ashrdi3),
    SYM_AS("__divdi3", tcc1___divdi3),         SYM_AS("__moddi3", tcc1___moddi3),
    SYM_AS("__udivdi3", tcc1___udivdi3),       SYM_AS("__umoddi3", tcc1___umoddi3),
    SYM_AS("__lshrdi3", tcc1___lshrdi3),
    SYM_AS("__fixdfdi", tcc1___fixdfdi),       SYM_AS("__fixsfdi", tcc1___fixsfdi),
    SYM_AS("__fixxfdi", tcc1___fixxfdi),       SYM_AS("__fixunsdfdi", tcc1___fixunsdfdi),
    SYM_AS("__fixunssfdi", tcc1___fixunssfdi), SYM_AS("__fixunsxfdi", tcc1___fixunsxfdi),
    SYM_AS("__floatundidf", tcc1___floatundidf), SYM_AS("__floatundisf", tcc1___floatundisf),
    SYM_AS("__floatundixf", tcc1___floatundixf),
    SYM_AS("cosfs_open", r3_cosfs_open), SYM_AS("cosfs_close", r3_cosfs_close),
    SYM_AS("cosfs_read", r3_cosfs_read), SYM_AS("cosfs_write", r3_cosfs_write),
    SYM_AS("cosfs_lseek", r3_cosfs_lseek), SYM_AS("cosfs_unlink", r3_cosfs_unlink),
    SYM_AS("cosfs_mkdir", r3_cosfs_mkdir),
    SYM_AS("fat32_open", r3_fat32_open), SYM_AS("fat32_close", r3_fat32_close),
    SYM_AS("fat32_read", r3_fat32_read), SYM_AS("fat32_write", r3_fat32_write),
    SYM_AS("fat32_lseek", r3_fat32_lseek), SYM_AS("fat32_unlink", r3_fat32_unlink),
    SYM_AS("fat32_mkdir", r3_fat32_mkdir),
};
#define GUEST_SYM_COUNT_RING3 (sizeof(guest_syms_ring3) / sizeof(guest_syms_ring3[0]))

/* ------------------------------------------------------ low level helpers */

/* x87 FPU: TinyCC keeps float constants in `long double`. */
static void tcc_os_fpu_init(void) {
#ifndef TCC_HOST_TEST                  /* ring 3 test harness cannot touch CR0 */
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~((1u << 2) | (1u << 3));   /* EM, TS off */
    cr0 |=  (1u << 1) | (1u << 5);     /* MP, NE on  */
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));
#endif
    __asm__ volatile("fninit");
}

/* int tcc_os_call_on_stack(void *stack_top, int (*fn)(void *), void *arg)
 * Runs fn(arg) on a different stack. The kernel tasks only have 16 KB stacks,
 * TinyCC's parser recursion needs far more. */
int tcc_os_call_on_stack(void *stack_top, int (*fn)(void *), void *arg);
__asm__(
    ".pushsection .text\n"
    ".globl tcc_os_call_on_stack\n"
    "tcc_os_call_on_stack:\n"
    "    pushl %ebp\n"
    "    movl  %esp, %ebp\n"
    "    pushl %ebx\n"
    "    movl  8(%ebp), %eax\n"      /* new stack top          */
    "    movl  12(%ebp), %ecx\n"     /* fn                     */
    "    movl  16(%ebp), %edx\n"     /* arg                    */
    "    movl  %esp, %ebx\n"         /* old esp (callee-saved) */
    "    movl  %eax, %esp\n"
    "    subl  $12, %esp\n"          /* keep 16-byte alignment */
    "    pushl %edx\n"
    "    call  *%ecx\n"
    "    movl  %ebx, %esp\n"
    "    popl  %ebx\n"
    "    popl  %ebp\n"
    "    ret\n"
    ".popsection\n");

/* --------------------------------------------------------- the compiler */

typedef struct {
    const char *name;      /* path (file mode) or display name (string mode) */
    const char *source;    /* NULL = compile the file `name`                 */
    int    argc;
    char **argv;
    int    compiled;       /* out: 1 if the program was built and started   */
    int    exit_code;      /* out                                            */
} run_req_t;

static void tcc_error_cb(void *opaque, const char *msg) {
    (void)opaque;
    klog_color(msg, 0xFF6060);
    klog("\n");
}

static int do_run(void *arg) {
    run_req_t *r = (run_req_t *)arg;
    int (*entry)(int, char **);
    volatile int code = 0;

    TCCState *s = tcc_new();
    if (!s) { kklog("cc: tcc_new() failed (out of memory?)"); return -1; }
    tcc_set_error_func(s, 0, tcc_error_cb);
    tcc_set_options(s, "-nostdlib");                 /* no libtcc1.a / crt files */
    if (tcc_set_output_type(s, TCC_OUTPUT_MEMORY) < 0) goto fail;

    for (unsigned i = 0; i < GUEST_SYM_COUNT; i++)
        tcc_add_symbol(s, guest_syms[i].name, guest_syms[i].addr);

    if ((r->source ? tcc_compile_string(s, r->source) : tcc_add_file(s, r->name)) < 0) goto fail;
    if (tcc_relocate(s) < 0) goto fail;

    entry = (int (*)(int, char **))tcc_get_symbol(s, "main");
    if (!entry) { kklog("cc: no main() function"); goto fail; }

    r->compiled = 1;
    guest_running = 1;
    if (setjmp(guest_exit_jb) == 0) code = entry(r->argc, r->argv);
    else                            code = guest_exit_code;
    guest_running = 0;
    r->exit_code = code;

    tcc_delete(s);
    return 0;
fail:
    tcc_delete(s);
    return -1;
}

typedef struct {
    const char *name;      /* path (file mode) or display name (string mode) */
    const char *source;    /* NULL = compile the file `name`                 */
    const char *out_path;  /* where to write the .o file on the drive        */
    int    ok;             /* out: 1 if the .o file was written              */
} compile_req_t;

static int do_compile_obj(void *arg) {
    compile_req_t *r = (compile_req_t *)arg;

    TCCState *s = tcc_new();
    if (!s) { kklog("cc: tcc_new() failed (out of memory?)"); return -1; }
    tcc_set_error_func(s, 0, tcc_error_cb);
    tcc_set_options(s, "-nostdlib");
    if (tcc_set_output_type(s, TCC_OUTPUT_OBJ) < 0) goto fail;

    for (unsigned i = 0; i < GUEST_SYM_COUNT; i++)
        tcc_add_symbol(s, guest_syms[i].name, guest_syms[i].addr);

    if ((r->source ? tcc_compile_string(s, r->source) : tcc_add_file(s, r->name)) < 0) goto fail;
    if (tcc_output_file(s, r->out_path) < 0) goto fail;

    r->ok = 1;
    tcc_delete(s);
    return 0;
fail:
    tcc_delete(s);
    return -1;
}

#define STACK_WANT (768 * 1024)
#define STACK_MIN  (128 * 1024)

static int on_big_stack(int (*fn)(void *), void *arg) {
    dbg_serial("[dbg/serial] on_big_stack: entered\n");
    size_t sz = STACK_WANT;
    uint8_t *mem = 0;
    while (sz >= STACK_MIN && !(mem = (uint8_t *)malloc(sz))) sz /= 2;
    if (!mem) { kklog("cc: not enough memory for the compiler stack"); return -1; }
    dbg_serial("[dbg/serial] on_big_stack: got stack mem at "); dbg_serial_hex((uint32_t)(uintptr_t)mem);
    dbg_serial(" size "); dbg_serial_hex((uint32_t)sz); dbg_serial("\n");
    memset(mem, 0xA5, 64);                            /* overflow canary */

    dbg_serial("[dbg/serial] on_big_stack: tcc_os_fpu_init()\n");
    tcc_os_fpu_init();
    uintptr_t top = ((uintptr_t)mem + sz) & ~(uintptr_t)15;
    dbg_serial("[dbg/serial] on_big_stack: switching stack to "); dbg_serial_hex((uint32_t)top); dbg_serial("\n");
    int rc = tcc_os_call_on_stack((void *)top, fn, arg);
    dbg_serial("[dbg/serial] on_big_stack: back on original stack, rc="); dbg_serial_hex((uint32_t)rc); dbg_serial("\n");

    for (int i = 0; i < 64; i++)
        if (mem[i] != 0xA5) { kklog("cc: WARNING: compiler stack overflowed (program too big/deep?)"); break; }
    free(mem);
    dbg_serial("[dbg/serial] on_big_stack: done\n");
    return rc;
}

int tcc_os_run_file(const char *path, int argc, char **argv, int *exit_code) {
    run_req_t r = { path, 0, argc, argv, 0, 0 };
    int rc = on_big_stack(do_run, &r);
    if (exit_code) *exit_code = r.exit_code;
    return (rc == 0 && r.compiled) ? 0 : -1;
}

int tcc_os_run_source(const char *name, const char *source, int argc, char **argv, int *exit_code) {
    run_req_t r = { name, source, argc, argv, 0, 0 };
    int rc = on_big_stack(do_run, &r);
    if (exit_code) *exit_code = r.exit_code;
    return (rc == 0 && r.compiled) ? 0 : -1;
}

/* ------------------------------------------------------ ring3 execution --
 * Same compile step as do_run(), but instead of calling main() directly in
 * the kernel, main() is handed to a fresh ring3 task and this (kernel, ring0)
 * side busy-waits for it to finish. Only one such run is in flight at a
 * time, matching the single-threaded nature of the "cc" shell command. */
static int (*g_user_entry)(int, char **);
static int             g_user_argc;
static char          **g_user_argv;
static volatile int    g_user_result;
static volatile int    g_user_done;

static void user_entry_trampoline(void) {
    int rc = g_user_entry(g_user_argc, g_user_argv);
    g_user_result = rc;
    g_user_done = 1;
    __asm__ volatile ("int $0x80" : : "a"(SYS_EXIT), "b"(rc) : "memory");
    for (;;) { }
}

static int do_run_ring3(void *arg) {
    run_req_t *r = (run_req_t *)arg;
    int (*entry)(int, char **);

    dbg_serial("[dbg/serial] do_run_ring3: entered (on switched stack)\n");

    dbg_serial("[dbg/serial] do_run_ring3: calling tcc_new()\n");
    TCCState *s = tcc_new();
    if (!s) { dbg_serial("[dbg/serial] do_run_ring3: tcc_new() returned NULL\n"); kklog("cc: tcc_new() failed (out of memory?)"); return -1; }
    dbg_serial("[dbg/serial] do_run_ring3: tcc_new() -> "); dbg_serial_hex((uint32_t)(uintptr_t)s); dbg_serial("\n");

    dbg_serial("[dbg/serial] do_run_ring3: calling tcc_set_error_func()\n");
    tcc_set_error_func(s, 0, tcc_error_cb);
    dbg_serial("[dbg/serial] do_run_ring3: tcc_set_error_func ok, calling tcc_set_options(-nostdlib)\n");
    tcc_set_options(s, "-nostdlib");
    dbg_serial("[dbg/serial] do_run_ring3: tcc_set_options ok\n");
    dbg_serial("[dbg/serial] do_run_ring3: tcc_set_output_type(MEMORY)\n");
    if (tcc_set_output_type(s, TCC_OUTPUT_MEMORY) < 0) { dbg_serial("[dbg/serial] do_run_ring3: set_output_type FAILED\n"); goto fail; }
    dbg_serial("[dbg/serial] do_run_ring3: set_output_type ok\n");

    dbg_serial("[dbg/serial] do_run_ring3: adding symbols, count="); dbg_serial_hex((uint32_t)GUEST_SYM_COUNT_RING3); dbg_serial("\n");
    for (unsigned i = 0; i < GUEST_SYM_COUNT_RING3; i++)
        tcc_add_symbol(s, guest_syms_ring3[i].name, guest_syms_ring3[i].addr);
    dbg_serial("[dbg/serial] do_run_ring3: symbols added\n");

    dbg_serial("[dbg/serial] do_run_ring3: compiling source, len="); dbg_serial_hex((uint32_t)(r->source ? strlen(r->source) : 0)); dbg_serial("\n");
    if ((r->source ? tcc_compile_string(s, r->source) : tcc_add_file(s, r->name)) < 0) {
        dbg_serial("[dbg/serial] do_run_ring3: compile FAILED\n"); goto fail;
    }
    dbg_serial("[dbg/serial] do_run_ring3: compile ok, relocating\n");
    if (tcc_relocate(s) < 0) { dbg_serial("[dbg/serial] do_run_ring3: relocate FAILED\n"); goto fail; }
    dbg_serial("[dbg/serial] do_run_ring3: relocate ok\n");

    entry = (int (*)(int, char **))tcc_get_symbol(s, "main");
    if (!entry) { kklog("cc: no main() function"); goto fail; }
    dbg_serial("[dbg/serial] do_run_ring3: main() resolved at "); dbg_serial_hex((uint32_t)(uintptr_t)entry); dbg_serial("\n");

    r->compiled   = 1;
    g_user_entry  = entry;
    g_user_argc   = r->argc;
    g_user_argv   = r->argv;
    g_user_done   = 0;
    g_user_result = 0;

    dbg_serial("[dbg/serial] do_run_ring3: calling create_user_task()\n");
    int slot = create_user_task(user_entry_trampoline, 1);
    if (slot < 0) {
        dbg_serial("[dbg/serial] do_run_ring3: create_user_task FAILED\n");
        kklog("cc: could not create ring3 task (out of task slots or memory)");
        goto fail;
    }
    dbg_serial("[dbg/serial] do_run_ring3: got task slot "); dbg_serial_hex((uint32_t)slot);
    dbg_serial(" state="); dbg_serial_hex((uint32_t)tasks[slot].state); dbg_serial(", entering wait loop\n");

    tcc_guest_blocking = 1;
    uint32_t spins = 0;
    uint32_t last_state = (uint32_t)-1;
    while (!g_user_done && tasks[slot].state != TASK_DEAD) {
        usb_poll_all();
        __asm__ volatile ("sti; hlt");
        spins++;
        if (tasks[slot].state != last_state) {
            dbg_serial("[dbg/serial] do_run_ring3: wait loop spin="); dbg_serial_hex(spins);
            dbg_serial(" task state changed -> "); dbg_serial_hex((uint32_t)tasks[slot].state); dbg_serial("\n");
            last_state = tasks[slot].state;
        }
        if ((spins & 0x3FF) == 0) {
            /* fires roughly once a second or so at the 1 kHz PIT tick if we
             * are genuinely spinning here and never getting switched to the
             * new task (or it never finishes) -- vs. a true freeze (no
             * interrupts firing at all), which would never print this. */
            dbg_serial("[dbg/serial] do_run_ring3: still waiting, spin="); dbg_serial_hex(spins);
            dbg_serial(" done="); dbg_serial_hex((uint32_t)g_user_done);
            dbg_serial(" state="); dbg_serial_hex((uint32_t)tasks[slot].state);
            dbg_serial(" ticks="); dbg_serial_hex(system_ticks); dbg_serial("\n");
        }
    }
    tcc_guest_blocking = 0;
    dbg_serial("[dbg/serial] do_run_ring3: wait loop exited after "); dbg_serial_hex(spins);
    dbg_serial(" spins, done="); dbg_serial_hex((uint32_t)g_user_done);
    dbg_serial(" state="); dbg_serial_hex((uint32_t)tasks[slot].state); dbg_serial("\n");

    if (!g_user_done) {
        kklog("cc: ring3 program crashed");
        r->exit_code = -1;
    } else {
        r->exit_code = g_user_result;
    }

    tcc_delete(s);
    dbg_serial("[dbg/serial] do_run_ring3: returning 0\n");
    return 0;
fail:
    dbg_serial("[dbg/serial] do_run_ring3: returning -1 (fail)\n");
    tcc_delete(s);
    return -1;
}

int tcc_os_run_file_ring3(const char *path, int argc, char **argv, int *exit_code) {
    run_req_t r = { path, 0, argc, argv, 0, 0 };
    int rc = on_big_stack(do_run_ring3, &r);
    if (exit_code) *exit_code = r.exit_code;
    return (rc == 0 && r.compiled) ? 0 : -1;
}

int tcc_os_run_source_ring3(const char *name, const char *source, int argc, char **argv, int *exit_code) {
    dbg_serial("[dbg/serial] tcc_os_run_source_ring3: called\n");
    run_req_t r = { name, source, argc, argv, 0, 0 };
    dbg_serial("[dbg/serial] tcc_os_run_source_ring3: calling on_big_stack\n");
    int rc = on_big_stack(do_run_ring3, &r);
    dbg_serial("[dbg/serial] tcc_os_run_source_ring3: on_big_stack returned\n");
    if (exit_code) *exit_code = r.exit_code;
    return (rc == 0 && r.compiled) ? 0 : -1;
}

int tcc_os_compile_obj_file(const char *path, const char *out_path) {
    compile_req_t r = { path, 0, out_path, 0 };
    int rc = on_big_stack(do_compile_obj, &r);
    return (rc == 0 && r.ok) ? 0 : -1;
}

int tcc_os_compile_obj_source(const char *name, const char *source, const char *out_path) {
    compile_req_t r = { name, source, out_path, 0 };
    int rc = on_big_stack(do_compile_obj, &r);
    return (rc == 0 && r.ok) ? 0 : -1;
}

void cmd_cc(int argc, char **argv) {
    int code = 0;
    if (argc < 2) {
        kklog("usage: cc <file.c> [args...]     compile and run a C file");
        kklog("       cc -e \"<C source>\"        compile and run a snippet");
        kklog("       cc -u <file.c> [args...]  compile and run as a ring3 (user-mode) task");
        kklog("       cc -u -e \"<C source>\"     same, from a snippet");
        kklog("       cc -c <file.c> -o <out.o>  compile to an object file and save it to the drive");
        return;
    }
    int ring3 = 0;
    if (strcmp(argv[1], "-u") == 0) {
        if (argc < 3) { kklog("cc -u: missing file or -e"); return; }
        ring3 = 1;
        argc--;
        argv++;
    }
    if (ring3 && strcmp(argv[1], "-e") == 0) {
        if (argc < 3) { kklog("cc -e: missing source"); return; }
        static char src[1024];
        src[0] = 0;
        for (int i = 2; i < argc; i++) {
            if (i > 2) strncat(src, " ", sizeof src - strlen(src) - 1);
            strncat(src, argv[i], sizeof src - strlen(src) - 1);
        }
        char *av[] = { "<cmdline>", 0 };
        if (tcc_os_run_source_ring3("<cmdline>", src, 1, av, &code) == 0)
            klogf("[cc] (ring3) exit code %d\n", code);
        return;
    }
    if (ring3) {
        if (tcc_os_run_file_ring3(argv[1], argc - 1, argv + 1, &code) == 0)
            klogf("[cc] (ring3) %s exited with code %d\n", argv[1], code);
        return;
    }
    if (strcmp(argv[1], "-c") == 0) {
        if (argc < 3) { kklog("cc -c: missing source file"); return; }
        const char *src_path = argv[2];
        const char *out_path = 0;
        for (int i = 3; i < argc - 1; i++)
            if (strcmp(argv[i], "-o") == 0) { out_path = argv[i + 1]; break; }
        if (!out_path) { kklog("cc -c: missing -o <out.o>"); return; }
        if (tcc_os_compile_obj_file(src_path, out_path) == 0)
            klogf("[cc] wrote %s\n", out_path);
        else
            klogf("[cc] failed to compile %s\n", src_path);
        return;
    }
    if (strcmp(argv[1], "-e") == 0) {
        if (argc < 3) { kklog("cc -e: missing source"); return; }
        static char src[1024];
        src[0] = 0;
        for (int i = 2; i < argc; i++) {
            if (i > 2) strncat(src, " ", sizeof src - strlen(src) - 1);
            strncat(src, argv[i], sizeof src - strlen(src) - 1);
        }
        char *av[] = { "<cmdline>", 0 };
        if (tcc_os_run_source("<cmdline>", src, 1, av, &code) == 0)
            klogf("[cc] exit code %d\n", code);
        return;
    }
    if (tcc_os_run_file(argv[1], argc - 1, argv + 1, &code) == 0)
        klogf("[cc] %s exited with code %d\n", argv[1], code);
}

/* guidemo is now a ring3 self-test: it exercises the guest_syms_ring3 API
 * group by group, klogf()'ing "step N: ..." right before each group and
 * "step N ok" right after. If the task freezes or faults, the last "step N"
 * line printed (without a matching "step N ok") tells us exactly which
 * group of calls is the culprit -- no need to guess from a silent hang.
 *
 * "guidemo crash" (an extra argv[1]) additionally runs one deliberately
 * forbidden operation at the very end (step 99), to confirm the kernel's
 * ring3 exception handler kills the task cleanly (klogf "TASK N KILLED:
 * exception ...") instead of hanging the whole machine. That run never
 * reaches "ALL TESTS PASSED" by design -- the crash *is* the pass. */
static const char guidemo_src[] =
"extern volatile int mouse_x, mouse_y;\n"
"extern volatile unsigned char mouse_buttons;\n"
"extern volatile unsigned int system_ticks;\n"
"extern int printf(const char *fmt, ...);\n"
"static void step(int n, const char* what) { klogf(\"[selftest] step %d: %s\\n\", n, what); }\n"
"static void ok(int n) { klogf(\"[selftest] step %d ok\\n\", n); }\n"
"\n"
"static int cmp_int(const void* a, const void* b) { return *(int*)a - *(int*)b; }\n"
"\n"
"int main(int argc, char** argv) {\n"
"    int do_crash = (argc > 1 && strcmp(argv[1], \"crash\") == 0);\n"
"\n"
"    klog_color(\"[selftest] ===== CrusadIA ring3 self-test starting =====\\n\", 0x60C0FF);\n"
"\n"
"    step(1, \"console logging (klog/kklog/klogf/klog_color)\");\n"
"    klog(\"  klog says hi\\n\");\n"
"    kklog(\"  kklog says hi\");\n"
"    klogf(\"  klogf says %s %d\\n\", \"hi\", 1);\n"
"    klog_color(\"  klog_color says hi\\n\", 0x00FF00);\n"
"    ok(1);\n"
"\n"
"    step(2, \"screen size + raw pixel/line/rect drawing\");\n"
"    int w = Wwidth(), h = Hheight();\n"
"    klogf(\"  screen is %dx%d\\n\", w, h);\n"
"    vesa_clear(0x202030);\n"
"    vesa_putpixel(10, 10, 0xFFFFFF);\n"
"    vesa_draw_rec(10, 20, 40, 20, 0x3050A0);\n"
"    vesa_draw_hor(10, 50, 60, 0xFFFFFF);\n"
"    vesa_draw_ver(10, 10, 60, 0xFFFFFF);\n"
"    vesa_draw_char('A', 10, 70, 0xFFFFFF, 0x202030);\n"
"    vesa_swap();\n"
"    ok(2);\n"
"\n"
"    step(3, \"mouse state read (mouse_x/mouse_y/mouse_buttons/mouse_poll_click)\");\n"
"    int mx = mouse_x, my = mouse_y, mb = mouse_buttons;\n"
"    klogf(\"  mouse at %d,%d buttons=%d\\n\", mx, my, mb);\n"
"    int cx, cy;\n"
"    mouse_poll_click(&cx, &cy);\n"
"    ok(3);\n"
"\n"
"    step(4, \"timing (system_ticks + os_sleep_ms)\");\n"
"    unsigned int t0 = system_ticks;\n"
"    os_sleep_ms(10);\n"
"    klogf(\"  system_ticks %u -> %u\\n\", t0, system_ticks);\n"
"    ok(4);\n"
"\n"
"    step(5, \"stdio printf family (printf/sprintf/snprintf)\");\n"
"    char pbuf[64];\n"
"    klogf(\"  printf addr=%x\\n\", (unsigned int)(unsigned long)printf);\n"
"    printf(\"  printf works: %d\\n\", 42);\n"
"    sprintf(pbuf, \"sprintf %d\", 7);\n"
"    snprintf(pbuf, sizeof pbuf, \"snprintf %s\", \"ok\");\n"
"    klogf(\"  %s\\n\", pbuf);\n"
"    ok(5);\n"
"\n"
"    step(6, \"heap (malloc/calloc/realloc/free)\");\n"
"    void* p1 = malloc(64);\n"
"    void* p2 = calloc(4, 16);\n"
"    p1 = realloc(p1, 128);\n"
"    klogf(\"  malloc=%p calloc=%p realloc=%p\\n\", p1, p2, p1);\n"
"    free(p1);\n"
"    free(p2);\n"
"    ok(6);\n"
"\n"
"    step(7, \"stdlib (abs/atoi/strtol/strtoul/strtod/itoa/qsort/bsearch)\");\n"
"    char ibuf[16];\n"
"    int arr[5] = { 5, 3, 1, 4, 2 };\n"
"    itoa(abs(-7), ibuf, 10);\n"
"    int v = atoi(\"123\") + (int)strtol(\"45\", 0, 10) + (int)strtoul(\"6\", 0, 10) + (int)strtod(\"1.0\", 0);\n"
"    qsort(arr, 5, sizeof(int), cmp_int);\n"
"    int key = 3;\n"
"    int* found = bsearch(&key, arr, 5, sizeof(int), cmp_int);\n"
"    klogf(\"  itoa=%s atoi/strtol/strtoul/strtod sum=%d sorted[0]=%d found=%d\\n\", ibuf, v, arr[0], found ? *found : -1);\n"
"    ok(7);\n"
"\n"
"    step(8, \"string/mem functions\");\n"
"    char sbuf[32], sbuf2[32];\n"
"    memset(sbuf, 0, sizeof sbuf);\n"
"    strcpy(sbuf, \"hello\");\n"
"    strncpy(sbuf2, sbuf, sizeof sbuf2);\n"
"    strcat(sbuf, \" world\");\n"
"    memcpy(sbuf2, sbuf, strlen(sbuf) + 1);\n"
"    int eq = strcmp(sbuf, sbuf2) == 0 && strncmp(sbuf, sbuf2, 5) == 0 && strcasecmp(sbuf, sbuf2) == 0;\n"
"    char* found_s = strstr(sbuf, \"world\");\n"
"    char* dup = strdup(sbuf);\n"
"    klogf(\"  sbuf=%s len=%d eq=%d found=%s dup=%s\\n\", sbuf, (int)strlen(sbuf), eq, found_s ? found_s : \"(null)\", dup ? dup : \"(null)\");\n"
"    free(dup);\n"
"    ok(8);\n"
"\n"
"    step(9, \"clickable buttons (vesa_buttons_clear/vesa_draw_button/vesa_buttons_poll, one shot)\");\n"
"    vesa_buttons_clear();\n"
"    vesa_draw_button(800, 800, 40, 40, 0x3050A0, \"TUNG\", 67);\n"
"    vesa_swap();\n"
"    vesa_buttons_poll();\n"
"    ok(9);\n"
"\n"
"    step(10, \"cosfs syscalls on a path that does not exist (expect a clean error, not a crash)\");\n"
"    int cfd = cosfs_open(\"/this_should_not_exist.tmp\", 0);\n"
"    klogf(\"  cosfs_open -> %d\\n\", cfd);\n"
"    if (cfd >= 0) cosfs_close(cfd);\n"
"    ok(10);\n"
"\n"
"    step(11, \"fat32 syscalls on a path that does not exist (expect a clean error, not a crash)\");\n"
"    int ffd = fat32_open(\"/this_should_not_exist.tmp\", 0);\n"
"    klogf(\"  fat32_open -> %d\\n\", ffd);\n"
"    if (ffd >= 0) fat32_close(ffd);\n"
"    ok(11);\n"
"\n"
"    if (!do_crash) {\n"
"        klog_color(\"[selftest] ===== ALL TESTS PASSED (run 'guidemo crash' to also test the forbidden-op path) =====\\n\", 0x00FF00);\n"
"        return 0;\n"
"    }\n"
"\n"
"    step(99, \"DELIBERATE forbidden ring0-only instruction (cli) -- this SHOULD fault and kill this task\");\n"
"    __asm__ volatile(\"cli\");\n"
"    klog_color(\"[selftest] step 99 did NOT fault -- unexpected!\\n\", 0xFF0000);\n"
"    return 0;\n"
"}\n";

void cmd_guidemo(int argc, char **argv) {
    int code = 0;
    char *av[2];
    av[0] = "guidemo";
    av[1] = (argc > 1) ? argv[1] : 0;
    int guest_argc = (argc > 1) ? 2 : 1;
    if (tcc_os_run_source_ring3("guidemo", guidemo_src, guest_argc, av, &code) == 0)
        klogf("[guidemo] exited with code %d\n", code);
}