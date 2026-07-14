#include "user_libc.h"

// 🛡️ Отключаем warning о бесконечной рекурсии (мы делаем это специально для теста)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winfinite-recursion"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"

void recurse(int i) {
    volatile char buf[1024]; 
    buf[0] = (char)i;
    recurse(i + 1);
}

#pragma GCC diagnostic pop

void _start() {
    printf("[TEST] Triggering Stack Overflow (64KB limit)...\n");
    recurse(0);
    
    printf("[FAIL] Stack guard page missed!\n");
    exit(1);
}