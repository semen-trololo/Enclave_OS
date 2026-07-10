#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include <stdbool.h>

struct open_file;

#define TASK_MAX_OPEN_FILES 16

typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_DEAD
} task_state_t;

typedef struct task {
    uint8_t fpu_state[512] __attribute__((aligned(16))); 
    
    uint32_t pid;             
    task_state_t state;       
    uint32_t esp;             
    uint32_t kernel_stack;    
    uint32_t* pdir_virt;      
    uint32_t cr3;             
    
    uint8_t fpu_initialized;

    struct open_file* fd_table[TASK_MAX_OPEN_FILES];
    struct vma_node* vma_head; 
    char name[32];
    struct task* next;
    struct task* prev;
} task_t;

void tasking_init(void);

// ✅ ИСПРАВЛЕНО: Добавлен параметр custom_pdir для передачи готового Address Space
task_t* task_create(const char* name, void (*entry_point)(void), 
                    bool is_user_mode, uint32_t user_esp, uint32_t* custom_pdir);

void task_yield(void);
void task_exit(void);
void schedule(void);
void fpu_release_ownership(task_t* task);
void task_print_list(void);

void task_init_fds(task_t* task);

// [ДЕНЬ 10] TASK ACCOUNTING & KILL
uint32_t task_get_count(void);
void task_kill_current(const char* reason);

extern task_t* current_task;

#endif 
