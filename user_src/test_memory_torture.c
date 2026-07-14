#include "user_libc.h"
#include "user_syscalls.h" // Для sys_yield

static uint32_t lcg_state = 0xDEADBEEF;
static uint32_t next_rand(void) {
    lcg_state = lcg_state * 1103515245 + 12345;
    return (lcg_state >> 16) & 0x7FFF;
}

static uint32_t make_pattern(uint8_t stage, uint32_t page_idx, uint32_t offset) {
    return ((uint32_t)stage << 24) | (page_idx << 12) | (offset & 0xFFF);
}

static int stage_linear_demand(uint32_t* base, uint32_t pages) {
    printf("[STAGE 1] Linear Demand Paging Storm...\n");
    for (uint32_t p = 0; p < pages; p++) {
        uint32_t* page = base + (p * 1024);
        for (uint32_t i = 0; i < 1024; i++) page[i] = make_pattern(1, p, i);
    }
    for (int i = 0; i < 5; i++) sys_yield();
    for (uint32_t p = 0; p < pages; p++) {
        uint32_t* page = base + (p * 1024);
        for (uint32_t i = 0; i < 1024; i++) {
            if (page[i] != make_pattern(1, p, i)) {
                printf("[FAIL] Stage 1: Corruption\n"); return 1;
            }
        }
    }
    printf("[PASS] Stage 1\n"); return 0;
}

static int stage_random_access(uint32_t* base, uint32_t pages) {
    printf("[STAGE 2] Random Access Matrix...\n");
    uint32_t saved_lcg_state = lcg_state;
    for (uint32_t k = 0; k < pages * 4; k++) {
        uint32_t p = next_rand() % pages;
        uint32_t i = next_rand() % 1024;
        base[p * 1024 + i] = make_pattern(2, p, i);
    }
    for (int i = 0; i < 5; i++) sys_yield(); 
    lcg_state = saved_lcg_state;
    for (uint32_t k = 0; k < pages * 4; k++) {
        uint32_t p = next_rand() % pages;
        uint32_t i = next_rand() % 1024;
        if (base[p * 1024 + i] != make_pattern(2, p, i)) {
            printf("[FAIL] Stage 2: Bleeding\n"); return 2;
        }
    }
    printf("[PASS] Stage 2\n"); return 0;
}

static int stage_brk_staircase(void) {
    printf("[STAGE 3] Brk Staircase Expansion (via malloc)...\n");
    uint32_t steps = 8;
    uint32_t step_size = 16384; // 16 KB per step
    for (uint32_t s = 0; s < steps; s++) {
        uint8_t* ptr = (uint8_t*)malloc(step_size);
        if (!ptr) { printf("[FAIL] Stage 3: malloc failed\n"); return 3; }
        for (uint32_t i = 0; i < step_size; i += 4096) {
            ptr[i] = 0xA5;
            if (ptr[i] != 0xA5) { printf("[FAIL] Stage 3: write failed\n"); return 3; }
        }
    }
    printf("[PASS] Stage 3\n"); return 0;
}

static int stage_oom_boundary(void) {
    printf("[STAGE 4] OOM Boundary Probe...\n");
    void* insane = malloc(256 * 1024 * 1024);
    if (insane != NULL) {
        printf("[FAIL] Stage 4: malloc allowed 256MB\n"); return 4;
    }
    printf("[PASS] Stage 4\n"); return 0;
}

static int stage_yield_storm(uint32_t* base, uint32_t pages) {
    printf("[STAGE 5] Yield Storm Coherency...\n");
    for (uint32_t p = 0; p < pages; p++) {
        uint32_t* page = base + (p * 1024);
        for (uint32_t i = 0; i < 256; i++) {
            page[i] = make_pattern(5, p, i);
            if (i % 64 == 0) sys_yield();
        }
    }
    for (uint32_t p = 0; p < pages; p++) {
        uint32_t* page = base + (p * 1024);
        for (uint32_t i = 0; i < 256; i++) {
            if (page[i] != make_pattern(5, p, i)) {
                printf("[FAIL] Stage 5: Race\n"); return 5;
            }
        }
    }
    printf("[PASS] Stage 5\n"); return 0;
}

static int stage_final_sweep(uint32_t* base, uint32_t pages) {
    printf("[STAGE 6] Final Integrity Sweep...\n");
    for (uint32_t p = 0; p < pages; p++) {
        uint32_t* page = base + (p * 1024);
        for (uint32_t i = 0; i < 1024; i++) page[i] = make_pattern(6, p, i);
    }
    sys_yield();
    for (uint32_t p = 0; p < pages; p++) {
        uint32_t* page = base + (p * 1024);
        for (uint32_t i = 0; i < 1024; i++) {
            if (page[i] != make_pattern(6, p, i)) {
                printf("[FAIL] Stage 6: Global corruption\n"); return 6;
            }
        }
    }
    printf("[PASS] Stage 6\n"); return 0;
}

void _start() {
    printf("\n=== MEMORY TORTURE TEST INITIATED ===\n");
    uint32_t test_pages = 32; // 128 KB test region
    
    // Пре-аллокация одного большого куска через libc
    uint32_t* heap_region = (uint32_t*)malloc(test_pages * 4096);
    if (!heap_region) {
        printf("[FATAL] Initial malloc failed\n");
        exit(255);
    }

    int res = 0;
    if (!res) res = stage_linear_demand(heap_region, test_pages);
    if (!res) res = stage_random_access(heap_region, test_pages);
    if (!res) res = stage_brk_staircase();
    if (!res) res = stage_oom_boundary();
    if (!res) res = stage_yield_storm(heap_region, test_pages);
    if (!res) res = stage_final_sweep(heap_region, test_pages);

    if (res == 0) printf("=== ALL STAGES PASSED. ===\n");
    else printf("=== TEST FAILED AT STAGE %d ===\n", res);
    
    exit(res);
}