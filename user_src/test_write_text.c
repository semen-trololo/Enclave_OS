#include "user_libc.h"

void _start() {
    printf("[TEST] Triggering W^X violation (Write to .text)...\n");
    
    // Пытаемся модифицировать первую инструкцию самой функции _start
    __asm__ volatile(
        "movb $0x90, (%0)" 
        : 
        : "r"(_start)
    );
    
    // Если дошли сюда, W^X не работает!
    printf("[FAIL] W^X protection failed!\n");
    exit(1);
}