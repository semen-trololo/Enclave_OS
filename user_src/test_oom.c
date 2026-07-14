#include "user_libc.h"

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    
    printf("[TEST] Triggering User-Space OOM via malloc...\n");
    
    void* huge_ptr = malloc(100 * 1024 * 1024);
    
    if (huge_ptr == NULL) {
        printf("[PASS] malloc correctly returned NULL on OOM (errno=%d)\n", errno);
        return 0;
    }
    
    printf("[WARN] malloc succeeded, touching pages to trigger VMM OOM...\n");
    volatile char* ptr = (char*)huge_ptr;
    for (uint32_t i = 0; i < (100 * 1024 * 1024); i += 4096) {
        ptr[i] = 'A';
    }
    
    printf("[FAIL] OOM protection failed!\n");
    return 1;
}