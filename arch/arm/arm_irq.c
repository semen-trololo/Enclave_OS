// ============================================================================
// arm_irq.c — BCM2835 Interrupt Controller + ARM Exception Dispatch
// ============================================================================
// Реализует hal_irq.h контракт для ARM (BCM2835, Raspberry Pi 1).
//
// ⚠️ ARM1176 (ARMv6): НЕ использовать __builtin_ctz / __builtin_clz.
//    GCC может сгенерировать rbit (ARMv7-only) → Undefined Instruction.
//    Используем software ctz32() — чистый C, без специальных инструкций.
// ============================================================================

#include <stdint.h>
#include "config.h"
#include "hal/hal_irq.h"
#include "hal/hal_cpu.h"
#include "hal/hal_uart.h"
#include "arm_trap.h"

// ============================================================================
// TASK FAULT API (arm_main.c)
// ============================================================================

extern void arm_task_fault_kill(uint32_t type,
                                uint32_t fault_pc,
                                uint32_t fault_cpsr,
                                uint32_t sp_usr,
                                uint32_t lr_usr)
                                __attribute__((noreturn));

// ============================================================================
// SOFTWARE CTZ (Count Trailing Zeros)
// ============================================================================
// Замена __builtin_ctz. Гарантированно НЕ генерирует rbit/clz.
// ARM1176 не имеет rbit (ARMv7). clz есть, но GCC может pairing rbit+clz.
// ============================================================================

static inline uint32_t ctz32(uint32_t x)
{
    uint32_t n = 0;
    if (!(x & 0x0000FFFFu)) { n += 16; x >>= 16; }
    if (!(x & 0x000000FFu)) { n +=  8; x >>=  8; }
    if (!(x & 0x0000000Fu)) { n +=  4; x >>=  4; }
    if (!(x & 0x00000003u)) { n +=  2; x >>=  2; }
    if (!(x & 0x00000001u)) { n +=  1; }
    return n;
}

// ============================================================================
// MMIO ACCESS (volatile, Higher-Half virtual addresses)
// ============================================================================
// Day 52 Zero Trust fix:
// Identity mapping для периферии (entries 512-527) НЕ копируется в user L1
// tables. Все MMIO-доступы из C-кода ядра ДОЛЖНЫ идти через BCM2835_VIRT,
// который транслирует физические адреса периферии в Higher-Half виртуальные
// адреса (0xE0000000+), замапленные в kernel space (entries 3584-3599).
// ============================================================================

static inline void mmio_write(uint32_t phys_addr, uint32_t val)
{
    *(volatile uint32_t *)BCM2835_VIRT(phys_addr) = val;
    hal_dsb();
}

static inline uint32_t mmio_read(uint32_t phys_addr)
{
    uint32_t val = *(volatile uint32_t *)BCM2835_VIRT(phys_addr);
    hal_dsb();
    return val;
}

// ============================================================================
// IRQ HANDLER TABLE
// ============================================================================

static hal_irq_handler_t irq_handlers[HAL_MAX_IRQ_LINES];

// ============================================================================
// EXCEPTION HANDLER TABLE
// ============================================================================

static hal_exception_handler_t exception_handlers[8];

// ============================================================================
// HAL IRQ INIT
// ============================================================================

void hal_irq_init(void)
{
    mmio_write(BCM2835_IRQ_DISABLE1,      0xFFFFFFFF);
    mmio_write(BCM2835_IRQ_DISABLE2,      0xFFFFFFFF);
    mmio_write(BCM2835_IRQ_DISABLE_BASIC, 0x000000FF);
    mmio_write(BCM2835_IRQ_FIQ_CTRL, 0);

    for (uint32_t i = 0; i < HAL_MAX_IRQ_LINES; i++) {
        irq_handlers[i] = (hal_irq_handler_t)0;
    }
    for (uint32_t i = 0; i < 8; i++) {
        exception_handlers[i] = (hal_exception_handler_t)0;
    }
}

// ============================================================================
// IRQ REGISTRATION
// ============================================================================

int hal_irq_register(uint32_t line, hal_irq_handler_t handler)
{
    if (line >= HAL_MAX_IRQ_LINES) return -1;
    if (!handler) return -1;
    irq_handlers[line] = handler;
    return 0;
}

// ============================================================================
// IRQ MASKING (per-line)
// ============================================================================

void hal_irq_enable_line(uint32_t line)
{
    if (line < 32) {
        mmio_write(BCM2835_IRQ_ENABLE1, (1u << line));
    } else if (line < 64) {
        mmio_write(BCM2835_IRQ_ENABLE2, (1u << (line - 32)));
    } else if (line < HAL_MAX_IRQ_LINES) {
        mmio_write(BCM2835_IRQ_ENABLE_BASIC, (1u << (line - HAL_IRQ_BASIC_BASE)));
    }
}

void hal_irq_disable_line(uint32_t line)
{
    if (line < 32) {
        mmio_write(BCM2835_IRQ_DISABLE1, (1u << line));
    } else if (line < 64) {
        mmio_write(BCM2835_IRQ_DISABLE2, (1u << (line - 32)));
    } else if (line < HAL_MAX_IRQ_LINES) {
        mmio_write(BCM2835_IRQ_DISABLE_BASIC, (1u << (line - HAL_IRQ_BASIC_BASE)));
    }
}

// ============================================================================
// EOI (BCM2835: no-op, handler clears source)
// ============================================================================

void hal_irq_eoi(uint32_t line)
{
    (void)line;
}

// ============================================================================
// IRQ DISPATCH (per-line)
// ============================================================================

void hal_irq_dispatch(uint32_t line, void* regs)
{
    if (line < HAL_MAX_IRQ_LINES && irq_handlers[line]) {
        irq_handlers[line](regs, line);
    }
}

// ============================================================================
// ARM IRQ ENTRY (вызывается из arm_vectors.S)
// ============================================================================

void arm_irq_entry(void* regs)
{
    uint32_t pend1 = mmio_read(BCM2835_IRQ_PEND1);
    uint32_t pend2 = mmio_read(BCM2835_IRQ_PEND2);
    uint32_t basic = mmio_read(BCM2835_IRQ_BASIC) & 0xFF;

    // GPU IRQs: PEND1 (lines 0-31)
    while (pend1) {
        uint32_t bit = ctz32(pend1);          // ← software, НЕ __builtin_ctz
        pend1 &= ~(1u << bit);
        hal_irq_dispatch(bit, regs);
    }

    // GPU IRQs: PEND2 (lines 32-63)
    while (pend2) {
        uint32_t bit = ctz32(pend2);          // ← software
        pend2 &= ~(1u << bit);
        hal_irq_dispatch(32 + bit, regs);
    }

    // Basic IRQs (lines 64-71)
    while (basic) {
        uint32_t bit = ctz32(basic);          // ← software
        basic &= ~(1u << bit);
        hal_irq_dispatch(HAL_IRQ_BASIC_BASE + bit, regs);
    }
}

// ============================================================================
// EXCEPTION REGISTRATION
// ============================================================================

int hal_exception_register(uint32_t type, hal_exception_handler_t handler)
{
    uint32_t idx = type / 4;
    if (idx >= 8) return -1;
    if (!handler) return -1;
    exception_handlers[idx] = handler;
    return 0;
}

// ============================================================================
// HELPER: hex print
// ============================================================================

static void hex8(uint32_t val, char* buf)
{
    for (int i = 7; i >= 0; i--) {
        buf[i] = "0123456789ABCDEF"[val & 0xF];
        val >>= 4;
    }
    buf[8] = '\0';
}

// ============================================================================
// FAULT NAME HELPER
// ============================================================================

static const char *fault_name(uint32_t type)
{
    switch (type) {
        case ARM_VECTOR_UNDEF: return "UNDEF";
        case ARM_VECTOR_PABT:  return "PABT";
        case ARM_VECTOR_DABT:  return "DABT";
        default:               return "EXC";
    }
}

// ============================================================================
// USER FAULT ENTRY (Day 47: fault isolation)
// ============================================================================
// Called from arm_vectors.S when UNDEF/PABT/DABT arrived from USR mode.
//
// Frame: struct arm_user_irq_frame (72 bytes)
//
// Policy:
//   - user fault is NOT a kernel bug
//   - kill current task
//   - keep kernel alive
// ============================================================================

__attribute__((noreturn))
void arm_user_fault_entry(struct arm_user_irq_frame *frame, uint32_t type)
{
    char buf[9];

    if (!frame) {
        hal_uart_puts("\r\n[FATAL] user fault with NULL frame\r\n");
        hal_irq_disable();
        for (;;) hal_halt();
    }

    // Sanity: vector code must have already detected USR mode.
    if ((frame->cpsr & 0x1F) != ARM_MODE_USR) {
        hal_uart_puts("\r\n[FATAL] user fault frame has non-user CPSR=0x");
        hex8(frame->cpsr, buf);
        hal_uart_puts(buf);
        hal_uart_puts("\r\n");

        hal_irq_disable();
        for (;;) hal_halt();
    }

    arm_task_fault_kill(type,
                        frame->pc,
                        frame->cpsr,
                        frame->sp_usr,
                        frame->lr_usr);

    // Should never reach here.
    hal_uart_puts("[FATAL] arm_task_fault_kill returned\r\n");
    hal_irq_disable();
    for (;;) hal_halt();
}

// ============================================================================
// KERNEL FAULT ENTRY (Day 47: fault isolation)
// ============================================================================
// Called from arm_vectors.S when UNDEF/PABT/DABT arrived from SVC/kernel mode.
//
// Frame: struct arm_trap_frame (64 bytes)
//
// Policy:
//   - kernel fault is a kernel bug
//   - fatal dump + halt
// ============================================================================

__attribute__((noreturn))
void arm_kernel_fault_entry(struct arm_trap_frame *frame, uint32_t type)
{
    char buf[9];

    hal_irq_disable();

    hal_uart_puts("\r\n[FATAL] Kernel ");
    hal_uart_puts(fault_name(type));
    hal_uart_puts(" exception: 0x");
    hex8(type, buf);
    hal_uart_puts(buf);
    hal_uart_puts("\r\n");

    // Optional registered low-level handler.
    // If it returns, we still treat kernel fault as fatal.
    uint32_t idx = type / 4;
    if (idx < 8 && exception_handlers[idx]) {
        exception_handlers[idx]((void *)frame, type);
    }

    if (frame) {
        hal_uart_puts("  r0=0x");  hex8(frame->r0, buf);  hal_uart_puts(buf);
        hal_uart_puts("  r1=0x");  hex8(frame->r1, buf);  hal_uart_puts(buf);
        hal_uart_puts("  r2=0x");  hex8(frame->r2, buf);  hal_uart_puts(buf);
        hal_uart_puts("\r\n");

        hal_uart_puts("  r3=0x");  hex8(frame->r3, buf);  hal_uart_puts(buf);
        hal_uart_puts("  r4=0x");  hex8(frame->r4, buf);  hal_uart_puts(buf);
        hal_uart_puts("  r5=0x");  hex8(frame->r5, buf);  hal_uart_puts(buf);
        hal_uart_puts("\r\n");

        hal_uart_puts("  r6=0x");  hex8(frame->r6, buf);  hal_uart_puts(buf);
        hal_uart_puts("  r7=0x");  hex8(frame->r7, buf);  hal_uart_puts(buf);
        hal_uart_puts("  r8=0x");  hex8(frame->r8, buf);  hal_uart_puts(buf);
        hal_uart_puts("\r\n");

        hal_uart_puts("  r9=0x");  hex8(frame->r9, buf);  hal_uart_puts(buf);
        hal_uart_puts("  r10=0x"); hex8(frame->r10, buf); hal_uart_puts(buf);
        hal_uart_puts("  r11=0x"); hex8(frame->r11, buf); hal_uart_puts(buf);
        hal_uart_puts("\r\n");

        hal_uart_puts("  r12=0x"); hex8(frame->r12, buf); hal_uart_puts(buf);
        hal_uart_puts("  lr=0x");  hex8(frame->lr, buf);  hal_uart_puts(buf);
        hal_uart_puts("\r\n");

        hal_uart_puts("  pc=0x");  hex8(frame->pc, buf);  hal_uart_puts(buf);
        hal_uart_puts("  cpsr=0x"); hex8(frame->cpsr, buf); hal_uart_puts(buf);
        hal_uart_puts("\r\n");
    } else {
        hal_uart_puts("  frame=NULL\r\n");
    }

    hal_uart_puts("[FATAL] System halted.\r\n");

    for (;;) hal_halt();
}