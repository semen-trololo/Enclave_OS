#include "task.h"
#include "pmm.h"
#include "heap.h"
#include "vfs.h"
#include "elf.h"
#include "paging.h"
#include "vma.h"
#include "serial.h"
#include "klib.h"
#include "config.h"
#include "shell.h"

// ============================================================================
// ВНУТРЕННЯЯ ФУНКЦИЯ ЗАПУСКА ПРОЦЕССА (Kernel Mode Exec)
// ============================================================================
static int spawn_process(const char* filename) {
    vfs_node_t* file_node = vfs_findnode(filename);
    if (!file_node) {
        serial_printf("[TEST RUNNER] File not found: %s\n", filename);
        return -1;
    }

    uint32_t* pdir_virt = vmm_create_address_space();
    if (!pdir_virt) {
        serial_print("[TEST RUNNER] OOM creating address space\n");
        return -1;
    }

    task_t temp_task;
    temp_task.pdir_virt = pdir_virt;
    temp_task.vma_head = NULL;

    uint32_t entry_point = elf_load(file_node, &temp_task);
    if (entry_point == 0) {
        serial_print("[TEST RUNNER] Failed to load ELF\n");
        vma_destroy_all(&temp_task);
        vmm_destroy_address_space(pdir_virt);
        return -1;
    }

    uint32_t stack_top = USER_STACK_VIRT_TOP;
    uint32_t stack_bottom = stack_top - USER_STACK_SIZE;

    // 🛡️ CRITICAL SECTION START: Запрещаем прерывания (PIT).
    // Это предотвращает Race Condition, при которой schedule() мог бы 
    // переключиться на новую задачу ДО того, как мы добавим VMA для стека и кучи.
    uint32_t eflags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(eflags));

    task_t* new_task = task_create(filename, (void (*)(void))entry_point, true, stack_top, pdir_virt);
    if (!new_task) {
        __asm__ volatile("push %0; popf" : : "r"(eflags)); // Восстанавливаем прерывания
        serial_print("[TEST RUNNER] Failed to create task\n");
        vma_destroy_all(&temp_task);
        vmm_destroy_address_space(pdir_virt);
        return -1;
    }

    // Перенос ВМА из temp в new_task
    new_task->vma_head = temp_task.vma_head;

    // Добавляем ВМА для стека и кучи
    if (vma_add(new_task, stack_bottom, stack_top, VMA_READ | VMA_WRITE) != 0) {
        __asm__ volatile("push %0; popf" : : "r"(eflags));
        task_kill_current("Test Runner OOM"); 
        return -1;
    }
    
    if (vma_add(new_task, USER_HEAP_START, USER_HEAP_START, VMA_READ | VMA_WRITE) != 0) {
        __asm__ volatile("push %0; popf" : : "r"(eflags));
        task_kill_current("Test Runner OOM");
        return -1;
    }

    // 🛡️ CRITICAL SECTION END: Address Space полностью сконструирован.
    // Теперь PIT может безопасно прервать нас и запланировать эту задачу.
    __asm__ volatile("push %0; popf" : : "r"(eflags));

    return new_task->pid;
}
// ============================================================================
// ОЖИДАНИЕ ЗАВЕРШЕНИЯ И СХОЖДЕНИЕ К ПОКОЮ (Quiescent Convergence)
// ============================================================================
// ============================================================================
// ОЖИДАНИЕ ЗАВЕРШЕНИЯ И СБОР ЗОМБИ (POSIX waitpid)
// ============================================================================
static int wait_for_cleanup(int pid) {
    int status = 0;
    
    // Блокирующее ожидание конкретного ребенка (или любого, если pid == -1)
    int reaped = task_waitpid(pid, &status, 0);
    if (reaped < 0) {
        serial_printf("[TEST RUNNER] waitpid failed for PID %d: %d\n", pid, reaped);
        return -1;
    }
    
    // Даем Reaper'у время очистить TASK_DEAD задачи из очереди
    for (int i = 0; i < 5; i++) task_yield();
    
    return status;
}

// Вспомогательная функция для "отлова" стабильного состояния кучи
static int32_t get_stable_heap_balance(void) {
    int32_t current_balance = heap_check_balance();
    int stable_count = 0;
    
    // Мы ждем, пока баланс не будет неизменным 5 раз подряд.
    // Это гарантирует, что фоновый Shell/IRQ завершил все временные kmalloc/kfree.
    while (stable_count < 5) {
        task_yield(); // Даем CPU фоновым задачам
        int32_t new_balance = heap_check_balance();
        
        if (new_balance == current_balance) {
            stable_count++;
        } else {
            stable_count = 0; // Баланс изменился, сбрасываем счетчик
            current_balance = new_balance;
        }
    }
    return current_balance;
}
// ============================================================================
// ЗАПУСК ОДНОГО ELF ТЕСТА
// ============================================================================
// ============================================================================
// ЗАПУСК ОДНОГО ELF ТЕСТА
// ============================================================================
static void run_elf_test(const char* name, const char* description) {
    k_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    k_printf("[TEST] Running: %s (%s)\n", name, description);
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    // 1. Снапшот ДО (тоже берем стабильный)
    int32_t pmm_before = pmm_check_balance();
    int32_t heap_before = get_stable_heap_balance(); 
    uint32_t tasks_before = task_get_count();

    // 2. Запуск
    int pid = spawn_process(name);
    if (pid < 0) {
        k_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
        k_print("[FAIL] Spawn failed!\n");
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return;
    }

    // 3. Ожидание смерти процесса и сбор Зомби (POSIX waitpid)
    int exit_status = wait_for_cleanup(pid);
    
    k_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    k_printf("[TEST] Exit status: %d\n", exit_status);
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    // 4. Снапшот ПОСЛЕ (Ждем схождения к покою)
    int32_t pmm_after = pmm_check_balance();
    int32_t heap_after = get_stable_heap_balance();
    uint32_t tasks_after = task_get_count();

    // 5. Валидация
    int leaked_pages = (pmm_after - pmm_before);
    int leaked_heap = (heap_after - heap_before);
    int zombies = (tasks_after - tasks_before);

    if (leaked_pages == 0 && leaked_heap == 0 && zombies == 0) {
        k_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        k_print("[PASS] No leaks, no zombies.\n");
    } else {
        k_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
        k_print("[FAIL] ");
        if (leaked_pages != 0) k_printf("PMM Leak: %d | ", leaked_pages);
        if (leaked_heap != 0) k_printf("Heap Leak: %d | ", leaked_heap);
        if (zombies != 0) k_printf("Zombies: %d | ", zombies);
        k_print("\n");
    }
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

// ============================================================================
// STRESS TEST WORKER (Kernel Mode Task)
// ============================================================================
static void stress_worker_task(void) {
    for (int i = 0; i < 5; i++) {
        task_yield(); 
    }
    task_exit(0); // ✅ FIX: Передаем код успешного завершения (0)
}

// ============================================================================
// ОБРАБОТЧИК СТРЕСС-ТЕСТОВ (Pillar 2)
// ============================================================================
void handle_stress(int argc, char args[MAX_ARGS][MAX_ARG_LEN]) {
    if (argc < 3) {
        k_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
        k_print("Usage: stress spawn <count>\n");
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return;
    }

    if (k_strcmp(args[1], "spawn") == 0) {
        uint32_t count = k_atoi(args[2]);
        if (count == 0) count = 100;
        if (count > 100000) {
            k_print("[STRESS] Limit is 100000 tasks to prevent kernel hang.\n");
            count = 100000;
        }

        k_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
        k_printf("[STRESS] Mass Spawn Test: Creating %u kernel tasks...\n", count);
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

        uint32_t baseline_tasks = task_get_count();
        int32_t pmm_before = pmm_check_balance();
        int32_t heap_before = heap_check_balance();

        uint32_t spawned = 0;
        uint32_t failed = 0;
        for (uint32_t i = 0; i < count; i++) {
            task_t* t = task_create("stress_worker", stress_worker_task, false, 0, NULL);
            if (t) {
                spawned++;
            } else {
                failed++;
                k_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
                k_printf("[STRESS] OOM/Limit reached at %u tasks!\n", i);
                k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                break;
            }
        }

        k_printf("[STRESS] Spawned: %u, Failed: %u. Waiting for children...\n", spawned, failed);

        // Собираем всех зомби-детей (родителем является Shell/TestRunner)
        for (uint32_t i = 0; i < spawned; i++) {
            int status;
            task_waitpid(-1, &status, 0); // -1 = ждать любого ребенка
        }
        
        // Даем Reaper'у время очистить память
        for(int i = 0; i < 10; i++) task_yield();

        int32_t pmm_after = pmm_check_balance();
        int32_t heap_after = heap_check_balance();
        uint32_t zombies = task_get_count() - baseline_tasks;

        if (pmm_after == pmm_before && heap_after == heap_before && zombies == 0) {
            k_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            k_printf("[PASS] Stress test OK. %u tasks created & reaped. No leaks.\n", spawned);
        } else {
            k_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
            k_print("[FAIL] Stress test failed!\n");
            if (pmm_after != pmm_before) k_printf("  PMM Leak: %d pages\n", pmm_after - pmm_before);
            if (heap_after != heap_before) k_printf("  Heap Leak: %d blocks\n", heap_after - heap_before);
            if (zombies != 0) k_printf("  Zombies: %u tasks\n", zombies);
        }
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    }
    else {
        k_print("Unknown stress command.\n");
    }
}
// ============================================================================
// ЭКСПОРТ ДЛЯ SHELL: Запуск ELF и ожидание завершения
// ============================================================================
int run_elf_and_wait(const char* filename) {
    int pid = spawn_process(filename);
    if (pid < 0) return -1;
    return wait_for_cleanup(pid);
}
// ============================================================================
// ТОЧКА ВХОДА (Команда run_tests)
// ============================================================================
void test_init(void) {
    k_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    k_print("\n========================================\n");
    k_print("      BARE METAL OS TEST SUITE (Day 10)\n");
    k_print("========================================\n\n");
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    // 🛡️ WARMUP PHASE
    serial_print("[TEST] Warming up Kernel Heap...\n");
    void* warmup1 = kmalloc(16384); 
    void* warmup2 = kmalloc(4096);  
    void* warmup3 = kmalloc(128);   
    
    if (warmup1) kfree(warmup1);
    if (warmup2) kfree(warmup2);
    if (warmup3) kfree(warmup3);
    
    for(int i = 0; i < 10; i++) task_yield();

    // Pillar 1: ELF Tests (User Space)
    run_elf_test("/bin/test_hello.elf", "Basic Hello World");
    run_elf_test("/bin/test_segfault.elf", "NULL Pointer Dereference");
    run_elf_test("/bin/test_write_text.elf", "W^X Violation (Write to .text)");
    run_elf_test("/bin/test_stack_overflow.elf", "Stack Guard Page");
    run_elf_test("/bin/test_oom.elf", "OOM Protection");
    run_elf_test("/bin/test_vfs_stress.elf", "VFS tmpfs 1000 files + CRC32");
    run_elf_test("/bin/test_memory_torture.elf", "Multi-Stage Memory Torture");
    run_elf_test("/bin/test_mmap.elf"," Test MMAP");

    k_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    k_print("\n========================================\n");
    k_print("             TEST SUITE FINISHED\n");
    k_print("========================================\n\n");
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}