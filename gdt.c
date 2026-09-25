#include "gdt.h"
#include "string.h"
#include "klog.h"

#define GDT_ENTRIES 7

static gdt_entry_t gdt[GDT_ENTRIES];
static gdt_ptr_t   gdt_ptr;
static tss_entry_t tss;

#define DEFAULT_KSTACK_SIZE 8192
static uint8_t default_kernel_stack[DEFAULT_KSTACK_SIZE] __attribute__((aligned(16)));

static void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit,
                          uint8_t access, uint8_t gran) {
    gdt[num].base_low    = base & 0xFFFF;
    gdt[num].base_middle  = (base >> 16) & 0xFF;
    gdt[num].base_high   = (base >> 24) & 0xFF;

    gdt[num].limit_low   = limit & 0xFFFF;
    gdt[num].granularity = (limit >> 16) & 0x0F;

    gdt[num].granularity |= gran & 0xF0;
    gdt[num].access      = access;
}

static void gdt_flush(void) {
    gdt_ptr.limit = (sizeof(gdt_entry_t) * GDT_ENTRIES) - 1;
    gdt_ptr.base  = (uint32_t)&gdt;

    asm volatile (
        "lgdt (%0)\n"
        "mov %1, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        "ljmp %2, $1f\n"
        "1:\n"
        :
        : "r"(&gdt_ptr), "i"(GDT_KERNEL_DATA_SEL), "i"(GDT_KERNEL_CODE_SEL)
        : "eax", "memory"
    );
}

static void write_tss(int32_t num, uint16_t ss0, uint32_t esp0) {
    uint32_t base  = (uint32_t)&tss;
    uint32_t limit = base + sizeof(tss_entry_t);

    gdt_set_gate(num, base, limit, 0xE9, 0x00);

    memset(&tss, 0, sizeof(tss_entry_t));
    tss.ss0  = ss0;
    tss.esp0 = esp0;
    tss.cs = GDT_KERNEL_CODE_SEL;
    tss.ss = tss.ds = tss.es = tss.fs = tss.gs = GDT_KERNEL_DATA_SEL;
    tss.iomap_base = sizeof(tss_entry_t);
}

void init_gdt(void) {
    memset(&gdt, 0, sizeof(gdt));

    gdt_set_gate(0, 0, 0, 0, 0);

    gdt_set_gate(1, 0, 0, 0, 0);
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x9A, 0xCF);
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0x92, 0xCF);
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xFA, 0xCF);
    gdt_set_gate(5, 0, 0xFFFFFFFF, 0xF2, 0xCF);

    write_tss(6, GDT_KERNEL_DATA_SEL,
              (uint32_t)(default_kernel_stack + DEFAULT_KSTACK_SIZE));

    gdt_flush();
    asm volatile ("ltr %%ax" : : "a"((uint16_t)GDT_TSS_SEL));
}

void tss_set_kernel_stack(uint32_t esp0) {
    tss.esp0 = esp0;
}

void __attribute__((noreturn)) enter_usermode(uint32_t entry, uint32_t user_stack) {
    asm volatile (
        "cli\n"
        "mov %2, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"

        "push %2\n"
        "push %1\n"
        "pushf\n"
        "pop %%eax\n"
        "or $0x200, %%eax\n"
        "push %%eax\n" 
        "push %3\n"
        "push %0\n"
        "iret\n"
        :
        : "r"(entry), "r"(user_stack),
          "i"(GDT_USER_DATA_SEL), "i"(GDT_USER_CODE_SEL)
        : "eax", "memory"
    );

    __builtin_unreachable();
}