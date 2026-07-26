#ifndef HAL_IRQ_H
#define HAL_IRQ_H

// ============================================================================
// HAL IRQ — Hardware Abstraction Layer: Interrupt Controller
// ============================================================================
// x86: IDT (256 vectors) + PIC 8259 (master/slave) + EOI
// ARM: Exception Vectors (8 entries) + BCM2835 IRQ Controller (MMIO)
//
// Portable kernel code включает ТОЛЬКО этот header.
// ============================================================================

#include <stdint.h>
#include "config.h"

// ============================================================================
// IRQ HANDLER TYPE
// ============================================================================
// Обработчик прерывания. Вызывается из arch-specific dispatcher.
// regs: указатель на сохранённый контекст (struct regs на x86, pt_regs на ARM)
// irq_line: номер IRQ линии (для EOI / ack)
typedef void (*hal_irq_handler_t)(void* regs, uint32_t irq_line);

// ============================================================================
// MAX IRQ LINES
// ============================================================================
#ifdef CONFIG_ARCH_X86
#define HAL_MAX_IRQ_LINES   16      // PIC 8259: 8 master + 8 slave
#define HAL_IRQ_BASE        32      // IDT vectors 32-47 (после remap)
#endif

#ifdef CONFIG_ARCH_ARM
// ============================================================================
// [P1] BCM2835 IRQ Line Namespace
// ============================================================================
// PEND1:  GPU IRQ 0–31   (32 линии)
// PEND2:  GPU IRQ 32–63  (32 линии)
// BASIC:  ARM Timer, Mailbox, GPU halted, ... (8 линий)
//
// HAL namespace: линии 0–63 = GPU (PEND1+PEND2)
//                линии 64–71 = Basic (ENABLE_BASIC / DISABLE_BASIC)
//
// hal_irq_dispatch() маппит:
//   line < 32  → PEND1 bit[line]
//   line < 64  → PEND2 bit[line - 32]
//   line >= 64 → BASIC bit[line - 64]
// ============================================================================
#define HAL_IRQ_GPU_BASE    0
#define HAL_IRQ_GPU_COUNT   64
#define HAL_IRQ_BASIC_BASE  64
#define HAL_IRQ_BASIC_COUNT 8
#define HAL_MAX_IRQ_LINES   (HAL_IRQ_GPU_COUNT + HAL_IRQ_BASIC_COUNT)  // 72
#define HAL_IRQ_BASE        0       // ARM: нет IDT, линии нумеруются с 0

// Convenience: convert BCM2835 basic IRQ number to HAL line
#define HAL_IRQ_BASIC(n)    (HAL_IRQ_BASIC_BASE + (n))
#endif

// ============================================================================
// INITIALIZATION
// ============================================================================

// Инициализация контроллера прерываний.
// x86: PIC remap (ICW1-ICW4), mask all lines
// ARM: BCM2835 IRQ controller disable all, setup exception vectors
void hal_irq_init(void);

// ============================================================================
// IRQ REGISTRATION
// ============================================================================

// Регистрация обработчика для IRQ линии.
// line: номер линии (0-based, аппаратный)
// handler: функция-обработчик
// Возвращает 0 при успехе, -1 при ошибке (line out of range).
int hal_irq_register(uint32_t line, hal_irq_handler_t handler);

// ============================================================================
// IRQ MASKING (per-line)
// ============================================================================

// Разрешить IRQ линию (unmask).
// x86: clear bit в PIC IMR (master/slave)
// ARM: write 1 в BCM2835_IRQ_ENABLE1/2/ENABLE_BASIC
void hal_irq_enable_line(uint32_t line);

// Запретить IRQ линию (mask).
// x86: set bit в PIC IMR
// ARM: write 1 в BCM2835_IRQ_DISABLE1/2/DISABLE_BASIC
void hal_irq_disable_line(uint32_t line);

// ============================================================================
// EOI (End of Interrupt)
// ============================================================================

// Отправка EOI контроллеру.
// x86: outb(0x20, 0x20) [master] + outb(0xA0, 0x20) [slave if line >= 8]
// ARM: BCM2835 НЕ требует EOI (level-triggered). Вместо этого:
//      очистить источник прерывания (timer clear, UART ICR, etc.)
//
// ⚠️ EOI Lock Bypass (x86): EOI отправляется ДО вызова C-обработчика.
//    Иначе schedule() переключит задачу и линия заблокируется навсегда.
//
// ⚠️ ARM: "EOI" = ack/clear источника. Вызывается ПОСЛЕ обработки.
//    Level-triggered: если не очистить — IRQ сработает повторно.
void hal_irq_eoi(uint32_t line);

// ============================================================================
// IRQ DISPATCH (вызывается из arch-specific asm stub)
// ============================================================================
// [P2] hal_irq_dispatch реализуется в arch/<arch>/irq.c (НЕ portable).
//
// x86 (arch/x86/isr.c):
//   hal_irq_eoi(line);          // EOI FIRST (Lock Bypass)
//   handlers[line](regs, line); // handler AFTER
//
// ARM (arch/arm/arm_irq.c):
//   handlers[line](regs, line); // handler FIRST (clears source)
//   // No explicit EOI — handler clears timer CS / UART ICR
//
// ⚠️ Portable code НЕ вызывает hal_irq_dispatch напрямую.
//    Вызов только из arch-specific asm stub (isr_asm.asm / arm_vectors.S).
// ============================================================================
void hal_irq_dispatch(uint32_t line, void* regs);

// ============================================================================
// EXCEPTION HANDLERS (CPU exceptions, не hardware IRQ)
// ============================================================================

#ifdef CONFIG_ARCH_X86
// x86: 32 исключения (0-31). Регистрируются через IDT.
// hal_irq_register работает только для линий 0-15 (hardware IRQ).
// Исключения регистрируются отдельно в idt.c.
#endif

#ifdef CONFIG_ARCH_ARM
// ARM: 8 исключений (Reset, Undef, SVC, PAbort, DAbort, Reserved, IRQ, FIQ).
// SVC = syscall entry. DAbort = page fault. IRQ = hardware interrupt.
// Вектора устанавливаются в hal_cpu_early_init() (VBAR = 0xFFFF0000).

// ARM exception handler type (отличается от IRQ handler)
typedef void (*hal_exception_handler_t)(void* regs, uint32_t exception_type);

// Регистрация обработчика исключения.
// type: ARM_VECTOR_* из config.h (UNDEF, SVC, PABT, DABT)
int hal_exception_register(uint32_t type, hal_exception_handler_t handler);
#endif

#endif // HAL_IRQ_H