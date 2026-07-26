#ifndef HAL_CPU_H
#define HAL_CPU_H

// ============================================================================
// HAL CPU — Hardware Abstraction Layer: CPU Control
// ============================================================================
// Контракт для arch/x86/ и arch/arm/ реализаций.
// Portable kernel code включает ТОЛЬКО этот header.
//
// x86: GDT, TSS, IDT, CR0/CR3/CR4, cli/sti/hlt
// ARM: CP15, CPSR, VBAR, cpsid/cpsie/wfi
// ============================================================================

#include <stdint.h>
#include "config.h"

// ============================================================================
// IRQ SAVE/RESTORE (Critical Section Primitive)
// ============================================================================
// irq_save() сохраняет текущее состояние прерываний и отключает их.
// irq_restore() восстанавливает сохранённое состояние.
//
// Паттерн использования:
//   uint32_t flags = hal_irq_save();
//   ... critical section ...
//   hal_irq_restore(flags);
//
// ⚠️ НЕ использовать безусловные cli/sti внутри критических секций.
//    Безусловные cli/sti допустимы ТОЛЬКО для: idle wait, fatal loops,
//    task trampoline (явное управление прерываниями).
// ============================================================================

#ifdef CONFIG_ARCH_X86

static inline uint32_t hal_irq_save(void) {
    uint32_t flags;
    __asm__ volatile (
        "pushfl\n\t"
        "popl %0\n\t"
        "cli"
        : "=r" (flags)
        :
        : "memory"
    );
    return flags;
}

static inline void hal_irq_restore(uint32_t flags) {
    __asm__ volatile (
        "pushl %0\n\t"
        "popfl"
        :
        : "r" (flags)
        : "memory", "cc"
    );
}

static inline void hal_irq_disable(void) {
    __asm__ volatile ("cli" ::: "memory");
}

static inline void hal_irq_enable(void) {
    __asm__ volatile ("sti" ::: "memory");
}

static inline void hal_halt(void) {
    __asm__ volatile ("hlt");
}

// Idle: включить прерывания и ждать. Атомарно на x86.
static inline void hal_cpu_idle(void) {
    __asm__ volatile ("sti; hlt; cli");
}

#endif // CONFIG_ARCH_X86

#ifdef CONFIG_ARCH_ARM

static inline uint32_t hal_irq_save(void) {
    uint32_t cpsr;
    __asm__ volatile (
        "mrs %0, cpsr\n\t"
        "cpsid i"
        : "=r" (cpsr)
        :
        : "memory"
    );
    return cpsr;
}

// ============================================================================
// [P4] Safe IRQ Restore (ARM)
// ============================================================================
// Восстанавливает ТОЛЬКО I-bit (IRQ disable) из saved_cpsr.
// Mode bits (M[4:0]), T, F — сохраняются из ТЕКУЩЕГО CPSR.
//
// Это гарантирует, что hal_irq_restore НЕ переключит режим процессора,
// даже если saved_cpsr был сохранён в другом режиме (например, IRQ mode).
//
// x86 не имеет этой проблемы: EFLAGS не содержит CPL.
// ============================================================================
static inline void hal_irq_restore(uint32_t saved_cpsr) {
    uint32_t cpsr;
    __asm__ volatile (
        "mrs %0, cpsr\n\t"           // read current CPSR
        "bic %0, %0, #0x80\n\t"      // clear I bit (enable IRQ)
        "tst %1, #0x80\n\t"          // was I set in saved?
        "orrne %0, %0, #0x80\n\t"    // if yes, restore I bit (disable IRQ)
        "msr cpsr_c, %0"             // write back (mode bits = current)
        : "=&r" (cpsr)
        : "r" (saved_cpsr)
        : "memory", "cc"
    );
}

static inline void hal_irq_disable(void) {
    __asm__ volatile ("cpsid i" ::: "memory");
}

static inline void hal_irq_enable(void) {
    __asm__ volatile ("cpsie i" ::: "memory");
}

static inline void hal_halt(void) {
    __asm__ volatile ("wfi");
}

// Idle: позволить pending IRQ обработаться.
// ============================================================================
// [FIX] ARM1176 WFI: QEMU raspi1ap не выводит CPU из WFI по System Timer IRQ.
// Реальный BCM2835 тоже имеет errata для WFI в некоторых режимах.
//
// Безопасная замена: cpsie i + NOP sled + cpsid i.
// NOP sled даёт окно для принятия pending IRQ.
// Не power-efficient, но КОРРЕКТНО. Для production (Cortex-A) — заменить на WFI.
// ============================================================================
static inline void hal_cpu_idle(void) {
    __asm__ volatile (
        "cpsie i\n\t"
        "mov r0, r0\n\t"       // NOP (ARM1176-safe encoding)
        "mov r0, r0\n\t"
        "mov r0, r0\n\t"
        "mov r0, r0\n\t"
        "cpsid i"
        ::: "memory", "r0"
    );
}

#endif // CONFIG_ARCH_ARM

// ============================================================================
// CPU INITIALIZATION
// ============================================================================

// Ранняя инициализация CPU (до MMU).
// x86: GDT + IDT + TSS + PIC remap
// ARM: VBAR setup, CP15 config, disable caches/MMU, set SVC mode
void hal_cpu_early_init(void);

// Поздняя инициализация (после MMU).
// x86: syscall_init (INT 0x80 DPL=3)
// ARM: vectors copy to 0xFFFF0000, domain setup
void hal_cpu_late_init(void);

// ============================================================================
// CPU INFO
// ============================================================================

// Возвращает 1 если прерывания включены, 0 если выключены.
static inline int hal_irq_enabled(void) {
#ifdef CONFIG_ARCH_X86
    uint32_t flags;
    __asm__ volatile ("pushfl; popl %0" : "=r" (flags));
    return (flags >> 9) & 1;
#endif
#ifdef CONFIG_ARCH_ARM
    uint32_t cpsr;
    __asm__ volatile ("mrs %0, cpsr" : "=r" (cpsr));
    return !(cpsr & ARM_CPSR_IRQ_DISABLE);
#endif
}

// ============================================================================
// MEMORY BARRIER (для MMIO)
// ============================================================================

static inline void hal_dsb(void) {
#ifdef CONFIG_ARCH_X86
    __asm__ volatile ("mfence" ::: "memory");
#endif
#ifdef CONFIG_ARCH_ARM
    __asm__ volatile ("mcr p15, 0, %0, c7, c10, 4" : : "r" (0) : "memory");
#endif
}

static inline void hal_dmb(void) {
#ifdef CONFIG_ARCH_X86
    __asm__ volatile ("" ::: "memory");  // Compiler barrier (x86 TSO)
#endif
#ifdef CONFIG_ARCH_ARM
    __asm__ volatile ("mcr p15, 0, %0, c7, c10, 5" : : "r" (0) : "memory");
#endif
}

static inline void hal_isb(void) {
#ifdef CONFIG_ARCH_X86
    __asm__ volatile ("" ::: "memory");  // x86 serializes implicitly
#endif
#ifdef CONFIG_ARCH_ARM
    __asm__ volatile ("mcr p15, 0, %0, c7, c5, 4" : : "r" (0) : "memory");
#endif
}

#endif // HAL_CPU_H