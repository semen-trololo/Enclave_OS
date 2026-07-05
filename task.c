#include "task.h"
#include "pmm.h"
#include "klib.h"
#include "serial.h"
#include "tss.h"

// Макрос для трансляции физического адреса в виртуальный (Direct Map)
// В День 6 мы замапили всю RAM в Higher Half (0xC0000000 + phys)
#define PHYS_TO_VIRT(addr) ((uint32_t)(addr) + 0xC0000000)

// Внешняя ASM-функция переключения контекста
extern void switch_context(uint32_t* old_esp, uint32_t new_esp);

// Глобальные переменные планировщика
task_t* current_task = 0;
uint32_t next_pid = 1;

// ==============================================================================
// ТРАМПЛИН ЗАДАЧИ (TASK WRAPPER)
// ==============================================================================
// Сюда попадают ВСЕ новые задачи перед стартом.
// В cdecl-соглашении аргументы лежат на стеке. Мы положили туда entry_point.
void task_entry_trampoline(void (*entry_point)(void)) {
    // 1. КРИТИЧЕСКИ ВАЖНО: Включаем прерывания!
    // Без этого задача навсегда останется с отключенным таймером,
    // так как switch_context вызывается из обработчика IRQ (где IF=0).
    __asm__ volatile("sti");
    
    // 2. Запускаем саму задачу
    entry_point();
    
    // 3. Если задача когда-либо вернется (например, забыла сделать sys_exit), 
    // корректно убиваем её, чтобы не крутиться в мусоре.
    task_exit();
}

// Вспомогательная функция: вставка задачи в кольцевой двусвязный список
static void task_queue_add(task_t* task) {
    if (!current_task) {
        // Первая задача в системе
        current_task = task;
        task->next = task;
        task->prev = task;
    } else {
        // Вставка сразу после current_task
        task->next = current_task->next;
        task->prev = current_task;
        current_task->next->prev = task;
        current_task->next = task;
    }
}

void tasking_init(void) {
    serial_print("[TASK] Initializing Task Manager...\n");
    
    // Создаем "Main Task" (тот, что прямо сейчас выполняет kernel_main)
    // Мы не выделяем ему новый стек, он использует boot-стек из boot.asm.
    // Используем static переменную, чтобы не зависеть от кучи (heap) на старте.
    static task_t main_task;
    k_memset(&main_task, 0, sizeof(task_t));
    
    main_task.pid = next_pid++;
    main_task.state = TASK_RUNNING;
    main_task.kernel_stack = 0; // Boot stack, не освобождаем при смерти
    
    const char* name = "main";
    int i = 0;
    while(name[i] && i < 31) {
        main_task.name[i] = name[i];
        i++;
    }
    main_task.name[i] = '\0';
    
    current_task = &main_task;
    main_task.next = &main_task;
    main_task.prev = &main_task;
    
    serial_print("[TASK] Main task registered.\n");
}

task_t* task_create(const char* name, void (*entry_point)(void)) {
    // 1. Выделяем физическую страницу под стек ядра (4096 байт)
    uint32_t stack_phys = pmm_alloc_page();
    if (stack_phys == 0) return 0;
    
    // 2. Выделяем страницу под саму структуру PCB
    uint32_t pcb_phys = pmm_alloc_page(); 
    if (pcb_phys == 0) return 0;

    // Транслируем физические адреса в виртуальные для безопасного доступа из C-кода
    task_t* new_task = (task_t*)PHYS_TO_VIRT(pcb_phys);
    k_memset(new_task, 0, sizeof(task_t));

    new_task->pid = next_pid++;
    new_task->state = TASK_READY;
    new_task->kernel_stack = stack_phys;
    
    // Копируем имя
    int i = 0;
    while(name[i] && i < 31) {
        new_task->name[i] = name[i];
        i++;
    }
    new_task->name[i] = '\0';

    // ========================================================================
    // МАГИЯ STACK FORGING (Версия 3.0: Исправление Invalid Opcode)
    // ========================================================================
    // Стек растёт ВНИЗ. Вершина стека = stack_phys + 4096.
    uint32_t* stack_top = (uint32_t*)PHYS_TO_VIRT(stack_phys + 4096);

    // 1. Аргумент для трамплина (лежит "глубже" в стеке, по большему адресу)
    // В cdecl аргументы пушатся ДО вызова функции.
    *(--stack_top) = (uint32_t)entry_point; 

    // 2. Адрес возврата из трамплина (куда прыгнуть, когда trampoline закончится)
    // Мы кладем сюда адрес task_exit, чтобы если трамплин "упадет" (чего он не должен), 
    // мы корректно завершились.
    *(--stack_top) = (uint32_t)task_exit; 

    // 3. Точка входа для switch_context (адрес, который снимет инструкция ret)
    *(--stack_top) = (uint32_t)task_entry_trampoline; 

    // 4. Callee-saved регистры, которые pop'ает switch_context (просто нули)
    *(--stack_top) = 0; // EBX
    *(--stack_top) = 0; // ESI
    *(--stack_top) = 0; // EDI
    *(--stack_top) = 0; // EBP <-- вершина стека в момент переключения!

    // Сохраняем указатель стека в PCB
    new_task->esp = (uint32_t)stack_top;

    // 4. Добавляем в очередь
    task_queue_add(new_task);
    
    serial_print("[TASK] Created new task: ");
    serial_print(name);
    serial_print("\n");

    return new_task;
}

void schedule(void) {
    if (!current_task || !current_task->next) return;
    
    task_t* old_task = current_task;
    task_t* new_task = current_task->next;
    
    if (old_task == new_task) return; // Задача всего одна, переключать некуда

    if (old_task->state == TASK_RUNNING) {
        old_task->state = TASK_READY;
    }
    
    new_task->state = TASK_RUNNING;
    current_task = new_task;

    // Обновляем TSS! Это критически важно для будущих прерываний из Ring 3.
    // Если задача имеет свой стек ядра, говорим процессору использовать его.
    if (new_task->kernel_stack != 0) {
        tss_set_kernel_stack(0x10, new_task->kernel_stack + 4096);
    }

    // Переключаем контекст! (Функция на ASM)
    switch_context(&old_task->esp, new_task->esp);
}

void task_yield(void) {
    schedule();
}

void task_exit(void) {
    // Помечаем задачу как мертвую
    current_task->state = TASK_DEAD;
    
    // Исключаем DEAD задачу из кольца, сшивая соседей
    if (current_task->next != current_task) {
        current_task->prev->next = current_task->next;
        current_task->next->prev = current_task->prev;
    } else {
        // Если это была последняя задача в системе
        current_task = 0; 
    }

    // Передаём управление следующей задаче
    schedule();
    
    // Fallback (никогда не выполнится, если есть другие задачи)
    while(1) __asm__ volatile("hlt");
}