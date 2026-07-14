#include "user_libc.h"

void _start() {
    printf("[USER] Hello from Ring 3! Test PASSED.\n");
    exit(0);
}