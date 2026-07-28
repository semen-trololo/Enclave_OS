// ============================================================================
// arm_main.c — Enclave OS ARM Kernel (Days 43-45: SVC + User Mode)
// ============================================================================
// Tasks:
//   PID 0: idle       (kernel, boot stack)
//   PID 1: user_a     (user mode, cooperative)
//   PID 2: user_b     (user mode, cooperative)
//
// This spike:
//   - enters user mode through svc-return trampoline
//   - uses svc #0 as syscall transport
//   - integrates user tasks with existing ARM scheduler
//   - keeps user IRQ disabled (CPSR.I = 1)
//
// True IRQ preemption from user mode comes later, when IRQ stub saves
// a full user trap frame.
// ============================================================================

#include <stdint.h>
#include "config.h"
#include "hal/hal_uart.h"
#include "hal/hal_cpu.h"
#include "hal/hal_irq.h"
#include "hal/hal_timer.h"

// ============================================================================
// EXTERNAL MODULES
// ============================================================================

extern void arm_context_switch(uint32_t *old_sp, uint32_t new_sp);
extern void arm_user_setup(void);
extern void arm_user_trampoline(void);

// ============================================================================
// TASK STRUCT
// ============================================================================

#define MAX_TASKS       4

#define TASK_FREE       0
#define TASK_READY      1
#define TASK_RUNNING    2

typedef struct {
    uint32_t    sp;         // Saved kernel stack pointer
    int         pid;        // Task PID
    const char *name;       // Debug name
    int         state;      // TASK_FREE / TASK_READY / TASK_RUNNING
} arm_task_t;

// ============================================================================
// SCHEDULER STATE
// ============================================================================

static arm_task_t tasks[MAX_TASKS];
static int current_task = 0;
static int num_tasks = 0;

// ============================================================================
// KERNEL STACKS FOR USER TASKS
// ============================================================================
// Each user task has:
//   - user stack in user data section
//   - kernel stack for SVC/IRQ entry and scheduler context
// ============================================================================

#define TASK_STACK_SIZE 4096

static uint32_t user_a_kstack[TASK_STACK_SIZE / 4] __attribute__((aligned(8)));
static uint32_t user_b_kstack[TASK_STACK_SIZE / 4] __attribute__((aligned(8)));

// ============================================================================
// HELPERS
// ============================================================================

static void uart_dec(uint32_t val)
{
    char buf[12];
    int i = 11;

    buf[i] = '\0';

    if (val == 0) {
        hal_uart_puts("0");
        return;
    }

    while (val > 0 && i > 0) {
        buf[--i] = (char)('0' + (val % 10));
        val /= 10;
    }

    hal_uart_puts(&buf[i]);
}

// ============================================================================
// SCHEDULER
// ============================================================================
// Called from:
//   - timer IRQ (preemptive kernel scheduling)
//   - sys_yield / sys_exit (cooperative user scheduling)
//
// Must be called with IRQ disabled.
// ============================================================================

void schedule(void)
{
    int next = -1;

    // Current task becomes READY only if it is still alive.
    if (tasks[current_task].state == TASK_RUNNING)
        tasks[current_task].state = TASK_READY;

    // Round-robin search.
    for (int i = 0; i < num_tasks; i++) {
        int candidate = (current_task + 1 + i) % num_tasks;

        if (tasks[candidate].state == TASK_READY) {
            next = candidate;
            break;
        }
    }

    // No READY task found.
    if (next == -1) {
        // If a non-idle task died, force idle as fallback.
        if (current_task != 0 && tasks[0].state != TASK_FREE) {
            tasks[0].state = TASK_READY;
            next = 0;
        } else if (tasks[current_task].state == TASK_READY) {
            tasks[current_task].state = TASK_RUNNING;
            return;
        } else {
            return;
        }
    }

    // Only current task is runnable.
    if (next == current_task) {
        tasks[next].state = TASK_RUNNING;
        return;
    }

    // Switch.
    tasks[next].state = TASK_RUNNING;

    int prev = current_task;
    current_task = next;

    arm_context_switch(&tasks[prev].sp, tasks[next].sp);
}

// ============================================================================
// TASK CONTROL API (used by arm_syscall.c)
// ============================================================================
// arm_syscall.c must not touch tasks[] or current_task directly.
// Scheduler internals remain private to arm_main.c.
// ============================================================================

int arm_current_pid(void)
{
    return current_task;
}

void arm_task_yield(void)
{
    schedule();
}

void arm_task_exit(void)
{
    // PID 0 is immortal idle. It must not exit.
    if (current_task == 0)
        return;

    hal_uart_puts("[KERN] task exit pid=");
    uart_dec((uint32_t)current_task);
    hal_uart_puts("\r\n");

    tasks[current_task].state = TASK_FREE;

    // Make sure idle can be selected as fallback.
    if (tasks[0].state != TASK_RUNNING)
        tasks[0].state = TASK_READY;

    schedule();
}

// ============================================================================
// USER TASK CREATE
// ============================================================================
// Forged kernel stack matches arm_context_switch():
//
//   push {r4-r12, lr}
//
// Layout:
//
//   stack_top
//   ┌────────────────────────────┐
//   │ lr  = arm_user_trampoline  │
//   │ r12 = CPSR (SVC, I=1)      │
//   │ r11 = 0                    │
//   │ r10 = 0                    │
//   │ r9  = 0                    │
//   │ r8  = 0                    │
//   │ r7  = 0                    │
//   │ r6  = kernel_stack_top     │
//   │ r5  = user_stack_top       │
//   │ r4  = user_pc              │
//   └────────────────────────────┘
//
// 10 words = 40 bytes, 8-byte aligned.
// ============================================================================

static void arm_task_create_user(int id,
                                 int pid,
                                 const char *name,
                                 uint32_t user_pc,
                                 uint32_t user_sp,
                                 uint32_t *kstack_base,
                                 uint32_t kstack_bytes)
{
    uint32_t stack_top = (uint32_t)kstack_base + kstack_bytes;
    stack_top &= ~7u;

    uint32_t *sp = (uint32_t *)stack_top;

    *(--sp) = (uint32_t)arm_user_trampoline;        // lr
    *(--sp) = ARM_CPSR_SVC_IRQ_DISABLED;            // r12 = CPSR
    *(--sp) = 0;                                    // r11
    *(--sp) = 0;                                    // r10
    *(--sp) = 0;                                    // r9
    *(--sp) = 0;                                    // r8
    *(--sp) = 0;                                    // r7
    *(--sp) = stack_top;                            // r6 = kernel_stack_top
    *(--sp) = user_sp;                              // r5 = user_stack_top
    *(--sp) = user_pc;                              // r4 = user_pc

    tasks[id].sp    = (uint32_t)sp;
    tasks[id].pid   = pid;
    tasks[id].name  = name;
    tasks[id].state = TASK_READY;
}

// ============================================================================
// KERNEL MAIN
// ============================================================================

void arm_kernel_main(uint32_t atags_addr, uint32_t machine_type)
{
    (void)atags_addr;
    (void)machine_type;

    // ------------------------------------------------------------------
    // 1. UART
    // ------------------------------------------------------------------

    hal_uart_init(115200);

    hal_uart_puts("\r\n");
    hal_uart_puts("========================================\r\n");
    hal_uart_puts("  Enclave OS — ARM Port\r\n");
    hal_uart_puts("  BCM2835 / ARM1176JZF-S / ARMv6\r\n");
    hal_uart_puts("  Alpha 0.6-arm-user\r\n");
    hal_uart_puts("========================================\r\n");
    hal_uart_puts("\r\n");

    // ------------------------------------------------------------------
    // 2. IRQ + Timer init
    // ------------------------------------------------------------------
    // Global IRQ are NOT enabled yet.
    // User mode spike runs cooperatively first.
    // ------------------------------------------------------------------

    hal_uart_puts("[INIT] hal_irq_init()...\r\n");
    hal_irq_init();

    hal_uart_puts("[INIT] hal_timer_init(1000)...\r\n");
    hal_timer_init(1000);

    // ------------------------------------------------------------------
    // 3. User memory + user image
    // ------------------------------------------------------------------

    hal_uart_puts("[INIT] ARM user memory setup...\r\n");
    arm_user_setup();

    // ------------------------------------------------------------------
    // 4. Create tasks
    // ------------------------------------------------------------------

    hal_uart_puts("[INIT] Creating tasks...\r\n");

    // PID 0: idle, uses current boot SVC stack.
    tasks[0].sp    = 0;
    tasks[0].pid   = 0;
    tasks[0].name  = "idle";
    tasks[0].state = TASK_RUNNING;

    current_task = 0;
    num_tasks = 3;

    // PID 1: user_a
    arm_task_create_user(1,
                         1,
                         "user_a",
                         ARM_USER_CODE_VADDR,
                         ARM_USER_STACK_A_TOP,
                         user_a_kstack,
                         TASK_STACK_SIZE);

    // PID 2: user_b
    arm_task_create_user(2,
                         2,
                         "user_b",
                         ARM_USER_CODE_VADDR,
                         ARM_USER_STACK_B_TOP,
                         user_b_kstack,
                         TASK_STACK_SIZE);

    hal_uart_puts("[OK]   PID 0: idle\r\n");
    hal_uart_puts("[OK]   PID 1: user_a\r\n");
    hal_uart_puts("[OK]   PID 2: user_b\r\n");
    hal_uart_puts("\r\n");

    // ------------------------------------------------------------------
    // 5. Enable IRQ
    // ------------------------------------------------------------------
    // Day 46:
    //   User tasks now run with CPSR.I = 0.
    //   Timer IRQ can preempt user mode directly.
    //
    // All required subsystems are already initialized:
    //   - UART
    //   - IRQ controller
    //   - timer
    //   - user memory
    //   - scheduler tasks
    //
    // Therefore it is safe to enable global IRQ before entering idle.
    // ------------------------------------------------------------------

    hal_uart_puts("[INIT] Enabling IRQ...\r\n");
    hal_irq_enable();
    hal_uart_puts("[OK]   Preemptive scheduler active.\r\n");
    hal_uart_puts("\r\n");

    // ------------------------------------------------------------------
    // 6. Preemptive user-mode pass
    // ------------------------------------------------------------------
    // Timer IRQ calls:
    //   timer_irq_handler()
    //   hal_timer_tick()
    //   schedule()
    //
    // The first timer quantum will switch from idle to user_a.
    // User tasks exit through sys_exit.
    // ------------------------------------------------------------------

    hal_uart_puts("[TEST] Running user tasks preemptively...\r\n");
    hal_uart_puts("\r\n");

    // ------------------------------------------------------------------
    // 7. Idle loop
    // ------------------------------------------------------------------
    // Do NOT manually call schedule() here.
    //
    // Manual schedule() with IRQ enabled can race with timer-driven
    // schedule(). The timer IRQ path is the only preemptive scheduler
    // entry point now.
    // ------------------------------------------------------------------

    for (;;) {
        __asm__ volatile ("mov r0, r0" ::: "memory");
    }
}