// user_src/test_segfault.c
#include "user_syscalls.h"

void _start() {
    volatile int* ptr = (int*)0x00000000;
    // Попытка записи в NULL. Должен сработать Page Fault -> SIGSEGV.
    *ptr = 42; 
    
    // Если мы дошли сюда, значит Guard Page не работает!
    sys_exit(1); 
}
