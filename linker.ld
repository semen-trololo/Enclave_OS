ENTRY(_start)

SECTIONS
{
    /* Физическая загрузка по адресу 1 MB */
    . = 0x00100000;
    
    /* НОВАЯ МЕТКА: Начало всего образа ядра (включая трамплин) */
    _boot_start = .;

    /* 1. ФИЗИЧЕСКИЕ СЕКЦИИ (Трамплин, Multiboot header, временные таблицы) */
    .boot : ALIGN(4K)
    {
        *(.multiboot)
        *(.boot)
    }

    .boot.data : ALIGN(4K)
    {
        *(.boot.data)
    }

    .boot.bss : ALIGN(4K)
    {
        *(.boot.bss)
    }

    /* Переход в виртуальное пространство (Higher Half) */
    . += 0xC0000000;
    _kernel_start = .;

    /* 2. ВИРТУАЛЬНЫЕ СЕКЦИИ */
    .text ALIGN(4K) : AT(ADDR(.text) - 0xC0000000)
    {
        *(.text)
    }

    .rodata ALIGN(4K) : AT(ADDR(.rodata) - 0xC0000000)
    {
        *(.rodata)
    }

    .data ALIGN(4K) : AT(ADDR(.data) - 0xC0000000)
    {
        *(.data)
    }

    .bss ALIGN(4K) : AT(ADDR(.bss) - 0xC0000000)
    {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }

    . = ALIGN(4K);
    _kernel_end = .;
}