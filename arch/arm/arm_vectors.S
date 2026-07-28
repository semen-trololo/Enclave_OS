@ ==========================================================================
@ arm_vectors.S — ARM Exception Vectors
@ ==========================================================================
@ v4: SVC is now a real syscall entry.
@
@ SVC/IRQ frame:
@   srsdb sp!, #0x13        ; push {LR_mode, SPSR_mode}
@   cps   #0x13
@   push  {r0-r12, lr}
@
@   8 bytes SRS + 56 bytes PUSH = 64 bytes
@   64 mod 8 = 0, AAPCS aligned.
@
@ rfeia sp! reads:
@   PC   <- saved LR
@   CPSR <- saved SPSR
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
1:
    mcr p15, 0, r0, c7, c0, 4
    b 1b

@ ==========================================================================
@ UNDEFINED INSTRUCTION (Day 47: fault isolation, explicit user/kernel path)
@ ==========================================================================

_undef_handler:
    sub lr, lr, #4              @ UNDEF return adjustment
    srsdb sp!, #0x13            @ Push {LR_und, SPSR_und} to SVC stack
    cps #0x13                   @ Ensure SVC mode

    @ ------------------------------------------------------------------
    @ Determine interrupted mode.
    @ After srsdb: [SP+0]=PC, [SP+4]=SPSR
    @ ------------------------------------------------------------------

    push {r0, r1}
    ldr r0, [sp, #12]           @ saved SPSR
    and r0, r0, #0x1F
    cmp r0, #0x10               @ ARM_MODE_USR
    pop {r0, r1}

    beq _undef_from_user

_undef_from_kernel:
    push {r0-r12, lr}           @ 64-byte kernel frame
    mov r0, sp
    mov r1, #0x04               @ ARM_VECTOR_UNDEF
    bl arm_kernel_fault_entry

    @ Should never return.
1:
    mcr p15, 0, r0, c7, c0, 4
    b 1b

_undef_from_user:
    sub sp, sp, #64             @ allocate user frame body
    stmia sp, {r0-r12}          @ save r0-r12 at offsets 0..48

    cps #0x1F                   @ SYS mode (shares user bank)
    mov r0, sp                  @ r0 = SP_usr
    mov r1, lr                  @ r1 = LR_usr
    cps #0x13                   @ back to SVC

    str r0, [sp, #52]           @ frame->sp_usr
    str r1, [sp, #56]           @ frame->lr_usr
    mov r0, #0
    str r0, [sp, #60]           @ frame->pad

    mov r0, sp                  @ r0 = user frame pointer
    mov r1, #0x04               @ ARM_VECTOR_UNDEF
    bl arm_user_fault_entry

    @ Should never return.
1:
    mcr p15, 0, r0, c7, c0, 4
    b 1b

@ ==========================================================================
@ SVC — SYSTEM CALL ENTRY
@ ==========================================================================
@ Syscall ABI:
@   r7    = syscall number
@   r0-r6 = arguments
@   r0    = return value
@
@ C dispatcher:
@   void arm_syscall_entry(struct arm_trap_frame *frame);
@
@ The dispatcher may call schedule() through sys_yield / sys_exit.
@ If the task is switched away, this stack frame remains on the task's
@ kernel stack and is restored when the task is scheduled back.
@ ==========================================================================

_svc_handler:
    srsdb sp!, #0x13            @ Push {LR_svc, SPSR_svc}
    cps #0x13                   @ Ensure SVC mode
    push {r0-r12, lr}           @ 14 regs = 56 bytes, total 64

    mov r0, sp                  @ r0 = struct arm_trap_frame *
    bl arm_syscall_entry

    pop {r0-r12, lr}
    rfeia sp!                   @ Return to interrupted mode

@ ==========================================================================
@ PREFETCH ABORT (Day 47: fault isolation, explicit user/kernel path)
@ ==========================================================================

_pabt_handler:
    sub lr, lr, #4              @ PABT return adjustment
    srsdb sp!, #0x13
    cps #0x13

    push {r0, r1}
    ldr r0, [sp, #12]
    and r0, r0, #0x1F
    cmp r0, #0x10
    pop {r0, r1}

    beq _pabt_from_user

_pabt_from_kernel:
    push {r0-r12, lr}
    mov r0, sp
    mov r1, #0x0C               @ ARM_VECTOR_PABT
    bl arm_kernel_fault_entry

1:
    mcr p15, 0, r0, c7, c0, 4
    b 1b

_pabt_from_user:
    sub sp, sp, #64
    stmia sp, {r0-r12}

    cps #0x1F
    mov r0, sp
    mov r1, lr
    cps #0x13

    str r0, [sp, #52]
    str r1, [sp, #56]
    mov r0, #0
    str r0, [sp, #60]

    mov r0, sp
    mov r1, #0x0C               @ ARM_VECTOR_PABT
    bl arm_user_fault_entry

1:
    mcr p15, 0, r0, c7, c0, 4
    b 1b

@ ==========================================================================
@ DATA ABORT (Day 47: fault isolation, explicit user/kernel path)
@ ==========================================================================

_dabt_handler:
    sub lr, lr, #8              @ DABT return adjustment
    srsdb sp!, #0x13
    cps #0x13

    push {r0, r1}
    ldr r0, [sp, #12]
    and r0, r0, #0x1F
    cmp r0, #0x10
    pop {r0, r1}

    beq _dabt_from_user

_dabt_from_kernel:
    push {r0-r12, lr}
    mov r0, sp
    mov r1, #0x10               @ ARM_VECTOR_DABT
    bl arm_kernel_fault_entry

1:
    mcr p15, 0, r0, c7, c0, 4
    b 1b

_dabt_from_user:
    sub sp, sp, #64
    stmia sp, {r0-r12}

    cps #0x1F
    mov r0, sp
    mov r1, lr
    cps #0x13

    str r0, [sp, #52]
    str r1, [sp, #56]
    mov r0, #0
    str r0, [sp, #60]

    mov r0, sp
    mov r1, #0x10               @ ARM_VECTOR_DABT
    bl arm_user_fault_entry

1:
    mcr p15, 0, r0, c7, c0, 4
    b 1b

@ ==========================================================================
@ RESERVED
@ ==========================================================================

_reserved_handler:
    cpsid if
1:
    mcr p15, 0, r0, c7, c0, 4
    b 1b

@ ==========================================================================
@ IRQ HANDLER (Day 46: user-mode preemption)
@ ==========================================================================
@ Supports two interrupted contexts:
@
@ 1. IRQ from SVC/kernel mode:
@
@      offset  0: r0
@      offset  4: r1
@      offset  8: r2
@      offset 12: r3
@      offset 16: r4
@      offset 20: r5
@      offset 24: r6
@      offset 28: r7
@      offset 32: r8
@      offset 36: r9
@      offset 40: r10
@      offset 44: r11
@      offset 48: r12
@      offset 52: lr_svc
@      offset 56: pc
@      offset 60: cpsr
@
@    Total: 64 bytes, 8-byte aligned.
@
@ 2. IRQ from USR/user mode:
@
@      offset  0: r0
@      offset  4: r1
@      offset  8: r2
@      offset 12: r3
@      offset 16: r4
@      offset 20: r5
@      offset 24: r6
@      offset 28: r7
@      offset 32: r8
@      offset 36: r9
@      offset 40: r10
@      offset 44: r11
@      offset 48: r12
@      offset 52: sp_usr
@      offset 56: lr_usr
@      offset 60: padding
@      offset 64: pc
@      offset 68: cpsr
@
@    Total: 72 bytes, 8-byte aligned.
@
@ AAPCS:
@   Both frame sizes are multiples of 8.
@ ==========================================================================

.global _irq_handler
_irq_handler:
    sub lr, lr, #4              @ IRQ return adjustment
    srsdb sp!, #0x13            @ Push {LR_irq, SPSR_irq} to SVC stack
    cps #0x13                   @ Switch to SVC mode

    @ ------------------------------------------------------------------
    @ Determine interrupted mode without destroying user registers.
    @
    @ After srsdb:
    @   SP     -> saved LR_irq / PC
    @   SP + 4 -> saved SPSR_irq / interrupted CPSR
    @
    @ Temporarily push r0/r1 so we can use them.
    @ ------------------------------------------------------------------

    push {r0, r1}
    ldr r0, [sp, #12]           @ saved SPSR_irq
    and r0, r0, #0x1F           @ CPSR mode bits
    cmp r0, #0x10               @ ARM_MODE_USR
    pop {r0, r1}

    beq _irq_from_user

_irq_from_kernel:
    @ ------------------------------------------------------------------
    @ Kernel/SVC IRQ path.
    @ This is the original Days 38-45 path.
    @ ------------------------------------------------------------------

    push {r0-r12, lr}           @ 14 regs = 56 bytes, total 64

    mov r0, sp                  @ r0 = kernel frame pointer
    bl arm_irq_entry

    pop {r0-r12, lr}
    rfeia sp!                   @ Return to kernel/SVC mode

_irq_from_user:
    @ ------------------------------------------------------------------
    @ User IRQ path.
    @
    @ Current SVC SP points to saved PC/CPSR block from srsdb:
    @
    @   [SP + 0] = PC
    @   [SP + 4] = CPSR
    @
    @ Allocate 64 bytes below it.
    @ Total user frame = 72 bytes.
    @ ------------------------------------------------------------------

    sub sp, sp, #64

    @ Save r0-r12 at offsets 0..48.
    stmia sp, {r0-r12}

    @ ------------------------------------------------------------------
    @ Save SP_usr and LR_usr.
    @
    @ r0/r1 are already saved on the frame and may be used as temps.
    @ SYS mode shares the user register bank, so:
    @   SP in SYS = SP_usr
    @   LR in SYS = LR_usr
    @ ------------------------------------------------------------------

    cps #0x1F                   @ SYS mode
    mov r0, sp                  @ r0 = SP_usr
    mov r1, lr                  @ r1 = LR_usr
    cps #0x13                   @ back to SVC mode

    str r0, [sp, #52]           @ frame->sp_usr
    str r1, [sp, #56]           @ frame->lr_usr

    mov r0, sp                  @ r0 = user frame pointer
    bl arm_irq_entry

    @ ------------------------------------------------------------------
    @ Restore user context.
    @ ------------------------------------------------------------------

    mov r0, sp                  @ frame base
    ldr r1, [r0, #52]           @ saved SP_usr

    cps #0x1F
    mov sp, r1                  @ restore SP_usr
    cps #0x13

    mov sp, r0                  @ SVC SP = frame base
    ldmia sp, {r0-r12, lr}^     @ restore user r0-r12 and LR_usr
    add sp, sp, #64             @ point to saved PC/CPSR
    rfeia sp!                   @ return to USR mode

@ ==========================================================================
@ FIQ
@ ==========================================================================

_fiq_handler:
    cpsid f
1:
    mcr p15, 0, r0, c7, c0, 4
    b 1b