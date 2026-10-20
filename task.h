#ifndef TASK_H
#define TASK_H
#include <stdint.h>
#include "heap.h"
#define TASK_MAX_OWNED_FRAMES 256
#define USER_STACK_TOP 0x90000000u
#define USER_STACK_SIZE 16384u
#define USER_HEAP_BASE 0x40000000u
typedef enum {
    TASK_READY,    
    TASK_RUNNING,  
    TASK_SLEEPING, 
    TASK_DEAD      
} task_state_t;
typedef struct {
    uint32_t esp;       
    uint32_t ebp;       
    uint32_t eip;       
    uint32_t pid;       
    task_state_t state; 
    uint32_t *page_directory;
    uint32_t priority;
    uint32_t kernel_stack_top;
    int is_user;
    uint32_t owned_frames[TASK_MAX_OWNED_FRAMES];
    int owned_frame_count;
    heap_arena_t heap;
    uint32_t heap_virt_next;
} task_t;
extern task_t tasks[];
int create_task(void (*entry_point)(), uint32_t priority);
int create_user_task(void (*entry_point)(), uint32_t priority);
int task_create_user(uint32_t priority);
void task_start_user(int slot, void (*entry_point)(), uint32_t user_esp);
int task_write_user_mem(int slot, uint32_t uva, const void* data, uint32_t len);
void task_abort_user(int slot);
int task_track_frames(int slot, const uint32_t* frames, int count);
int task_untrack_frame(int slot, uint32_t frame);
int task_map_user_page(int slot, uint32_t phys, uint32_t virt, uint32_t flags);
uint32_t task_translate_user_ptr(int slot, uint32_t virt);
#define USER_HEAP_MAX 0x80000000u
uint32_t task_get_heap_break(int slot);
uint32_t task_set_heap_break(int slot, uint32_t new_break);
void task_exit(void);
uint32_t schedule_handler(uint32_t esp);
void init_multitasking();
#endif