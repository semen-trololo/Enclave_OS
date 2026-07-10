// user_src/test_hello.c
#include "user_syscalls.h"

void _start() {
    const char* msg = "[USER] Hello from Ring 3! Test PASSED.\n";
    sys_write(1, msg, 41);
    sys_exit(0);
}
