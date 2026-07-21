#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include <stdbool.h>
#include "idt.h"
#include "vfs.h"

// ============================================================================
// FORWARD DECLARATIONS (Разрыв циклической зависимости с vma.h)
// ============================================================================
struct vma_node;
typedef struct vma_node vma_node_t;

// ============================================================================
// TASK STATES
// ============================================================================
#define TASK_RUNNING    0
#define TASK_READY      1
#define TASK_SLEEPING   2
#define TASK_ZOMBIE     3
#define TASK_DEAD       4

// ============================================================================
// PROCESS TREE FLAGS (Day 14)
// ============================================================================
#define TASK_MAX_OPEN_FILES 256

// ============================================================================
// TASK STRUCTURE (PCB - Process Control Block)
// ============================================================================
typedef struct task {
    // ⚠️ FPU state ДОЛЖЕН быть первым полем для 16-byte alignment
    uint8_t fpu_state[512] __attribute__((aligned(16)));
    int fpu_initialized;
    
    // Process Identity
    uint32_t pid;
    char name[32];
    uint8_t state;
    
    // Memory Management
    uint32_t cr3;                    // Physical address of Page Directory
    uint32_t* pdir_virt;             // Virtual address of Page Directory
    uint32_t kernel_stack_virt;      // Virtual address of kernel stack
    vma_node_t* vma_head;            // Virtual Memory Areas list
    
    // Context Switching
    uint32_t esp;                    // Stack pointer (saved during switch)
    
    // File Descriptors
    struct open_file* fd_table[TASK_MAX_OPEN_FILES];
    
    // Process Tree (Day 14)
    struct task* parent;             // Parent process
    struct task* children;           // First child
    struct task* next_sibling;       // Next sibling
    int exit_code;                   // Exit code (for waitpid)
    int orphan_on_exit;              // Unix-style: adopt children to init
    int monitor_children;            // Erlang-style: kill children on exit
    
    // Scheduling
    struct task* next;               // Next task in run queue
    struct task* prev;               // Previous task in run queue
    struct task* reaper_next;        // Next task in reaper queue
    
    // [ДЕНЬ 15] TIMER QUEUE SUPPORT
    uint32_t sleep_until;            // PIT tick to wake up (0 = event-based sleep)
} task_t;

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================
extern task_t* current_task;
extern task_t* init_task;

// ============================================================================
// API
// ============================================================================
void tasking_init(void);
task_t* task_create(const char* name, void (*entry_point)(void), 
                    bool is_user_mode, uint32_t user_esp, uint32_t* custom_pdir);
void task_exit(int exit_code);
void task_yield(void);
void task_timer_tick(uint32_t tick);  // ← DIP-3: callback для timer
void schedule(void);
void task_kill_current(const char* reason);
void task_print_list(void);
uint32_t task_get_count(void);

//  Process Management
int task_fork(struct regs* r);
int task_waitpid(int pid, int* status, int options);

//  FPU Management
void fpu_release_ownership(task_t* task);

#endif // TASK_H
