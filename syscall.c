#include "syscall.h"
#include "idt.h"
#include "isr.h"
#include "klib.h"
#include "vfs.h"   // Маршрутизация в VFS
#include "task.h"  // Для task_exit()
#include "serial.h"
#include <stdint.h>
#include <stdbool.h>

typedef int (*syscall_func_t)(struct regs* r);

#define MAX_SYSCALLS 256
static syscall_func_t syscall_table[MAX_SYSCALLS];

// ========================================================================
// 🛡 USER POINTER VALIDATION (Защита от CVE)
// ========================================================================
#define USER_SPACE_END 0xC0000000 // KERNEL_VIRT_BASE

static inline bool is_user_pointer(const void* ptr, size_t size) {
    uint32_t addr = (uint32_t)ptr;
    // 1. Указатель не должен смотреть в Kernel Space
    if (addr >= USER_SPACE_END) return false;
    // 2. Размер не должен быть абсурдным
    if (size > USER_SPACE_END) return false;
    // 3. Защита от переполнения (wrap-around) при сложении
    if (addr + size > USER_SPACE_END) return false; 
    return true;
}

// ========================================================================
// Реализации системных вызовов
// ========================================================================

static int sys_exit_handler(struct regs* r) {
    uint32_t exit_code = r->ebx;
    serial_printf("[SYSCALL] PID %d exiting with code %u\n", current_task->pid, exit_code);

    // 🛡️ ИСПРАВЛЕНО: Убираем Context Hijacking.
    // task_exit() корректно закроет FD, пометит задачу как DEAD, 
    // и передаст управление планировщику (Reaper освободит память).
    task_exit(); 
    
    // Недостижимо, так как task_exit() делает schedule() и не возвращается
    return 0;
}

static int sys_write_handler(struct regs* r) {
    int fd = (int)r->ebx;
    const void* buf = (const void*)r->ecx;
    uint32_t count = r->edx;

    if (!is_user_pointer(buf, count)) {
        serial_printf("[SYSCALL] FATAL: Ring 3 attempted kernel write! Addr: 0x%x\n", (uint32_t)buf);
        return -1; // EFAULT
    }

    // Делегируем работу в VFS (используется fd_table процесса)
    return sys_write(fd, buf, count);
}

static int sys_read_handler(struct regs* r) {
    int fd = (int)r->ebx;
    void* buf = (void*)r->ecx;
    uint32_t count = r->edx;

    if (!is_user_pointer(buf, count)) {
        serial_printf("[SYSCALL] FATAL: Ring 3 attempted kernel read! Addr: 0x%x\n", (uint32_t)buf);
        return -1; // EFAULT
    }

    return sys_read(fd, buf, count);
}

static int sys_yield_handler(struct regs* r) {
    (void)r;
    extern void schedule(void);
    schedule(); 
    return 0;
}

// ========================================================================
// Диспетчер системных вызовов
// ========================================================================
static void syscall_dispatcher(struct regs* r) {
    uint32_t syscall_num = r->eax;

    if (syscall_num >= MAX_SYSCALLS || syscall_table[syscall_num] == 0) {
        serial_printf("[SYSCALL] Invalid syscall number: %u\n", syscall_num);
        r->eax = (uint32_t)-1; 
        return;
    }

    r->eax = (uint32_t)syscall_table[syscall_num](r);
}

// ========================================================================
// Инициализация
// ========================================================================
void syscall_init(void) {
    for (int i = 0; i < MAX_SYSCALLS; i++) syscall_table[i] = 0;

    syscall_table[SYS_EXIT]  = sys_exit_handler;
    syscall_table[SYS_READ]  = sys_read_handler;
    syscall_table[SYS_WRITE] = sys_write_handler;
    syscall_table[SYS_YIELD] = sys_yield_handler;

    extern void isr128(); 
    idt_set_gate(128, (uint32_t)isr128, 0x08, 0xEE); // DPL=3 для Ring 3

    isr_register_handler(128, syscall_dispatcher);
    serial_print("[SYSCALL] INT 0x80 dispatcher initialized.\n");
}
