#include "user_libc.h"

void _start() {
    printf("[TEST] Triggering User-Space OOM via malloc...\n");
    
    // Пытаемся выделить 100 МБ. Bump allocator вызовет sys_brk, 
    // который упрется в USER_HEAP_MAX_SIZE и вернет -ENOMEM.
    void* huge_ptr = malloc(100 * 1024 * 1024);
    
    if (huge_ptr == NULL) {
        printf("[PASS] malloc correctly returned NULL on OOM (errno=%d)\n", errno);
        exit(0);
    }
    
    // Если ядро по какой-то ошибке разрешило sys_brk, пытаемся затронуть страницы,
    // чтобы триггерить Page Fault и реактивный OOM Trap.
    printf("[WARN] malloc succeeded, touching pages to trigger VMM OOM...\n");
    volatile char* ptr = (char*)huge_ptr;
    for (uint32_t i = 0; i < (100 * 1024 * 1024); i += 4096) {
        ptr[i] = 'A';
    }
    
    printf("[FAIL] OOM protection failed!\n");
    exit(1);
}