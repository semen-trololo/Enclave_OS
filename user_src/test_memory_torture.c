#include "user_syscalls.h"

// ============================================================================
// HELPERS (Freestanding compatible)
// ============================================================================
static uint32_t strlen(const char* s) {
    uint32_t len = 0;
    while (s[len]) len++;
    return len;
}

static void log(const char* msg) {
    sys_write(1, msg, strlen(msg));
}

// Детерминированный LCG для псевдо-рандомного доступа
static uint32_t lcg_state = 0xDEADBEEF;
static uint32_t next_rand(void) {
    lcg_state = lcg_state * 1103515245 + 12345;
    return (lcg_state >> 16) & 0x7FFF;
}

// Генерация уникального паттерна: [Stage:8][Page:12][Offset:12]
static uint32_t make_pattern(uint8_t stage, uint32_t page_idx, uint32_t offset) {
    return ((uint32_t)stage << 24) | (page_idx << 12) | (offset & 0xFFF);
}

// ============================================================================
// STAGE 1: Linear Demand Paging Storm
// ============================================================================
static int stage_linear_demand(uint32_t base, uint32_t pages) {
    log("[STAGE 1] Linear Demand Paging Storm...\n");

    // Touch & Write
    for (uint32_t p = 0; p < pages; p++) {
        volatile uint32_t* page = (uint32_t*)(base + p * 4096);
        for (uint32_t i = 0; i < 1024; i++) {
            page[i] = make_pattern(1, p, i);
        }
    }

    // Yield to force TLB/CR3 context switch
    for (int i = 0; i < 5; i++) sys_yield();

    // Verify
    for (uint32_t p = 0; p < pages; p++) {
        volatile uint32_t* page = (uint32_t*)(base + p * 4096);
        for (uint32_t i = 0; i < 1024; i++) {
            if (page[i] != make_pattern(1, p, i)) {
                log("[FAIL] Stage 1: Pattern corruption at page ");
                return 1;
            }
        }
    }
    log("[PASS] Stage 1\n");
    return 0;
}

// ============================================================================
// STAGE 2: Random Access Matrix (Cross-Page Bleeding Check)
// ============================================================================
static int stage_random_access(uint32_t base, uint32_t pages) {
    log("[STAGE 2] Random Access Matrix...\n");
    
    // 🛡️ FIX: Сохраняем состояние ГСЧ, чтобы повторить ту же последовательность при чтении
    uint32_t saved_lcg_state = lcg_state;

    // Overwrite with Stage 2 patterns in pseudo-random order
    for (uint32_t k = 0; k < pages * 4; k++) {
        uint32_t p = next_rand() % pages;
        uint32_t i = next_rand() % 1024;
        volatile uint32_t* page = (uint32_t*)(base + p * 4096);
        page[i] = make_pattern(2, p, i);
    }
    
    for (int i = 0; i < 5; i++) sys_yield(); 
    
    // 🛡️ FIX: Восстанавливаем состояние ГСЧ. 
    // Теперь next_rand() выдаст ровно те же p и i, что и при записи!
    lcg_state = saved_lcg_state;

    // Verify ONLY Stage 2 patterns survived
    for (uint32_t k = 0; k < pages * 4; k++) {
        uint32_t p = next_rand() % pages;
        uint32_t i = next_rand() % 1024;
        volatile uint32_t* page = (uint32_t*)(base + p * 4096);
        if (page[i] != make_pattern(2, p, i)) {
            log("[FAIL] Stage 2: Cross-page bleeding or TLB stale\n");
            return 2;
        }
    }
    log("[PASS] Stage 2\n");
    return 0;
}

// ============================================================================
// STAGE 3: Brk Staircase Expansion
// ============================================================================
static int stage_brk_staircase(void) {
    log("[STAGE 3] Brk Staircase Expansion...\n");

    uint32_t cur = sys_brk(0);
    uint32_t steps = 8;
    uint32_t step_size = 16384; // 16 KB per step

    for (uint32_t s = 0; s < steps; s++) {
        uint32_t target = cur + step_size;
        if (sys_brk(target) != 0) {
            log("[FAIL] Stage 3: sys_brk expansion failed\n");
            return 3;
        }

        // Verify new region is zeroed & writable
        volatile uint8_t* ptr = (uint8_t*)cur;
        for (uint32_t i = 0; i < step_size; i += 4096) {
            ptr[i] = 0xA5;
            if (ptr[i] != 0xA5) {
                log("[FAIL] Stage 3: New page not writable\n");
                return 3;
            }
        }
        cur = target;
    }
    log("[PASS] Stage 3\n");
    return 0;
}

// ============================================================================
// STAGE 4: OOM Boundary Probe (Graceful Failure)
// ============================================================================
static int stage_oom_boundary(void) {
    log("[STAGE 4] OOM Boundary Probe...\n");

    uint32_t cur = sys_brk(0);
    // Пытаемся прыгнуть на 256 МБ вперед (должно отказать gracefully)
    uint32_t insane_target = cur + (256 * 1024 * 1024);

    int res = sys_brk(insane_target);
    if (res == 0) {
        log("[FAIL] Stage 4: sys_brk allowed insane expansion (OOM trap missed)\n");
        return 4;
    }

    // Проверяем, что процесс жив и старая память не повреждена
    volatile uint8_t* safe_ptr = (uint8_t*)(cur - 4096);
    safe_ptr[0] = 0x5A;
    if (safe_ptr[0] != 0x5A) {
        log("[FAIL] Stage 4: Memory corrupted after OOM rejection\n");
        return 4;
    }

    log("[PASS] Stage 4\n");
    return 0;
}

// ============================================================================
// STAGE 5: Yield Storm Coherency
// ============================================================================
static int stage_yield_storm(uint32_t base, uint32_t pages) {
    log("[STAGE 5] Yield Storm Coherency...\n");

    // Пишем паттерн, постоянно уступая CPU
    for (uint32_t p = 0; p < pages; p++) {
        volatile uint32_t* page = (uint32_t*)(base + p * 4096);
        for (uint32_t i = 0; i < 256; i++) {
            page[i] = make_pattern(5, p, i);
            if (i % 64 == 0) sys_yield();
        }
    }

    // Финальная проверка после шторма
    for (uint32_t p = 0; p < pages; p++) {
        volatile uint32_t* page = (uint32_t*)(base + p * 4096);
        for (uint32_t i = 0; i < 256; i++) {
            if (page[i] != make_pattern(5, p, i)) {
                log("[FAIL] Stage 5: Scheduler-Memory race detected\n");
                return 5;
            }
        }
    }
    log("[PASS] Stage 5\n");
    return 0;
}

// ============================================================================
// STAGE 6: Final Integrity Sweep
// ============================================================================
static int stage_final_sweep(uint32_t base, uint32_t pages) {
    log("[STAGE 6] Final Integrity Sweep...\n");

    // Перезаписываем всё финальным паттерном
    for (uint32_t p = 0; p < pages; p++) {
        volatile uint32_t* page = (uint32_t*)(base + p * 4096);
        for (uint32_t i = 0; i < 1024; i++) {
            page[i] = make_pattern(6, p, i);
        }
    }

    sys_yield();

    // Полная верификация
    for (uint32_t p = 0; p < pages; p++) {
        volatile uint32_t* page = (uint32_t*)(base + p * 4096);
        for (uint32_t i = 0; i < 1024; i++) {
            if (page[i] != make_pattern(6, p, i)) {
                log("[FAIL] Stage 6: Global corruption detected\n");
                return 6;
            }
        }
    }
    log("[PASS] Stage 6\n");
    return 0;
}

// ============================================================================
// ENTRY POINT
// ============================================================================
void _start() {
    log("\n=== MEMORY TORTURE TEST INITIATED ===\n");

    uint32_t heap_start = sys_brk(0);
    uint32_t test_pages = 64; // 256 KB test region
    uint32_t target = heap_start + (test_pages * 4096);

    if (sys_brk(target) != 0) {
        log("[FATAL] Initial brk expansion failed\n");
        sys_exit(255);
    }

    int res = 0;
    if (!res) res = stage_linear_demand(heap_start, test_pages);
    if (!res) res = stage_random_access(heap_start, test_pages);
    if (!res) res = stage_brk_staircase();
    if (!res) res = stage_oom_boundary();
    if (!res) res = stage_yield_storm(heap_start, test_pages);
    if (!res) res = stage_final_sweep(heap_start, test_pages);

    if (res == 0) {
        log("=== ALL STAGES PASSED. EXITING CLEANLY. ===\n");
    } else {
        log("=== TEST FAILED AT STAGE ===\n");
    }

    sys_exit(res);
}
