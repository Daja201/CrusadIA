#include "idt.h"
#include "io.h"
#include <string.h>
#include "string.h"
#include "klog.h"
#include "vesa.h"

extern uint32_t boot_fb_addr; 
extern uint32_t boot_fb_width;
extern uint32_t boot_fb_height;
volatile uint32_t system_ticks = 0;

idt_entry_t idt[256];
idt_ptr_t idt_ptr;

extern void isr0(); extern void isr1(); extern void isr2(); extern void isr3();
extern void isr4(); extern void isr5(); extern void isr6(); extern void isr7();
extern void isr8(); extern void isr9(); extern void isr10(); extern void isr11();
extern void isr12(); extern void isr13(); extern void isr14(); extern void isr15();
extern void isr16(); extern void isr17(); extern void isr18(); extern void isr19();
extern void isr20(); extern void isr21(); extern void isr22(); extern void isr23();
extern void isr24(); extern void isr25(); extern void isr26(); extern void isr27();
extern void isr28(); extern void isr29(); extern void isr30(); extern void isr31();
extern void isr32(); extern void isr33(); extern void isr34(); extern void isr35();
extern void isr36(); extern void isr37(); extern void isr38(); extern void isr39();
extern void isr40(); extern void isr41(); extern void isr42(); extern void isr43();
extern void isr44(); extern void isr45(); extern void isr46(); extern void isr47();

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low = base & 0xFFFF;
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].sel = sel;
    idt[num].always0 = 0;
    idt[num].flags = flags | 0x60;
}

void init_idt() {
    idt_ptr.limit = sizeof(idt_entry_t) * 256 - 1;
    idt_ptr.base = (uint32_t)&idt;
    memset(&idt, 0, sizeof(idt_entry_t) * 256);
    idt_set_gate(0, (uint32_t)isr0, 0x10, 0x8E);
    idt_set_gate(1, (uint32_t)isr1, 0x10, 0x8E);
    idt_set_gate(2, (uint32_t)isr2, 0x10, 0x8E);
    idt_set_gate(3, (uint32_t)isr3, 0x10, 0x8E);
    idt_set_gate(4, (uint32_t)isr4, 0x10, 0x8E);
    idt_set_gate(5, (uint32_t)isr5, 0x10, 0x8E);
    idt_set_gate(6, (uint32_t)isr6, 0x10, 0x8E);
    idt_set_gate(7, (uint32_t)isr7, 0x10, 0x8E);
    idt_set_gate(8, (uint32_t)isr8, 0x10, 0x8E);
    idt_set_gate(9, (uint32_t)isr9, 0x10, 0x8E);
    idt_set_gate(10, (uint32_t)isr10, 0x10, 0x8E);
    idt_set_gate(11, (uint32_t)isr11, 0x10, 0x8E);
    idt_set_gate(12, (uint32_t)isr12, 0x10, 0x8E);
    idt_set_gate(13, (uint32_t)isr13, 0x10, 0x8E);
    idt_set_gate(14, (uint32_t)isr14, 0x10, 0x8E);
    idt_set_gate(15, (uint32_t)isr15, 0x10, 0x8E);
    idt_set_gate(16, (uint32_t)isr16, 0x10, 0x8E);
    idt_set_gate(17, (uint32_t)isr17, 0x10, 0x8E);
    idt_set_gate(18, (uint32_t)isr18, 0x10, 0x8E);
    idt_set_gate(19, (uint32_t)isr19, 0x10, 0x8E);
    idt_set_gate(20, (uint32_t)isr20, 0x10, 0x8E);
    idt_set_gate(21, (uint32_t)isr21, 0x10, 0x8E);
    idt_set_gate(22, (uint32_t)isr22, 0x10, 0x8E);
    idt_set_gate(23, (uint32_t)isr23, 0x10, 0x8E);
    idt_set_gate(24, (uint32_t)isr24, 0x10, 0x8E);
    idt_set_gate(25, (uint32_t)isr25, 0x10, 0x8E);
    idt_set_gate(26, (uint32_t)isr26, 0x10, 0x8E);
    idt_set_gate(27, (uint32_t)isr27, 0x10, 0x8E);
    idt_set_gate(28, (uint32_t)isr28, 0x10, 0x8E);
    idt_set_gate(29, (uint32_t)isr29, 0x10, 0x8E);
    idt_set_gate(30, (uint32_t)isr30, 0x10, 0x8E);
    idt_set_gate(31, (uint32_t)isr31, 0x10, 0x8E);
    idt_set_gate(32, (uint32_t)isr32, 0x10, 0x8E);
    idt_set_gate(33, (uint32_t)isr33, 0x10, 0x8E);
    idt_set_gate(34, (uint32_t)isr34, 0x10, 0x8E);
    idt_set_gate(35, (uint32_t)isr35, 0x10, 0x8E);
    idt_set_gate(36, (uint32_t)isr36, 0x10, 0x8E);
    idt_set_gate(37, (uint32_t)isr37, 0x10, 0x8E);
    idt_set_gate(38, (uint32_t)isr38, 0x10, 0x8E);
    idt_set_gate(39, (uint32_t)isr39, 0x10, 0x8E);
    idt_set_gate(40, (uint32_t)isr40, 0x10, 0x8E);
    idt_set_gate(41, (uint32_t)isr41, 0x10, 0x8E);
    idt_set_gate(42, (uint32_t)isr42, 0x10, 0x8E);
    idt_set_gate(43, (uint32_t)isr43, 0x10, 0x8E);
    idt_set_gate(44, (uint32_t)isr44, 0x10, 0x8E);
    idt_set_gate(45, (uint32_t)isr45, 0x10, 0x8E);
    idt_set_gate(46, (uint32_t)isr46, 0x10, 0x8E);
    idt_set_gate(47, (uint32_t)isr47, 0x10, 0x8E);
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    outb(0x21, 0x0);
    outb(0xA1, 0x0);
    asm volatile("lidt %0" : : "m"(idt_ptr));
}

void fault_handler(registers_t *regs) {
    if (regs->int_no >= 32 && regs->int_no <= 47) {
        if (regs->int_no >= 40) {
            outb(0xA0, 0x20);
        }
        outb(0x20, 0x20);
        if (regs->int_no == 32) {
            system_ticks++;
        }
        return;
    }
    if (regs->int_no < 32) {
        asm volatile("cli");
        
        if (boot_fb_addr != 0) {
            uint32_t *fb = (uint32_t *)boot_fb_addr;
            c_x = 0;
            c_y = 0;
            klogf_color("EXCEPTION: %d | EIP: 0x%x | ERR: 0x%x\n", 0x00FF00, regs->int_no, regs->eip, regs->err_code);
            extern void vesa_swap(void);
            vesa_swap();
            for (int y = 0; y < 8; y++) {
                for (int x = 370; x < 1280; x++) {
                    fb[y * 1280 + x] = 0x000000;
                }
            }
            for (int x = 1280 * 8; x < 1280 * 720; x++) {
                fb[x] = 0x000000;
            }
            for (int y = 300; y < 330; y++) {
                for (int x = 400; x < 550; x++) {
                    fb[y * 1280 + x] = 0xFF0000;
                }
            }
            for (int y = 300; y < 330; y++) {
                for (int x = 730; x < 880; x++) {
                    fb[y * 1280 + x] = 0xFF0000;
                }
            }
            for (int y = 420; y < 450; y++) {
                for (int x = 550; x < 730; x++) {
                    fb[y * 1280 + x] = 0xFF0000;
                }
            }
        }
        
        for (;;);
    }
}