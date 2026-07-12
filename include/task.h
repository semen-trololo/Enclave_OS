#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include <stdbool.h>

struct open_file;
struct vma_node;

#define TASK_MAX_OPEN_FILES 16

typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_SLEEPING,  // [ДЕНЬ 14] Процесс спит (ждет ребенка в waitpid)
    TASK_ZOMBIE,    // [ДЕНЬ 14] Процесс мертв, но PCB жив (ждет waitpid)
    TASK_DEAD       // Процесс полностью мертв, готов к освобождению Reaper'ом
} task_state_t;

typedef struct task {
    uint8_t fpu_state[512] __attribute__((aligned(16))); 
    
    uint32_t pid;             
    task_state_t state;       
    uint32_t esp;             
    uint32_t kernel_stack_virt;
    uint32_t* pdir_virt;      
    uint32_t cr3;             
    
    uint8_t fpu_initialized;

    struct open_file* fd_table[TASK_MAX_OPEN_FILES];
    struct vma_node* vma_head; 
    char name[32];
    
    // Указатели для планировщика (Run Queue - кольцевой список)
    struct task* next;
    struct task* prev;
    
    // Указатель для сборщика мусора (Reaper Queue)
    struct task* reaper_next; 
    
    // ========================================================================
    // [ДЕНЬ 14] PROCESS TREE & SUPERVISOR TREES
    // ========================================================================
    struct task* parent;           // Родительский процесс
    struct task* children;         // Голова списка детей
    struct task* next_sibling;     // Следующий брат/сестра
    
    int exit_code;                 // Код выхода (сохраняется в Zombie state)
    
    // Supervisor Tree flags (Erlang-style vs Unix-style)
    int orphan_on_exit;            // 1 = Unix-style (усыновить детей init)
    int monitor_children;          // 1 = Erlang-style (убить детей при смерти)
} task_t;

void tasking_init(void);

task_t* task_create(const char* name, void (*entry_point)(void), 
                    bool is_user_mode, uint32_t user_esp, uint32_t* custom_pdir);

void task_yield(void);
void task_exit(void);
void schedule(void);
void fpu_release_ownership(task_t* task);
void task_print_list(void);

void task_init_fds(task_t* task);

uint32_t task_get_count(void);
void task_kill_current(const char* reason);

extern task_t* current_task;

struct regs; // Forward declaration

// Создает копию текущего процесса (fork).
// r - указатель на struct regs из syscall_dispatcher (нужен для обнуления EAX у ребенка)
int task_fork(struct regs* r);

// Ожидает завершения ребенка.
int task_waitpid(int pid, int* status, int options);

// Глобальный указатель на Init Task (PID 1)
extern task_t* init_task;

// Константа для WNOHANG
#define WNOHANG 1

#endif 