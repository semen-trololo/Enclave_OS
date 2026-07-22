//task.c

#include "task.h"
#include "pmm.h"
#include "klib.h"
#include "serial.h"
#include "tss.h"
#include "paging.h"
#include "isr.h"
#include "vfs.h"        // ← vfs_close_fd() теперь здесь
#include "vma.h"
// #include "syscall.h"  ← УДАЛЕНО (CYCLE-3, DIP-1)
#include "heap.h"
#include <stdbool.h>
#include "config.h"
#include "elf.h"
#include "timer.h"      // ← DIP-3: timer_get_ticks() для wake_sleepers(

extern void switch_context(uint32_t* old_esp, uint32_t new_esp, uint32_t new_cr3);
extern void ret_from_fork(void);

task_t* current_task = 0;
uint32_t next_pid = 1;
static task_t* fpu_owner = 0;

static task_t* dead_tasks_head = NULL;

static uint32_t task_count = 0;
task_t* init_task = 0;
uint32_t task_get_count(void) { return task_count; }

// ============================================================================
// ТРАМПЛИН (Task Wrapper)
// ============================================================================
void task_entry_trampoline(void (*entry_point)(void), bool is_user_mode, uint32_t user_esp) {
    __asm__ volatile("sti"); 
    
    if (is_user_mode) {
        extern void enter_usermode(uint32_t entry_point, uint32_t user_esp);
        enter_usermode((uint32_t)entry_point, user_esp);
    } else {
        entry_point();
        task_exit(0);
    }
}

// ============================================================================
// LAZY FPU SWITCHING (INT 7 - #NM HANDLER)
// ============================================================================
static volatile int in_nm_handler = 0;

static void device_not_available_handler(struct regs* r) {
    (void)r;
    if (in_nm_handler) {
        serial_print("\n[FPU] FATAL: Recursive #NM detected!\n");
        while(1) __asm__("cli; hlt");
    }
    in_nm_handler = 1;

    __asm__ volatile("clts"); 

    uint32_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    if (!(cr4 & (1 << 9))) {
        cr4 |= (1 << 9) | (1 << 10);
        __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));
    }

    if (fpu_owner && fpu_owner != current_task) {
        __asm__ volatile("fxsave (%0)" : : "r"(fpu_owner->fpu_state) : "memory");
    }
    
    if (current_task->fpu_initialized) {
        __asm__ volatile("fxrstor (%0)" : : "r"(current_task->fpu_state) : "memory");
    } else {
        __asm__ volatile("fninit");
        __asm__ volatile("fxsave (%0)" : : "r"(current_task->fpu_state) : "memory");
        current_task->fpu_initialized = 1;
    }
    
    fpu_owner = current_task;
    in_nm_handler = 0; 
}

void fpu_release_ownership(task_t* task) {
    if (fpu_owner == task) fpu_owner = 0;
}

// ============================================================================
// УПРАВЛЕНИЕ ОЧЕРЕДЬЮ (RUN QUEUE)
// ============================================================================
static void task_queue_add(task_t* task) {
    if (!current_task) {
        current_task = task;
        task->next = task;
        task->prev = task;
    } else {
        task->next = current_task->next;
        task->prev = current_task;
        current_task->next->prev = task;
        current_task->next = task;
    }
}

// ============================================================================
// ИНИЦИАЛИЗАЦИЯ ПЛАНИРОВЩИКА
// ============================================================================
void tasking_init(void) {
    serial_print("[TASK] Initializing Task Manager...\n");
    task_count = 0;
    
    uint32_t main_pcb_phys = pmm_alloc_page();
    if (main_pcb_phys == 0) {
        serial_print("[TASK] FATAL: OOM allocating main_task PCB!\n");
        while(1) __asm__("cli; hlt");
    }
    
    task_t* main_task_ptr = (task_t*)PHYS_TO_VIRT(main_pcb_phys);
    k_memset(main_task_ptr, 0, sizeof(task_t));
    
    main_task_ptr->pid = next_pid++;
    main_task_ptr->state = TASK_RUNNING;
    main_task_ptr->kernel_stack_virt = 0;
    main_task_ptr->sleep_until = 0;
    
    main_task_ptr->cr3 = VIRT_TO_PHYS((uint32_t)boot_page_directory);

    const char* name = "main";
    int i = 0; while(name[i] && i < 31) { main_task_ptr->name[i] = name[i]; i++; }
    main_task_ptr->name[i] = '\0';
    
    current_task = main_task_ptr;
    main_task_ptr->next = main_task_ptr;
    main_task_ptr->prev = main_task_ptr;
    main_task_ptr->reaper_next = NULL;
    main_task_ptr->parent = NULL;
    main_task_ptr->children = NULL;
    main_task_ptr->next_sibling = NULL;
    main_task_ptr->exit_code = 0;
    main_task_ptr->orphan_on_exit = 1;
    main_task_ptr->monitor_children = 0;
    init_task = main_task_ptr;
    task_count++;
    
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1 << 2); 
    cr0 |= (1 << 1);  
    cr0 |= (1 << 5);  
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));

    uint32_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1 << 9);  
    cr4 |= (1 << 10); 
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));
    
    isr_register_handler(7, device_not_available_handler);
    
    serial_print("[TASK] Main task registered.\n");
    serial_print("[TASK] FPU Lazy Switching enabled.\n");
}

// ============================================================================
// СОЗДАНИЕ НОВОЙ ЗАДАЧИ
// ============================================================================
task_t* task_create(const char* name, void (*entry_point)(void), 
                    bool is_user_mode, uint32_t user_esp, uint32_t* custom_pdir) {
    
    uint32_t stack_top = vmm_alloc_kernel_stack();
    if (stack_top == 0) return 0;

    uint32_t pcb_phys = pmm_alloc_page(); 
    if (pcb_phys == 0) {
        vmm_free_kernel_stack(stack_top);
        return 0;
    }

    task_t* new_task = (task_t*)PHYS_TO_VIRT(pcb_phys);
    k_memset(new_task, 0, sizeof(task_t));
    new_task->vma_head = NULL; 
    new_task->sleep_until = 0;

    new_task->pid = next_pid++;
    new_task->state = TASK_READY;
    new_task->kernel_stack_virt = stack_top; // 🛡️ FIX: Теперь это TOP, а не base!
    new_task->reaper_next = NULL;  
    
    if (is_user_mode) {
        if (custom_pdir) {
            new_task->pdir_virt = custom_pdir;
            new_task->cr3 = VIRT_TO_PHYS((uint32_t)custom_pdir);
        } else {
            new_task->pdir_virt = vmm_create_address_space();
            if (!new_task->pdir_virt) {
                vmm_free_kernel_stack(stack_top); // 🛡️ FIX: Корректный откат
                pmm_free_page(pcb_phys);
                return 0;
            }
            new_task->cr3 = VIRT_TO_PHYS((uint32_t)new_task->pdir_virt);
        }
    } else {
        new_task->pdir_virt = boot_page_directory;
        new_task->cr3 = VIRT_TO_PHYS((uint32_t)boot_page_directory);
    }

    int i = 0; while(name[i] && i < 31) { new_task->name[i] = name[i]; i++; }
    new_task->name[i] = '\0';

    // 🛡️ FIX: Стек растет вниз от stack_top (убрали stack_virt + stack_size)
    uint32_t* stack_ptr = (uint32_t*)stack_top;
    
    *(--stack_ptr) = user_esp;                      
    *(--stack_ptr) = (uint32_t)is_user_mode;        
    *(--stack_ptr) = (uint32_t)entry_point;         
    *(--stack_ptr) = (uint32_t)task_exit;           
   
    *(--stack_ptr) = (uint32_t)task_entry_trampoline; 
    *(--stack_ptr) = 0; // EBX
    *(--stack_ptr) = 0; // ESI
    *(--stack_ptr) = 0; // EDI
    *(--stack_ptr) = 0; // EBP
    
    new_task->esp = (uint32_t)stack_ptr;

    for (int j = 0; j < TASK_MAX_OPEN_FILES; j++) new_task->fd_table[j] = 0;
    
    new_task->parent = current_task;
    new_task->children = NULL;
    new_task->next_sibling = NULL;
    new_task->exit_code = 0;
    new_task->orphan_on_exit = 1;
    new_task->monitor_children = 0;
    
    if (current_task) {
        new_task->next_sibling = current_task->children;
        current_task->children = new_task;
    }

    // ========================================================================
    // RUN QUEUE INSERTION (IRQ-SAFE)
    // ========================================================================
    // Используем irq_save()/irq_restore(), чтобы не ломать вложенные
    // критические секции.
    // ========================================================================
    irq_flags_t flags = irq_save();
    task_queue_add(new_task);
    task_count++;
    irq_restore(flags);

    serial_printf("[TASK] Created PID %d: %s (mode: %s)\n", 
                  new_task->pid, name, is_user_mode ? "USER" : "KERNEL");
    return new_task;
}

// ============================================================================
// RESPAWN INIT TASK (PID 1)
// ============================================================================
// Если Init упал, ядро автоматически перезапускает его из Reaper Phase.
// Это реализует принцип "Let it crash" для корневого процесса.
//
// T1 FIX:
//   - temp_task теперь полностью обнуляется.
//   - Stack VMA создаётся корректно как [bottom, top).
//   - user_esp находится внутри VMA.
//   - init_task обновляется на новый Init.
//   - Новый Init отцепляется от current_task.
//   - PID 1 сохраняется как инвариант системы.
//   - Init использует monitor_children = 1:
//       если Init умирает, его прямой ребёнок shell тоже убивается.
//
// [T3/T4 HARDENING]
//   - Все критические секции переведены на irq_save()/irq_restore().
// ============================================================================
static void respawn_init_task(void) {
    serial_print("[REAPER] ⚠️ PID 1 (Init) died! Respawning...\n");

    vfs_node_t* init_node = vfs_findnode("/sbin/init.elf");
    if (!init_node) {
        serial_print("[REAPER] FATAL: /sbin/init.elf not found! Cannot respawn Init.\n");
        while (1) { __asm__ volatile("cli; hlt"); }
    }

    uint32_t* new_pdir = vmm_create_address_space();
    if (!new_pdir) {
        serial_print("[REAPER] FATAL: OOM creating address space for Init!\n");
        while (1) { __asm__ volatile("cli; hlt"); }
    }

    //
    // T1 CRITICAL FIX:
    // temp_task должен быть полностью обнулён перед elf_load().
    //
    task_t temp_task;
    k_memset(&temp_task, 0, sizeof(task_t));

    temp_task.pdir_virt = new_pdir;
    temp_task.cr3 = VIRT_TO_PHYS((uint32_t)new_pdir);
    temp_task.vma_head = NULL;

    uint32_t entry = elf_load(init_node, &temp_task);
    if (entry == 0) {
        serial_print("[REAPER] FATAL: Failed to load /sbin/init.elf!\n");
        vma_destroy_all(&temp_task);
        vmm_destroy_address_space(new_pdir);
        while (1) { __asm__ volatile("cli; hlt"); }
    }

    //
    // FIX: Корректный User Stack.
    //
    // VMA должна быть:
    //   [USER_STACK_VIRT_TOP - USER_STACK_SIZE, USER_STACK_VIRT_TOP)
    //
    // user_esp должен быть внутри VMA:
    //   USER_STACK_VIRT_TOP - 16
    //
    uint32_t stack_vma_top    = USER_STACK_VIRT_TOP;
    uint32_t stack_vma_bottom = stack_vma_top - USER_STACK_SIZE;
    uint32_t user_esp         = stack_vma_top - 16;

    // ========================================================================
    // CRITICAL SECTION START
    // ========================================================================
    // Дальше мы модифицируем VMA, создаём задачу, обновляем process tree и
    // глобальный init_task. Это должно быть атомарно относительно timer IRQ.
    // ========================================================================
    irq_flags_t flags = irq_save();

    //
    // Добавляем VMA во временный task ДО task_create().
    // Если task_create() упадёт, мы корректно уничтожим temp_task.vma_head.
    //
    if (vma_add(&temp_task, stack_vma_bottom, stack_vma_top, VMA_READ | VMA_WRITE) < 0 ||
        vma_add(&temp_task, USER_HEAP_START, USER_HEAP_START, VMA_READ | VMA_WRITE) < 0) {

        serial_print("[REAPER] FATAL: OOM creating Init VMAs!\n");

        vma_destroy_all(&temp_task);
        vmm_destroy_address_space(new_pdir);

        irq_restore(flags);
        while (1) { __asm__ volatile("cli; hlt"); }
    }

    task_t* new_init = task_create("/sbin/init.elf",
                                   (void (*)(void))entry,
                                   true,
                                   user_esp,
                                   new_pdir);

    if (!new_init) {
        serial_print("[REAPER] FATAL: Failed to create Init task!\n");

        vma_destroy_all(&temp_task);
        vmm_destroy_address_space(new_pdir);

        irq_restore(flags);
        while (1) { __asm__ volatile("cli; hlt"); }
    }

    //
    // Передаём VMA из temp_task новой задаче.
    //
    new_init->vma_head = temp_task.vma_head;
    temp_task.vma_head = NULL;

    //
    // Сохраняем инвариант:
    //   PID 1 == Init.
    //
    // Старый PID 1 уже мёртв и находится в Reaper cleanup,
    // поэтому повторно используем PID 1.
    //
    new_init->pid = 1;

    //
    // Init policy:
    //
    // Init у нас — watchdog shell.
    // Если Init умирает, старый shell нужно убить, чтобы новый Init
    // мог чисто открыть /dev/console и запустить новый shell.
    //
    new_init->orphan_on_exit   = 0;
    new_init->monitor_children = 1;

    //
    // Отцепляем новый Init от current_task.
    //
    // task_create() по умолчанию делает new_init ребёнком current_task.
    // Для корневого Init это неправильно.
    //
    if (current_task && current_task->children) {
        if (current_task->children == new_init) {
            current_task->children = new_init->next_sibling;
        } else {
            task_t* sibling = current_task->children;

            while (sibling && sibling->next_sibling != new_init) {
                sibling = sibling->next_sibling;
            }

            if (sibling) {
                sibling->next_sibling = new_init->next_sibling;
            }
        }
    }

    new_init->parent = NULL;
    new_init->next_sibling = NULL;

    //
    // CRITICAL:
    // Обновляем глобальный init_task.
    //
    // Иначе task_exit() будет усыновлять сирот в старый освобождённый Init.
    //
    init_task = new_init;

    // ========================================================================
    // CRITICAL SECTION END
    // ========================================================================
    irq_restore(flags);

    serial_printf("[REAPER] ✓ Init respawned successfully as PID %d\n", new_init->pid);
}

// ============================================================================
// ПЛАНИРОВЩИК (ROUND-ROBIN + REAPER + IDLE HLT)
// ============================================================================
void schedule(void) {
    // ========================================================================
    // IRQ-SAFE ENTRY
    // ========================================================================
    // Сохраняем EFLAGS, чтобы корректно работать даже если schedule()
    // вызван из уже запрещённой критической секции.
    // ========================================================================
    irq_flags_t flags = irq_save();

    // 🛡️ REAPER PHASE: Очищаем мертвые задачи ПЕРЕД планированием.
    if (dead_tasks_head != NULL) {
        while (dead_tasks_head != NULL) {
            task_t* dead = dead_tasks_head;
            dead_tasks_head = dead->reaper_next; 
        
            serial_printf("[REAPER] Cleaning up PID %d (%s)\n", dead->pid, dead->name);
            
            // 🛡️ [ДЕНЬ 24] RESPAWN INIT: Если умер PID 1, перезапускаем его
            if (dead->pid == 1) {
                respawn_init_task();
            }
        
            // 🛡️ [ДЕНЬ 16] FIX: Освобождаем стек через VMM (снимает маппинг и Guard Page)
            if (dead->kernel_stack_virt != 0) {
                vmm_free_kernel_stack(dead->kernel_stack_virt);
            }
            
            pmm_free_page(VIRT_TO_PHYS((uint32_t)dead));
            task_count--;
        }
    }

    if (!current_task || !current_task->next) {
        irq_restore(flags);
        return;
    }
    
    task_t* old_task = current_task;
    task_t* new_task = current_task->next;
    
    while (new_task != old_task && 
           (new_task->state == TASK_SLEEPING || 
            new_task->state == TASK_ZOMBIE || 
            new_task->state == TASK_DEAD)) {
        new_task = new_task->next;
    }
    
    if (old_task == new_task) {
        if (old_task->state != TASK_RUNNING) {
            //
            // Специальный idle-wait:
            //   sti; hlt; cli
            //
            // Это НЕ критическая секция, а осознанное разрешение прерываний
            // на время HLT. Поэтому здесь оставляем как есть.
            //
            __asm__ volatile("sti; hlt; cli");
        }
        irq_restore(flags);
        return; 
    }

    if (old_task->state == TASK_RUNNING) old_task->state = TASK_READY;
    new_task->state = TASK_RUNNING;
    current_task = new_task;

    if (new_task->kernel_stack_virt != 0) {
        // 🛡️ [ДЕНЬ 16] FIX: kernel_stack_virt теперь уже TOP, + 16384 не нужен!
        tss_set_kernel_stack(0x10, new_task->kernel_stack_virt);
    }

    switch_context(&old_task->esp, new_task->esp, new_task->cr3);
    
    // --- ЗДЕСЬ ПРОДОЛЖАЕТ ВЫПОЛНЕНИЕ УЖЕ СТАРАЯ ЗАДАЧА ПОСЛЕ CONTEXT SWITCH ---

    irq_restore(flags);
}

void task_yield(void) { schedule(); }

// ============================================================================
// DIP-3 FIX: Timer Tick Handler (перенесено из timer.c)
// ============================================================================
// Timer (L2) больше не знает о задачах. Kernel (L7) инжектит эту функцию
// как callback через timer_set_tick_callback().
//
// Логика:
//   1. Пробудить спящих по таймеру (sleep_until > 0).
//   2. Каждые 10 тиков (10 мс) — preemptive schedule.
// ============================================================================
static void wake_sleepers(void) {
    if (!current_task) return;
    task_t* t = current_task;
    do {
        if (t->state == TASK_SLEEPING && t->sleep_until > 0) {
            if (timer_get_ticks() >= t->sleep_until) {
                t->state = TASK_READY;
                t->sleep_until = 0;
            }
        }
        t = t->next;
    } while (t != current_task);
}

void task_timer_tick(uint32_t tick) {
    wake_sleepers();

    if (tick % 10 == 0) {
        schedule();
    }
}

// ============================================================================
// [T1 / INIT RESPAWN HARDENING]
// ============================================================================
// Принудительно зачищает детей задачи при смерти родителя.
// Используется для PID 1 и для monitor_children.
// [T3/T4 HARDENING]
//   - Run queue / reaper queue модификации переведены на irq_save/irq_restore.
// ============================================================================
static void task_cleanup_children_on_exit(task_t* task) {
    if (!task) return;

    task_t* child = task->children;

    while (child != NULL) {
        task_t* next = child->next_sibling;

        if (child->state != TASK_DEAD) {

            if (child->state != TASK_ZOMBIE) {

                if (child->children != NULL) {
                    task_cleanup_children_on_exit(child);
                }

                serial_printf("[TASK] Killing child PID %d (%s) because parent PID %d exits\n",
                              child->pid, child->name, task->pid);

                fpu_release_ownership(child);

                // ============================================================
                // T2 FIX: Закрываем FD убитого ребёнка через vfs_close_fd().
                // Раньше здесь был комментарий "пока не делается безопасно".
                // Теперь vfs_close_fd() работает для ЛЮБОЙ задачи.
                // ============================================================
                for (int i = 0; i < TASK_MAX_OPEN_FILES; i++) {
                    if (child->fd_table[i] != NULL) {
                        vfs_close_fd(child, i);
                    }
                }

                vma_destroy_all(child);

                if (child->pdir_virt && child->pdir_virt != boot_page_directory) {
                    vmm_destroy_address_space(child->pdir_virt);
                    child->pdir_virt = NULL;
                }
            }

            child->state = TASK_DEAD;
            child->exit_code = -1;

            irq_flags_t flags = irq_save();

            if (child->next && child->prev && child->next != child) {
                child->prev->next = child->next;
                child->next->prev = child->prev;
                child->next = child;
                child->prev = child;
            }

            child->reaper_next = dead_tasks_head;
            dead_tasks_head = child;

            irq_restore(flags);
        }

        child = next;
    }

    task->children = NULL;
}

// ============================================================================
// ЗАВЕРШЕНИЕ ЗАДАЧИ (Normal Exit)
// ============================================================================
// [T1 / INIT RESPAWN HARDENING]
//   - PID 1 больше не усыновляет детей самому себе.
//   - PID 1 при смерти убивает свой shell через task_cleanup_children_on_exit().
//   - monitor_children использует общий безопасный cleanup helper.
//
// [T3/T4 HARDENING]
//   - Критическая секция PID 1 / Reaper Queue переведена на irq_save/restore.
//
// ⚠️ NOTE:
//   Цикл sys_close() здесь пока не меняем. Это связано с отдельным багом T2:
//   sys_close() из Ring 0 / нарушение Zero Trust.
// ============================================================================
void task_exit(int exit_code) {
    fpu_release_ownership(current_task);

    // ========================================================================
    // T2 FIX: FD cleanup через internal kernel API, НЕ через syscall.
    // vfs_close_fd() работает для ЛЮБОЙ задачи, IRQ-safe, orphan semantics.
    // ========================================================================
    for (int i = 0; i < TASK_MAX_OPEN_FILES; i++) {
        if (current_task->fd_table[i] != 0) {
            vfs_close_fd(current_task, i);
            // vfs_close_fd уже обнуляет fd_table[i]
        }
    }

    //
    // Process tree policy.
    //
    if (current_task->pid == 1) {
        //
        // PID 1 не должен усыновлять детей в мёртвый init_task.
        // Вместо этого убиваем прямой shell и потомков.
        //
        task_cleanup_children_on_exit(current_task);

    } else if (current_task->orphan_on_exit) {
        //
        // Unix-style adoption: передаём детей в init_task.
        //
        while (current_task->children != NULL) {
            task_t* child = current_task->children;
            current_task->children = child->next_sibling;

            if (init_task && init_task != current_task) {
                child->parent = init_task;
                child->next_sibling = init_task->children;
                init_task->children = child;

                serial_printf("[TASK] PID %d adopted by Init Task (PID %d)\n",
                              child->pid, init_task->pid);
            } else {
                //
                // Если init_task отсутствует или это сам current_task,
                // не создаём use-after-free.
                //
                child->parent = NULL;
                child->next_sibling = NULL;
            }
        }

    } else if (current_task->monitor_children) {
        //
        // Erlang-style: убиваем детей при смерти родителя.
        //
        task_cleanup_children_on_exit(current_task);
    }

    //
    // Освобождаем ресурсы памяти ДО того, как стать зомби.
    //
    vma_destroy_all(current_task);

    if (current_task->pdir_virt && current_task->pdir_virt != boot_page_directory) {
        /* 🛡️ T5 FIX: Переключаем CR3 на boot_page_directory ПЕРЕД уничтожением PD.
         * Иначе CR3 указывает на освобождённую физическую страницу,
         * и timer IRQ между destroy и schedule() вызовет Triple Fault
         * (нарушение SLA #1 — Бессмертное Ядро). */
        uint32_t boot_cr3 = VIRT_TO_PHYS((uint32_t)boot_page_directory);
        vmm_switch_pdir(boot_cr3);

        vmm_destroy_address_space(current_task->pdir_virt);
        current_task->pdir_virt = NULL;
        current_task->cr3 = boot_cr3;
    }

    current_task->state = TASK_ZOMBIE;
    current_task->exit_code = exit_code;

    if (current_task->parent && current_task->parent->state == TASK_SLEEPING) {
        current_task->parent->state = TASK_READY;
        current_task->parent->sleep_until = 0;
        serial_printf("[TASK] Waking up parent PID %d\n", current_task->parent->pid);
    }

    // ========================================================================
    // PID 1 SPECIAL CASE / REAPER QUEUE (IRQ-SAFE)
    // ========================================================================
    irq_flags_t flags = irq_save();

    task_t* dead_task = current_task;

    //
    // INIT TASK SPECIAL CASE:
    //
    // У Init Task (PID 1) нет родителя, который бы вызвал waitpid.
    // Поэтому мы принудительно переводим его в TASK_DEAD,
    // удаляем из Run Queue и кидаем в Reaper Queue.
    //
    if (dead_task->pid == 1) {
        if (dead_task->next && dead_task->prev && dead_task->next != dead_task) {
            dead_task->prev->next = dead_task->next;
            dead_task->next->prev = dead_task->prev;

            dead_task->next = dead_task;
            dead_task->prev = dead_task;
        }

        dead_task->state = TASK_DEAD;
        dead_task->reaper_next = dead_tasks_head;
        dead_tasks_head = dead_task;

        serial_print("[TASK] PID 1 (Init) pushed directly to Reaper Queue.\n");
    }

    irq_restore(flags);

    schedule();

    while (1) {
        __asm__ volatile("cli; hlt");
    }
}

// ============================================================================
// [T1 FIX] ПРИНУДИТЕЛЬНОЕ УБИЙСТВО (Page Fault / OOM Killer)
// ============================================================================
// [T1 / INIT RESPAWN HARDENING]
//   - PID 1 больше не усыновляет детей самому себе.
//   - PID 1 при kill убивает свой shell через task_cleanup_children_on_exit().
//   - monitor_children использует общий безопасный cleanup helper.
//
// [T3/T4 HARDENING]
//   - Критическая секция PID 1 / Reaper Queue переведена на irq_save/restore.
//
// ⚠️ NOTE:
//   Цикл sys_close() здесь пока не меняем. Это связано с отдельным багом T2:
//   sys_close() из Ring 0 / нарушение Zero Trust.
// ============================================================================
void task_kill_current(const char* reason) {
    if (!current_task || current_task->pid == 0) {
        serial_printf("[KILL] FATAL: Attempt to kill invalid task: %s\n", reason);
        while (1) {
            __asm__ volatile("cli; hlt");
        }
    }

    serial_printf("[KILL] PID %d (%s) killed: %s\n",
                  current_task->pid, current_task->name, reason);

    fpu_release_ownership(current_task);

    // ========================================================================
    // T2 FIX: FD cleanup через internal kernel API.
    // ========================================================================
    for (int i = 0; i < TASK_MAX_OPEN_FILES; i++) {
        if (current_task->fd_table[i] != 0) {
            vfs_close_fd(current_task, i);
        }
    }

    //
    // Process tree policy.
    //
    if (current_task->pid == 1) {
        //
        // PID 1 не должен усыновлять детей в мёртвый init_task.
        // Вместо этого убиваем прямой shell и потомков.
        //
        task_cleanup_children_on_exit(current_task);

    } else if (current_task->orphan_on_exit) {
        //
        // Unix-style adoption: передаём детей в init_task.
        //
        while (current_task->children != NULL) {
            task_t* child = current_task->children;
            current_task->children = child->next_sibling;

            if (init_task && init_task != current_task) {
                child->parent = init_task;
                child->next_sibling = init_task->children;
                init_task->children = child;

                serial_printf("[TASK] PID %d adopted by Init Task (PID %d)\n",
                              child->pid, init_task->pid);
            } else {
                //
                // Если init_task отсутствует или это сам current_task,
                // не создаём use-after-free.
                //
                child->parent = NULL;
                child->next_sibling = NULL;
            }
        }

    } else if (current_task->monitor_children) {
        //
        // Erlang-style: убиваем детей при смерти родителя.
        //
        task_cleanup_children_on_exit(current_task);
    }

    //
    // Освобождаем память и становимся зомби.
    // Это позволяет waitpid корректно забрать статус и отправить задачу в Reaper.
    //
    vma_destroy_all(current_task);

    if (current_task->pdir_virt && current_task->pdir_virt != boot_page_directory) {
        /* 🛡️ T5 FIX: Переключаем CR3 на boot_page_directory ПЕРЕД уничтожением PD.
         * Иначе CR3 указывает на освобождённую физическую страницу,
         * и timer IRQ между destroy и schedule() вызовет Triple Fault
         * (нарушение SLA #1 — Бессмертное Ядро). */
        uint32_t boot_cr3 = VIRT_TO_PHYS((uint32_t)boot_page_directory);
        vmm_switch_pdir(boot_cr3);

        vmm_destroy_address_space(current_task->pdir_virt);
        current_task->pdir_virt = NULL;
        current_task->cr3 = boot_cr3;
    }

    current_task->state = TASK_ZOMBIE;
    current_task->exit_code = -1;

    if (current_task->parent && current_task->parent->state == TASK_SLEEPING) {
        current_task->parent->state = TASK_READY;
        current_task->parent->sleep_until = 0;
        serial_printf("[TASK] Waking up parent PID %d (Child killed)\n",
                      current_task->parent->pid);
    }

    // ========================================================================
    // PID 1 SPECIAL CASE / REAPER QUEUE (IRQ-SAFE)
    // ========================================================================
    irq_flags_t flags = irq_save();

    task_t* dead_task = current_task;

    //
    // INIT TASK SPECIAL CASE:
    //
    if (dead_task->pid == 1) {
        if (dead_task->next && dead_task->prev && dead_task->next != dead_task) {
            dead_task->prev->next = dead_task->next;
            dead_task->next->prev = dead_task->prev;

            dead_task->next = dead_task;
            dead_task->prev = dead_task;
        }

        dead_task->state = TASK_DEAD;
        dead_task->reaper_next = dead_tasks_head;
        dead_tasks_head = dead_task;

        serial_print("[TASK] PID 1 (Init) pushed directly to Reaper Queue.\n");
    }

    irq_restore(flags);

    schedule();

    while (1) {
        __asm__ volatile("cli; hlt");
    }
}

// ============================================================================
// TASK FORK (Copy-on-Write + POSIX FD Inheritance)
// ============================================================================
// Создаёт ребёнка с CoW Address Space, клонирует FD Table (с увеличением
// ref_count для open_file_t и vfs_node_t), VMA List, FPU State.
// Ребёнок видит 0 как результат fork(), родитель — PID ребёнка.
//
// [T3/T4 FIX]
//   - FD inheritance теперь выполняется под irq_save()/irq_restore().
//   - open_file_t->ref_count++ защищён от прерываний.
//   - vfs_node_t->ref_count++ защищён тем же IRQ-safe участком.
//   - Убраны безусловные cli/sti внутри FD inheritance.
// ============================================================================
int task_fork(struct regs* r) {
    // 🛡️ FIX: VMM аллокатор вместо kmalloc
    // Возвращает TOP стека (не base!), ниже — Guard Page (unmapped).
    uint32_t stack_top = vmm_alloc_kernel_stack();
    if (stack_top == 0) return -ENOMEM;
    
    // Выделяем PCB (Process Control Block) — 1 физическая страница
    uint32_t pcb_phys = pmm_alloc_page();
    if (pcb_phys == 0) {
        vmm_free_kernel_stack(stack_top);
        return -ENOMEM;
    }
    
    task_t* child = (task_t*)PHYS_TO_VIRT(pcb_phys);
    k_memset(child, 0, sizeof(task_t));
    
    // Process Identity
    child->pid = next_pid++;
    child->state = TASK_READY;
    child->kernel_stack_virt = stack_top; // 🛡️ FIX: Это TOP, не base!
    child->reaper_next = NULL;
    child->sleep_until = 0;
    
    // 🛡️ Copy-on-Write Address Space Cloning
    // Создаёт новый Page Directory, клонирует Kernel Space (768-1023) из
    // boot_page_directory и User Space (0-767) из родителя с флагом PAGE_COW.
    // Физические страницы НЕ копируются — только PTE помечаются READ-ONLY.
    child->pdir_virt = vmm_clone_address_space(current_task->pdir_virt);
    if (!child->pdir_virt) {
        vmm_free_kernel_stack(stack_top);
        pmm_free_page(pcb_phys);
        return -ENOMEM;
    }
    child->cr3 = VIRT_TO_PHYS((uint32_t)child->pdir_virt);
    
    // 🛡️ Deep Copy VMA List (каждая нода — отдельный kmalloc)
    if (vma_clone(child, current_task) < 0) {
        vmm_destroy_address_space(child->pdir_virt);
        vmm_free_kernel_stack(stack_top);
        pmm_free_page(pcb_phys);
        return -ENOMEM;
    }
    
    // Копируем имя процесса
    k_strncpy(child->name, current_task->name, sizeof(child->name));
    
    // ========================================================================
    // 🛡️POSIX FILE DESCRIPTOR INHERITANCE (T3/T4 FIX)
    // ========================================================================
    // По POSIX при fork() ребёнок наследует открытые FD, разделяя с родителем
    // Open File Descriptions (open_file_t). Мы ОБЯЗАНЫ увеличить ref_count,
    // иначе sys_close в ребёнке освободит память, оставив родителя с UAF.
    //
    // CRITICAL:
    //   Весь цикл наследования FD должен быть IRQ-safe.
    //
    //   Раньше здесь было:
    //     open_file_t->ref_count++ без защиты;
    //     vfs_node_t->ref_count++ под cli/sti без сохранения EFLAGS.
    //
    //   Это могло привести к:
    //     - race condition на ref_count;
    //     - преждевременному sti внутри внешней критической секции;
    //     - use-after-free / double-free / refcount leak.
    // ========================================================================
    irq_flags_t fd_flags = irq_save();

    for (int i = 0; i < TASK_MAX_OPEN_FILES; i++) {
        open_file_t* of = current_task->fd_table[i];

        child->fd_table[i] = of;

        if (of) {
            // Увеличиваем счетчик ссылок на Open File Description
            of->ref_count++;

            // Увеличиваем счетчик ссылок на Inode (vfs_node_t)
            if (of->node) {
                of->node->ref_count++;
            }
        }
    }

    irq_restore(fd_flags);
    
    // Копируем FPU state (512 байт, 16-byte aligned — первое поле в task_t)
    k_memcpy(child->fpu_state, current_task->fpu_state, 512);
    child->fpu_initialized = current_task->fpu_initialized;
    
    // Process Tree: ребёнок становится сыном текущего процесса
    child->parent = current_task;
    child->children = NULL;
    child->next_sibling = current_task->children;
    current_task->children = child;
    child->exit_code = 0;
    child->orphan_on_exit = current_task->orphan_on_exit;
    child->monitor_children = current_task->monitor_children;
    
    // ========================================================================
    // 🛡️ KERNEL STACK COPY (с учетом новой архитектуры TOP)
    // ========================================================================
    // Копируем kernel stack родителя в stack ребёнка. Это нужно, потому что
    // regs* r указывает на структуру, сохранённую на kernel stack при syscall.
    // Мы должны скопировать её в stack ребёнка и модифицировать EAX=0.
    // ========================================================================
    uint32_t parent_stack_base = current_task->kernel_stack_virt - KERNEL_STACK_USABLE_SIZE;
    uint32_t child_stack_base = stack_top - KERNEL_STACK_USABLE_SIZE;
    
    k_memcpy((void*)child_stack_base, (void*)parent_stack_base, KERNEL_STACK_USABLE_SIZE);
    
    // Вычисляем смещение regs относительно базы родительского стека
    uint32_t offset_from_base = (uint32_t)r - parent_stack_base;
    struct regs* original_child_r = (struct regs*)(child_stack_base + offset_from_base);
    
    // Сдвигаем regs вниз на 32 байта для нового стекового фрейма ret_from_fork
    // Это нужно, чтобы ret_from_fork имел места для callee-saved регистров
    uint32_t shift = 32;
    struct regs* child_r = (struct regs*)((uint32_t)original_child_r - shift);
    k_memcpy(child_r, original_child_r, sizeof(struct regs));
    
    // 🛡️ CRITICAL: Ребёнок видит 0 как результат fork()
    child_r->eax = 0;
    
    // ========================================================================
    // STACK FORGING: Подготовка стека для ret_from_fork трамплина
    // ========================================================================
    // После switch_context процессор выполнит ret, который загрузит EIP из
    // стека. Мы кладём туда адрес ret_from_fork, который восстановит регистры
    // и сделает iret в Ring 3 (или вернётся в Ring 0 для kernel tasks).
    // ========================================================================
    uint32_t* child_stack_ptr = (uint32_t*)child_r;
    
    *(--child_stack_ptr) = (uint32_t)child_r;      // Аргумент для ret_from_fork
    *(--child_stack_ptr) = (uint32_t)ret_from_fork; // Return address (EIP после ret)
    *(--child_stack_ptr) = 0; // EBX (callee-saved)
    *(--child_stack_ptr) = 0; // ESI (callee-saved)
    *(--child_stack_ptr) = 0; // EDI (callee-saved)
    *(--child_stack_ptr) = 0; // EBP (callee-saved)
    
    child->esp = (uint32_t)child_stack_ptr;
    
    // ========================================================================
    // RUN QUEUE INSERTION (IRQ-SAFE)
    // ========================================================================
    // Защищаем модификацию двусвязного списка run queue от прерываний PIT.
    // Если PIT прервёт нас посередине, schedule() может увидеть битый список.
    // ========================================================================
    irq_flags_t rq_flags = irq_save();
    task_queue_add(child);
    task_count++;
    irq_restore(rq_flags);
    
    serial_printf("[TASK] Fork: PID %d -> PID %d (CoW)\n", current_task->pid, child->pid);
    
    // Родитель видит PID ребёнка как результат fork()
    return child->pid;
}

// ============================================================================
// TASK WAITPID
// ============================================================================
int task_waitpid(int pid, int* status, int options) {
    while (1) {
        task_t* child = current_task->children;
        task_t* zombie = NULL;
        
        while (child != NULL) {
            if (child->state == TASK_ZOMBIE) {
                if (pid == -1 || child->pid == (uint32_t)pid) {
                    zombie = child;
                    break;
                }
            }
            child = child->next_sibling;
        }
        
        if (zombie) {
            int exit_code = zombie->exit_code;
            
            if (status) {
                *status = exit_code;
            }
            
            uint32_t zombie_pid = zombie->pid;
            
            // Удаляем из списка детей
            if (current_task->children == zombie) {
                current_task->children = zombie->next_sibling;
            } else {
                task_t* sibling = current_task->children;
                while (sibling && sibling->next_sibling != zombie) {
                    sibling = sibling->next_sibling;
                }
                if (sibling) {
                    sibling->next_sibling = zombie->next_sibling;
                }
            }
            
            // ====================================================================
            // RUN QUEUE / REAPER QUEUE UPDATE (IRQ-SAFE)
            // ====================================================================
            irq_flags_t flags = irq_save();
            
            // 🛡️ CRITICAL FIX: Удаляем Зомби из Run Queue ПЕРЕД отправкой в Reaper.
            // Это безопасно, так как текущая задача (родитель) находится в Run Queue,
            // и schedule() корректно обойдет очередь без бесконечных циклов.
            if (zombie->next != zombie) {
                zombie->prev->next = zombie->next;
                zombie->next->prev = zombie->prev;
            }
            
            zombie->state = TASK_DEAD;
            zombie->reaper_next = dead_tasks_head;
            dead_tasks_head = zombie;
            
            irq_restore(flags);
            
            serial_printf("[TASK] waitpid: Reaped PID %d (exit code: %d)\n", zombie_pid, exit_code);
            
            return zombie_pid;
        }
        
        if (options & WNOHANG) {
            return 0;
        }
        
        current_task->state = TASK_SLEEPING;
        current_task->sleep_until = 0;
        serial_printf("[TASK] PID %d sleeping in waitpid\n", current_task->pid);
        schedule();
    }
}

// ============================================================================
// ОТЛАДКА (ps)
// ============================================================================
void task_print_list(void) {
    if (!current_task) {
        k_print("[PS] No tasks running.\n");
        return;
    }

    k_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    k_print("\n  PID | STATE   | NAME\n");
    k_print("------+---------+----------------\n");
    
    task_t* t = current_task;
    do {
        const char* state_str = "UNKNOWN";
        uint8_t state_color = VGA_COLOR_LIGHT_GREY;
        
        switch (t->state) {
            case TASK_RUNNING: 
                state_str = "RUNNING"; 
                state_color = VGA_COLOR_LIGHT_GREEN; 
                break;
            case TASK_READY:   
                state_str = "READY";   
                state_color = VGA_COLOR_YELLOW; 
                break;
            case TASK_SLEEPING:
                state_str = "SLEEPING";
                state_color = VGA_COLOR_LIGHT_BLUE;
                break;
            case TASK_ZOMBIE:
                state_str = "ZOMBIE";
                state_color = VGA_COLOR_MAGENTA;
                break;
            case TASK_DEAD:    
                state_str = "DEAD";    
                state_color = VGA_COLOR_RED; 
                break;
        }
        
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        k_printf(" %3d  | ", t->pid);
        
        k_set_color(state_color, VGA_COLOR_BLACK);
        k_print(state_str);
        
        int len = 0; while(state_str[len]) len++;
        for (int i = 0; i < 7 - len; i++) k_print(" ");
        
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        k_print(" | ");
        k_print(t->name);
        k_print("\n");
        
        t = t->next;
    } while (t != current_task);
    
    k_print("\n");
}
