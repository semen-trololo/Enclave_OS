#include "user_libc.h"

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("[USER] Hello from Ring 3! Test PASSED.\n");
    return 0;
}