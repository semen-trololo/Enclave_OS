// ============================================================================
// arm_main.c — Enclave OS ARM Kernel (Days 43-45: SVC + User Mode)
//               Day 52: Per-Process 4KB Isolation
// ============================================================================
// Tasks:
//   PID 0: idle       (kernel, boot stack, boot TTBR0)
//   PID 1: user_a     (user mode, isolated 4KB address space)
//   PID 2: user_b     (user mode, isolated 4KB address space)
//
// This spike:
//   - enters user mode through svc-return trampoline
//   - uses svc #0 as syscall transport
//   - integrates user tasks with existing ARM scheduler
//   - keeps user IRQ disabled (CPSR.I = 1)
//   - each user task has isolated address space (Day 52)
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
#include "hal/hal_mmu.h"
#include "arm_pmm.h"
#include "arm_vmm.h"
#include "arm_fb.h"


// ============================================================================
// EXTERNAL MODULES
// ============================================================================

extern void arm_context_switch(uint32_t *old_sp, uint32_t new_sp, uint32_t new_ttbr0);
extern uint32_t arm_user_create_space_and_load_image(void);
extern void arm_user_trampoline(void);

// ============================================================================
// TASK STRUCT
// ============================================================================

#define MAX_TASKS       4

#define TASK_FREE       0
#define TASK_READY      1
#define TASK_RUNNING    2

#define TASK_SLEEPING   3

typedef struct {
    uint32_t    sp;             // Saved kernel stack pointer
    uint32_t    ttbr0_phys;     // Physical address of L1 table (for TTBR0)
    uint32_t    *space_virt;    // Virtual address of L1 table (for VMM API)
    int         pid;            // Task PID
    const char *name;           // Debug name
    int         state;          // TASK_FREE / TASK_READY / TASK_RUNNING / TASK_SLEEPING
    uint64_t    wakeup_tick;    // Absolute tick for TASK_SLEEPING
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

// Global variables to pass entry point and stack from ELF loader
uint32_t g_user_entry_point = 0;
uint32_t g_user_stack_top = 0;

// Модификация функции arm_task_create_user() — теперь принимает entry_point и user_sp как параметры:

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

// В arm_kernel_main(), заменить секцию создания задач:

    // PID 1: user_a (Isolated Enclave)
    uint32_t space_a = arm_user_create_space_and_load_image();
    if (space_a == 0) {
        hal_uart_puts("[FATAL] Failed to load ELF for user_a\r\n");
        for (;;) hal_halt();
    }
    tasks[1].ttbr0_phys = space_a;
    tasks[1].space_virt = (uint32_t *)PHYS_TO_VIRT(space_a);
    
    arm_task_create_user(1,
                         1,
                         "user_a",
                         g_user_entry_point,
                         g_user_stack_top,
                         user_a_kstack,
                         TASK_STACK_SIZE);

    // PID 2: user_b (Isolated Enclave)
    g_user_entry_point = 0;
    g_user_stack_top = 0;
    
    uint32_t space_b = arm_user_create_space_and_load_image();
    if (space_b == 0) {
        hal_uart_puts("[FATAL] Failed to load ELF for user_b\r\n");
        for (;;) hal_halt();
    }
    tasks[2].ttbr0_phys = space_b;
    tasks[2].space_virt = (uint32_t *)PHYS_TO_VIRT(space_b);

    arm_task_create_user(2,
                         2,
                         "user_b",
                         g_user_entry_point,
                         g_user_stack_top,
                         user_b_kstack,
                         TASK_STACK_SIZE);
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

static void uart_hex(uint32_t val)
{
    char buf[9];

    for (int i = 7; i >= 0; i--) {
        buf[i] = "0123456789ABCDEF"[val & 0xF];
        val >>= 4;
    }

    buf[8] = '\0';
    hal_uart_puts(buf);
}

static const char *fault_reason(uint32_t type)
{
    switch (type) {
        case 0x04: return "UNDEF";
        case 0x08: return "SVC";
        case 0x0C: return "PABT";
        case 0x10: return "DABT";
        default:   return "FAULT";
    }
}

void schedule(void)
{
    int next = -1;

    // Current task becomes READY only if it is still alive and not sleeping.
    if (tasks[current_task].state == TASK_RUNNING)
        tasks[current_task].state = TASK_READY;

    // Round-robin search.
    for (int i = 0; i < num_tasks; i++) {
        int candidate = (current_task + 1 + i) % num_tasks;

        // Skip sleeping tasks.
        if (tasks[candidate].state == TASK_SLEEPING)
            continue;

        if (tasks[candidate].state == TASK_READY) {
            next = candidate;
            break;
        }
    }

    // No READY task found.
    if (next == -1) {
        // If a non-idle task died, force idle as fallback.
        if (current_task != 0 && tasks[0].state != TASK_FREE && tasks[0].state != TASK_SLEEPING) {
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

    // ------------------------------------------------------------------
    // Day 47 guard: never switch to a task with uninitialized SP.
    //
    // idle SP is created dynamically by the first timer IRQ context
    // switch. If it is still zero while we are trying to switch to it,
    // something is seriously wrong.
    // ------------------------------------------------------------------
    if (tasks[next].sp == 0) {
        hal_uart_puts("[FATAL] schedule: target sp == 0 pid=");
        uart_dec((uint32_t)next);
        hal_uart_puts("\r\n");

        hal_irq_disable();
        for (;;) hal_halt();
    }

    // Switch.
    tasks[next].state = TASK_RUNNING;

    int prev = current_task;
    current_task = next;

    arm_context_switch(&tasks[prev].sp, tasks[next].sp, tasks[next].ttbr0_phys);
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
// TASK SLEEP API (Day 49: Blocking Syscalls)
// ============================================================================

extern uint64_t hal_timer_get_ticks(void);

void arm_task_set_sleep(uint32_t ms)
{
    if (ms == 0)
        return;

    // Overflow protection: reject absurdly large sleep times.
    if (ms > 0x7FFFFFFF) {
        hal_uart_puts("[WARN] sys_sleep: ms too large, rejecting\r\n");
        return;
    }

    uint64_t current = hal_timer_get_ticks();
    tasks[current_task].wakeup_tick = current + (uint64_t)ms;
    tasks[current_task].state = TASK_SLEEPING;

    // Yield CPU. Scheduler will skip TASK_SLEEPING tasks.
    schedule();
}

void arm_task_check_wakeup(uint64_t current_tick)
{
    for (int i = 0; i < num_tasks; i++) {
        if (tasks[i].state == TASK_SLEEPING && current_tick >= tasks[i].wakeup_tick) {
            tasks[i].state = TASK_READY;
        }
    }
}
// ============================================================================
// FAULT KILL (Day 47: user fault isolation)
// ============================================================================
// Called from arm_user_fault_entry() when a user-mode exception occurs.
//
// Policy:
//   - print diagnostic to UART
//   - mark current task TASK_FREE
//   - ensure idle is runnable
//   - schedule()
//   - never return
//
// PID 0 is immortal. If PID 0 somehow reaches this path, halt.
// ============================================================================

__attribute__((noreturn))
void arm_task_fault_kill(uint32_t type,
                         uint32_t fault_pc,
                         uint32_t fault_cpsr,
                         uint32_t sp_usr,
                         uint32_t lr_usr)
{
    hal_irq_disable();

    // Defensive: fault kill must never be applied to idle.
    if (current_task <= 0 || current_task >= num_tasks) {
        hal_uart_puts("[FATAL] fault kill with invalid current_task\r\n");
        for (;;) hal_halt();
    }

    if (current_task == 0) {
        hal_uart_puts("[FATAL] fault in PID 0\r\n");
        for (;;) hal_halt();
    }

    hal_uart_puts("[FAULT] user ");
    hal_uart_puts(fault_reason(type));
    hal_uart_puts(" pid=");
    uart_dec((uint32_t)current_task);
    hal_uart_puts(" name=");
    hal_uart_puts(tasks[current_task].name ? tasks[current_task].name : "?");
    hal_uart_puts(" pc=0x");
    uart_hex(fault_pc);
    hal_uart_puts(" cpsr=0x");
    uart_hex(fault_cpsr);
    hal_uart_puts(" sp_usr=0x");
    uart_hex(sp_usr);
    hal_uart_puts(" lr_usr=0x");
    uart_hex(lr_usr);
    hal_uart_puts("\r\n");

    // Kill current task.
    tasks[current_task].state = TASK_FREE;

    // Ensure idle is runnable as fallback.
    if (tasks[0].state != TASK_RUNNING)
        tasks[0].state = TASK_READY;

    // Switch away. Must be called with IRQ disabled.
    schedule();

    // Should never reach here.
    hal_uart_puts("[FATAL] schedule returned after fault kill\r\n");
    for (;;) hal_halt();
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
    (void)machine_type;

    // ------------------------------------------------------------------
    // 1. UART
    // ------------------------------------------------------------------

    hal_uart_init(115200);

    hal_uart_puts("\r\n");
    hal_uart_puts("========================================\r\n");
    hal_uart_puts("  Enclave OS — ARM Port\r\n");
    hal_uart_puts("  BCM2835 / ARM1176JZF-S / ARMv6\r\n");
    hal_uart_puts("  Alpha 0.6-arm-vmm\r\n");
    hal_uart_puts("========================================\r\n");
    hal_uart_puts("\r\n");
    // ------------------------------------------------------------------
    // 1.5 PMM (Day 51B: ARM Physical Memory Manager)
    // ------------------------------------------------------------------
    // ВАЖНО: PMM должен быть инициализирован ДО создания user spaces,
    // потому что user memory теперь аллоцируется динамически через PMM.
    // ------------------------------------------------------------------

    arm_pmm_init(atags_addr, BCM2835_RAM_SIZE_DEFAULT);

    // Резервирование статических regions:
    // 1. Lower 64 KB (ARM exception vectors area)
    arm_pmm_reserve_range(0x00000000, 0x00010000);

    // 2. Kernel image (.text, .rodata, .data, .bss)
    extern uint32_t _kernel_start;
    extern uint32_t _kernel_end;
    uint32_t kernel_phys_start = 0x00010000;
    uint32_t kernel_size = (uint32_t)&_kernel_end - (uint32_t)&_kernel_start;
    arm_pmm_reserve_range(kernel_phys_start, kernel_size);

    arm_pmm_check_balance();

    // ------------------------------------------------------------------
    // 1.6 VMM (Day 52: ARM Virtual Memory Manager)
    // ------------------------------------------------------------------
    hal_mmu_init();
    
    // Zero Trust: Tear down identity mapping now that we are in Higher Half.
    arm_vmm_teardown_identity();

        hal_mmu_init();
    
    // Zero Trust: Tear down identity mapping now that we are in Higher Half.
    arm_vmm_teardown_identity();

    // ------------------------------------------------------------------
    // 1.7 Framebuffer (Day 55 video spike)
    // ------------------------------------------------------------------
    // ВАЖНО:
    //   arm_fb_init() вызывается ДО создания user address spaces,
    //   чтобы framebuffer mapping в boot TTBR0 был скопирован
    //   во все будущие user L1 tables через hal_mmu_create_space().
    // ------------------------------------------------------------------

    if (arm_fb_init() == 0) {
        arm_fb_test_pattern();
    } else {
        hal_uart_puts("[FB] framebuffer unavailable, continuing UART-only\r\n");
    }

    // ------------------------------------------------------------------
    // 2. IRQ + Timer init

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
    // 3. Create tasks (Per-Process Isolation)
    // ------------------------------------------------------------------

    hal_uart_puts("[INIT] Creating isolated tasks...\r\n");

    // PID 0: idle, uses current boot SVC stack and boot TTBR0.
    tasks[0].sp         = 0;
    tasks[0].ttbr0_phys = arm_vmm_get_boot_ttbr0();
    tasks[0].space_virt = (uint32_t *)0;
    tasks[0].pid        = 0;
    tasks[0].name       = "idle";
    tasks[0].state      = TASK_RUNNING;

    current_task = 0;
    num_tasks = 3;

    // PID 1: user_a (Isolated Enclave)
    uint32_t space_a = arm_user_create_space_and_load_image();
    tasks[1].ttbr0_phys = space_a;
    tasks[1].space_virt = (uint32_t *)PHYS_TO_VIRT(space_a);
    
    arm_task_create_user(1,
                         1,
                         "user_a",
                         ARM_USER_CODE_VA_4K,
                         ARM_USER_STACK_VA_4K,
                         user_a_kstack,
                         TASK_STACK_SIZE);

    // PID 2: user_b (Isolated Enclave)
    uint32_t space_b = arm_user_create_space_and_load_image();
    tasks[2].ttbr0_phys = space_b;
    tasks[2].space_virt = (uint32_t *)PHYS_TO_VIRT(space_b);

    arm_task_create_user(2,
                         2,
                         "user_b",
                         ARM_USER_CODE_VA_4K,
                         ARM_USER_STACK_VA_4K,
                         user_b_kstack,
                         TASK_STACK_SIZE);

    hal_uart_puts("[OK]   PID 0: idle (boot TTBR0)\r\n");
    hal_uart_puts("[OK]   PID 1: user_a (isolated 4KB space)\r\n");
    hal_uart_puts("[OK]   PID 2: user_b (isolated 4KB space)\r\n");
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
    //   - user memory (per-process isolated)
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
    // User tasks exit through sys_exit or UNDEF (crash test).
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
        __asm__ volatile ("cpsie i; mov r0, r0" ::: "memory");
    }
}
