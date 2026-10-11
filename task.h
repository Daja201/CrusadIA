#ifndef TASK_H
#define TASK_H
#include <stdint.h>
#define TASK_MAX_OWNED_FRAMES 256
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
} task_t;
extern task_t tasks[];
int create_task(void (*entry_point)(), uint32_t priority);
int create_user_task(void (*entry_point)(), uint32_t priority);
int task_track_frames(int slot, const uint32_t* frames, int count);
void task_exit(void);
uint32_t schedule_handler(uint32_t esp);
void init_multitasking();
#endif