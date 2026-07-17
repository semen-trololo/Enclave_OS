//task.c

#include "task.h"
#include "pmm.h"
#include "klib.h"
#include "serial.h"
#include "tss.h"
#include "paging.h"
#include "isr.h"
#include "vfs.h"
#include "vma.h"
#include "syscall.h"
#include "heap.h"
#include <stdbool.h>
#include "config.h"
#include "elf.h"

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
    
    task_init_fds(main_task_ptr);
    
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
    
    // 🛡️ [ДЕНЬ 16] FIX: Используем VMM аллокатор с Hardware Guard Page
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
    task_init_fds(new_task); 
    
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
    uint32_t eflags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(eflags));
    task_queue_add(new_task);
    task_count++;
    __asm__ volatile("push %0; popf" : : "r"(eflags));

    serial_printf("[TASK] Created PID %d: %s (mode: %s)\n", 
                  new_task->pid, name, is_user_mode ? "USER" : "KERNEL");
    return new_task;
}
// ============================================================================
// [ДЕНЬ 24] RESPAWN INIT TASK (PID 1)
// Если Init упал, ядро автоматически перезапускает его из Reaper Phase.
// Это реализует принцип "Let it crash" для корневого процесса.
// ============================================================================
static void respawn_init_task(void) {
    serial_print("[REAPER] ⚠️ PID 1 (Init) died! Respawning...\n");
    
    vfs_node_t* init_node = vfs_findnode("/sbin/init.elf");
    if (!init_node) {
        serial_print("[REAPER] FATAL: /sbin/init.elf not found! Cannot respawn.\n");
        return;
    }

    uint32_t* new_pdir = vmm_create_address_space();
    if (!new_pdir) {
        serial_print("[REAPER] FATAL: OOM creating address space for Init!\n");
        return;
    }

    task_t temp_task;
    temp_task.pdir_virt = new_pdir;
    temp_task.vma_head = NULL;

    uint32_t entry = elf_load(init_node, &temp_task);
    if (entry == 0) {
        serial_print("[REAPER] FATAL: Failed to load /sbin/init.elf!\n");
        vma_destroy_all(&temp_task);
        vmm_destroy_address_space(new_pdir);
        return;
    }

    uint32_t stack_top = USER_STACK_VIRT_TOP - 16;
    
    // 🛡️ CRITICAL: Защищаем создание задачи от прерываний PIT
    uint32_t eflags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(eflags));
    
    task_t* new_init = task_create("/sbin/init.elf", (void (*)(void))entry, true, stack_top, new_pdir);
    if (!new_init) {
        serial_print("[REAPER] FATAL: Failed to create Init task!\n");
        vma_destroy_all(&temp_task);
        vmm_destroy_address_space(new_pdir);
        __asm__ volatile("push %0; popf" : : "r"(eflags));
        return;
    }

    new_init->vma_head = temp_task.vma_head;
    new_init->orphan_on_exit = 1; // Новый init усыновляет сирот

    vma_add(new_init, stack_top - USER_STACK_SIZE, stack_top, VMA_READ | VMA_WRITE);
    vma_add(new_init, USER_HEAP_START, USER_HEAP_START, VMA_READ | VMA_WRITE);

    __asm__ volatile("push %0; popf" : : "r"(eflags));

    serial_printf("[REAPER] ✓ Init respawned successfully as PID %d\n", new_init->pid);
}
// ============================================================================
// ПЛАНИРОВЩИК (ROUND-ROBIN + REAPER + IDLE HLT)
// ============================================================================

void schedule(void) {
    uint32_t eflags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(eflags));

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
        __asm__ volatile("push %0; popf" : : "r"(eflags));
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
            __asm__ volatile("sti; hlt; cli");
        }
        __asm__ volatile("push %0; popf" : : "r"(eflags));
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
    
    // --- ЗДЕСЬ ПРОДОЛЖАЕТ ВЫПОЛНЕНИЕ УЖЕ НОВАЯ ЗАДАЧА ---

    __asm__ volatile("push %0; popf" : : "r"(eflags));
}

void task_yield(void) { schedule(); }

// ============================================================================
// ЗАВЕРШЕНИЕ ЗАДАЧИ (Normal Exit)
// ============================================================================
void task_exit(int exit_code) {
    fpu_release_ownership(current_task);
    
    for (int i = 0; i < TASK_MAX_OPEN_FILES; i++) {
        if (current_task->fd_table[i] != 0) {
            sys_close(i); 
            current_task->fd_table[i] = 0; 
        }
    }
    
    if (current_task->orphan_on_exit) {
        while (current_task->children != NULL) {
            task_t* child = current_task->children;
            current_task->children = child->next_sibling;
            
            child->parent = init_task;
            child->next_sibling = init_task->children;
            init_task->children = child;
            
            serial_printf("[TASK] PID %d adopted by Init Task (PID 1)\n", child->pid);
        }
    } else if (current_task->monitor_children) {
        task_t* child = current_task->children;
        while (child != NULL) {
            task_t* next = child->next_sibling;
            if (child->state != TASK_ZOMBIE && child->state != TASK_DEAD) {
                serial_printf("[TASK] Killing child PID %d (Supervisor Tree)\n", child->pid);
                child->state = TASK_ZOMBIE;
                child->exit_code = -1;
                
                if (child->next != child) {
                    child->prev->next = child->next;
                    child->next->prev = child->prev;
                }
                
                // 🛡️ ФИКС: Зомби-дети должны пройти через waitpid, а не сразу в Reaper
                // Оставляем их в списке детей, чтобы родитель (или init) их забрал
            }
            child = next;
        }
        current_task->children = NULL;
    }
    
    // 🛡️ Освобождаем ресурсы памяти ДО того, как стать зомби
    vma_destroy_all(current_task);
    if (current_task->pdir_virt && current_task->pdir_virt != boot_page_directory) {
        vmm_destroy_address_space(current_task->pdir_virt);
        current_task->pdir_virt = NULL;
    }

    current_task->state = TASK_ZOMBIE;
    current_task->exit_code = exit_code;
    
    if (current_task->parent && current_task->parent->state == TASK_SLEEPING) {
        current_task->parent->state = TASK_READY;
        current_task->parent->sleep_until = 0;
        serial_printf("[TASK] Waking up parent PID %d\n", current_task->parent->pid);
    }
    
    uint32_t eflags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(eflags));
    
    task_t* dead_task = current_task; 

    if (dead_task->next != dead_task) {
        dead_task->prev->next = dead_task->next;
        dead_task->next->prev = dead_task->prev;
    }
    
    // 🛡️ [ДЕНЬ 24] INIT TASK SPECIAL CASE:
    // У Init Task (PID 1) нет родителя, который бы вызвал waitpid.
    // Поэтому мы принудительно переводим его в TASK_DEAD и кидаем в Reaper Queue.
    if (dead_task->pid == 1) {
        dead_task->state = TASK_DEAD;
        uint32_t eflags_local;
        __asm__ volatile("pushf; pop %0; cli" : "=r"(eflags_local));
        dead_task->reaper_next = dead_tasks_head;
        dead_tasks_head = dead_task;
        __asm__ volatile("push %0; popf" : : "r"(eflags_local));
        serial_print("[TASK] PID 1 (Init) pushed directly to Reaper Queue.\n");
    }
    // Для остальных задач: НЕ добавляем в dead_tasks_head! Это сделает waitpid.
    
    __asm__ volatile("push %0; popf" : : "r"(eflags));

    schedule(); 
    
    while(1) __asm__ volatile("cli; hlt"); 
}

// ============================================================================
// [ДЕНЬ 10] ПРИНУДИТЕЛЬНОЕ УБИЙСТВО (Page Fault / OOM Killer)
// ============================================================================
void task_kill_current(const char* reason) {
    if (!current_task || current_task->pid == 0) {
        serial_printf("[KILL] FATAL: Attempt to kill invalid task: %s\n", reason);
        while(1) __asm__ volatile("cli; hlt");
    }
    
    serial_printf("[KILL] PID %d (%s) killed: %s\n", current_task->pid, current_task->name, reason);
    
    fpu_release_ownership(current_task);
    
    for (int i = 0; i < TASK_MAX_OPEN_FILES; i++) {
        if (current_task->fd_table[i] != 0) {
            sys_close(i);
            current_task->fd_table[i] = 0;
        }
    }
    
    // 🛡️ КРИТИЧЕСКИЙ ФИКС: Освобождаем память и становимся ЗОМБИ (как task_exit)
    // Это позволяет waitpid корректно забрать статус и отправить задачу в Reaper.
    vma_destroy_all(current_task);
    if (current_task->pdir_virt && current_task->pdir_virt != boot_page_directory) {
        vmm_destroy_address_space(current_task->pdir_virt);
        current_task->pdir_virt = NULL;
    }

    current_task->state = TASK_ZOMBIE; // 🛡️ СТАНОВИМСЯ ЗОМБИ, А НЕ DEAD!
    current_task->exit_code = -1; // Убито ядром (SIGSEGV/SIGKILL)
    
    if (current_task->parent && current_task->parent->state == TASK_SLEEPING) {
        current_task->parent->state = TASK_READY;
        current_task->parent->sleep_until = 0;
        serial_printf("[TASK] Waking up parent PID %d (Child killed)\n", current_task->parent->pid);
    }
    
    uint32_t eflags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(eflags));
    
    task_t* dead_task = current_task; 

    if (dead_task->next != dead_task) {
        dead_task->prev->next = dead_task->next;
        dead_task->next->prev = dead_task->prev;
    }
    
    // 🛡️ [ДЕНЬ 24] INIT TASK SPECIAL CASE:
    // У Init Task (PID 1) нет родителя, который бы вызвал waitpid.
    // Поэтому мы принудительно переводим его в TASK_DEAD и кидаем в Reaper Queue.
    if (dead_task->pid == 1) {
        dead_task->state = TASK_DEAD;
        uint32_t eflags_local;
        __asm__ volatile("pushf; pop %0; cli" : "=r"(eflags_local));
        dead_task->reaper_next = dead_tasks_head;
        dead_tasks_head = dead_task;
        __asm__ volatile("push %0; popf" : : "r"(eflags_local));
        serial_print("[TASK] PID 1 (Init) pushed directly to Reaper Queue.\n");
    }
    // Для остальных задач: НЕ добавляем в dead_tasks_head! Это сделает waitpid.
    
    __asm__ volatile("push %0; popf" : : "r"(eflags));
    
    schedule();
    while(1) __asm__ volatile("cli; hlt"); 
}

// ============================================================================
// [ДЕНЬ 14 + ДЕНЬ 24] TASK FORK (Copy-on-Write + POSIX FD Inheritance)
// ============================================================================
// Создаёт ребёнка с CoW Address Space, клонирует FD Table (с увеличением
// ref_count для open_file_t и vfs_node_t), VMA List, FPU State.
// Ребёнок видит 0 как результат fork(), родитель — PID ребёнка.
// ============================================================================
int task_fork(struct regs* r) {
    // 🛡️ [ДЕНЬ 16] FIX: VMM аллокатор вместо kmalloc
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
    
    // 🛡️ [ДЕНЬ 14] Copy-on-Write Address Space Cloning
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
    // 🛡️ [ДЕНЬ 24] POSIX FILE DESCRIPTOR INHERITANCE (CRITICAL FIX)
    // ========================================================================
    // По POSIX при fork() ребёнок наследует открытые FD, разделяя с родителем
    // Open File Descriptions (open_file_t). Мы ОБЯЗАНЫ увеличить ref_count,
    // иначе sys_close в ребёнке освободит память, оставив родителя с UAF.
    // ========================================================================
    for (int i = 0; i < TASK_MAX_OPEN_FILES; i++) {
        child->fd_table[i] = current_task->fd_table[i];
        
        if (child->fd_table[i]) {
            // Увеличиваем счетчик ссылок на Open File Description
            child->fd_table[i]->ref_count++;
            
            // Увеличиваем счетчик ссылок на Inode (vfs_node_t)
            if (child->fd_table[i]->node) {
                __asm__ volatile("cli");
                child->fd_table[i]->node->ref_count++;
                __asm__ volatile("sti");
            }
        }
    }
    
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
    // 🛡️ [ДЕНЬ 16] KERNEL STACK COPY (с учетом новой архитектуры TOP)
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
    // Это нужно, чтобы ret_from_fork имел место для callee-saved регистров
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
    // RUN QUEUE INSERTION (IRQ Safety)
    // ========================================================================
    // Защищаем модификацию двусвязного списка run queue от прерываний PIT.
    // Если PIT прервёт нас посередине, schedule() может увидеть битый список.
    // ========================================================================
    uint32_t eflags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(eflags));
    task_queue_add(child);
    task_count++;
    __asm__ volatile("push %0; popf" : : "r"(eflags));
    
    serial_printf("[TASK] Fork: PID %d -> PID %d (CoW)\n", current_task->pid, child->pid);
    
    // Родитель видит PID ребёнка как результат fork()
    return child->pid;
}

// ============================================================================
// [ДЕНЬ 14] TASK WAITPID
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
            
            zombie->state = TASK_DEAD;
            
            uint32_t eflags;
            __asm__ volatile("pushf; pop %0; cli" : "=r"(eflags));
            
            zombie->reaper_next = dead_tasks_head;
            dead_tasks_head = zombie;
            
            __asm__ volatile("push %0; popf" : : "r"(eflags));
            
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