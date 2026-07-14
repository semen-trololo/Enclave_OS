#include "user_libc.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winfinite-recursion"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"

void recurse(int i) {
    volatile char buf[1024]; 
    buf[0] = (char)i;
    recurse(i + 1);
}

#pragma GCC diagnostic pop

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    
    printf("[TEST] Triggering Stack Overflow (64KB limit)...\n");
    recurse(0);
    
    printf("[FAIL] Stack guard page missed!\n");
    return 1;
}