@ ==========================================================================
@ arm_user_asm.S — ARM User Mode Trampoline + Test Image
@ ==========================================================================
@
@ This file contains:
@   1. arm_user_trampoline()
@      First-time entry point used by scheduler context switch.
@
@   2. arm_enter_user_first()
@      Sets user SP, user CPSR, clears registers, returns to user mode.
@
@   3. _user_test_start / _user_test_end
@      Raw ARM user test image copied to ARM_USER_CODE_VADDR.
@
@ User CPSR:
@   0xD0 = USR mode | FIQ disabled | IRQ disabled
@
@ IRQ from user mode is not supported yet, so user runs with I=1.
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
@ ==========================================================================

.global arm_enter_user_first
arm_enter_user_first:
    mov r3, r0                  @ r3 = user_pc
    mov r4, r1                  @ r4 = user_sp
    mov r5, r2                  @ r5 = kernel_stack_top

    @ Set user stack pointer.
    cps #0x1F                   @ SYS mode
    mov sp, r4                  @ SP_usr = user_sp
    cps #0x13                   @ back to SVC mode

    @ Set kernel stack pointer for future SVC/IRQ entry.
    mov sp, r5

    @ Prepare user CPSR.
    @
    @ Day 46:
    @   ARM_CPSR_USER = USR mode | FIQ disabled | IRQ enabled
    @   value = 0x50
    @
    @ This allows timer IRQ to preempt user code.
    ldr r0, =ARM_CPSR_USER
    msr spsr_cxsf, r0

    @ Return address.
    mov lr, r3                  @ LR = user_pc

    @ Clear registers to avoid leaking kernel state.
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

    @ Enter user mode.
    movs pc, lr

@ ==========================================================================
@ USER TEST IMAGE
@ ==========================================================================
@ This code is copied to ARM_USER_CODE_VADDR (0x00100000) and executed
@ in USR mode.
@
@ Syscall ABI:
@   r7    = syscall number
@   r0-r6 = arguments
@   r0    = return value
@
@ Test sequence:
@   1. sys_write(1, data_buffer, 16)
@   2. invalid syscall -> -ENOSYS
@   3. sys_yield()
@   4. sys_write(1, data_buffer, 16)
@   5. sys_exit(0)
@ ==========================================================================

.align 2
.global _user_test_start
.global _user_test_end

_user_test_start:

    @ ---------------------------------------------------------------
    @ r4 = iterations
    @ r6 = busy-loop count
    @ ---------------------------------------------------------------

    mov r4, #3
    ldr r6, =1000000

    @ ---------------------------------------------------------------
    @ invalid syscall once
    @ ---------------------------------------------------------------

    mov r7, #99
    svc #0

1:

    @ ---------------------------------------------------------------
    @ Print PID as a single character + newline.
    @
    @ getpid() returns:
    @   1 for user_a
    @   2 for user_b
    @
    @ We build a 2-byte buffer on the user stack:
    @   [0] = '0' + pid
    @   [1] = '\n'
    @
    @ User stack is inside ARM_USER_DATA_VADDR region,
    @ so sys_write validation accepts it.
    @ ---------------------------------------------------------------

    mov r7, #122                @ SYS_getpid
    svc #0

    add r0, r0, #48             @ '0' + pid
    mov r1, #10                 @ '\n'

    push {r0, r1}               @ 2-byte buffer on user stack

    mov r0, #1                  @ fd = stdout
    mov r1, sp                  @ buf
    mov r2, #2                  @ len
    mov r7, #4                  @ SYS_write
    svc #0

    add sp, sp, #8              @ drop temporary buffer

    @ ---------------------------------------------------------------
    @ Busy loop.
    @ This is the preemption window.
    @ ---------------------------------------------------------------

    mov r5, r6

2:
    subs r5, r5, #1
    bne 2b

    subs r4, r4, #1
    bne 1b

    @ ---------------------------------------------------------------
    @ sys_exit(0)
    @ ---------------------------------------------------------------

    mov r0, #0
    mov r7, #1                  @ SYS_exit
    svc #0

3:
    b 3b

    .ltorg

_user_test_end: