/* user_linker.ld - Linker script для user-space программ */
ENTRY(_start)

SECTIONS
{
    /* Стандартный адрес начала ELF в Linux (в диапазоне User Space) */
    . = 0x08048000;
    
    .text : {
        *(.text)
        *(.text.*)
    }
    
    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }
    
    .data : {
        *(.data)
        *(.data.*)
    }
    
    .bss : {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }
    
    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame)
    }
}