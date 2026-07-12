// user_src/test_mmap.c
// Тест для Day 12: sys_mmap, sys_munmap, sys_mprotect

#include "user_syscalls.h"

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

static void print(const char* s) {
    int len = 0;
    while(s[len]) len++;
    sys_write(1, s, len);
}

static void print_hex(unsigned int val) {
    char buf[11];
    buf[0] = '0'; buf[1] = 'x';
    for(int i = 9; i >= 2; i--) {
        int digit = val & 0xF;
        buf[i] = digit < 10 ? '0' + digit : 'A' + digit - 10;
        val >>= 4;
    }
    buf[10] = '\n';
    sys_write(1, buf, 11);
}

// ============================================================================
// MAIN TEST LOGIC
// ============================================================================

void _start() {
    print("[TEST] Starting Day 12 mmap test...\n");
    
    // 1. Allocate 8KB (2 pages)
    void* ptr = sys_mmap(0, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    // Linux standard: errors are returned as negative values cast to void*
    if ((unsigned int)ptr > 0xFFFFF000) {
        print("[FAIL] mmap returned error: ");
        print_hex((unsigned int)ptr);
        sys_exit(1);
    }
    print("[PASS] mmap allocated at: ");
    print_hex((unsigned int)ptr);
    
    // 2. Write to memory (triggers Demand Paging / Page Fault)
    char* c = (char*)ptr;
    c[0] = 'H';
    c[1] = 'i';
    c[4096] = '!'; // Write to second page to trigger second PF
    print("[PASS] Wrote to mapped memory (Demand Paging worked)\n");
    
    // 3. mprotect to READ ONLY
    int ret = sys_mprotect(ptr, 8192, PROT_READ);
    if (ret < 0) {
        print("[FAIL] mprotect failed: ");
        print_hex((unsigned int)ret);
        sys_exit(2);
    }
    print("[PASS] mprotect(PROT_READ) succeeded\n");
    
    // 4. Read is OK
    if (c[0] == 'H' && c[4096] == '!') {
        print("[PASS] Read from protected memory is OK\n");
    }
    
    // 5. munmap
    ret = sys_munmap(ptr, 8192);
    if (ret < 0) {
        print("[FAIL] munmap failed: ");
        print_hex((unsigned int)ret);
        sys_exit(3);
    }
    print("[PASS] munmap succeeded\n");
    
    print("[TEST] All tests passed!\n");
    print("[NOTE] To test W^X SIGSEGV, uncomment the write after mprotect in source.\n");
    
    /* 
     * 💀 UNCOMMENT TO TEST SIGSEGV (W^X Violation):
     * c[0] = 'X'; // This will trigger Page Fault -> SIGSEGV -> Task Kill
     */
    
    sys_exit(0);
}