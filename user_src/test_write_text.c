#include "user_libc.h"

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    
    printf("[TEST] Triggering W^X violation (Write to .text)...\n");
    
    __asm__ volatile(
        "movb $0x90, (%0)" 
        : 
        : "r"(main)
    );
    
    printf("[FAIL] W^X protection failed!\n");
    return 1;
}