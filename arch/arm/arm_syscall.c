// ============================================================================
// arm_syscall.c — ARM Syscall Dispatcher (Days 43-45)
// ============================================================================
// ARM syscall ABI:
//
//   r7    = syscall number
//   r0-r6 = arguments
//   r0    = return value
//
// Syscall numbers are Enclave/x86 frozen numbers.
// Only transport changes: svc #0 instead of int 0x80.
// ============================================================================

#include <stdint.h>
#include "config.h"
#include "hal/hal_uart.h"
#include "arm_trap.h"
#include "kerrno.h"

#ifndef EBADF
#define EBADF   9
#endif

#ifndef EFAULT
#define EFAULT  14
#endif

#ifndef EINVAL
#define EINVAL  22
#endif

#ifndef ENOSYS
#define ENOSYS  38
#endif

// ============================================================================
// SYSCALL NUMBERS (Frozen Enclave/x86 ABI)
// ============================================================================

#define SYS_exit        1
#define SYS_write       4
#define SYS_getpid      122
#define SYS_yield       158
#define SYS_sleep       230

// ============================================================================
// TASK INTERFACE (arm_main.c)
// ============================================================================

extern int  arm_current_pid(void);
extern void arm_task_yield(void);
extern void arm_task_exit(void);
extern void arm_task_set_sleep(uint32_t ms);

// ============================================================================
// HELPERS
// ============================================================================

static void uart_u32(uint32_t value)
{
    char buf[12];
    int i = 11;

    buf[i] = '\0';

    if (value == 0) {
        hal_uart_putc('0');
        return;
    }

    while (value > 0 && i > 0) {
        buf[--i] = (char)('0' + (value % 10));
        value /= 10;
    }

    hal_uart_puts(&buf[i]);
}

// ============================================================================
// ZERO TRUST VALIDATION
// ============================================================================
// Spike policy:
//   For sys_write we only accept buffers inside explicitly mapped user
//   regions. This is stricter than generic < KERNEL_SPACE_START and keeps
//   the kernel from proxying arbitrary kernel-readable memory.
// ============================================================================

static int is_user_buffer(uint32_t addr, uint32_t len)
{
    uint32_t end;

    if (len == 0)
        return 1;

    end = addr + len;

    // Overflow.
    if (end < addr)
        return 0;

    // Hard kernel boundary.
    if (addr >= KERNEL_SPACE_START)
        return 0;

    // User data region.
    if (addr >= ARM_USER_DATA_VADDR &&
        end <= ARM_USER_DATA_VADDR + ARM_USER_DATA_SIZE)
        return 1;

    // User code region is also readable.
    // Useful if user code embeds read-only strings.
    if (addr >= ARM_USER_CODE_VADDR &&
        end <= ARM_USER_CODE_VADDR + ARM_USER_CODE_SIZE)
        return 1;

    return 0;
}

// ============================================================================
// SYSCALL HANDLERS
// ============================================================================

static int32_t sys_exit_handler(uint32_t status)
{
    hal_uart_puts("[SYS] exit(status=");
    uart_u32(status);
    hal_uart_puts(")\r\n");

    // May never return if scheduler switches away.
    arm_task_exit();

    return 0;
}

static int32_t sys_write_handler(uint32_t fd, uint32_t buf, uint32_t len)
{
    const char *ptr;
    uint32_t i;

    if (fd != 1 && fd != 2)
        return -EBADF;

    if (len > 4096)
        return -EINVAL;

    if (!is_user_buffer(buf, len))
        return -EFAULT;

    ptr = (const char *)buf;

    for (i = 0; i < len; i++)
        hal_uart_putc(ptr[i]);

    return (int32_t)len;
}

static int32_t sys_getpid_handler(void)
{
    return (int32_t)arm_current_pid();
}

static int32_t sys_yield_handler(void)
{
    arm_task_yield();
    return 0;
}

static int32_t sys_sleep_handler(uint32_t ms)
{
    hal_uart_puts("[SYS] sleep(ms=");
    uart_u32(ms);
    hal_uart_puts(")\r\n");

    if (ms > 0x7FFFFFFF)
        return -EINVAL;

    arm_task_set_sleep(ms);
    return 0;
}

// ============================================================================
// SYSCALL ENTRY
// ============================================================================

void arm_syscall_entry(arm_user_frame_t *frame)
{
    uint32_t num = frame->r7;
    int32_t ret;

    switch (num) {
        case SYS_exit:
            ret = sys_exit_handler(frame->r0);
            break;

        case SYS_write:
            ret = sys_write_handler(frame->r0, frame->r1, frame->r2);
            break;

        case SYS_getpid:
            ret = sys_getpid_handler();
            break;

        case SYS_yield:
            ret = sys_yield_handler();
            break;

        case SYS_sleep:
            ret = sys_sleep_handler(frame->r0);
            break;

        default:
            hal_uart_puts("[SYS] invalid syscall ");
            uart_u32(num);
            hal_uart_puts("\r\n");
            ret = -ENOSYS;
            break;
    }

    frame->r0 = (uint32_t)ret;
}