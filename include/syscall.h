#ifndef SYSCALL_H
#define SYSCALL_H

#include "idt.h"

// Номера системных вызовов (Linux x86 ABI)
#define SYS_EXIT    1
#define SYS_FORK    2
#define SYS_READ    3
#define SYS_WRITE   4
#define SYS_OPEN    5  // ✅ ДОБАВЛЕНО
#define SYS_CLOSE   6  // ✅ ДОБАВЛЕНО
#define SYS_UNLINK  10  // ✅ ДОБАВЛЕНО
#define SYS_YIELD   24
#define SYS_BRK 45 // Номер системного вызова brk (как в Linux)
#define SYS_EXEC 11 // Номер системного вызова exec

// Инициализация таблицы системных вызовов и регистрация в IDT
void syscall_init(void);

#endif
