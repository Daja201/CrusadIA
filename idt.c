#include "idt.h"
#include "io.h"
#include "string.h"
#include "string.h"
#include "klog.h"
#include "vesa.h"
#include "task.h"
#include "vfs.h"
#include "paging.h"
#include "serial.h"

/* Minimal, allocation-free, printf-free serial dump used only from the fault
 * handler: a crashing kernel cannot safely rely on printf/klog (which may
 * themselves touch the heap, the thing that might be the reason we're here)
 * so this writes straight to the COM1 UART one byte at a time. */
static void dbg_serial_str(const char *s) { while (*s) write_serial(*s++); }
static void dbg_serial_hex(uint32_t v) {
    const char *hex = "0123456789ABCDEF";
    write_serial('0'); write_serial('x');
    for (int i = 28; i >= 0; i -= 4) write_serial(hex[(v >> i) & 0xF]);
}

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
extern void isr128();

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
    idt_set_gate(128, (uint32_t)isr128, 0x10, 0x8E);
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01 & ~0x04);
    outb(0xA1, 0x01 & ~0x10);
    outb(0x21, 0x0);
    outb(0xA1, 0x0);
    asm volatile("lidt %0" : : "m"(idt_ptr));
}

extern task_t tasks[];
extern int current_task;

void fault_handler(registers_t *regs) {
    if (regs->int_no >= 32 && regs->int_no <= 47) {
        if (regs->int_no >= 40) outb(0xA0, 0x20);
        outb(0x20, 0x20);
        if (regs->int_no == 32) system_ticks++;
        return;
    }
    if (regs->int_no < 32) {
        if ((regs->cs & 3) == 3 && current_task >= 0) {
            uint32_t cr2;
            asm volatile("mov %%cr2, %0" : "=r"(cr2));
            c_x = 0;
            c_y = 0;
            klogf_color("TASK %d KILLED: exception %d at eip 0x%x err 0x%x cr2 0x%x esp 0x%x\n", 0xFF0000,
                        tasks[current_task].pid, regs->int_no, regs->eip, regs->err_code, cr2, regs->useresp);

            if (regs->int_no == 14) {
                uint32_t pd_index = cr2 >> 22;
                uint32_t pt_index = (cr2 >> 12) & 0x03FF;

                uint32_t *live_dir = tasks[current_task].page_directory;
                uint32_t live_pde = live_dir ? live_dir[pd_index] : 0;
                uint32_t global_pde = page_directory[pd_index];

                uint32_t live_pte = 0;
                if (live_pde & 0x1) {
                    uint32_t *live_table = (uint32_t*)(live_pde & ~0xFFFu);
                    live_pte = live_table[pt_index];
                }
                uint32_t global_pte = 0;
                if (global_pde & 0x1) {
                    uint32_t *global_table = (uint32_t*)(global_pde & ~0xFFFu);
                    global_pte = global_table[pt_index];
                }

                klogf_color("PF DEBUG: pd_index 0x%x pt_index 0x%x\n", 0xFFFF00, pd_index, pt_index);
                klogf_color("PF DEBUG: task.pd[pd_index]=0x%x task.pt[pt_index]=0x%x\n", 0xFFFF00,
                            live_pde, live_pte);
                klogf_color("PF DEBUG: global.pd[pd_index]=0x%x global.pt[pt_index]=0x%x\n", 0xFFFF00,
                            global_pde, global_pte);
                klogf_color("PF DEBUG: task.page_directory=0x%x global page_directory=0x%x\n", 0xFFFF00,
                            (uint32_t)live_dir, (uint32_t)page_directory);
            }

            extern void vesa_swap(void);
            vesa_swap();
            vfs_task_killed(current_task);
            tasks[current_task].state = TASK_DEAD;
            for (;;) {
                asm volatile("sti; hlt");
            }
        }
        {
            uint32_t cr2_dump;
            asm volatile("mov %%cr2, %0" : "=r"(cr2_dump));
            dbg_serial_str("\n[FAULT/serial] ring0 exception int_no="); dbg_serial_hex(regs->int_no);
            dbg_serial_str(" eip="); dbg_serial_hex(regs->eip);
            dbg_serial_str(" err="); dbg_serial_hex(regs->err_code);
            dbg_serial_str(" cr2="); dbg_serial_hex(cr2_dump);
            dbg_serial_str(" esp="); dbg_serial_hex(regs->esp);
            dbg_serial_str(" cs="); dbg_serial_hex(regs->cs);
            dbg_serial_str("\n");
        }

        asm volatile("cli");
        
        if (boot_fb_addr != 0) {
            uint32_t *fb = (uint32_t *)boot_fb_addr;
            c_x = 0;
            c_y = 0;
            klogf_color("EXCEPTION: %d | EIP: 0x%x | ERR: 0x%x\n", 0x00FF00, regs->int_no, regs->eip, regs->err_code);
            extern void vesa_swap(void);
            vesa_swap();

            uint32_t screen_width = Wwidth();
            uint32_t screen_height = Hheight();
            uint32_t stride = Wpitch() / 4;
            if (stride == 0) stride = screen_width;

            int ref_w = 1280;
            int ref_h = 720;

            for (int y = 0; y < 8 && y < (int)screen_height; y++) {
                for (int x = 370 * (int)screen_width / ref_w; x < (int)screen_width; x++) {
                    fb[y * stride + x] = 0x000000;
                }
            }
            for (int y = 8; y < (int)screen_height; y++) {
                for (int x = 0; x < (int)screen_width; x++) {
                    fb[y * stride + x] = 0x000000;
                }
            }
            for (int y = 300 * (int)screen_height / ref_h; y < 330 * (int)screen_height / ref_h; y++) {
                for (int x = 400 * (int)screen_width / ref_w; x < 550 * (int)screen_width / ref_w; x++) {
                    fb[y * stride + x] = 0xFF0000;
                }
            }
            for (int y = 300 * (int)screen_height / ref_h; y < 330 * (int)screen_height / ref_h; y++) {
                for (int x = 730 * (int)screen_width / ref_w; x < 880 * (int)screen_width / ref_w; x++) {
                    fb[y * stride + x] = 0xFF0000;
                }
            }
            for (int y = 420 * (int)screen_height / ref_h; y < 450 * (int)screen_height / ref_h; y++) {
                for (int x = 550 * (int)screen_width / ref_w; x < 730 * (int)screen_width / ref_w; x++) {
                    fb[y * stride + x] = 0xFF0000;
                }
            }
        }
        
        for (;;);
    }
}