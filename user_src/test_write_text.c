// user_src/test_write_text.c
#include "user_syscalls.h"

void _start() {
    // Пытаемся модифицировать первую инструкцию самой функции _start (код в .text)
    // movb $0x90, (%eax) -> записываем NOP по адресу _start
    __asm__ volatile(
        "movb $0x90, (%0)" 
        : 
        : "r"(_start)
    );
    
    // Если дошли сюда, W^X не работает!
    sys_exit(1);
}
