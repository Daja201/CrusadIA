#include "task.h"
#include <stdint.h>
#include "pmm.h"

#define MAX_TASKS 16
extern volatile uint32_t system_ticks;
task_t tasks[MAX_TASKS];
int current_task = -1;
int num_tasks = 0;

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

void task_exit() {
    tasks[current_task].state = TASK_DEAD;
    for (;;) {
        asm volatile("sti; hlt");
    }
}

void init_multitasking() {
    tasks[0].pid = 0;
    tasks[0].state = TASK_RUNNING;
    num_tasks = 1;
    current_task = 0;
}

uint32_t schedule_handler(uint32_t esp) {
    system_ticks++;
    if (num_tasks <= 1) {
        return esp;
    }
    if (current_task >= 0) {
        tasks[current_task].esp = esp;
    }
    current_task++;
    if (current_task >= num_tasks) {
        current_task = 0;
    }
    return tasks[current_task].esp;
}

void create_task(void (*entry_point)(), uint32_t priority) {
    if (num_tasks >= MAX_TASKS) return;
    void* stack_mem = pmm_alloc_block(); 
    if (!stack_mem) return;
    int priority_int = 1;
    uint32_t *stack = (uint32_t *)((uint32_t)stack_mem + 4096);
    *(--stack) = (uint32_t)task_exit;
    *(--stack) = 0x0202;                
    *(--stack) = 0x10;          
    *(--stack) = (uint32_t)entry_point; 
    *(--stack) = 0;                  
    *(--stack) = 32;    
    *(--stack) = priority_int;            
    for (int i = 0; i < 7; i++) *(--stack) = 0; 
    for (int i = 0; i < 4; i++) *(--stack) = 0x18; 
    for (int i = priority_int; i > 0; i--) {
        tasks[num_tasks].esp = (uint32_t)stack;
        tasks[num_tasks].pid = num_tasks;
        tasks[num_tasks].state = TASK_READY;
        num_tasks++;
    }
}