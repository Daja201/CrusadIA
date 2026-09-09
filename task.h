#ifndef TASK_H
#define TASK_H
#include <stdint.h>
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
} task_t;
void create_task(void (*entry_point)());
uint32_t schedule_handler(uint32_t esp);
void init_multitasking();
#endif