@ ==========================================================================
@ arm_boot.S — Enclave OS ARM Bootstrap (v4: Higher Half Stack Fix)
@ ==========================================================================
@ Raspberry Pi 1 (BCM2835, ARM1176JZF-S, ARMv6)
@ QEMU raspi1ap: kernel loaded at 0x00010000
@
@ Задача:
@   1. Сохранить r1 (machine type), r2 (ATAGS)
@   2. Установить SVC mode, отключить IRQ/FIQ
@   3. Настроить стеки (SVC, IRQ, FIQ, ABT, UND, SYS) — ФИЗИЧЕСКИЕ адреса
@   4. Очистить BSS
@   5. Создать Translation Table (один TTBR0, identity + higher half)
@   6. Включить MMU
@   7. Прыгнуть в Higher Half (0xC0000000)
@   8. ПЕРЕЗАГРУЗИТЬ ВСЕ СТЕКИ с KERNEL_VMA (для Zero Trust Identity Teardown)
@   9. Установить VBAR
@  10. Вызвать arm_kernel_main(atags, machine_type)
@
@ ⚠️ ВЕСЬ код в .boot.text исполняется по ФИЗИЧЕСКИМ адресам.
@    Никаких вызовов в .text (VMA 0xC0000000+) до включения MMU!
@
@ ⚠️ ARMv6: НЕТ инструкций isb/dsb/dmb (UAL). Используем CP15:
@    ISB = mcr p15, 0, r0, c7, c5, 4
@    DSB = mcr p15, 0, r0, c7, c10, 4
@    DMB = mcr p15, 0, r0, c7, c10, 5
@
@ ⚠️ Day 52 Zero Trust Identity Teardown:
@    Ядро удалит identity mapping (lower 512 MB) сразу после входа в C.
@    Все стеки исключений ДОЛЖНЫ быть переведены в виртуальные адреса
@    (Direct Map 0xC0000000+), иначе первый же Data Abort уронит CPU
@    в бесконечный цикл (Double Fault) при попытке push в невалидный стек.
@ ==========================================================================

.section .boot.text, "ax"
.arm
.align 2

@ ==========================================================================
@ ENTRY POINT
@ ==========================================================================
.global _start
_start:
    @ --- Save boot registers (r0=0, r1=machine, r2=ATAGS) ---
    ldr r3, =_saved_machine_type
    str r1, [r3]
    ldr r3, =_saved_atags
    str r2, [r3]

    @ --- Disable IRQ/FIQ, ensure SVC mode ---
    cpsid if, #0x13           @ CPSR = SVC mode | I=1 | F=1

    @ --- Invalidate caches & TLB (clean state) ---
    mov r0, #0
    mcr p15, 0, r0, c7, c7, 0    @ Invalidate I+D caches
    mcr p15, 0, r0, c8, c7, 0    @ Invalidate TLB (TLBIALL)
    mcr p15, 0, r0, c7, c10, 4   @ DSB

    @ --- Disable MMU, caches, branch prediction (clean boot) ---
    mrc p15, 0, r0, c1, c0, 0    @ Read SCTLR
    bic r0, r0, #(1 << 0)        @ M=0: MMU off
    bic r0, r0, #(1 << 2)        @ C=0: D-cache off
    bic r0, r0, #(1 << 12)       @ I=0: I-cache off
    bic r0, r0, #(1 << 11)       @ Z=0: Branch prediction off
    mcr p15, 0, r0, c1, c0, 0    @ Write SCTLR
    mov r0, #0
    mcr p15, 0, r0, c7, c5, 4   @ ISB (ARMv6: CP15)

    @ --- Setup stacks for each CPU mode (PHYSICAL addresses) ---
    bl setup_stacks

    @ --- Clear BSS (physical addresses) ---
    bl clear_bss

    @ --- Build Translation Table (single TTBR0) ---
    bl build_page_tables

    @ --- Configure and enable MMU ---
    bl enable_mmu

    @ --- Jump to Higher Half ---
    ldr pc, =_higher_half_entry

@ ==========================================================================
@ SETUP STACKS (PHYSICAL ADDRESSES — before MMU)
@ ==========================================================================
@ ARM1176: отдельные банки SP для каждого режима.
@ Стеки в .boot.bss (physical), доступны до и после MMU.
@
@ ⚠️ Эти физические адреса станут НЕВАЛИДНЫМИ после Identity Teardown
@    в arm_kernel_main(). Они будут перезагружены в _higher_half_entry.
@ ==========================================================================
setup_stacks:
    @ SVC mode (kernel stack)
    cps #0x13                   @ SVC mode
    ldr sp, =_svc_stack_top

    @ IRQ mode
    cps #0x12                   @ IRQ mode
    ldr sp, =_irq_stack_top

    @ FIQ mode
    cps #0x11                   @ FIQ mode
    ldr sp, =_fiq_stack_top

    @ Abort mode
    cps #0x17                   @ ABT mode
    ldr sp, =_abt_stack_top

    @ Undefined mode
    cps #0x1B                   @ UND mode
    ldr sp, =_und_stack_top

    @ System/User mode (kernel thread, later)
    cps #0x1F                   @ SYS mode
    ldr sp, =_sys_stack_top

    @ Back to SVC
    cps #0x13
    bx lr

@ ==========================================================================
@ CLEAR BSS (PHYSICAL ADDRESSES)
@ ==========================================================================
@ _bss_start/_bss_end — VMA (0xC0000000+).
@ До MMU работаем по физическим адресам: phys = virt - 0xC0000000.
@ ==========================================================================
clear_bss:
    ldr r0, =_bss_start
    ldr r1, =_bss_end
    ldr r3, =0xC0000000
    sub r0, r0, r3              @ phys = virt - KERNEL_VMA
    sub r1, r1, r3
    mov r2, #0
1:
    cmp r0, r1
    strlt r2, [r0], #4
    blt 1b
    bx lr

@ ==========================================================================
@ BUILD PAGE TABLES (v3: один TTBR0, без TTBR1)
@ ==========================================================================
@ TTBR0: 4096 entries × 4B = 16 KB (16KB aligned)
@
@ Маппинг (Section, 1MB pages):
@   [0-511]     0x00000000 → 0x00000000  Lower 512MB (Identity - TO BE TORN DOWN)
@   [512-527]   0x20000000 → 0x20000000  Peripherals (16MB, Device)
@   [3072-3327] 0xC0000000 → 0x00000000  Higher Half RAM (256MB)
@   [3584-3599] 0xE0000000 → 0x20000000  Higher Half Peripherals (16MB)
@   Остальные: Fault (0x00000000)
@
@ AP = 0b01 (Kernel RW, User None) — на старте нет user процессов.
@ Domain 0 = Kernel (Manager в DACR).
@ ==========================================================================
build_page_tables:
    push {r4-r10, lr}

    @ --- Zero TTBR0 (16 KB) ---
    ldr r0, =_ttbr0_base
    mov r1, #0
    mov r2, #(4096 * 4)         @ 16384 bytes
1:
    subs r2, r2, #4
    str r1, [r0, r2]
    bne 1b

    @ --- [0-511] Identity map Lower 512MB (RAM + reserved) ---
    @ Section descriptor: PA | TEX=001 | AP=01 | C=1 | B=1 | Type=10
    @ = PA | 0x1000 | 0x400 | 0x8 | 0x4 | 0x2 = PA | 0x140E
    ldr r0, =_ttbr0_base
    mov r3, #0                  @ PA = 0x00000000
    mov r4, #512                @ 512 entries (512 MB)
    ldr r5, =0x140E             @ RAM flags
2:
    orr r6, r3, r5              @ descriptor = PA | flags
    str r6, [r0], #4
    ldr r7, =0x100000
    add r3, r3, r7              @ PA += 1MB
    subs r4, r4, #1
    bne 2b

    @ --- [512-527] Identity map Peripherals: 0x20000000 (16 × 1MB) ---
    @ Section descriptor: PA | AP=01 | XN=1 | B=1 | Type=10
    @ = PA | 0x400 | 0x10 | 0x4 | 0x2 = PA | 0x416
    ldr r0, =_ttbr0_base
    add r0, r0, #(512 * 4)      @ Entry 512 (VA 0x20000000)
    ldr r3, =0x20000000         @ PA = 0x20000000
    mov r4, #16                 @ 16 entries (16 MB)
    ldr r5, =0x416              @ Device flags (XN, no cache)
3:
    orr r6, r3, r5
    str r6, [r0], #4
    ldr r7, =0x100000
    add r3, r3, r7
    subs r4, r4, #1
    bne 3b

    @ --- [3072-3327] Higher Half RAM: 0xC0000000 → 0x00000000 (256 × 1MB) ---
    ldr r0, =_ttbr0_base
    add r0, r0, #(3072 * 4)     @ Entry 3072 = VA 0xC0000000
    mov r3, #0                  @ PA = 0x00000000
    mov r4, #256                @ 256 entries (256 MB)
    ldr r5, =0x140E             @ RAM flags
4:
    orr r6, r3, r5
    str r6, [r0], #4
    ldr r7, =0x100000
    add r3, r3, r7
    subs r4, r4, #1
    bne 4b

    @ --- [3584-3599] Higher Half Peripherals: 0xE0000000 → 0x20000000 ---
    ldr r0, =_ttbr0_base
    add r0, r0, #(3584 * 4)     @ Entry 3584 = VA 0xE0000000
    ldr r3, =0x20000000         @ PA = 0x20000000
    mov r4, #16                 @ 16 entries (16 MB)
    ldr r5, =0x416              @ Device flags
5:
    orr r6, r3, r5
    str r6, [r0], #4
    ldr r7, =0x100000
    add r3, r3, r7
    subs r4, r4, #1
    bne 5b

    pop {r4-r10, lr}
    bx lr

@ ==========================================================================
@ ENABLE MMU (v3: один TTBR0, без TTBR1)
@ ==========================================================================
@ 1. Set TTBR0 (16KB aligned)
@ 2. Set TTBR1 = 0 (не используется)
@ 3. Set DACR: Domain 0 = Manager
@ 4. Enable MMU (SCTLR.M = 1)
@ ==========================================================================
enable_mmu:
    @ --- TTBR0 ---
    ldr r0, =_ttbr0_base
    mcr p15, 0, r0, c2, c0, 0   @ Write TTBR0

    @ --- TTBR1 = 0 (не используется) ---
    mov r0, #0
    mcr p15, 0, r0, c2, c0, 1   @ Write TTBR1 = 0

    @ --- DACR: Domain 0 = Manager, Domain 1 = Client ---
    @ Domain 0 (Kernel) = Manager (0b11) -> bits [1:0] = 11
    @ Domain 1 (User)   = Client  (0b01) -> bits [3:2] = 01
    @ Value = 0x07. Client mode enables AP bit enforcement for user pages.
    mov r0, #0x07
    mcr p15, 0, r0, c3, c0, 0   @ Write DACR

    @ --- ISB: ensure TTBR/DACR writes complete ---
    mov r0, #0
    mcr p15, 0, r0, c7, c5, 4   @ ISB (ARMv6)

    @ --- Enable MMU ---
    mrc p15, 0, r0, c1, c0, 0   @ Read SCTLR
    orr r0, r0, #(1 << 0)       @ M=1: MMU enable
    @ Caches OFF на старте (включим в arm_cpu.c после проверки)
    mcr p15, 0, r0, c1, c0, 0   @ Write SCTLR
    mov r0, #0
    mcr p15, 0, r0, c7, c5, 4   @ ISB (ARMv6)

    bx lr

@ ==========================================================================
@ HIGHER HALF ENTRY (VMA = 0xC0000000+)
@ ==========================================================================
@ После включения MMU: PC = 0xC0000000+.
@ Теперь работаем по виртуальным адресам.
@
@ [Day 52] Zero Trust Identity Teardown fix:
@ Все стеки (SVC, IRQ, FIQ, ABT, UND, SYS) были инициализированы физическими
@ адресами в setup_stacks (до MMU). После удаления identity mapping в
@ arm_vmm_teardown_identity() они становятся невалидными.
@
@ Мы ОБЯЗАНЫ перегрузить их, добавив KERNEL_VMA (0xC0000000), чтобы
@ исключения могли корректно сохранять регистры в Direct Map.
@
@ Также _saved_atags и _saved_machine_type находятся в .boot.data
@ (физические адреса). Мы читаем их через Direct Map.
@ ==========================================================================
.section .text
.arm
.align 2

.global _higher_half_entry
_higher_half_entry:
    @ --- Set VBAR to vector table (arm_vectors.S) ---
    ldr r0, =_vector_table
    mcr p15, 0, r0, c12, c0, 0     @ Write VBAR
    mov r0, #0
    mcr p15, 0, r0, c7, c5, 4     @ ISB (ARMv6: ensure VBAR write completes)

    @ --- Reload ALL stacks to Higher Half virtual addresses ---
    @ Символы стеков имеют VMA = физический адрес (в .boot.bss).
    @ Добавляем KERNEL_VMA (0xC0000000) для получения виртуального адреса.
    ldr r1, =0xC0000000

    cps #0x13                   @ SVC mode
    ldr r0, =_svc_stack_top
    add r0, r0, r1
    mov sp, r0

    cps #0x12                   @ IRQ mode
    ldr r0, =_irq_stack_top
    add r0, r0, r1
    mov sp, r0

    cps #0x11                   @ FIQ mode
    ldr r0, =_fiq_stack_top
    add r0, r0, r1
    mov sp, r0

    cps #0x17                   @ ABT mode
    ldr r0, =_abt_stack_top
    add r0, r0, r1
    mov sp, r0

    cps #0x1B                   @ UND mode
    ldr r0, =_und_stack_top
    add r0, r0, r1
    mov sp, r0

    cps #0x1F                   @ SYS mode
    ldr r0, =_sys_stack_top
    add r0, r0, r1
    mov sp, r0

    cps #0x13                   @ Back to SVC

    @ --- Load saved boot params (PHYS_TO_VIRT for .boot.data symbols) ---
    ldr r2, =_saved_atags
    add r2, r2, r1              @ virtual address in Direct Map
    ldr r0, [r2]                @ r0 = ATAGS physical address (passed to C)
    
    ldr r2, =_saved_machine_type
    add r2, r2, r1              @ virtual address in Direct Map
    ldr r1, [r2]                @ r1 = machine type (passed to C)

    @ --- Call C kernel ---
    bl arm_kernel_main

    @ --- Halt (should never return) ---
1:
    cpsid i
    wfi
    b 1b

@ ==========================================================================
@ DATA: Saved boot registers (physical, .boot.data)
@ ==========================================================================
.section .boot.data, "aw"
.align 2

_saved_machine_type: .word 0
_saved_atags:        .word 0

@ ==========================================================================
@ BSS: Page Table + Stacks (physical, .boot.bss)
@ ==========================================================================
.section .boot.bss, "aw"
.align 14                       @ 16KB alignment for TTBR0

.global _ttbr0_base
_ttbr0_base: .space (4096 * 4) @ 16 KB (единственная таблица)

@ --- Stacks (physical) ---
.align 4

_svc_stack_base: .space (16 * 1024)   @ 16 KB SVC stack
.global _svc_stack_top
_svc_stack_top:

_irq_stack_base: .space (4 * 1024)    @ 4 KB IRQ stack
.global _irq_stack_top
_irq_stack_top:

_fiq_stack_base: .space (1 * 1024)    @ 1 KB FIQ stack
.global _fiq_stack_top
_fiq_stack_top:

_abt_stack_base: .space (1 * 1024)    @ 1 KB ABT stack
.global _abt_stack_top
_abt_stack_top:

_und_stack_base: .space (1 * 1024)    @ 1 KB UND stack
.global _und_stack_top
_und_stack_top:

_sys_stack_base: .space (16 * 1024)   @ 16 KB SYS stack
.global _sys_stack_top
_sys_stack_top: