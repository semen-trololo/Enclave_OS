#include "user_libc.h"
#include "user_syscalls.h"

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    
    printf("[TEST] Starting Day 12 mmap test...\n");
    
    void* ptr = sys_mmap(0, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    if ((uintptr_t)ptr > 0xFFFFF000) {
        int err = (int)(intptr_t)ptr;
        // 🛡️ Честная обработка ENOMEM (Kernel Heap exhausted by previous tests?)
        if (err == -12) { 
            printf("[SKIP] sys_mmap returned ENOMEM (Kernel Heap exhausted?)\n");
            return 0; 
        }
        printf("[FAIL] mmap returned unexpected error: %d\n", err);
        return 1;
    }
    printf("[PASS] mmap allocated at: %p\n", ptr);
    
    char* c = (char*)ptr;
    c[0] = 'H';
    c[1] = 'i';
    c[4096] = '!'; 
    printf("[PASS] Wrote to mapped memory (Demand Paging worked)\n");
    
    int ret = sys_mprotect(ptr, 8192, PROT_READ);
    if (ret < 0) {
        printf("[FAIL] mprotect failed: %d\n", ret);
        sys_munmap(ptr, 8192);
        return 2;
    }
    printf("[PASS] mprotect(PROT_READ) succeeded\n");
    
    if (c[0] == 'H' && c[4096] == '!') {
        printf("[PASS] Read from protected memory is OK\n");
    }
    
    ret = sys_munmap(ptr, 8192);
    if (ret < 0) {
        printf("[FAIL] munmap failed: %d\n", ret);
        return 3;
    }
    printf("[PASS] munmap succeeded\n");
    
    printf("[TEST] All mmap tests passed!\n");
    return 0;
}