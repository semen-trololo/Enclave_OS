/* user_linker.ld */
ENTRY(_start)

SECTIONS {
    /* Пользовательский код начинается с 0x01000000 (защита от NULL pointer) */
    . = 0x01000000;

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
    
    /* Отбрасываем ненужные секции компилятора */
    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame)
    }
}
