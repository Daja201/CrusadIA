#ifndef GDT_H
#define GDT_H
#include <stdint.h>

#define GDT_NULL_SEL        0x00
#define GDT_KERNEL_CODE_SEL 0x10
#define GDT_KERNEL_DATA_SEL 0x18
#define GDT_USER_CODE_SEL   (0x20 | 3)
#define GDT_USER_DATA_SEL   (0x28 | 3)
#define GDT_TSS_SEL         0x30

#define KERNEL_CS GDT_KERNEL_CODE_SEL
#define KERNEL_DS GDT_KERNEL_DATA_SEL
#define USER_CS   GDT_USER_CODE_SEL
#define USER_DS   GDT_USER_DATA_SEL

struct gdt_entry_struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));
typedef struct gdt_entry_struct gdt_entry_t;

struct gdt_ptr_struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));
typedef struct gdt_ptr_struct gdt_ptr_t;

struct tss_entry_struct {
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed));
typedef struct tss_entry_struct tss_entry_t;

void init_gdt(void);

void tss_set_kernel_stack(uint32_t esp0);

void __attribute__((noreturn)) enter_usermode(uint32_t entry, uint32_t user_stack);

#endif