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

static uint32_t ticks_left_on_current = 0;

uint32_t schedule_handler(uint32_t esp) {
    system_ticks++;
    if (num_tasks <= 1) {
        return esp;
    }
    if (current_task >= 0) {
        tasks[current_task].esp = esp;
    }

    if (ticks_left_on_current > 0) {
        ticks_left_on_current--;
        return tasks[current_task].esp;
    }

    for (;;) {
        current_task++;
        if (current_task >= num_tasks) {
            current_task = 0;
        }
        if (tasks[current_task].state == TASK_READY ||
            tasks[current_task].state == TASK_RUNNING) {
            break;
        }
    }
    uint32_t p = tasks[current_task].priority;
    ticks_left_on_current = (p > 0) ? (p - 1) : 0;

    return tasks[current_task].esp;
}

void create_task(void (*entry_point)(), uint32_t priority) {
    if (num_tasks >= MAX_TASKS) return;
    void* stack_mem = pmm_alloc_block(); 
    if (!stack_mem) return;
    uint32_t *stack = (uint32_t *)((uint32_t)stack_mem + 4096);
    *(--stack) = (uint32_t)task_exit;
    *(--stack) = 0x0202;                
    *(--stack) = 0x10;          
    *(--stack) = (uint32_t)entry_point; 
    *(--stack) = 0;                  
    *(--stack) = 32;    
    *(--stack) = 0;
    for (int i = 0; i < 7; i++) *(--stack) = 0; 
    for (int i = 0; i < 4; i++) *(--stack) = 0x18; 

    tasks[num_tasks].esp = (uint32_t)stack;
    tasks[num_tasks].pid = num_tasks;
    tasks[num_tasks].state = TASK_READY;
    tasks[num_tasks].priority = priority;
    num_tasks++;
}