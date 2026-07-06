#include "task.h"
#include "pmm.h"
#include "klib.h"
#include "serial.h"
#include "tss.h"
#include "paging.h"


// ✅ ОБНОВЛЕННАЯ СИГНАТУРА: Добавлен третий аргумент new_cr3
extern void switch_context(uint32_t* old_esp, uint32_t new_esp, uint32_t new_cr3);

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
    
    static task_t main_task;
    k_memset(&main_task, 0, sizeof(task_t));
    
    main_task.pid = next_pid++;
    main_task.state = TASK_RUNNING;
    main_task.kernel_stack = 0; 
    
    // ✅ ДЕНЬ 7.5: Main task использует глобальный PD ядра
    main_task.pdir_virt = boot_page_directory;
    main_task.cr3 = VIRT_TO_PHYS((uint32_t)boot_page_directory);

    // ... (копирование имени и настройка указателей next/prev) ...
    const char* name = "main";
    int i = 0; while(name[i] && i < 31) { main_task.name[i] = name[i]; i++; }
    main_task.name[i] = '\0';
    
    current_task = &main_task;
    main_task.next = &main_task;
    main_task.prev = &main_task;
    
    serial_print("[TASK] Main task registered.\n");
}

task_t* task_create(const char* name, void (*entry_point)(void)) {
    // 1. Выделяем стек и PCB (как было)
    uint32_t stack_phys = pmm_alloc_page();
    if (stack_phys == 0) return 0;
    uint32_t pcb_phys = pmm_alloc_page(); 
    if (pcb_phys == 0) return 0;

    task_t* new_task = (task_t*)PHYS_TO_VIRT(pcb_phys);
    k_memset(new_task, 0, sizeof(task_t));

    new_task->pid = next_pid++;
    new_task->state = TASK_READY;
    new_task->kernel_stack = stack_phys;
    
    // ✅ ДЕНЬ 7.5: Создаем изолированное адресное пространство!
    new_task->pdir_virt = vmm_create_address_space();
    if (!new_task->pdir_virt) {
        serial_print("[TASK] OOM: Failed to create Address Space!\n");
        pmm_free_page(stack_phys);
        pmm_free_page(pcb_phys);
        return 0;
    }
    // Сохраняем ФИЗИЧЕСКИЙ адрес для загрузки в CR3
    new_task->cr3 = VIRT_TO_PHYS((uint32_t)new_task->pdir_virt);

    // ... (копирование имени) ...
    int i = 0; while(name[i] && i < 31) { new_task->name[i] = name[i]; i++; }
    new_task->name[i] = '\0';

    // Stack Forging (без изменений)
    uint32_t* stack_top = (uint32_t*)PHYS_TO_VIRT(stack_phys + 4096);
    *(--stack_top) = (uint32_t)entry_point; 
    *(--stack_top) = (uint32_t)task_exit; 
    *(--stack_top) = (uint32_t)task_entry_trampoline; 
    *(--stack_top) = 0; // EBX
    *(--stack_top) = 0; // ESI
    *(--stack_top) = 0; // EDI
    *(--stack_top) = 0; // EBP
    new_task->esp = (uint32_t)stack_top;

    task_queue_add(new_task);
    serial_print("[TASK] Created new task with isolated memory: ");
    serial_print(name); serial_print("\n");
    return new_task;
}

void schedule(void) {
    if (!current_task || !current_task->next) return;
    
    task_t* old_task = current_task;
    task_t* new_task = current_task->next;
    
    if (old_task == new_task) return; 

    if (old_task->state == TASK_RUNNING) old_task->state = TASK_READY;
    new_task->state = TASK_RUNNING;
    current_task = new_task;

    if (new_task->kernel_stack != 0) {
        tss_set_kernel_stack(0x10, new_task->kernel_stack + 4096);
    }

    // ✅ ОБНОВЛЕННЫЙ ВЫЗОВ: Передаем новый CR3!
    switch_context(&old_task->esp, new_task->esp, new_task->cr3);
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