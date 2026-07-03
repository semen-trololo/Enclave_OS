ВСЕГДА ТОЛЬКО ДОБАВЛЯЙ НОВЫЕ ЗНАНИЯ, ЕСЛИ НЕ БЫЛО ИЗМЕНЕНИЯ В КОДЕ ИЛИ АРХИТЕКТУРЕ.
========================================================================
БАЗА ЗНАНИЙ: РАЗРАБОТКА ОС С НУЛЯ (BARE METAL OS)
========================================================================
[БЛОК СОХРАНЕНИЯ КОНТЕКСТА - MEMSAVE]
------------------------------------------------------------------------
[Проект]
Название: Bare Metal OS (учебно-исследовательская лабораторная работа)
Роль: Сеньор-разработчик (ментор) и студент
Архитектура: x86, 32-битный защищённый режим
Загрузчик: Multiboot 1 (GRUB / прямой запуск через QEMU)
Инструменты: i686-linux-gnu-gcc, NASM, GNU ld, Make, QEMU, Git
Среда разработки: Linux (Kali / Debian)

[Текущий прогресс]
Статус: Дни 1-4 завершены полностью. День 6.2 (Multiboot Memory Map) завершен.
Выполнено:
- Multiboot заголовок, стек ядра 256 КБ, linker.ld.
- VGA драйвер 80x50 (теперь как fallback).
- Bochs VBE драйвер: 1024x768 @ 32bpp через I/O порты (0x01CE/0x01CF).
- Framebuffer драйвер с шрифтом 8x16, автоскроллингом и streaming API.
- GDT (Flat Memory Model + User/TSS сегменты), IDT, ISR/IRQ ASM-заглушки.
- Remap PIC 8259A (INT 32-47), PS/2 клавиатура с Ring Buffer.
- Базовый Shell с токенайзером (help, clear, uptime, pmm, heap, syscall, memmap).
- k_printf (variadic), PIT таймер 1000 Гц, k_sleep().
- PMM: битмап на 512 МБ, pmm_alloc_page/free_page, стресс-тесты.
- VMM: Direct Map 512 МБ, vmm_map_page с динамической аллокацией PT, invlpg и TLB flush.
- Kernel Heap 32 MB @ 0xD0000000, Buddy System (Implicit Binary Tree), Block Header.
- Strategy Pattern: klib прозрачно выбирает между framebuffer и VGA.
- TSS (Task State Segment): инициализация, загрузка в TR, настройка ESP0.
- Расширенная GDT: добавлены User Code, User Data и TSS дескрипторы.
- Инфраструктура Syscalls (INT 0x80): DPL=3, таблица сисколлов, sys_write/sys_exit.
- [NEW] Парсинг Multiboot E820 Memory Map (динамическое определение RAM).
- [NEW] Архитектура PMM "Safe by Default" + "Punching Holes" (резервирование).
- [NEW] Использование Linker Symbols (_kernel_start/_kernel_end) для защиты ядра.
- [NEW] Исправление Bootstrap Problem (вывод логов до включения Paging).
- [NEW] Команда `memmap` в Shell для визуализации E820 карты.

[Структура файлов]
1. include/ - gdt.h, idt.h, isr.h, keyboard.h, klib.h, pic.h, port_io.h, shell.h, vga.h, timer.h, pmm.h, paging.h, heap.h, framebuffer.h, tss.h, syscall.h, multiboot.h
2. boot.asm - Multiboot, Bochs VBE инициализация, настройка стека, вызов kernel_main.
3. kernel.c - точка входа, правильная последовательность инициализации (Bootstrap).
4. linker.ld - карта памяти с метками _kernel_start и _kernel_end.
5. Makefile - автоматизация сборки (с флагами обезвреживания GCC).
6. vga.c, klib.c, gdt.c, idt.c, isr.c, pic.c, keyboard.c, shell.c, timer.c
7. framebuffer.c - графический драйвер (Bochs VBE).
8. descriptors_flush.asm, isr_asm.asm, usermode.asm - ASM-заглушки.
9. pmm.c, paging.c, heap.c - подсистемы памяти.
10. tss.c, syscall.c - разделение привилегий и системные вызовы.
11. user_task.c - заготовка для будущего User Mode процесса.
12. .gitignore

========================================================================
КРИТИЧЕСКИЕ ТЕХНИЧЕСКИЕ НЮАНСЫ (ОБЯЗАТЕЛЬНО К ПРОЧТЕНИЮ)
========================================================================
1. ОБЕЗВРЕЖИВАНИЕ СИСТЕМНОГО GCC (Kali/Debian):
Системный компилятор по умолчанию включает PIE и Stack Protector.
В Makefile ОБЯЗАТЕЛЬНЫ флаги:
CFLAGS += -fno-pie -fno-pic -fno-stack-protector -ffreestanding -nostdlib
LDFLAGS += -no-pie
Без этого ядро падает в Triple Fault из-за релоцируемых адресов и отсутствия __stack_chk_fail.

2. ВЫРАВНИВАНИЕ СТРУКТУР MMU:
Page Directory и Page Tables ДОЛЖНЫ иметь атрибут:
__attribute__((aligned(4096)))
Иначе процессор сгенерирует Page Fault при загрузке адреса в CR3.

3. РЕГИСТРЫ УПРАВЛЕНИЯ И ЗАЩИТА:
- CR3 = ФИЗИЧЕСКИЙ адрес Page Directory.
- CR0.PG (бит 31) = 1 (включить Paging).
- CR0.WP (бит 16) = 1 (Write Protect - запрет ядру писать в RO страницы).

4. VOLATILE ДЛЯ АСИНХРОННЫХ ДАННЫХ:
Переменные, изменяемые в контексте прерываний (head, tail в Ring Buffer, tick_count в PIT), ДОЛЖНЫ быть объявлены как volatile. Иначе компилятор закэширует их в регистрах CPU.

5. DIRECT MAP (ЛИНЕЙНОЕ ОТОБРАЖЕНИЕ):
Вся физическая RAM (512 МБ) замаплена идентично (Virt == Phys).
Это индустриальный стандарт, решающий проблему "Курицы и яйца": ядро может кастовать физический адрес к C-указателю `(uint32_t*)phys_addr` для редактирования таблиц страниц без Page Fault.

6. БЕЗОПАСНОСТЬ СТЕКА:
Большие буферы (например, массив адресов для стресс-теста PMM на 131072 элемента) ОБЯЗАНЫ быть `static` (секция .bss). Локальный массив на стеке вызовет Stack Overflow и Triple Fault.

7. РАЗДЕЛЕНИЕ ОТВЕТСТВЕННОСТИ (SEPARATION OF CONCERNS):
Функции ядра (pmm_alloc_page, kmalloc) при ошибках возвращают 0/NULL. Они НЕ ДОЛЖНЫ вызывать k_printf. Вывод ошибки на экран — задача вызывающего кода (Shell).

8. BUDDY SYSTEM И АРИФМЕТИКА БЛИЗНЕЦОВ:
Адрес (или индекс) buddy-блока вычисляется через битовую операцию XOR: `buddy = node ^ size` (или `index ^ 1`). Это обеспечивает O(1) поиск близнеца для слияния (merge) при kfree().

9. МАППИНГ FRAMEBUFFER ЗА ПРЕДЕЛАМИ DIRECT MAP:
Bochs VBE размещает LFB по адресу 0xFD000000 (~4 GB), что за пределами Direct Map (512 MB).
КРИТИЧЕСКИ ВАЖНО: Фреймбуфер должен быть замаплен в Page Tables ДО включения CR0.PG = 1 (внутри paging_init). Иначе первая запись в FB вызовет Page Fault.

10. TLB ИНВАЛИДАЦИЯ ПРИ СОЗДАНИИ НОВЫХ PAGE TABLES:
Когда vmm_map_page() создаёт новую Page Table (PDE был 0, стал Present), процессор может не перечитать PDE из-за кэширования.
ОБЯЗАТЕЛЬНО после записи PDE нужно:
uint32_t cr3;
__asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
__asm__ volatile("mov %0, %%cr3" : : "r"(cr3));
Это сбросит весь TLB и гарантирует, что MMU увидит новую таблицу.

11. ПОРЯДОК ИНИЦИАЛИЗАЦИИ (BOOTSTRAP PROBLEM):
Правильный порядок: fb_init() -> pmm_init() -> paging_init() (внутри которой маппится FB и включается CR0.PG) -> heap_init().
Важно: до вызова paging_init() регистр CR0.PG = 0 (Paging выключен). Это значит, что линейные адреса равны физическим. Ядро может писать напрямую в физический адрес фреймбуфера (0xFD000000) для вывода логов инициализации PMM без настройки Page Tables.

12. IDT GATE DPL ДЛЯ СИСТЕМНЫХ ВЫЗОВОВ:
По умолчанию ISR/IRQ регистрируются с флагом 0x8E (DPL=0). Чтобы Ring 3 код мог легально вызвать INT 0x80, этот вектор ДОЛЖЕН быть зарегистрирован с флагом 0xEE (Present=1, DPL=3). Иначе процессор сгенерирует General Protection Fault (#GP) при попытке user-space кода сделать `int 0x80`.

13. TSS И АППАРАТНОЕ ПЕРЕКЛЮЧЕНИЕ СТЕКА:
При прерывании из Ring 3 процессор аппаратно читает SS0 и ESP0 из структуры TSS и переключается на безопасный стек ядра. Без корректно настроенного TSS (и загруженного через `ltr` регистра TR) любое прерывание (например, таймер) из User Mode приведет к Triple Fault (Invalid TSS / Stack Fault).

14. MULTIBOOT E820 И SAFE BY DEFAULT:
При инициализации PMM сначала ВСЕ страницы в битмапе помечаются как ЗАНЯТЫЕ. Затем парсится E820 карта и освобождаются только регионы с type=1 (Available). В конце "пробиваются дыры" (Punching Holes) — явно резервируются нижний 1 МБ и образ ядра. Это защищает от выделения памяти поверх ACPI/BIOS даже при багах в парсере.
При итерации по E820 массиву шаг указателя равен: `mmap->size + sizeof(uint32_t)`.

15. LINKER SYMBOLS ДЛЯ РЕЗЕРВИРОВАНИЯ ПАМЯТИ:
Вместо хардкода размера ядра (например, "2 МБ"), используются метки `_kernel_start` и `_kernel_end` из linker.ld. В C они объявляются как `extern uint8_t _kernel_start[];` и кастуются в `uintptr_t`. Это гарантирует защиту ядра, стека и статических массивов (.bss) при любом изменении кода.

16. ОГРАНИЧЕНИЯ САМОПИСНОГО K_PRINTF:
Наш k_printf поддерживает только базовые спецификаторы (%d, %u, %x, %p, %s, %c, %%). Модификаторы ширины и заполнения (например, `%08x` или `%6u`) НЕ ПОДДЕРЖИВАЮТСЯ. Их использование ломает парсинг `va_list`, из-за чего спецификатор `%s` может прочитать число из стека вместо указателя на строку, что приведет к чтению невалидной памяти и Kernel Panic (Page Fault).

========================================================================
АРХИТЕКТУРА ОС (ПОДСИСТЕМЫ)
========================================================================
[ЗАГРУЗКА И ИНИЦИАЛИЗАЦИЯ]
1. Multiboot header -> GRUB переключает в Protected Mode (CR0.PG=0).
2. boot.asm: Bochs VBE init через I/O порты -> настройка стека (256 КБ @ stack_top) -> передача fb_params и multiboot_info.
3. kernel_main:
   FB init (прямая запись в_phys_ addr) -> GDT -> TSS -> IDT -> Syscalls -> PIC -> Keyboard -> Timer -> PMM (парсинг E820) -> VMM (маппинг FB + включение CR0.PG=1) -> Heap -> Shell.

[ГРАФИЧЕСКИЙ РЕЖИМ (Bochs VBE)]
Инициализация (Real Mode в boot.asm):
- I/O порты: 0x01CE (index), 0x01CF (data).
- Установка: XRES=1024, YRES=768, BPP=32, LFB enabled.
- Framebuffer физический адрес: 0xFD000000 (стандарт для Bochs VBE).
- Параметры сохраняются в структуру fb_params (address, width, height, pitch, bpp).
Framebuffer драйвер (framebuffer.c):
- Шрифт: встроенный 8x16 (ASCII 32-126, 95 глифов).
- Цвета: 32-bit RGB (0x00RRGGBB).
- Потоковый вывод: fb_putc() с курсором и автоскроллингом.
- Скроллинг: копирование всего framebuffer вверх на одну строку (O(width × height)).
- Позиционный вывод: fb_put_char(x, y), fb_draw_string(x, y).
- API: fb_init(), fb_is_available(), fb_put_pixel(), fb_clear(), fb_fill_rect().

[ВИДЕО АБСТРАКЦИЯ (Strategy Pattern)]
klib.c прозрачно выбирает бэкенд:
- output_char(c): if (fb_is_available()) fb_putc(c); else vga_putc(c);
- k_set_color(vga_fg, vga_bg): синхронизирует VGA-цвета и framebuffer-цвета (маппинг VGA -> RGB).
- k_putchar(c): универсальный вывод символа (для shell input echo).
- k_clear(): очищает framebuffer или VGA в зависимости от режима.
Shell и все подсистемы (PMM, Heap, Timer) используют k_print/k_printf и НЕ зависят от конкретного видео-бэкенда.

[УПРАВЛЕНИЕ ПАМЯТЬЮ]
Физическая память (PMM):
- Битмап: 1 бит = 4 КБ страница. 16 КБ битмапа на 512 МБ RAM.
- Алгоритм First Fit.
- Инициализация: Safe by Default (все занято) -> Парсинг Multiboot E820 (освобождение Available) -> Punching Holes (резервирование нижнего 1 МБ и образа ядра через Linker Symbols).
- API: pmm_alloc_page() -> phys_addr (0 при OOM), pmm_free_page(phys_addr).
- Диагностика: pmm_get_memory_map() для команды `memmap`.
Виртуальная память (VMM):
- 2-уровневая трансляция: Page Directory (10 бит) -> Page Table (10 бит) -> Offset (12 бит).
- vmm_map_page(virt, phys, flags) с динамической аллокацией PT через PMM.
- Инвалидация TLB: invlpg для отдельных страниц, CR3 reload для новых Page Tables.
- Direct Map: первые 512 МБ (Virt == Phys) для bootstrap и редактирования PT.
- Маппинг FB: выполняется внутри paging_init() до включения CR0.PG.
Kernel Heap:
- 32 MB пул @ 0xD0000000 (Pre-allocated через PMM + VMM).
- Buddy System: Implicit Binary Tree Array (16 КБ).
- Block Header: {size, magic=0xDEADBEEF} перед каждым блоком (защита от double-free).
- kmalloc() -> ptr+16, kfree() -> читает header, merge с buddy через XOR.
- MAX_ORDER = 13 (блоки от 4 KB до 32 MB).

[ПРЕРЫВАНИЯ, ПРИВИЛЕГИИ И УСТРОЙСТВА]
GDT: Flat Memory Model + User Segments + TSS (Code 0x9A, Data 0x92, User Code 0xFA, User Data 0xF2, TSS 0x89).
IDT: 256 векторов, загружается через `lidt`. INT 0x80 имеет DPL=3.
TSS: 104 байта, загружается через `ltr`. Обеспечивает ESP0 для Ring 3 -> Ring 0 stack switch.
Syscalls: Таблица указателей (sys_write, sys_exit, sys_yield). Вызов через INT 0x80 (EAX = номер).
ISR: ASM stub (pusha, сегменты) -> C handler -> popa -> `iret`.
PIC: Master (IRQ 0-7) + Slave (IRQ 8-15), remap на INT 32-47.
EOI: 0x20 в Master, 0x20 + 0xA0 для Slave PIC.
Клавиатура: IRQ1 -> INT 33, порт 0x60/0x64, Set 1 scancodes.
- Ring Buffer: volatile head/tail, k_getchar() для Shell.
- Модификаторы: Shift, Ctrl, CapsLock (state machine).
Таймер PIT: Канал 0 @ IRQ0, Режим 3 (Square Wave).
- 1000 Гц (делитель 1193), 1 тик = 1 мс.
- k_sleep(ms): hlt loop с проверкой tick_count.

========================================================================
API REFERENCE (СПРАВОЧНИК БИБЛИОТЕК)
========================================================================
[PORT_IO.H]
void outb(uint16_t port, uint8_t val);
uint8_t inb(uint16_t port);

[VGA.H]
void vga_init(void);
void vga_set_color(uint8_t fg, uint8_t bg);
void vga_putc(char c);  // Поддержка \r \b \t
void clear(void);

[FRAMEBUFFER.H]
void fb_init(framebuffer_info_t* info);
int fb_is_available(void);
void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color);
void fb_clear(uint32_t color);
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_putc(char c);  // Streaming output с курсором
void fb_print(const char* str);
void fb_set_color(uint32_t fg, uint32_t bg);
void fb_set_cursor(uint32_t x, uint32_t y);
uint32_t fb_get_width(void);
uint32_t fb_get_height(void);

[KLIB.H]
size_t k_strlen(const char* str);
void k_print(const char* str);  // Универсальный вывод (FB или VGA)
void k_putchar(char c);         // Универсальный символ (для shell echo)
void k_clear(void);             // Универсальная очистка экрана
void k_set_color(uint8_t vga_fg, uint8_t vga_bg);  // Универсальная установка цвета
int k_strcmp(const char* s1, const char* s2);
int k_strncmp(const char* s1, const char* s2, size_t n);
void* k_memset(void* ptr, int value, size_t num);
void* k_memcpy(void* dest, const void* src, size_t num);
int k_memcmp(const void* s1, const void* s2, size_t n);
void k_itoa(int value, char* buf, int base);
void k_uitoa(unsigned int value, char* buf, int base);
void k_printf(const char* fmt, ...);  // %d %u %x %p %s %c %% (БЕЗ модификаторов ширины!)
int k_atoi(const char* str);
uint32_t k_atoh(const char* str);  // HEX parsing

[TIMER.H]
void timer_init(uint32_t frequency);
uint32_t timer_get_ticks(void);
void k_sleep(uint32_t ms);

[GDT.H / IDT.H / PIC.H / ISR.H]
void gdt_install(void);
void idt_install(void);
void isr_register_handler(uint8_t n, isr_handler_t handler);
void irq_register_handler(uint8_t n, isr_handler_t handler);
void pic_remap(void);
void irq_set_mask(uint8_t IRQline);
void irq_clear_mask(uint8_t IRQline);
void cli(void);
void sti(void);

[TSS.H]
void tss_install(void);
void tss_set_kernel_stack(uint32_t ss0, uint32_t esp0);

[SYSCALL.H]
#define SYS_EXIT    1
#define SYS_WRITE   4
#define SYS_YIELD   24
void syscall_init(void);

[KEYBOARD.H / SHELL.H]
void keyboard_install(void);
char k_getchar(void);
void shell_run(void);

[MULTIBOOT.H]
#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002
#define MULTIBOOT_INFO_MEM_MAP     0x00000040
typedef struct { ... } __attribute__((packed)) multiboot_info_t;
typedef struct { uint32_t size; uint64_t addr; uint64_t len; uint32_t type; } __attribute__((packed)) multiboot_memory_map_t;

[PMM.H]
typedef struct { uint64_t addr; uint64_t len; uint32_t type; } e820_entry_t;
void pmm_init(multiboot_info_t* info);
uint32_t pmm_alloc_page(void);  // Returns 0 on OOM
void pmm_free_page(uint32_t phys_addr);
uint32_t pmm_get_used_pages(void);
uint32_t pmm_get_free_pages(void);
uint32_t pmm_get_total_pages(void);
const e820_entry_t* pmm_get_memory_map(uint32_t* count);

[PAGING.H]
void paging_init(void);
void vmm_map_page(uint32_t virt, uint32_t phys, uint32_t flags);

[HEAP.H]
void heap_init(void);
void* kmalloc(size_t size);
void kfree(void* ptr);
void heap_print_status(void);
void heap_run_tests(void);

[VGA_COLOR PALETTE]
BLACK=0, BLUE=1, GREEN=2, CYAN=3, RED=4, MAGENTA=5, BROWN=6,
LIGHT_GREY=7, DARK_GREY=8, LIGHT_BLUE=9, LIGHT_GREEN=10, LIGHT_CYAN=11,
LIGHT_RED=12, LIGHT_MAGENTA=13, YELLOW=14, WHITE=15

[FRAMEBUFFER COLORS (0x00RRGGBB)]
COLOR_BLACK=0x00000000, COLOR_WHITE=0x00FFFFFF, COLOR_RED=0x00FF0000,
COLOR_GREEN=0x0000FF00, COLOR_BLUE=0x000000FF, COLOR_YELLOW=0x00FFFF00,
COLOR_CYAN=0x0000FFFF, COLOR_MAGENTA=0x00FF00FF, COLOR_ORANGE=0x00FF8000,
COLOR_GREY=0x00808080, COLOR_DARKGREY=0x00404040, COLOR_LIGHT_GREY=0x00C0C0C0

Архитектурная Карта Памяти (То, что мы построили)
Вот как теперь выглядит адресное пространство твоей ОС. Сохрани эту схему, она — фундамент всей системы!

 ВИРТУАЛЬНАЯ ПАМЯТЬ (Virtual Memory - VMM)
 =========================================
 0xFFFFFFFF +-------------------------+
            |      Kernel Space       | (Стек ядра, будущие модули)
 0xFE000000 +-------------------------+
            |                         |
 0xFD000000 +-------------------------+ <-- VBE Framebuffer (1024x768x32)
            |   Видеопамять (LFB)     | (Маппинг с флагом PCD - без кэша!)
 0xFC000000 +-------------------------+
            |                         |
            |      Kernel Heap        | (Динамическая память, kmalloc)
 0xD0000000 +-------------------------+ <-- Начало Kernel Heap (32 MB)
            |                         |
            |      Direct Map         | (Вся физ. RAM доступна здесь)
            |   Virt = Phys + 0xC0... | (Нужно для работы VMM/PMM)
            |                         |
 0xC01XXXXX +-------------------------+ <-- _kernel_start (Код и данные ядра)
            |    .text, .rodata       |
            |    .data, .bss          |
 0xC0000000 +-------------------------+ <-- Начало Higher Half (3 GB)
            |      User Space         | (Пока не мапится, пустота)
 0x00000000 +-------------------------+


 ФИЗИЧЕСКАЯ ПАМЯТЬ (Physical Memory - PMM)
 =========================================
 0xFFFFFFFF +-------------------------+
            |      APIC / IOAPIC      | (Зарезервировано)
 0xFEC00000 +-------------------------+
            |    PCI MMIO HOLE        | (Зарезервировано PMM)
            |  (Включая VBE LFB)      | <-- 0xFD000000 (Фреймбуфер)
 0xE0000000 +-------------------------+
            |      Свободная RAM      | (Выдается pmm_alloc_page)
 0x00400000 +-------------------------+ <-- Конец резерва бут-таблиц
            | Boot Page Tables / GDT  | (Зарезервировано PMM)
 0x00100000 +-------------------------+ <-- _boot_start (1 MB)
            |  Legacy BIOS / VGA RAM  | (Зарезервировано PMM)
 0x000A0000 +-------------------------+
            |   Real Mode IVT / BDA   | (Зарезервировано PMM)
 0x00000000 +-------------------------+

========================================================================
ПЛАН РАЗВИТИЯ ОС (ДОРОЖНАЯ КАРТА ОТ МЕНТОРА)
========================================================================
[ДЕНЬ 4: ЗАВЕРШЕНИЕ] Этап 4.3 - Privilege Separation
[x] 4.3.1 Task State Segment (TSS)
- Структура TSS (104 байта): SS0, ESP0, CR3, EIP, EFLAGS.
- GDT descriptor для TSS (type 0x89, DPL=0).
- Инструкция `ltr` для загрузки TR регистра.
- Настройка ESP0 для kernel stack при syscall.
[ ] 4.3.2 Переключение в User Mode (Ring 3) -> ОТЛОЖЕНО до Дня 6 (Higher Half Kernel) для безопасности памяти.
- Создание user code/data segments в GDT (DPL=3).
- ASM stub для `iret` в Ring 3: push user SS/ESP, EFLAGS, CS/EIP.
- Запуск первой user-space функции (бесконечный цикл).
[x] 4.3.3 Системные вызовы (syscall)
- INT 0x80 handler в IDT.
- Convention: EAX = syscall number, EBX/ECX/EDX = args.
- Syscall table: массив указателей на функции.
- Реализация: sys_write, sys_exit, sys_yield.
- Проверка прав доступа (user pointers validation).
[ ] 4.3.4 Защита памяти (User/Supervisor) -> ОТЛОЖЕНО до Дня 6.
- User pages: U/S бит = 1 в PTE.
- Kernel pages: U/S = 0 (доступ только из Ring 0).
- Page Fault handler: обработка нарушений прав доступа.

[ДЕНЬ 5] Оптимизация графического режима и производительности
[ ] 5.1 Оптимизация PMM (алгоритм First Fit)
- Hint-индекс для кэширования последней позиции поиска.
- Поиск по 32-битным словам (вместо побайтового).
- __builtin_ctz для быстрого поиска первого свободного бита (BSF/TZCNT).
- Команда `pmm bench` для измерения производительности.
[ ] 5.2 Оптимизация framebuffer скроллинга
- Двойная буферизация (offscreen rendering).
- memmove вместо построчного копирования.
- Dirty region tracking (скроллить только изменённые области).
[ ] 5.3 Улучшенный шрифт и рендеринг
- Anti-aliasing для текста (сглаживание).
- Поддержка Unicode (UTF-8).
- Переключаемые шрифты (8x8, 8x16, 16x32).

[ДЕНЬ 6] Архитектурный рефакторинг и продвинутая память
[ ] 6.1 Higher Half Kernel (0xC0000000)
- Разделение адресного пространства: 3 GB user / 1 GB kernel.
- Перекомпиляция с -Ttext=0xC0100000.
- Identity mapping первых 4 МБ для bootstrap.
- Переключение на higher half через `jump`.
- Обновление linker.ld: виртуальные адреса + 0xC0000000.
[x] 6.2 Multiboot Memory Map (E820)
- Парсинг Multiboot info structure (mmap_addr, mmap_length).
- Обработка memory map entries (available/reserved/ACPI).
- Динамическое определение доступной RAM (вместо хардкода).
- Архитектура Safe by Default + Punching Holes.
- Использование Linker Symbols для резервирования ядра.
- Обход PCI hole (0xE0000000-0xF0000000).
- Команда `memmap` в Shell.
[ ] 6.3 On-demand Paging
- Lazy allocation: страницы выделяются при Page Fault.
- PF handler: аллокация физической страницы, маппинг.
- Zero-filled pages (demand zero).

[ДЕНЬ 7] Процессы и планировщик
[ ] 7.1 Process Control Block (PCB)
- Структура процесса: PID, state, registers, page directory.
- Kernel stack для каждого процесса.
- Linked list всех процессов.
[ ] 7.2 Context Switching
- save_context / restore_context ASM функции.
- Переключение CR3 (page directory).
- Сохранение/восстановление регистров.
[ ] 7.3 Round-Robin Scheduler
- Ready queue (FIFO).
- Time slice: 10 мс (10 тиков PIT).
- Preemption по timer interrupt.
[ ] 7.4 Системные вызовы для процессов
- fork(): создание копии процесса (с COW).
- exec(): загрузка нового образа.
- exit(): завершение процесса.
- wait(): ожидание дочернего процесса.

[ДЕНЬ 8] Файловая система и Storage
[ ] 8.1 RAM Disk (tmpfs)
- In-memory файловая система.
- Inode структура: type, size, blocks[].
- Directory entries: name, inode number.
- VFS layer: абстракция над FS.
[ ] 8.2 Базовые операции
- open(), read(), write(), close().
- mkdir(), rmdir(), ls(), cat() в Shell.
[ ] 8.3 ATA/IDE Driver (PIO mode)
- Программирование портов 0x1F0-0x1F7.
- Чтение секторов (LBA28).
- Интеграция с VFS.

[ДЕНЬ 9] User Space и ELF Loader
[ ] 9.1 ELF Format Parser
- ELF header: magic, entry point, phoff.
- Program headers: LOAD segments.
- Validation: проверка секций.
[ ] 9.2 ELF Loader
- Аллокация virtual memory для segments.
- Загрузка .text, .data, .bss из файла.
- Настройка entry point и передача управления в Ring 3.
[ ] 9.3 Разделение библиотек
- Static linking для user apps.
- Передача аргументов (argc/argv) через стек user space.

[ДЕНЬ 10] Улучшение Shell и Debug Tools
[ ] 10.1 Advanced Shell
- History (стрелки вверх/вниз).
- Tab completion.
- Pipes: cmd1 | cmd2.
- Redirects: cmd > file, cmd < file.
[ ] 10.2 Debug Tools
- `ps`: список процессов и их состояние.
- `top`: CPU usage и статистика.
- `meminfo`: детальная статистика PMM/VMM/Heap.
- `dmesg`: kernel log buffer (ring buffer для логов ядра).

[ДЕНЬ 11] Polish, Testing и Документация
[ ] 11.1 Testing Suite
- Unit tests для klib функций.
- Integration tests: fork/exec/wait.
- Stress tests: memory leak detection, scheduler thrashing.
[ ] 11.2 Документация
- README.md: установка, запуск, зависимости.
- ARCHITECTURE.md: дизайн решения и диаграммы.
- API.md: полное описание функций с примерами.
[ ] 11.3 CI/CD (опционально)
- GitHub Actions для автоматической сборки.
- Запуск тестов в headless QEMU.

========================================================================
СОВЕТЫ ОТ МЕНТОРА (CODE REVIEW & BEST PRACTICES)
========================================================================
1. Приоритеты разработки:
- Стабильность > Фичи. Ядро не должно падать при некорректном вводе в Shell.
- Тесты перед коммитом. Каждый новый модуль должен иметь команду в Shell для проверки.
- Документация параллельно с кодом.
2. Debug Techniques:
- Serial port output (COM1 @ 0x3F8) для логирования без VGA.
- QEMU `-d int` для трассировки прерываний.
- Bochs debugger для step-by-step отладки ASM.
- Triple Fault analysis: анализ последней инструкции перед reboot.
3. Security (Постепенное внедрение):
- User pointer validation (copy_from_user / copy_to_user).
- Stack canaries (если позволит freestanding environment).
- NX bit (No Execute) для data pages.
- ASLR (Address Space Layout Randomization) для user space.
4. Performance:
- Batch page allocations (выделять по несколько страниц за раз).
- Lazy TLB invalidation (инвалидировать TLB только при необходимости).
- Kernel preemption points (точки вытеснения в long-running loops).
5. Графический режим:
- Маппинг framebuffer внутри paging_init() непосредственно перед включением CR0.PG.
- Инвалидировать TLB (через CR3 reload) при создании новых Page Tables.
- Инициализировать framebuffer ДО подсистем с отладочным выводом (используя CR0.PG=0).
- Использовать Strategy Pattern для прозрачного переключения между видео-бэкендами.

========================================================================
[КОНЕЦ БАЗЫ ЗНАНИЙ]
