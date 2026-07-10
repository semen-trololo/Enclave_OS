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
// Аналог sys_exec, но без проверки is_user_pointer, так как мы в Ring 0.
// Возвращает PID запущенного процесса или -1 при ошибке.
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

    // Временная структура для elf_load
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

    task_t* new_task = task_create(filename, (void (*)(void))entry_point, true, stack_top, pdir_virt);
    if (!new_task) {
        serial_print("[TEST RUNNER] Failed to create task\n");
        vma_destroy_all(&temp_task);
        vmm_destroy_address_space(pdir_virt);
        return -1;
    }

    // Перенос VMA из temp в new_task
    new_task->vma_head = temp_task.vma_head;

    // Добавляем VMA для стека и кучи
    vma_add(new_task, stack_bottom, stack_top, VMA_READ | VMA_WRITE);
    vma_add(new_task, USER_HEAP_START, USER_HEAP_START, VMA_READ | VMA_WRITE);

    return new_task->pid;
}

// ============================================================================
// ОЖИДАНИЕ ЗАВЕРШЕНИЯ (Грязный Поллинг)
// ============================================================================
// Ждем, пока task_count не вернется к baseline.
// Внутри цикла вызываем task_yield(), чтобы планировщик мог работать и чистить Reaper.
static void wait_for_cleanup(uint32_t baseline_tasks) {
    uint32_t timeout = 10000; // Защита от бесконечного цикла
    while (task_get_count() > baseline_tasks && timeout > 0) {
        task_yield(); // Передаем CPU планировщику -> schedule() -> Reaper
        timeout--;
    }
    if (timeout == 0) {
        serial_print("[TEST RUNNER] TIMEOUT: Task did not exit!\n");
    }
}

// ============================================================================
// ЗАПУСК ОДНОГО ELF ТЕСТА
// ============================================================================
static void run_elf_test(const char* name, const char* description) {
    k_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    k_printf("[TEST] Running: %s (%s)\n", name, description);
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    // 1. Снапшот ДО
    int32_t pmm_before = pmm_check_balance();
    int32_t heap_before = heap_check_balance();
    uint32_t tasks_before = task_get_count();

    // 2. Запуск
    int pid = spawn_process(name);
    if (pid < 0) {
        k_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
        k_print("[FAIL] Spawn failed!\n");
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return;
    }

    // 3. Ожидание
    wait_for_cleanup(tasks_before);

    // 4. Снапшот ПОСЛЕ
    int32_t pmm_after = pmm_check_balance();
    int32_t heap_after = heap_check_balance();
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
// Простая задача, которая живет пару квантов времени и умирает.
// Используется для теста mass spawn.
static void stress_worker_task(void) {
    // Делаем несколько переключений контекста, чтобы нагружить планировщик
    for (int i = 0; i < 5; i++) {
        task_yield(); 
    }
    task_exit();
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
        if (count > 1000) {
            k_print("[STRESS] Limit is 1000 tasks to prevent kernel hang.\n");
            count = 1000;
        }

        k_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
        k_printf("[STRESS] Mass Spawn Test: Creating %u kernel tasks...\n", count);
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

        // 1. Снапшот ДО
        uint32_t baseline_tasks = task_get_count();
        int32_t pmm_before = pmm_check_balance();
        int32_t heap_before = heap_check_balance();

        // 2. Массовый спавн
        uint32_t spawned = 0;
        uint32_t failed = 0;
        for (uint32_t i = 0; i < count; i++) {
            // is_user_mode = false, user_esp = 0, custom_pdir = NULL (Kernel task)
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

        k_printf("[STRESS] Spawned: %u, Failed: %u. Waiting for Reaper...\n", spawned, failed);

        // 3. Ожидание завершения (Грязный поллинг)
        // Reaper чистит dead_tasks_head внутри schedule(), который вызывается через task_yield()
        uint32_t timeout = (spawned * 50) + 1000; 
        while (task_get_count() > baseline_tasks && timeout > 0) {
            task_yield(); 
            timeout--;
        }

        if (timeout == 0) {
            k_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
            k_print("[STRESS] TIMEOUT: Reaper didn't clean up all tasks!\n");
        }

        // 4. Снапшот ПОСЛЕ и Валидация
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
// ТОЧКА ВХОДА (Команда run_tests)
// ============================================================================
void test_init(void) {
    k_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    k_print("\n========================================\n");
    k_print("      BARE METAL OS TEST SUITE (Day 10)\n");
    k_print("========================================\n\n");
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    // Pillar 1: ELF Tests (User Space)
    run_elf_test("/bin/test_hello.elf", "Basic Hello World");
    run_elf_test("/bin/test_segfault.elf", "NULL Pointer Dereference");
    run_elf_test("/bin/test_write_text.elf", "W^X Violation (Write to .text)");
    run_elf_test("/bin/test_stack_overflow.elf", "Stack Guard Page");
    run_elf_test("/bin/test_oom.elf", "OOM Protection");

    k_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    k_print("\n========================================\n");
    k_print("             TEST SUITE FINISHED\n");
    k_print("========================================\n\n");
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}
