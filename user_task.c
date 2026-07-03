#include <stdint.h>
#include "syscall.h"

// Эта функция будет выполняться в Ring 3 (User Mode)
void user_task(void) {
    // 1. Демонстрация системного вызова sys_write (INT 0x80)
    const char* msg = "[RING 3] Hello from User Mode! Returning to kernel...\n";
    
    // Вызываем sys_write (номер 4)
    // EAX = 4 (SYS_WRITE), EBX = 1 (stdout), ECX = buf, EDX = count
    __asm__ volatile (
        "mov $4, %%eax\n"       
        "mov $1, %%ebx\n"       
        "mov %0, %%ecx\n"       
        "mov $56, %%edx\n"      // Длина строки
        "int $0x80\n"
        : : "r"(msg) : "eax", "ebx", "ecx", "edx", "memory"
    );

    // 2. Завершение user process через sys_exit
    // Это вернет управление в kernel_main, который запустит Shell
    // EAX = 1 (SYS_EXIT), EBX = 0 (exit code)
    __asm__ volatile (
        "mov $1, %%eax\n"       
        "mov $0, %%ebx\n"       
        "int $0x80\n"
        : : : "eax", "ebx", "memory"
    );
    
    // Fallback: если sys_exit не сработал (не должно произойти)
    while (1) {
        __asm__ volatile("nop");
    }
}