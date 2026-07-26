@ ==========================================================================
@ arm_vectors.S — ARM Exception Vectors (v3: AAPCS-aligned IRQ handler)
@ ==========================================================================
@ v3 FIX: push {r0-r12} → push {r0-r12, lr}
@   13 regs = 52 bytes → SP mod 8 = 4 (MISALIGNED, AAPCS violation)
@   14 regs = 56 bytes → SP mod 8 = 0 (ALIGNED ✅)
@
@ Без alignment GCC генерирует strd/ldrd с misaligned SP → corruption.
@ ==========================================================================

.section .text
.arm
.align 5

@ ==========================================================================
@ VECTOR TABLE
@ ==========================================================================
.global _vector_table
_vector_table:
    b _reset_handler              @ 0x00
    b _undef_handler              @ 0x04
    b _svc_handler                @ 0x08
    b _pabt_handler               @ 0x0C
    b _dabt_handler               @ 0x10
    b _reserved_handler           @ 0x14
    b _irq_handler                @ 0x18
    b _fiq_handler                @ 0x1C

@ ==========================================================================
@ RESET
@ ==========================================================================
_reset_handler:
    cpsid if
1:  mcr p15, 0, r0, c7, c0, 4
    b 1b

@ ==========================================================================
@ UNDEFINED INSTRUCTION
@ ==========================================================================
_undef_handler:
    sub lr, lr, #4
    srsdb sp!, #0x13
    cps #0x13
    push {r0-r12, lr}            @ 14 regs = 56 bytes, aligned ✅
    mov r0, sp
    mov r1, #0x04
    bl arm_exception_entry
    pop {r0-r12, lr}
    rfeia sp!

@ ==========================================================================
@ SVC (Syscall)
@ ==========================================================================
_svc_handler:
    srsdb sp!, #0x13
    cps #0x13
    push {r0-r12, lr}            @ 14 regs, aligned ✅
    mov r0, sp
    mov r1, #0x08
    bl arm_exception_entry
    pop {r0-r12, lr}
    rfeia sp!

@ ==========================================================================
@ PREFETCH ABORT
@ ==========================================================================
_pabt_handler:
    sub lr, lr, #4
    srsdb sp!, #0x13
    cps #0x13
    push {r0-r12, lr}            @ 14 regs, aligned ✅
    mov r0, sp
    mov r1, #0x0C
    bl arm_exception_entry
    pop {r0-r12, lr}
    rfeia sp!

@ ==========================================================================
@ DATA ABORT (Page Fault)
@ ==========================================================================
_dabt_handler:
    sub lr, lr, #8
    srsdb sp!, #0x13
    cps #0x13
    push {r0-r12, lr}            @ 14 regs, aligned ✅
    mov r0, sp
    mov r1, #0x10
    bl arm_exception_entry
    pop {r0-r12, lr}
    rfeia sp!

@ ==========================================================================
@ RESERVED
@ ==========================================================================
_reserved_handler:
    cpsid if
1:  mcr p15, 0, r0, c7, c0, 4
    b 1b

@ ==========================================================================
@ IRQ HANDLER (Hardware Interrupt)
@ ==========================================================================
@ [v3 FIX] push {r0-r12, lr} вместо push {r0-r12}.
@   SRS:   8 bytes  (LR_irq + SPSR_irq)
@   PUSH:  56 bytes (14 regs: r0-r12 + lr_svc)
@   Total: 64 bytes → 64 mod 8 = 0 ✅
@
@ Stack layout (SVC stack, full descending):
@
@   SP → r0
@        r1
@        ...
@        r12
@        lr_svc              ← alignment padding (не используется rfeia)
@        LR_irq (adjusted)   ← from SRS
@        SPSR_irq            ← from SRS
@
@ rfeia sp! читает {PC, CPSR} = {LR_irq, SPSR_irq} после pop.
@ ==========================================================================
.global _irq_handler
_irq_handler:
    sub lr, lr, #4              @ Adjust: return address = LR_irq - 4
    srsdb sp!, #0x13            @ Push {LR_irq, SPSR_irq} → SVC stack
    cps #0x13                   @ Switch to SVC mode
    push {r0-r12, lr}           @ 14 regs = 56 bytes → SP mod 8 = 0 ✅

    mov r0, sp                  @ r0 = context pointer (8-byte aligned)
    bl arm_irq_entry            @ C dispatcher

    pop {r0-r12, lr}            @ Restore (lr = lr_svc, не используется)
    rfeia sp!                   @ Pop {PC, CPSR} = {LR_irq, SPSR_irq}

@ ==========================================================================
@ FIQ (not used)
@ ==========================================================================
_fiq_handler:
    cpsid f
1:  mcr p15, 0, r0, c7, c0, 4
    b 1b