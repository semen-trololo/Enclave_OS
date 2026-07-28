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
// MMIO ACCESS (volatile, no reorder)
// ============================================================================

static inline void mmio_write(uint32_t addr, uint32_t val)
{
    *(volatile uint32_t*)addr = val;
    hal_dsb();
}

static inline uint32_t mmio_read(uint32_t addr)
{
    uint32_t val = *(volatile uint32_t*)addr;
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
// ARM EXCEPTION ENTRY (вызывается из arm_vectors.S)
// ============================================================================
// [DIAG] Добавлен вывод LR (faulting address) для диагностики.
//
// Stack layout при входе (после srsdb + push в arm_vectors.S):
//
//   SP → r0
//        r1
//        ...
//        r12
//        LR_svc (saved by push {r0-r12, lr})
//        LR_exception (adjusted, from SRS)
//        SPSR_exception (from SRS)
//
// LR_exception = адрес инструкции, вызвавшей исключение.
// ============================================================================

void arm_exception_entry(void* regs, uint32_t type)
{
    uint32_t idx = type / 4;

    if (idx < 8 && exception_handlers[idx]) {
        exception_handlers[idx](regs, type);
        return;
    }

    // No handler — fatal
    char buf[9];
    uint32_t* r = (uint32_t*)regs;

    hal_uart_puts("\r\n[FATAL] Unhandled exception: 0x");
    hex8(type, buf);
    hal_uart_puts(buf);
    hal_uart_puts("\r\n");

    if (regs) {
        // r[0..12] = saved r0-r12
        hal_uart_puts("  r0=0x");  hex8(r[0], buf);  hal_uart_puts(buf);
        hal_uart_puts("  r1=0x");  hex8(r[1], buf);  hal_uart_puts(buf);
        hal_uart_puts("  r2=0x");  hex8(r[2], buf);  hal_uart_puts(buf);
        hal_uart_puts("\r\n");
        hal_uart_puts("  r3=0x");  hex8(r[3], buf);  hal_uart_puts(buf);
        hal_uart_puts("  r4=0x");  hex8(r[4], buf);  hal_uart_puts(buf);
        hal_uart_puts("  r5=0x");  hex8(r[5], buf);  hal_uart_puts(buf);
        hal_uart_puts("\r\n");
        hal_uart_puts("  r6=0x");  hex8(r[6], buf);  hal_uart_puts(buf);
        hal_uart_puts("  r7=0x");  hex8(r[7], buf);  hal_uart_puts(buf);
        hal_uart_puts("  r8=0x");  hex8(r[8], buf);  hal_uart_puts(buf);
        hal_uart_puts("\r\n");
        hal_uart_puts("  r9=0x");  hex8(r[9], buf);  hal_uart_puts(buf);
        hal_uart_puts("  r10=0x"); hex8(r[10], buf); hal_uart_puts(buf);
        hal_uart_puts("  r11=0x"); hex8(r[11], buf); hal_uart_puts(buf);
        hal_uart_puts("\r\n");
        hal_uart_puts("  r12=0x"); hex8(r[12], buf); hal_uart_puts(buf);

        // r[13] = LR_svc (from push {r0-r12, lr})
        // r[14] = LR_exception (adjusted return address, from SRS)
        // r[15] = SPSR_exception (from SRS)
        hal_uart_puts("  lr_svc=0x"); hex8(r[13], buf); hal_uart_puts(buf);
        hal_uart_puts("\r\n");
        hal_uart_puts("  fault_addr=0x"); hex8(r[14], buf); hal_uart_puts(buf);
        hal_uart_puts("  spsr=0x"); hex8(r[15], buf); hal_uart_puts(buf);
        hal_uart_puts("\r\n");
    }

    hal_uart_puts("[FATAL] System halted.\r\n");

    hal_irq_disable();
    for (;;) {
        hal_halt();
    }
}