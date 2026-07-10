// user_src/test_stack_overflow.c
#include "user_syscalls.h"

// volatile заставляет компилятор реально выделять место в стеке каждый раз
void recurse(int i) {
    volatile char buf[1024]; 
    buf[0] = (char)i;
    recurse(i + 1);
}

void _start() {
    // Запускаем бесконечную рекурсию. 
    // Стек (64KB) переполнится, и мы упадем в Guard Page.
    recurse(0);
    sys_exit(1);
}
