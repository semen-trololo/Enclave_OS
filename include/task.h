#ifndef TASK_H
#define TASK_H

#include <stdint.h>

// Состояния процесса
typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_DEAD
} task_state_t;

typedef struct task {
    uint32_t pid;
    task_state_t state;
    uint32_t esp;             // Сохраненный указатель стека
    uint32_t kernel_stack;    // Физический адрес базы стека ядра
    
    // ✅ НОВЫЕ ПОЛЯ ДЛЯ ДНЯ 7.5
    uint32_t* pdir_virt;      // Виртуальный адрес Page Directory (для C-кода)
    uint32_t cr3;             // Физический адрес Page Directory (для CR3)

    char name[32];
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
