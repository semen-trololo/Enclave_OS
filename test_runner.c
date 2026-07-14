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
// ВРЕМЕННЫЕ ОПРЕДЕЛЕНИЯ (WNOHANG для опроса)
// ============================================================================
#define WNOHANG 1
#define WAITPID_TIMEOUT_YIELDS 1000

// ============================================================================
// ВНУТРЕННЯЯ ФУНКЦИЯ ЗАПУСКА ПРОЦЕССА (Kernel Mode Exec)
// ============================================================================
static int spawn_process(const char* filename) {
    serial_printf("[SPAWN] === Starting spawn for: %s ===\n", filename);
    
    vfs_node_t* file_node = vfs_findnode(filename);
    if (!file_node) {
        serial_printf("[SPAWN] ❌ File not found: %s\n", filename);
        return -1;
    }

    uint32_t* pdir_virt = vmm_create_address_space();
    if (!pdir_virt) return -1;

    task_t temp_task;
    temp_task.pdir_virt = pdir_virt;
    temp_task.vma_head = NULL;

    uint32_t entry_point = elf_load(file_node, &temp_task);
    if (entry_point == 0) {
        vma_destroy_all(&temp_task);
        vmm_destroy_address_space(pdir_virt);
        return -1;
    }

    uint32_t stack_top = USER_STACK_VIRT_TOP - 16;
    uint32_t stack_bottom = stack_top - USER_STACK_SIZE;

    uint32_t eflags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(eflags));

    task_t* new_task = task_create(filename, (void (*)(void))entry_point, true, stack_top, pdir_virt);
    if (!new_task) {
        __asm__ volatile("push %0; popf" : : "r"(eflags));
        vma_destroy_all(&temp_task);
        vmm_destroy_address_space(pdir_virt);
        return -1;
    }

    new_task->vma_head = temp_task.vma_head;

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

    __asm__ volatile("push %0; popf" : : "r"(eflags));
    serial_printf("[SPAWN] ✓ PID %d ready\n", new_task->pid);
    return new_task->pid;
}

// ============================================================================
// ОЖИДАНИЕ ЗАВЕРШЕНИЯ (Deterministic Reaping + Hard Sync)
// ============================================================================
static int wait_for_cleanup(int pid, uint32_t expected_task_count) {
    serial_printf("[WAIT] Polling PID %d...\n", pid);
    int status = 0;
    int yield_count = 0;
    
    while (yield_count < WAITPID_TIMEOUT_YIELDS) {
        int reaped = task_waitpid(pid, &status, WNOHANG);
        
        if (reaped > 0) {
            serial_printf("[WAIT] ✓ Normal exit: status=%d\n", status);
            
            // 🛡️ HARD SYNC: Ждем, пока Reaper физически не освободит ресурсы
            // и task_count не вернется к ожидаемому значению.
            int sync_attempts = 0;
            while (task_get_count() > expected_task_count && sync_attempts < 500) {
                task_yield();
                sync_attempts++;
            }
            
            if (task_get_count() > expected_task_count) {
                serial_printf("[WAIT] ⚠️ Reaper timeout! task_count is still %d (expected %d)\n", 
                              task_get_count(), expected_task_count);
            } else {
                serial_printf("[WAIT] ✓ Reaper confirmed: resources freed.\n");
            }
            
            return status;
        }
        if (reaped < 0) {
            serial_printf("[WAIT] ✓ Process destroyed by Reaper (Crash detected)\n");
            for(int i=0; i<200; i++) task_yield();
            return 0; 
        }
        
        if (yield_count > 200 && reaped == 0) {
            serial_printf("[WAIT] ✓ Process hard-killed by Kernel (No zombie left)\n");
            for(int i=0; i<200; i++) task_yield();
            return 0; 
        }
        
        task_yield();
        yield_count++;
    }
    
    serial_printf("[WAIT] ⚠️ TIMEOUT\n");
    return -1;
}

// ============================================================================
// ФУНКЦИИ СТАБИЛИЗАЦИИ (Quiescent State Convergence)
// Ждем, пока Reaper освободит все отложенные ресурсы
// ============================================================================
static int32_t get_stable_pmm_balance(void) {
    // 🛡️ PRE-FLUSH: Даем Reaper'у время физически освободить Page Directory
    for(int i=0; i<50; i++) task_yield(); 
    int32_t current = pmm_check_balance();
    int stable = 0, iter = 0;
    while (stable < 5 && iter < 50) {
        task_yield(); iter++;
        int32_t next = pmm_check_balance();
        if (next == current) stable++;
        else { stable = 0; current = next; }
    }
    return current;
}

static int32_t get_stable_heap_balance(void) {
    // 🛡️ PRE-FLUSH: Даем Reaper'у время физически освободить Page Directory
    for(int i=0; i<50; i++) task_yield(); 
    int32_t current = heap_check_balance();
    int stable = 0, iter = 0;
    while (stable < 5 && iter < 50) {
        task_yield(); iter++;
        int32_t next = heap_check_balance();
        if (next == current) stable++;
        else { stable = 0; current = next; }
    }
    return current;
}

static uint32_t get_stable_task_count(void) {
    // 🛡️ PRE-FLUSH: Даем Reaper'у время физически освободить Page Directory
    for(int i=0; i<50; i++) task_yield(); 
    uint32_t current = task_get_count();
    int stable = 0, iter = 0;
    while (stable < 5 && iter < 50) {
        task_yield(); iter++;
        uint32_t next = task_get_count();
        if (next == current) stable++;
        else { stable = 0; current = next; }
    }
    return current;
}

// ============================================================================
// ЗАПУСК ОДНОГО ELF ТЕСТА (С проверкой логики и утечек + Deterministic Reaping)
// ============================================================================
static void run_elf_test(const char* name, const char* description, int expected_exit) {
    k_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    k_printf("\n[TEST] ═══════════════════════════════════════\n");
    k_printf("[TEST] Running: %s (%s)\n", name, description);
    k_printf("[TEST] ═══════════════════════════════════════\n");
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    int32_t pmm_before = get_stable_pmm_balance();
    int32_t heap_before = get_stable_heap_balance(); 
    uint32_t tasks_before = get_stable_task_count();

    int pid = spawn_process(name);
    if (pid < 0) {
        k_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
        k_print("[FAIL] Spawn failed!\n");
        serial_print("[FAIL] Spawn failed!\n");
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return;
    }

    // 🛡️ Передаем tasks_before для Hard Sync (Deterministic Reaping)
    int exit_status = wait_for_cleanup(pid, tasks_before);
    
    if (exit_status < 0 && expected_exit != -1) { // -1 это 0xFFFFFFFF (unsigned)
        k_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
        k_print("[FAIL] Wait timeout!\n");
        serial_print("[FAIL] Wait timeout!\n");
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return;
    }
    
    k_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    k_printf("[TEST] Exit status: %d (Expected: %d)\n", exit_status, expected_exit);
    serial_printf("[TEST] Exit status: %d (Expected: %d)\n", exit_status, expected_exit);
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    int32_t pmm_after = get_stable_pmm_balance();
    int32_t heap_after = get_stable_heap_balance();
    uint32_t tasks_after = get_stable_task_count();

    int leaked_pages = (pmm_after - pmm_before);
    int leaked_heap = (heap_after - heap_before);
    int zombies = (tasks_after - tasks_before);

    // 🛡️ КОМПЛЕКСНАЯ ПРОВЕРКА: И логика теста, и отсутствие утечек
    int logic_pass = (exit_status == expected_exit);
    int memory_pass = (leaked_pages == 0 && leaked_heap == 0 && zombies == 0);

    if (logic_pass && memory_pass) {
        k_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        k_print("[PASS] Test logic OK, No leaks, no zombies.\n");
        serial_print("[PASS] Test logic OK, No leaks, no zombies.\n");
    } else {
        k_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
        k_print("[FAIL] ");
        serial_print("[FAIL] ");
        
        // 1. Формируем строку для VGA консоли (String Builder Pattern)
        char vga_msg[256];
        vga_msg[0] = '\0';
        
        #define APPEND_STR(s) do { \
            int _l = k_strlen(vga_msg); \
            if (_l + k_strlen(s) < 255) { \
                k_memcpy(vga_msg + _l, s, k_strlen(s) + 1); \
            } \
        } while(0)
        
        #define APPEND_INT(i) do { \
            char _tmp[16]; \
            k_itoa(i, _tmp, 10); \
            APPEND_STR(_tmp); \
        } while(0)

        if (!logic_pass) {
            APPEND_STR("Logic: got ");
            APPEND_INT(exit_status);
            APPEND_STR(", expected ");
            APPEND_INT(expected_exit);
            APPEND_STR(" | ");
            
            serial_printf("Logic: got %d, expected %d | ", exit_status, expected_exit);
        }
        if (leaked_pages != 0) {
            APPEND_STR("PMM Leak: ");
            APPEND_INT(leaked_pages);
            APPEND_STR(" | ");
            
            serial_printf("PMM Leak: %d | ", leaked_pages);
        }
        if (leaked_heap != 0) {
            APPEND_STR("Heap Leak: ");
            APPEND_INT(leaked_heap);
            APPEND_STR(" | ");
            
            serial_printf("Heap Leak: %d | ", leaked_heap);
        }
        if (zombies != 0) {
            APPEND_STR("Zombies: ");
            APPEND_INT(zombies);
            APPEND_STR(" | ");
            
            serial_printf("Zombies: %d | ", zombies);
        }
        
        APPEND_STR("\n");
        k_print(vga_msg);
        serial_print("\n");
        
        #undef APPEND_STR
        #undef APPEND_INT
    }
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}
// ============================================================================
// STRESS TEST WORKER (Kernel Mode Task)
// ============================================================================
static void stress_worker_task(void) {
    for (int i = 0; i < 5; i++) task_yield(); 
    task_exit(0);
}

// ============================================================================
// ОБРАБОТЧИК СТРЕСС-ТЕСТОВ (Pillar 2)
// ============================================================================
void handle_stress(int argc, char args[MAX_ARGS][MAX_ARG_LEN]) {
    if (argc < 3) {
        k_print("Usage: stress spawn <count>\n");
        return;
    }

    if (k_strcmp(args[1], "spawn") == 0) {
        uint32_t count = k_atoi(args[2]);
        if (count == 0) count = 100;
        if (count > 100000) count = 100000;

        k_printf("[STRESS] Mass Spawn Test: Creating %u kernel tasks...\n", count);

        uint32_t baseline_tasks = get_stable_task_count();
        int32_t pmm_before = get_stable_pmm_balance();
        int32_t heap_before = get_stable_heap_balance();

        uint32_t spawned = 0, failed = 0;
        for (uint32_t i = 0; i < count; i++) {
            task_t* t = task_create("stress_worker", stress_worker_task, false, 0, NULL);
            if (t) spawned++;
            else { failed++; break; }
        }

        k_printf("[STRESS] Spawned: %u, Failed: %u. Waiting...\n", spawned, failed);

        for (uint32_t i = 0; i < spawned; i++) {
            int status;
            task_waitpid(-1, &status, 0); 
        }
        
        int32_t pmm_after = get_stable_pmm_balance();
        int32_t heap_after = get_stable_heap_balance();
        uint32_t zombies = get_stable_task_count() - baseline_tasks;

        if (pmm_after == pmm_before && heap_after == heap_before && zombies == 0) {
            k_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            k_printf("[PASS] Stress test OK. %u tasks reaped. No leaks.\n", spawned);
        } else {
            k_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
            k_print("[FAIL] Stress test failed!\n");
        }
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    }
}

// ============================================================================
// ЭКСПОРТ ДЛЯ SHELL (С поддержкой Deterministic Reaping)
// ============================================================================
int run_elf_and_wait(const char* filename) {
    // 🛡️ Замеряем количество задач ДО создания новой, чтобы знать, 
    // когда Reaper закончит свою работу.
    uint32_t tasks_before = task_get_count(); 
    
    int pid = spawn_process(filename);
    if (pid < 0) return -1;
    
    return wait_for_cleanup(pid, tasks_before);
}

// ============================================================================
// ТОЧКА ВХОДА (Команда run_tests)
// ============================================================================
void test_init(void) {
    k_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    k_print("\n========================================\n");
    k_print("      BARE METAL OS TEST SUITE (Day 16)\n");
    k_print("========================================\n\n");
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    serial_printf("\n[WARMUP] Warming up Kernel Heap...\n");
    void* w1 = kmalloc(16384); void* w2 = kmalloc(4096); void* w3 = kmalloc(128);   
    if (w1) kfree(w1); if (w2) kfree(w2); if (w3) kfree(w3);
    for(int i = 0; i < 10; i++) task_yield();

    // Pillar 1: ELF Tests (User Space)
    run_elf_test("/bin/test_hello.elf", "Basic Hello World", 0);
    run_elf_test("/bin/test_segfault.elf", "NULL Pointer Dereference", -1); // Краш = -1
    run_elf_test("/bin/test_write_text.elf", "W^X Violation", -1);         // Краш = -1
    run_elf_test("/bin/test_stack_overflow.elf", "Stack Guard Page", -1);  // Краш = -1
    run_elf_test("/bin/test_oom.elf", "OOM Protection", 0);
    run_elf_test("/bin/test_vfs_stress.elf", "VFS tmpfs 1000 files", 0);
    run_elf_test("/bin/test_memory_torture.elf", "Memory Torture", 0);
    run_elf_test("/bin/test_mmap.elf"," Test MMAP", 0);

    k_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    k_print("\n========================================\n");
    k_print("             TEST SUITE FINISHED\n");
    k_print("========================================\n\n");
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}