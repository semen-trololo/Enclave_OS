#ifndef TASK_H
#define TASK_H

#include <stdint.h>

// Состояния процесса
typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_DEAD
} task_state_t;

// Process Control Block (PCB)
typedef struct task {
    uint32_t pid;
    task_state_t state;
    uint32_t esp;             // Сохраненный указатель стека (для switch_context)
    uint32_t kernel_stack;    // Физический адрес базы выделенной страницы стека
    char name[32];

    // Указатели для кольцевого двусвязного списка
    struct task* next;
    struct task* prev;
} task_t;

// Инициализация подсистемы многозадачности
void tasking_init(void);

// Создание нового потока (пока работаем в Ring 0)
task_t* task_create(const char* name, void (*entry_point)(void));

// Добровольная отдача кванта времени
void task_yield(void);

// Завершение текущего потока
void task_exit(void);

// Главный планировщик (вызывается из таймера или системных вызовов)
void schedule(void);

#endif
