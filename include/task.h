#ifndef TASK_H
#define TASK_H

#include <stdint.h>

typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_DEAD
} task_state_t;

typedef struct task {
    // ✅ КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ ВЫРАВНИВАНИЯ (ДЕНЬ 7.4):
    // fpu_state ДОЛЖНА быть первым полем. 
    // pmm_alloc_page возвращает адреса, кратные 4096, что гарантирует 
    // 100% аппаратное выравнивание по 16 байт для fxsave/fxrstor.
    uint8_t fpu_state[512] __attribute__((aligned(16))); 
    
    uint32_t pid;             
    task_state_t state;       
    uint32_t esp;             
    uint32_t kernel_stack;    
    uint32_t* pdir_virt;      
    uint32_t cr3;             
    
    uint8_t fpu_initialized;

    char name[32];
    struct task* next;
    struct task* prev;
} task_t;

void tasking_init(void);
task_t* task_create(const char* name, void (*entry_point)(void));
void task_yield(void);
void task_exit(void);
void schedule(void);
void fpu_release_ownership(task_t* task);
// Вывод списка всех задач в консоль (для команды 'ps' в Shell)
void task_print_list(void);

#endif