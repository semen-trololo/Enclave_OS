@ ==========================================================================
@ arm_context.S — ARM Context Switch (v2: CPSR save/restore)
@ ==========================================================================
@ v2 FIX: добавлен CPSR в сохраняемый контекст.
@   Без CPSR новая задача наследует I=1 из IRQ handler → IRQ мёртв.
@
@ Сохраняет: r4-r11 (callee-saved) + r12 (CPSR) + LR
@ 10 регистров = 40 bytes. 40 mod 8 = 0 ✅ (AAPCS)
@
@ void arm_context_switch(uint32_t *old_sp, uint32_t new_sp);
@
@ Stack layout (10 regs):
@   SP+0:  r4
@   SP+4:  r5
@   ...
@   SP+28: r11
@   SP+32: r12 = CPSR
@   SP+36: lr
@
@ ⚠️ msr cpsr_c пишет ТОЛЬКО control byte (bits 0-7: mode, T, F, I).
@    Condition flags (N,Z,C,V) НЕ затрагиваются.
@
@ ⚠️ Для kernel tasks (SVC mode): CPSR всегда 0x13 (SVC, I=0).
@    Для user tasks (USR mode): понадобится rfeia, не msr. (Days 43-44)
@ ==========================================================================

.section .text
.arm
.align 2

.global arm_context_switch
arm_context_switch:
    mrs  r12, cpsr            @ r12 = current CPSR
    push {r4-r12, lr}         @ Save callee-saved + CPSR + LR (40 bytes)
    str  sp, [r0]             @ *old_sp = current SP
    mov  sp, r1               @ Switch to new task's stack
    pop  {r4-r12, lr}         @ Restore new task's context
    msr  cpsr_c, r12          @ Restore CPSR (mode, I, F)
    bx   lr                   @ Jump to new task (or resume)