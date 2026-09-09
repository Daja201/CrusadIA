#ifndef IDT_H
#define IDT_H
#include <stdint.h>
struct idt_entry_struct {
    uint16_t base_low;      
    uint16_t sel;           
    uint8_t  always0;       
    uint8_t  flags;         
    uint16_t base_high;     
} __attribute__((packed));

typedef struct idt_entry_struct idt_entry_t;
typedef struct idt_entry_struct idt_entry_t;
struct idt_ptr_struct {
    uint16_t limit;  
    uint32_t base;   
} __attribute__((packed));
typedef struct idt_ptr_struct idt_ptr_t;
typedef struct {
    uint32_t gs, fs, es, ds;                          
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;  
    uint32_t int_no, err_code;                        
    uint32_t eip, cs, eflags, useresp, ss;            
} registers_t;
void init_idt();
#endif