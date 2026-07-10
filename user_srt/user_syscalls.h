// user_src/user_syscalls.h
#ifndef USER_SYSCALLS_H
#define USER_SYSCALLS_H

#include <stdint.h>

// Номера системных вызовов (должны совпадать с syscall.h ядра)
#define SYS_EXIT    1
#define SYS_READ    3
#define SYS_WRITE   4
#define SYS_YIELD   24
#define SYS_BRK     45
#define SYS_EXEC    11

// Универсальный макрос для syscall (до 3 аргументов)
static inline int syscall3(int num, int a1, int a2, int a3) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a" (ret)
        : "a" (num), "b" (a1), "c" (a2), "d" (a3)
        : "memory"
    );
    return ret;
}

static inline int syscall1(int num, int a1) {
    return syscall3(num, a1, 0, 0);
}

static inline int syscall0(int num) {
    return syscall3(num, 0, 0, 0);
}

// Обертки для удобства
static inline void sys_exit(int code) {
    syscall1(SYS_EXIT, code);
    while(1) {} // На случай, если syscall не сработал
}

static inline int sys_write(int fd, const void* buf, uint32_t count) {
    return syscall3(SYS_WRITE, fd, (int)buf, count);
}

static inline int sys_read(int fd, void* buf, uint32_t count) {
    return syscall3(SYS_READ, fd, (int)buf, count);
}

static inline void sys_yield(void) {
    syscall0(SYS_YIELD);
}

static inline int sys_brk(uint32_t new_brk) {
    return syscall1(SYS_BRK, new_brk);
}

#endif // USER_SYSCALLS_H
