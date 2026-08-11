/* ==========================================================================
 * user_test.ld — Linker script for ARM user ELF test image (Task 2.4)
 * ==========================================================================
 * Цель:
 *   - создать минимальный статический ARM ELF
 *   - один PT_LOAD segment
 *   - virtual address 0x00001000
 *   - R/X, без W (W^X compliant)
 * ========================================================================== */

OUTPUT_ARCH(arm)
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5) ALIGN(0x1000); /* FLAGS(5) = PF_R | PF_X */
}

SECTIONS
{
    /* User code base.
     * Совпадает с ARM_USER_CODE_VA_4K из config.h.
     */
    . = 0x00001000;

    .text :
    {
        KEEP(*(.text .text.*))
        *(.rodata .rodata.*)
    } :text

    /DISCARD/ :
    {
        *(.ARM.exidx*)
        *(.ARM.extab*)
        *(.comment)
        *(.note*)
        *(.ARM.attributes*)
    }
}
