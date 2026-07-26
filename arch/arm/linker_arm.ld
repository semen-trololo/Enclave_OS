/* ==========================================================================
 * linker_arm.ld — Enclave OS ARM Linker Script (v5)
 * ==========================================================================
 * Правило: VMA = LMA + KERNEL_VMA для КАЖДОЙ секции.
 * AT(ADDR(.section) - KERNEL_VMA) гарантирует это.
 * ========================================================================== */

ENTRY(_start)

PHYS_BASE   = 0x00010000;   /* QEMU raspi1ap load address */
KERNEL_VMA  = 0xC0000000;

SECTIONS
{
    /* ======================================================================
     * .boot — Bootstrap (PHYSICAL, MMU OFF)
     * VMA == LMA
     * ====================================================================== */
    . = PHYS_BASE;

    .boot : {
        _boot_start = .;
        KEEP(*(.boot.text))
        KEEP(*(.boot.data))
        KEEP(*(.boot.bss))
        . = ALIGN(4096);
        _boot_end = .;
    }

    /* ======================================================================
     * Higher Half: VMA = LMA + KERNEL_VMA
     * ====================================================================== */
    . = KERNEL_VMA + _boot_end;

    .text ALIGN(4096) : AT(ADDR(.text) - KERNEL_VMA) {
        _kernel_start = .;
        _text_start = .;
        *(.text .text.*)
        *(.gnu.linkonce.t.*)
        _text_end = .;
    }

    .rodata ALIGN(4096) : AT(ADDR(.rodata) - KERNEL_VMA) {
        _rodata_start = .;
        *(.rodata .rodata.*)
        *(.gnu.linkonce.r.*)
        _rodata_end = .;
    }

    .data ALIGN(4096) : AT(ADDR(.data) - KERNEL_VMA) {
        _data_start = .;
        *(.data .data.*)
        *(.gnu.linkonce.d.*)
        _data_end = .;
    }

    .bss ALIGN(4096) : AT(ADDR(.bss) - KERNEL_VMA) {
        _bss_start = .;
        *(.bss .bss.*)
        *(.gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(4096);
        _bss_end = .;
    }

    _kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame*)
        *(.ARM.exidx*)
        *(.ARM.extab*)
    }
}