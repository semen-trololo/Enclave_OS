#ifndef SYSCALL_H
#define SYSCALL_H

#include "idt.h"
#include "kerrno.h"  // ✅ Подключаем POSIX errno
#include <stdint.h>

// ============================================================================
// POSIX mman.h CONSTANTS (Day 12)
// Эти константы нужны для sys_mmap, sys_munmap, sys_mprotect
// ============================================================================
#define PROT_READ       0x1
#define PROT_WRITE      0x2
#define PROT_EXEC       0x4
#define PROT_NONE       0x0

#define MAP_SHARED      0x01
#define MAP_PRIVATE     0x02
#define MAP_FIXED       0x10
#define MAP_ANONYMOUS   0x20

// ========================================================================
// Номера системных вызовов (Linux x86 ABI compatible)
// ========================================================================
#define SYS_EXIT      1
#define SYS_FORK      2
#define SYS_READ      3
#define SYS_WRITE     4
#define SYS_OPEN      5
#define SYS_CLOSE     6
#define SYS_WAITPID   7    // ✅ Зарезервировано для Дня 14
#define SYS_CREAT     8    // ✅ Зарезервировано
#define SYS_LINK      9
#define SYS_UNLINK    10
#define SYS_EXEC      11
#define SYS_CHDIR     12
#define SYS_TIME      13
#define SYS_MKNOD     14
#define SYS_CHMOD     15
#define SYS_BRK       45
#define SYS_MMAP      90
#define SYS_MUNMAP    91
#define SYS_GETPID    122  // ✅ Зарезервировано для Дня 14
#define SYS_MPROTECT  125
#define SYS_YIELD     158  // Custom: добровольный yield

// ========================================================================
// Инициализация таблицы системных вызовов
// ========================================================================
void syscall_init(void);

#endif // SYSCALL_H