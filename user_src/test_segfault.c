#include "user_libc.h"

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    
    printf("[TEST] Triggering NULL pointer dereference...\n");
    
    volatile int* ptr = (int*)0x00000000;
    *ptr = 42; // Должен сработать Page Fault -> SIGSEGV
    
    printf("[FAIL] Should have crashed! Guard page missed.\n");
    return 1; 
}