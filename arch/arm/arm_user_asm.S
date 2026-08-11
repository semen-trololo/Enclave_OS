@ ==========================================================================
@ arm_user_asm.S — ARM User Mode Trampoline + Embedded ELF Image
@ ==========================================================================
@ Day 54: ELF Loader Integration
@
@ This file contains:
@   1. arm_user_trampoline()
@      First-time entry point used by scheduler context switch.
@
@   2. arm_enter_user_first()
@      Sets user SP, user CPSR, clears registers, returns to user mode.
@
@   3. _user_elf_start / _user_elf_end
@      Embedded ARM ELF binary loaded by arm_elf_load().
@
@ User CPSR:
@   ARM_CPSR_USER = 0x50 = USR mode | FIQ disabled | IRQ enabled
@
@ Day 46: Timer IRQ can preempt user code directly.
@ Day 47: User fault isolation (UNDEF/PABT/DABT kill task, kernel alive).
@ Day 49: Blocking syscall test (sys_sleep).
@ Day 54: ELF loader integration (replaces raw binary).
@ ==========================================================================

#include "config.h"

.section .text
.arm
.align 2

@ ==========================================================================
@ USER TASK TRAMPOLINE
@ ==========================================================================
@ This is the first function executed by a newly created user task after
@ arm_context_switch() restores its forged kernel stack.
@
@ Forged registers:
@   r4 = user_pc
@   r5 = user_stack_top
@   r6 = kernel_stack_top
@ ==========================================================================

.global arm_user_trampoline
arm_user_trampoline:
    mov r0, r4                  @ user_pc
    mov r1, r5                  @ user_stack_top
    mov r2, r6                  @ kernel_stack_top
    b   arm_enter_user_first

@ ==========================================================================
@ ENTER USER MODE (FIRST TIME)
@ ==========================================================================
@ void arm_enter_user_first(uint32_t user_pc,
@                           uint32_t user_sp,
@                           uint32_t kernel_stack_top)
@
@ r0 = user_pc
@ r1 = user_sp
@ r2 = kernel_stack_top
@
@ This function:
@   1. Sets SP_usr to user_sp
@   2. Sets SP_svc to kernel_stack_top (for future SVC/IRQ entry)
@   3. Prepares SPSR with ARM_CPSR_USER (IRQ enabled)
@   4. Clears all general-purpose registers (Zero Trust)
@   5. Returns to user mode via movs pc, lr
@ ==========================================================================

.global arm_enter_user_first
arm_enter_user_first:
    mov r3, r0                  @ r3 = user_pc
    mov r4, r1                  @ r4 = user_sp
    mov r5, r2                  @ r5 = kernel_stack_top

    @ ------------------------------------------------------------------
    @ Set user stack pointer.
    @
    @ We must switch to SYS mode to access SP_usr.
    @ SYS mode (0x1F) shares register bank with USR mode.
    @ ------------------------------------------------------------------
    cps #0x1F                   @ Switch to SYS mode
    mov sp, r4                  @ SP_usr = user_sp
    cps #0x13                   @ Back to SVC mode

    @ ------------------------------------------------------------------
    @ Set kernel stack pointer for future SVC/IRQ entry.
    @ This is the stack used by syscall handlers and IRQ handlers.
    @ ------------------------------------------------------------------
    mov sp, r5

    @ ------------------------------------------------------------------
    @ Prepare user CPSR.
    @
    @ ARM_CPSR_USER = USR mode | FIQ disabled | IRQ enabled
    @ Value = 0x50
    @
    @ Day 46: IRQ enabled allows timer IRQ to preempt user code.
    @ ------------------------------------------------------------------
    ldr r0, =ARM_CPSR_USER
    msr spsr_cxsf, r0

    @ ------------------------------------------------------------------
    @ Set return address to user entry point.
    @ ------------------------------------------------------------------
    mov lr, r3                  @ LR = user_pc

    @ ------------------------------------------------------------------
    @ Zero Trust: Clear all general-purpose registers.
    @
    @ This prevents leaking kernel state into user mode.
    @ User code should not see any remnants of kernel pointers/values.
    @ ------------------------------------------------------------------
    mov r0, #0
    mov r1, #0
    mov r2, #0
    mov r3, #0
    mov r4, #0
    mov r5, #0
    mov r6, #0
    mov r7, #0
    mov r8, #0
    mov r9, #0
    mov r10, #0
    mov r11, #0
    mov r12, #0

    @ ------------------------------------------------------------------
    @ Enter user mode.
    @
    @ movs pc, lr:
    @   - Copies LR to PC (jumps to user_pc)
    @   - Copies SPSR to CPSR (switches to USR mode, enables IRQ)
    @ ------------------------------------------------------------------
    movs pc, lr

@ ==========================================================================
@ EMBEDDED ELF IMAGE
@ ==========================================================================
@ This is the ARM ELF binary that will be loaded by arm_elf_load().
@
@ The ELF file is compiled from user_src/user_test.c and embedded here
@ via .incbin directive. The linker places it in .rodata section.
@
@ arm_user_create_space_and_load_image() passes _user_elf_start and
@ _user_elf_end to arm_elf_load() which parses and loads it into
@ isolated address space.
@ ==========================================================================

.section .rodata
.align 4
.global _user_elf_start
.global _user_elf_end

_user_elf_start:
    .incbin "build/arm/user_test.elf"
_user_elf_end:

.ltorg
