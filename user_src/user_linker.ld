/* user_linker.ld - Простой linker script для User Space ELF */
ENTRY(_start)

SECTIONS
{
    . = 0x08048000; /* Базовый адрес загрузки */

    /* === Сегмент КОДА (Read + Execute) === */
    .text : {
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
    }

    /* Выравнивание на новую страницу (4KB) для W^X Enforcement */
    . = ALIGN(4096);

    /* === Сегмент ДАННЫХ (Read + Write) === */
    .data : {
        *(.data)
        *(.data.*)
    }

    .bss : {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }

    /* === Утилизация мусора === */
    /DISCARD/ : {
        *(.note.gnu.build-id)
        *(.comment)
        *(.note.*)
    }
}