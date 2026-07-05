# БАЗА ЗНАНИЙ: РАЗРАБОТКА ОС С НУЛЯ (BARE METAL OS)

## [БЛОК СОХРАНЕНИЯ КОНТЕКСТА - MEMSAVE]

### Проект
- **Название**: Bare Metal OS (учебно-исследовательская лабораторная работа)
- **Роль**: Сеньор-разработчик (ментор) и студент
- **Архитектура**: x86, 32-битный защищённый режим
- **Загрузчик**: Multiboot 1 (GRUB)
- **Формат дистрибутива**: Загрузочный ISO-образ (grub-mkrescue)
- **Инструменты**: i686-elf-gcc (или i686-linux-gnu-gcc), NASM, GNU ld, Make, QEMU, Git, xorriso
- **Среда разработки**: Linux (Kali / Debian / Arch)

### Текущий прогресс
**Статус**: Дни 1-4 завершены полностью. День 6.1 (Higher Half) и 6.2 (E820) завершены.

**Выполнено (Ключевые вехи)**:
- **Загрузка и Видео**: Multiboot header, Bochs VBE (1024x768x32bpp), VGA fallback, Strategy Pattern в klib.
- **Прерывания и Устройства**: GDT/IDT, ISR/IRQ ASM-заглушки, PIC 8259A (INT 32-47), PS/2 клавиатура (Ring Buffer), PIT таймер (1000 Гц).
- **Управление памятью**: PMM (Битмап 512 МБ, E820 парсинг, Safe by Default), VMM (Direct Map, динамические PT, TLB flush), Kernel Heap (Buddy System 32 MB).
- **Архитектурный рефакторинг**: Higher Half Kernel (0xC0000000), Bulletproof Mapping (всей RAM), Изоляция от ABI (глобальные переменные для Multiboot).
- **Разделение привилегий (День 4)**:
  - TSS (Task State Segment) и аппаратное переключение стеков.
  - Расширенная GDT (User Code/Data сегменты с DPL=3).
  - Инфраструктура Syscalls (INT 0x80, DPL=3, sys_write/sys_exit).
  - **Успешный переход в Ring 3 (User Mode)** через `IRET`.
  - **Context Hijacking**: возврат управления из Ring 3 в Ring 0 через модификацию стека в `sys_exit`.

### Структура файлов
1. `include/` - gdt.h, idt.h, isr.h, keyboard.h, klib.h, pic.h, port_io.h, shell.h, vga.h, timer.h, pmm.h, paging.h, heap.h, framebuffer.h, tss.h, syscall.h, multiboot.h, serial.h
2. `boot.asm` - Multiboot, VBE init, Higher Half маппинг, трамплин в kernel_main.
3. `kernel.c` - точка входа, последовательность Bootstrap.
4. `linker.ld` - карта памяти с метками `_kernel_start` и `_kernel_end`.
5. `Makefile` - автоматизация сборки ядра и генерации ISO (grub-mkrescue).
6. `grub.cfg` - конфигурация загрузчика GRUB для ISO.
7. `vga.c`, `klib.c`, `gdt.c`, `idt.c`, `isr.c`, `pic.c`, `keyboard.c`, `shell.c`, `timer.c`, `serial.c`
8. `framebuffer.c` - графический драйвер (Bochs VBE).
9. `descriptors_flush.asm`, `isr_asm.asm`, `usermode.asm` - ASM-заглушки.
10. `pmm.c`, `paging.c`, `heap.c` - подсистемы памяти.
11. `tss.c`, `syscall.c` - разделение привилегий и системные вызовы.
12. `user_task.c` - первый процесс, выполняемый в Ring 3.
13. `.gitignore`

---

## КРИТИЧЕСКИЕ ТЕХНИЧЕСКИЕ НЮАНСЫ (ОБЯЗАТЕЛЬНО К ПРОЧТЕНИЮ)

### 1. ОБЕЗВРЕЖИВАНИЕ СИСТЕМНОГО GCC И ОТКЛЮЧЕНИЕ SSE
Системный компилятор по умолчанию включает PIE, Stack Protector и SSE-инструкции.
В Makefile **ОБЯЗАТЕЛЬНЫ** флаги:
```makefile
CFLAGS += -fno-pie -fno-pic -fno-stack-protector -ffreestanding -nostdlib
CFLAGS += -mno-sse -mno-sse2 -mno-mmx -mno-3dnow -mincoming-stack-boundary=2
LDFLAGS += -no-pie

    SSE/MMX: Отключение SSE в ядре — это индустриальный стандарт (Linux, Windows). Ядро не должно использовать FPU/SSE напрямую, чтобы избежать необходимости сохранять FPU-контекст при каждом прерывании (что критически замедлит систему). Ring 3 программы смогут использовать SSE позже, когда появится планировщик с FPU Context Switching.
    Stack Boundary: Флаг -mincoming-stack-boundary=2 снимает требование 16-байтного выравнивания стека, что предотвращает падения в ASM-заглушках прерываний.

2. ВЫРАВНИВАНИЕ СТРУКТУР MMU
Page Directory и Page Tables ДОЛЖНЫ иметь атрибут __attribute__((aligned(4096))). Иначе процессор сгенерирует Page Fault при загрузке адреса в CR3.
3. PAGE DIRECTORY ENTRY (PDE) USER BIT
В x86 двухуровневая трансляция памяти требует, чтобы бит U/S (User/Supervisor) был установлен И В PDE, И В PTE для доступа из Ring 3.
Если PDE имеет U/S=0 (Supervisor), то весь 4-мегабайтный регион недоступен из Ring 3, даже если в дочерних PTE стоит U/S=1.
РЕШЕНИЕ: В paging_init() необходимо пропатчить все существующие PDE:
for (uint32_t i = 0; i < 1024; i++) {
    if (boot_page_directory[i] & PAGE_PRESENT) {
        boot_page_directory[i] |= PAGE_USER;
    }
}
4. TSS И АППАРАТНОЕ ПЕРЕКЛЮЧЕНИЕ СТЕКА
При прерывании из Ring 3 процессор аппаратно читает SS0 и ESP0 из структуры TSS и переключается на безопасный стек ядра. Без корректно настроенного TSS (и загруженного через ltr регистра TR) любое прерывание (например, таймер) из User Mode приведет к Triple Fault.
5. ПЕРЕХОД В RING 3 (USER MODE) И ВОЗВРАТ (CONTEXT HIJACKING)
Единственный легальный способ перейти из Ring 0 в Ring 3 — инструкция IRET.
Архитектура "Фальшивого прерывания" (в usermode.asm):
mov ax, 0x23          ; User Data Segment (0x20 | 3)
mov ds, ax ...
push 0x23             ; SS
push user_esp         ; ESP
pushf; pop eax; or eax, 0x200; push eax ; EFLAGS с включенным IF!
push 0x1B             ; CS (0x18 | 3)
push user_task        ; EIP
iret
Возврат в Ring 0 (Context Hijacking):
Вместо сложного хака стека для iret, sys_exit просто делает sti (разрешает прерывания) и напрямую вызывает shell_run(). Мы уже находимся в Ring 0 на безопасном стеке ядра (благодаря TSS), поэтому это математически безопасно до появления планировщика (День 7).
6. РЕГИСТРЫ УПРАВЛЕНИЯ И ЗАЩИТА

    CR3 = ФИЗИЧЕСКИЙ адрес Page Directory.
    CR0.PG (бит 31) = 1 (включить Paging).
    CR0.WP (бит 16) = 1 (Write Protect - запрет ядру писать в RO страницы).

7. VOLATILE ДЛЯ АСИНХРОННЫХ ДАННЫХ
Переменные, изменяемые в контексте прерываний (head, tail в Ring Buffer, tick_count в PIT), ДОЛЖНЫ быть объявлены как volatile.
8. DIRECT MAP И БЕЗОПАСНОСТЬ СТЕКА

    Direct Map: Вся физическая RAM (512 МБ) замаплена идентично (Virt == Phys + 0xC0000000).
    Стек: Большие буферы (например, для стресс-теста PMM) ОБЯЗАНЫ быть static (секция .bss). Локальный массив на стеке вызовет Stack Overflow и Triple Fault.

9. BUDDY SYSTEM И АРИФМЕТИКА БЛИЗНЕЦОВ
Адрес buddy-блока вычисляется через XOR: buddy = node ^ size. Это обеспечивает O(1) поиск близнеца для слияния (merge) при kfree().
10. МАППИНГ FRAMEBUFFER И TLB

    Bochs VBE LFB (0xFD000000) должен быть замаплен ДО включения CR0.PG.
    После записи новой PDE ОБЯЗАТЕЛЬНО нужен CR3 reload (сброс TLB), иначе MMU не увидит новую Page Table.

11. ПОРЯДОК ИНИЦИАЛИЗАЦИИ (BOOTSTRAP)
fb_init() -> pmm_init() -> paging_init() -> heap_init(). До paging_init() CR0.PG=0, можно писать напрямую в физические адреса.
12. MULTIBOOT E820 И SAFE BY DEFAULT
Сначала ВСЕ страницы заняты. Затем парсится E820 (освобождение type=1). В конце "пробиваются дыры" (резервирование 1 МБ и ядра через Linker Symbols _kernel_start/_kernel_end).
13. ОГРАНИЧЕНИЯ K_PRINTF
Наш k_printf поддерживает %d, %u, %x, %p, %s, %c, %%. Модификаторы ширины (%08x) НЕ ПОДДЕРЖИВАЮТСЯ и ломают парсинг va_list.
14. BULLETPROOF HIGHER HALF (4MB БАРЬЕР)
В boot.asm НЕЛЬЗЯ маппить только первые 4 МБ. Нужно скопировать ВСЕ 128 PT из Identity Map в Higher Half (индексы 768+). Иначе глобальные переменные > 4 МБ вызовут Page Fault.
15. ИЗОЛЯЦИЯ ОТ ABI (BOOT ARGUMENTS)
Параметры Multiboot НИКОГДА не передаются через стек kernel_main. Они сохраняются в глобальные переменные в .boot.data. Сигнатура kernel_main — void.
АРХИТЕКТУРА ОС (ПОДСИСТЕМЫ)
[ЗАГРУЗКА И ИНИЦИАЛИЗАЦИЯ]

    GRUB (ISO) -> Protected Mode (CR0.PG=0).
    boot.asm: VBE init -> Higher Half Mapping -> Передача fb_params и multiboot_info через глобальные переменные.
    kernel_main: FB init -> GDT -> TSS -> IDT -> Syscalls -> PIC -> Keyboard -> Timer -> PMM -> VMM (CR0.PG=1) -> Heap -> User Task -> Shell.

[ПРЕРЫВАНИЯ, ПРИВИЛЕГИИ И УСТРОЙСТВА]

    GDT: Flat Model + User Segments (DPL=3) + TSS.
    IDT: 256 векторов. INT 0x80 имеет DPL=3 (0xEE).
    TSS: 104 байта, ltr. Обеспечивает ESP0 для Ring 3 -> Ring 0 switch.
    Syscalls: Таблица указателей. Вызов через INT 0x80 (EAX = номер).
    ISR: ASM stub -> C handler -> iret.
    PIC: Master/Slave remap на INT 32-47. EOI: 0x20 / 0xA0.
    Клавиатура: IRQ1, Ring Buffer, k_getchar().
    Таймер PIT: 1000 Гц, k_sleep().

ИНСТРУКЦИЯ ПО СБОРКЕ И ЗАПУСКУ (ISO)
Теперь проект собирается не как сырой бинарник (kernel.bin), а как полноценный загрузочный ISO-образ с использованием GRUB. Это позволяет тестировать ОС на реальном железе (через Rufus/Ventoy) и в любых гипервизорах.
1. Требования к системе

    Кросс-компилятор: i686-elf-gcc (или системный gcc с флагами -m32 -ffreestanding).
    Ассемблер: nasm.
    Инструменты создания ISO: grub-pc-bin, grub-common, xorriso, mtools.
    (В Debian/Ubuntu/Kali: sudo apt install grub-pc-bin grub-common xorriso mtools)

2. Структура каталогов для ISO
В корне проекта должна быть папка isodir/ со следующей структурой:
isodir/
└── boot/
    ├── grub/
    │   └── grub.cfg      <-- Конфигурация загрузчика
    └── kernel.bin        <-- Скомпилированное ядро (копируется сюда Makefile'ом)
Содержимое isodir/boot/grub/grub.cfg:
set timeout=0
set default=0

menuentry "Bare Metal OS" {
    multiboot /boot/kernel.bin
    boot
}
Сборка (Makefile targets)

    make или make all: Компилирует все .c и .asm файлы, линкует их в isodir/boot/kernel.bin с использованием linker.ld.
    make iso:
        Выполняет сборку ядра.
        Вызывает grub-mkrescue для упаковки isodir/ в файл bare_metal_os.iso.
    make run: Запускает QEMU с созданным ISO-образом.
    make clean: Удаляет объектные файлы, kernel.bin и *.iso.

4. Запуск в QEMU
qemu-system-i386 -cdrom bare_metal_os.iso -m 512M -serial stdio
    -cdrom: Указывает QEMU загружаться с ISO через BIOS/GRUB.
    -m 256M: Выделяет 256 МБ RAM (наш PMM рассчитан на 512 МБ, но 256 МБ достаточно для тестов).
    -serial stdio: Выводит отладочные логи serial_print() прямо в терминал хоста.

ПЛАН РАЗВИТИЯ ОС (ДОРОЖНАЯ КАРТА)
[ДЕНЬ 4: ЗАВЕРШЕНО] Privilege Separation

    4.3.1 Task State Segment (TSS) и ltr.
    4.3.2 Переключение в User Mode (Ring 3) через IRET.
    4.3.3 Системные вызовы (INT 0x80, DPL=3, sys_write/sys_exit).
    4.3.4 Базовая защита памяти (Вариант А: PAGE_USER для всех страниц).
    4.3.5 Context Hijacking (возврат из Ring 3 в Ring 0).

[ДЕНЬ 5] Оптимизация графического режима и производительности

    5.1 Оптимизация PMM (__builtin_ctz, поиск по 32-битным словам).
    5.2 Оптимизация framebuffer (Double buffering, Dirty regions).
    5.3 Улучшенный шрифт и рендеринг (Anti-aliasing, Unicode).

[ДЕНЬ 6] Архитектурный рефакторинг и продвинутая память

    6.1 Higher Half Kernel (0xC0000000) и Bulletproof Mapping.
    6.2 Multiboot Memory Map (E820) и Safe by Default.
    6.3 On-demand Paging (Lazy allocation, Zero-filled pages, PF handler).

[ДЕНЬ 7] Процессы и планировщик

    7.1 Process Control Block (PCB) и Linked List.
    7.2 Context Switching (save/restore registers, CR3 switch).
    7.3 Round-Robin Scheduler (Preemption по таймеру).
    7.4 FPU Context Switching (fxsave/fxrstor для поддержки SSE в Ring 3).
    7.5 Настоящая изоляция памяти (Вариант Б: отдельные Page Directories для процессов).

[ДЕНЬ 8] Файловая система и Storage

    8.1 RAM Disk (tmpfs) и Inode структура.
    8.2 VFS layer и базовые операции (open, read, write, ls).
    8.3 ATA/IDE Driver (PIO mode, LBA28).

[ДЕНЬ 9] User Space и ELF Loader

    9.1 ELF Format Parser (Program headers, LOAD segments).
    9.2 ELF Loader (Загрузка .text/.data в User Space).
    9.3 Разделение библиотек и передача argc/argv.

[ДЕНЬ 10] Улучшение Shell и Debug Tools

    10.1 Advanced Shell (History, Tab completion, Pipes, Redirects).
    10.2 Debug Tools (ps, top, meminfo, dmesg).

[ДЕНЬ 11] Polish, Testing и Документация

    11.1 Testing Suite (Unit tests, Stress tests).
    11.2 Документация (README, ARCHITECTURE, API).
    11.3 CI/CD (GitHub Actions, headless QEMU tests).

СОВЕТЫ ОТ МЕНТОРА (CODE REVIEW & BEST PRACTICES)

    Приоритеты: Стабильность > Фичи. Тесты перед коммитом. Документация параллельно с кодом.
    Debug Techniques:
        serial_print() для логирования без VGA.
        QEMU -d int -D qemu.log для трассировки прерываний.
        Анализ Triple Fault через последние записи в serial-логе.
    Security: User pointer validation, NX bit, ASLR (в будущем).
    Производительность: Batch page allocations, Lazy TLB invalidation.
    Сборка ISO: Всегда тестируй финальную версию через make iso и загрузку с -cdrom. Прямой запуск kernel.bin через -kernel bypass'ит GRUB и может скрыть проблемы с Multiboot-заголовком или выравниванием.

========================================================================
[КОНЕЦ БАЗЫ ЗНАНИЙ]
