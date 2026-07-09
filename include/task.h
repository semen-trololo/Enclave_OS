#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include <stdbool.h>

// Forward declaration для избежания циклических зависимостей с vfs.h
// Мы не инклюдим vfs.h сюда, чтобы ядро VFS и ядро процессов были слабосвязанными.
struct open_file;

// Ограничение на количество открытых файлов на процесс.
// Должно жестко синхронизироваться с VFS_MAX_OPEN_FILES в vfs.h
#define TASK_MAX_OPEN_FILES 16

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

    // 🆕 VFS ИНТЕГРАЦИЯ (ДЕНЬ 8.1):
    // Таблица файловых дескрипторов процесса.
    // Индекс массива = FD (0=stdin, 1=stdout, 2=stderr...).
    // Хранит указатели на open_file_t, которые содержат курсор (offset) и vfs_node_t.
    struct open_file* fd_table[TASK_MAX_OPEN_FILES];
    // 🆕 VMA SUBSYSTEM (ДЕНЬ 9)
    struct vma_node* vma_head; 
    char name[32];
    struct task* next;
    struct task* prev;
} task_t;

void tasking_init(void);
task_t* task_create(const char* name, void (*entry_point)(void), 
                    bool is_user_mode, uint32_t user_esp);
void task_yield(void);
void task_exit(void);
void schedule(void);
void fpu_release_ownership(task_t* task);
void task_print_list(void);

// 🆕 VFS API для менеджера процессов
// Инициализирует fd_table[0,1,2] стандартными потоками (tty/serial)
void task_init_fds(task_t* task);

// 🆕 ЭКСПОРТ ГЛОБАЛЬНОЙ ПЕРЕМЕННОЙ ДЛЯ VFS
// Позволяет системным вызовам (sys_open, sys_read) получать доступ 
// к таблице файловых дескрипторов ТЕКУЩЕГО выполняемого процесса.
extern task_t* current_task;
// Убийство текущего процесса (вызывается из Page Fault Handler)
void task_kill_current(const char* reason);

#endif