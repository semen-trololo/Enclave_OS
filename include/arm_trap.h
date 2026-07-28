// ============================================================================
// arm_trap.h — ARM Trap Frame Layout
// ============================================================================
//
// Kernel/SVC trap frame:
//
//   srsdb sp!, #0x13
//   cps   #0x13
//   push  {r0-r12, lr}
//
// Resulting stack frame:
//
//   offset  0: r0
//   offset  4: r1
//   offset  8: r2
//   offset 12: r3
//   offset 16: r4
//   offset 20: r5
//   offset 24: r6
//   offset 28: r7       <- ARM syscall number
//   offset 32: r8
//   offset 36: r9
//   offset 40: r10
//   offset 44: r11
//   offset 48: r12
//   offset 52: lr
//   offset 56: pc
//   offset 60: cpsr
//
// Total: 64 bytes, 8-byte aligned.
//
// ----------------------------------------------------------------------------
//
// User IRQ trap frame (Day 46):
//
//   offset  0: r0
//   offset  4: r1
//   offset  8: r2
//   offset 12: r3
//   offset 16: r4
//   offset 20: r5
//   offset 24: r6
//   offset 28: r7
//   offset 32: r8
//   offset 36: r9
//   offset 40: r10
//   offset 44: r11
//   offset 48: r12
//   offset 52: sp_usr
//   offset 56: lr_usr
//   offset 60: padding
//   offset 64: pc
//   offset 68: cpsr
//
// Total: 72 bytes, 8-byte aligned.
// ============================================================================

#ifndef ARM_TRAP_H
#define ARM_TRAP_H

#ifndef __ASSEMBLER__

#include <stdint.h>

// ----------------------------------------------------------------------------
// Kernel/SVC trap frame.
// Used by:
//   - SVC syscall entry
//   - IRQ from kernel/SVC mode
//   - current fatal exception stubs
// ----------------------------------------------------------------------------

struct arm_trap_frame {
    uint32_t r0;        // 0
    uint32_t r1;        // 4
    uint32_t r2;        // 8
    uint32_t r3;        // 12
    uint32_t r4;        // 16
    uint32_t r5;        // 20
    uint32_t r6;        // 24
    uint32_t r7;        // 28  <- syscall number
    uint32_t r8;        // 32
    uint32_t r9;        // 36
    uint32_t r10;       // 40
    uint32_t r11;       // 44
    uint32_t r12;       // 48
    uint32_t lr;        // 52
    uint32_t pc;        // 56
    uint32_t cpsr;      // 60
};

// ----------------------------------------------------------------------------
// User IRQ trap frame.
// Used by:
//   - IRQ from USR mode
//
// This frame is NOT interchangeable with struct arm_trap_frame.
// C code must not cast user IRQ frame to struct arm_trap_frame.
// ----------------------------------------------------------------------------

struct arm_user_irq_frame {
    uint32_t r0;        // 0
    uint32_t r1;        // 4
    uint32_t r2;        // 8
    uint32_t r3;        // 12
    uint32_t r4;        // 16
    uint32_t r5;        // 20
    uint32_t r6;        // 24
    uint32_t r7;        // 28
    uint32_t r8;        // 32
    uint32_t r9;        // 36
    uint32_t r10;       // 40
    uint32_t r11;       // 44
    uint32_t r12;       // 48
    uint32_t sp_usr;    // 52
    uint32_t lr_usr;    // 56
    uint32_t pad;       // 60
    uint32_t pc;        // 64
    uint32_t cpsr;      // 68
};

#endif // __ASSEMBLER__

// ----------------------------------------------------------------------------
// Kernel/SVC frame assembly offsets.
// ----------------------------------------------------------------------------

#define ARM_TF_R0           0
#define ARM_TF_R1           4
#define ARM_TF_R2           8
#define ARM_TF_R3           12
#define ARM_TF_R4           16
#define ARM_TF_R5           20
#define ARM_TF_R6           24
#define ARM_TF_R7           28
#define ARM_TF_R8           32
#define ARM_TF_R9           36
#define ARM_TF_R10          40
#define ARM_TF_R11          44
#define ARM_TF_R12          48
#define ARM_TF_LR           52
#define ARM_TF_PC           56
#define ARM_TF_CPSR         60
#define ARM_TF_SIZE         64

// ----------------------------------------------------------------------------
// User IRQ frame assembly offsets.
// ----------------------------------------------------------------------------

#define ARM_UTF_R0          0
#define ARM_UTF_R1          4
#define ARM_UTF_R2          8
#define ARM_UTF_R3          12
#define ARM_UTF_R4          16
#define ARM_UTF_R5          20
#define ARM_UTF_R6          24
#define ARM_UTF_R7          28
#define ARM_UTF_R8          32
#define ARM_UTF_R9          36
#define ARM_UTF_R10         40
#define ARM_UTF_R11         44
#define ARM_UTF_R12         48
#define ARM_UTF_SP_USR      52
#define ARM_UTF_LR_USR      56
#define ARM_UTF_PAD         60
#define ARM_UTF_PC          64
#define ARM_UTF_CPSR        68
#define ARM_UTF_SIZE        72

#endif // ARM_TRAP_H