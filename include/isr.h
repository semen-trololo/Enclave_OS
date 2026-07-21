#ifndef ISR_H
#define ISR_H

#include <stdint.h>
#include "idt.h"

// Устанавливает все ISR и IRQ в таблицу IDT
void isr_install(void);
void irq_install(void);

// ============================================================================
// БАЗОВЫЕ PRIMITIVE: безусловные cli/sti
// ============================================================================
// Используются только там, где действительно нужно явно разрешить/запретить
// прерывания, например:
//   - task_entry_trampoline()
//   - fatal loops
//   - idle wait: sti; hlt; cli
//
// Для критических секций использовать irq_save()/irq_restore().
// ============================================================================
static inline void cli(void) { __asm__ volatile("cli"); }
static inline void sti(void) { __asm__ volatile("sti"); }

// ============================================================================
// IRQ-SAFE CRITICAL SECTION PRIMITIVES
// ============================================================================
// irq_save():
//   pushf
//   pop flags
//   cli
//   return flags
//
// irq_restore(flags):
//   push flags
//   popf
//
// Это сохраняет исходное состояние EFLAGS.IF:
//   - если прерывания были разрешены, они будут разрешены после restore;
//   - если прерывания были запрещены, они останутся запрещены.
//
// Использовать для всех критических секций вместо пары cli/sti.
// ============================================================================
typedef uint32_t irq_flags_t;

static inline __attribute__((always_inline)) irq_flags_t irq_save(void)
{
    irq_flags_t flags;

    __asm__ volatile (
        "pushf\n\t"
        "pop %0\n\t"
        "cli\n\t"
        : "=r" (flags)
        :
        : "memory", "cc"
    );

    return flags;
}

static inline __attribute__((always_inline)) void irq_restore(irq_flags_t flags)
{
    __asm__ volatile (
        "push %0\n\t"
        "popf\n\t"
        :
        : "r" (flags)
        : "memory", "cc"
    );
}

#endif // ISR_H
