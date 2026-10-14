#include "task.h"
#include <stdint.h>
#include "pmm.h"
#include "gdt.h"
#include "vfs.h"
#include "paging.h"

#define TASK_STACK_SIZE 16384
#define TASK_KSTACK_SIZE 8192
#define TASK_GUARD_SIZE 4096
#define MAX_TASKS 32
extern volatile uint32_t system_ticks;
task_t tasks[MAX_TASKS];
int current_task = -1;
int num_tasks = 0;
static uint8_t task_stacks[MAX_TASKS][TASK_GUARD_SIZE + TASK_STACK_SIZE] __attribute__((aligned(4096)));
static uint8_t task_kstacks[MAX_TASKS][TASK_GUARD_SIZE + TASK_KSTACK_SIZE] __attribute__((aligned(4096)));

static uint32_t next_pid = 0;

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static int find_dead_slot(void) {
    for (int i = 0; i < num_tasks; i++) {
        if (tasks[i].state == TASK_DEAD) return i;
    }
    return -1;
}

static void free_task_address_space(task_t *t) {
    if (!t->page_directory || t->page_directory == page_directory) return;
    switch_page_directory(page_directory);
    uint32_t *dir = t->page_directory;
    for (int i = 0; i < 1024; i++) {
        uint32_t entry = dir[i];
        if (!(entry & 0x1)) continue;
        uint32_t global_entry = page_directory[i];
        if ((global_entry & 0x1) && (entry & ~0xFFFu) == (global_entry & ~0xFFFu)) continue;
        pmm_free_block((void*)(entry & ~0xFFFu));
    }
    pmm_free_block((void*)((uint32_t)dir & ~0xFFFu));
    for (int i = 0; i < t->owned_frame_count; i++) {
        pmm_free_block((void*)t->owned_frames[i]);
    }
    t->owned_frame_count = 0;
    t->page_directory = 0;
}

int task_track_frames(int slot, const uint32_t* frames, int count) {
    if (slot < 0 || slot >= num_tasks) return -1;
    if (tasks[slot].owned_frame_count + count > TASK_MAX_OWNED_FRAMES) return -1;
    for (int i = 0; i < count; i++) {
        tasks[slot].owned_frames[tasks[slot].owned_frame_count++] = frames[i];
    }
    return 0;
}

int task_map_user_page(int slot, uint32_t phys, uint32_t virt, uint32_t flags) {
    if (slot < 0 || slot >= num_tasks) return -1;
    uint32_t* dir = tasks[slot].page_directory;
    if (!dir) return -1;
    unmap_page_dir(dir, virt);
    map_page_dir(dir, phys, virt, flags);
    return 0;
}

uint32_t task_translate_user_ptr(int slot, uint32_t virt) {
    if (slot < 0 || slot >= num_tasks) return 0;
    uint32_t* dir = tasks[slot].page_directory;
    if (!dir) return 0;
    uint32_t pd_index = virt >> 22;
    uint32_t pt_index = (virt >> 12) & 0x03FF;
    if (!(dir[pd_index] & 0x1)) return 0;
    uint32_t* table = (uint32_t*)(dir[pd_index] & ~0xFFFu);
    if (!(table[pt_index] & 0x1)) return 0;
    return (table[pt_index] & ~0xFFFu) | (virt & 0xFFFu);
}

void task_exit() {
    vfs_task_killed(current_task);
    free_task_address_space(&tasks[current_task]);
    tasks[current_task].state = TASK_DEAD;
    for (;;) {
        asm volatile("sti; hlt");
    }
}

void init_multitasking() {
    tasks[0].pid = 0;
    tasks[0].state = TASK_RUNNING;
    tasks[0].page_directory = page_directory;
    num_tasks = 1;
    current_task = 0;
    next_pid = 1;
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
        tss_set_kernel_stack(tasks[current_task].kernel_stack_top);
        return tasks[current_task].esp;
    }

    int start_task = current_task;
    int found = 0;
    for (int tries = 0; tries < num_tasks; tries++) {
        current_task++;
        if (current_task >= num_tasks) {
            current_task = 0;
        }
        if (tasks[current_task].state == TASK_READY ||
            tasks[current_task].state == TASK_RUNNING) {
            found = 1;
            break;
        }
    }
    if (!found) {
        if (tasks[0].state != TASK_DEAD) {
            current_task = 0;
            tasks[0].state = TASK_RUNNING;
        } else {
            current_task = (start_task >= 0 && start_task < num_tasks) ? start_task : 0;
        }
    }
    uint32_t p = tasks[current_task].priority;
    ticks_left_on_current = (p > 0) ? (p - 1) : 0;

    tss_set_kernel_stack(tasks[current_task].kernel_stack_top);
    switch_page_directory(tasks[current_task].page_directory);

    return tasks[current_task].esp;
}

int create_task(void (*entry_point)(), uint32_t priority) {
    int slot = find_dead_slot();
    if (slot < 0) {
        if (num_tasks >= MAX_TASKS) return -1;
        slot = num_tasks;
        num_tasks++;
    }
    tasks[slot].owned_frame_count = 0;
    heap_arena_init(&tasks[slot].heap);
    uint32_t *stack = (uint32_t *)(task_stacks[slot] + TASK_GUARD_SIZE + TASK_STACK_SIZE);
    *(--stack) = 0x0202;                /* eflags */
    *(--stack) = 0x10;                  /* cs (ring0) */
    *(--stack) = (uint32_t)entry_point; /* eip */
    *(--stack) = 0;                     /* err_code (skipped by "add esp,8") */
    *(--stack) = 32;                    /* int_no  (skipped by "add esp,8") */
    for (int i = 0; i < 8; i++) *(--stack) = 0;    /* dummy pusha regs (eax..edi) */
    for (int i = 0; i < 4; i++) *(--stack) = 0x18; /* dummy ds/es/fs/gs */

    tasks[slot].esp = (uint32_t)stack;
    tasks[slot].pid = next_pid++;
    tasks[slot].state = TASK_READY;
    tasks[slot].priority = priority;
    tasks[slot].is_user = 0;
    tasks[slot].kernel_stack_top = (uint32_t)(task_kstacks[slot] + TASK_GUARD_SIZE + TASK_KSTACK_SIZE);

    uint32_t* dir = clone_kernel_page_directory();
    if (!dir) {
        tasks[slot].state = TASK_DEAD;
        return -1;
    }
    unmap_page_dir(dir, (uint32_t)task_stacks[slot]);
    unmap_page_dir(dir, (uint32_t)task_kstacks[slot]);
    tasks[slot].page_directory = dir;
    return slot;
}

int task_create_user(uint32_t priority) {
    int slot = find_dead_slot();
    if (slot < 0) {
        if (num_tasks >= MAX_TASKS) return -1;
        slot = num_tasks;
        num_tasks++;
    }
    tasks[slot].owned_frame_count = 0;
    tasks[slot].state = TASK_DEAD;
    tasks[slot].priority = priority;
    tasks[slot].is_user = 1;
    heap_arena_init(&tasks[slot].heap);
    tasks[slot].heap_virt_next = USER_HEAP_BASE;
    tasks[slot].kernel_stack_top = (uint32_t)(task_kstacks[slot] + TASK_GUARD_SIZE + TASK_KSTACK_SIZE);

    uint32_t* dir = clone_kernel_page_directory();
    if (!dir) return -1;
    tasks[slot].page_directory = dir;

    uint32_t stack_bottom = USER_STACK_TOP - TASK_STACK_SIZE;
    for (uint32_t va = stack_bottom; va < USER_STACK_TOP; va += PMM_BLOCK_SIZE) {
        void* phys = pmm_alloc_block();
        if (!phys) {
            task_abort_user(slot);
            return -1;
        }
        uint32_t frame = (uint32_t)phys;
        if (task_track_frames(slot, &frame, 1) != 0) {
            pmm_free_block(phys);
            task_abort_user(slot);
            return -1;
        }
        task_map_user_page(slot, frame, va, 0x07);
    }

    return slot;
}

void task_abort_user(int slot) {
    if (slot < 0 || slot >= num_tasks) return;
    free_task_address_space(&tasks[slot]);
    tasks[slot].state = TASK_DEAD;
}

void task_start_user(int slot, void (*entry_point)()) {
    if (slot < 0 || slot >= num_tasks) return;

    uint32_t user_stack_top = USER_STACK_TOP;
    uint32_t kernel_stack_top = tasks[slot].kernel_stack_top;
    uint32_t *kstack = (uint32_t *)kernel_stack_top;

    *(--kstack) = GDT_USER_DATA_SEL;
    *(--kstack) = user_stack_top;
    *(--kstack) = 0x0202;
    *(--kstack) = GDT_USER_CODE_SEL;
    *(--kstack) = (uint32_t)entry_point;
    *(--kstack) = 0;
    *(--kstack) = 128;
    for (int i = 0; i < 8; i++) *(--kstack) = 0;
    for (int i = 0; i < 4; i++) *(--kstack) = GDT_USER_DATA_SEL;

    tasks[slot].esp = (uint32_t)kstack;
    tasks[slot].pid = next_pid++;
    tasks[slot].state = TASK_READY;

    unmap_page_dir(tasks[slot].page_directory, (uint32_t)task_kstacks[slot]);
}

int create_user_task(void (*entry_point)(), uint32_t priority) {
    int slot = task_create_user(priority);
    if (slot < 0) return -1;
    task_start_user(slot, entry_point);
    return slot;
}