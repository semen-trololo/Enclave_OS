#include "user_libc.h"

void _start() {
    printf("[TEST] Triggering NULL pointer dereference...\n");
    
    volatile int* ptr = (int*)0x00000000;
    *ptr = 42; // Должен сработать Page Fault -> SIGSEGV
    
    // Если мы дошли сюда, значит Guard Page не работает!
    printf("[FAIL] Should have crashed! Guard page missed.\n");
    exit(1); 
}