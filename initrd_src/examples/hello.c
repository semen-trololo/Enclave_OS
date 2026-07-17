// ============================================================================
// ENCLAVE OS - Self-Hosting Test: Hello World
// Этот файл будет скомпилирован ВНУТРИ Enclave OS через TinyCC
// ============================================================================

#include "user_libc.h"

int main(int argc, char** argv) {
    printf("========================================\n");
    printf("  SELF-HOSTING SUCCESS!\n");
    printf("========================================\n");
    printf("This program was compiled INSIDE Enclave OS\n");
    printf("using TinyCC running in Ring 3.\n");
    printf("\n");
    printf("Arguments received: %d\n", argc);
    for (int i = 0; i < argc; i++) {
        printf("  argv[%d] = %s\n", i, argv[i]);
    }
    printf("\n");

    // Простая математика для проверки ALU
    int sum = 0;
    for (int i = 1; i <= 10; i++) {
        sum += i;
    }
    printf("Sum of 1..10 = %d (should be 55)\n", sum);

    printf("========================================\n");
    return 0;
}
