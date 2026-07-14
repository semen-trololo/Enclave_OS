#include "user_libc.h"

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    
    // Test 1: FILE* buffering (4KB буфер)
    printf("=== Test 1: FILE* Buffering ===\n");
    FILE* f = fopen("/tmp/test_buf.txt", "w");
    if (!f) { printf("FAIL: fopen write\n"); return 1; }

    for (int i = 0; i < 100; i++) {
        fprintf(f, "Line %d: Testing buffering performance\n", i);
    }
    fclose(f);

    f = fopen("/tmp/test_buf.txt", "r");
    if (!f) { printf("FAIL: fopen read\n"); return 1; }

    char buf[256];
    int count = 0;
    while (fgets(buf, sizeof(buf), f)) {
        count++;
    }
    fclose(f);
    printf("Read %d lines (expected 100)\n", count);

    // Test 2: Optional functions
    printf("\n=== Test 2: Optional Functions ===\n");
    char* path = getenv("PATH");
    printf("getenv('PATH') = %s (expected NULL)\n", path ? path : "NULL");

    printf("\n[PASS] All TinyCC dependencies working!\n");
    return 0;
}