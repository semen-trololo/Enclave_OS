📘 BARE METAL OS — Полная Архитектурная Документация
Single Source of Truth (SSOT) | Версия: Day 8.1

1. СРЕДА РАЗРАБОТКИ

Название: Bare Metal OS (учебно-исследовательская лабораторная работа)
Архитектура: x86, 32-битный защищённый режим (Protected Mode), Higher Half Kernel (0xC0000000)
Загрузчик: Multiboot 1 (GRUB)
Формат дистрибутива: Загрузочный ISO-образ (grub-mkrescue) + Initrd (TAR UStar)
Среда разработки: Linux (Kali / Debian / Arch)
🛠 Инструментарий
Кросс-компилятор: i686-linux-gnu-gcc (или i686-elf-gcc)
Ассемблер: nasm
Линкер: GNU ld
Сборка: Make, xorriso, grub-pc-bin, mtools
Эмуляция: QEMU (qemu-system-i386)
Контроль версий: Git
⚙️ Сборка проекта и флаги компиляции
Проект работает в Freestanding Environment (без libc). Использование стандартных заголовков <stdio.h>, <stdlib.h> запрещено. Разрешены только ISO C builtins: <stdint.h>, <stddef.h>, <stdarg.h>, <stdbool.h>.
Критические CFLAGS (Makefile):
CFLAGS  = -m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Iinclude
CFLAGS += -fno-pie -fno-pic -fno-stack-protector  # Отключение защит системного GCC
CFLAGS += -mno-sse -mno-sse2 -mno-mmx -mno-3dnow  # Запрет FPU/SSE в ядре (индустриальный стандарт)
CFLAGS += -mincoming-stack-boundary=2 -g          # Снятие 16-byte ABI alignment для ASM-трамплинов
LDFLAGS:
LDFLAGS = -T linker.ld -nostdlib -no-pie -lgcc
Принцип "Голая ОС" (Bare Metal):
Ядро не использует FPU/SSE напрямую, чтобы избежать необходимости сохранять 512-байтный FPU-контекст при каждом прерывании. Математика с плавающей точкой доступна только в User Space (Ring 3) через механизм Lazy FPU Switching.
Запуск:
make iso && make run
# qemu-system-i386 -cdrom build/metal_os.iso -m 1024M -serial stdio -no-reboot

2. СТРУКТУРА ПРОЕКТА
project_root/
├── isodir/                   # Корневая директория для генерации ISO
│   └── boot/
│       ├── grub/
│       │   └── grub.cfg      # Конфигурация GRUB (multiboot /boot/kernel.bin)
│       ├── kernel.bin        # Скомпилированное ядро (копируется Makefile'ом)
│       └── initrd.tar        # RAM-диск (TAR UStar, генерируется из initrd_src/)
│
├── include/                  # Заголовочные файлы (API подсистем)
│   ├── gdt.h, idt.h, isr.h, pic.h, tss.h
│   ├── pmm.h, paging.h, heap.h
│   ├── task.h, vfs.h, initrd.h
│   ├── vga.h, framebuffer.h, keyboard.h, timer.h, serial.h
│   ├── klib.h, shell.h, syscall.h, multiboot.h, port_io.h
│   └── univga_font.h         # PSF1 шрифт с кириллицей
│
├── boot.asm                  # Multiboot, VBE, Higher Half Mapping
├── linker.ld                 # Карта памяти (LMA/VMA)
├── kernel.c                  # Точка входа (kernel_main), Bootstrap
│
├── descriptors_flush.asm     # ASM: lgdt, lidt, ltr
├── isr_asm.asm               # ASM: ISR/IRQ stubs (pusha, segment swap)
├── context_switch.asm        # ASM: save/restore regs, CR3 switch, CR0.TS
├── usermode.asm              # ASM: IRET в Ring 3 (Fake Interrupt)
│
├── pmm.c, paging.c, heap.c   # Подсистемы памяти
├── task.c                    # PCB, Round-Robin, Lazy FPU
├── vfs.c, initrd.c           # Файловая система и RAM-диск
├── gdt.c, idt.c, isr.c       # Дескрипторы и прерывания
├── pic.c, tss.c, syscall.c   # Железо и системные вызовы
├── vga.c, framebuffer.c      # Графика (Text 80x50 + GUI 1024x768)
├── keyboard.c, timer.c       # Драйверы PS/2 и PIT
├── serial.c                  # COM1 (Headless debug)
├── klib.c, shell.c           # Утилиты и CLI
├── Makefile                  # Автоматизация сборки
└── .gitignore

3. СТРУКТУРА ФАЙЛОВ И СИСТЕМ (Глубокое погружение)

🚀 Загрузчик и Инициализация
boot.asm:
Содержит Multiboot Header (magiс 0x1BADB002).
Инициализирует Bochs VBE (1024x768x32bpp) через порты 0x01CE/0x01CF.
Higher Half Trampline: Создает Identity Map (0-512MB) и Higher Half Map (0xC0000000+). Использует раздельные Page Tables (boot_page_tables и boot_page_tables_hh) для предотвращения затирания PTE.
Маппит Framebuffer (0xFD000000) с флагом PAGE_PCD (Cache Disable).
Сохраняет eax (magic) и ebx (mmap) в глобальные переменные .boot.data, избегая уязвимостей стека.
linker.ld:
Разделяет секции на физические (.boot*) и виртуальные (.text, .data, .bss).
Использует AT(ADDR(...) - 0xC0000000) для корректной LMA (Load Memory Address).
Экспортирует символы _boot_start, _kernel_start, _kernel_end для PMM.
kernel.c:
Точка входа kernel_main. Реализует жесткую последовательность Bootstrap.
Содержит стресс-тесты: Day 6.3 (On-Demand Paging), Ring 3 Transition, x87 FPU Math Task.

🧠 Управление Памятью (Memory Management)
pmm.c (Physical Memory Manager):
pmm_max_page: Динамически вычисляется из E820 карты при загрузке. Ограничивает сканирование битмапа только существующей физической памятью, предотвращая доступ за пределы RAM. Лимит массива битмапа статический (4GB / 128KB).
Safe by Default: Изначально вся память помечена как занятая.
E820 Parsing: Читает карту памяти от GRUB, освобождает регионы type=1.
Punching Holes: Резервирует нижний 1MB, образ ядра, Multiboot info, PCI MMIO Hole.
O(1) Allocation: Использует битмап и аппаратную инструкцию __builtin_ctz (BSF/TZCNT) для поиска первого свободного бита в 32-битном слове.
paging.c (Virtual Memory Manager):
Direct Map: Первые 512MB RAM замаплены в 0xC0000000+.
On-Demand Paging (Lazy Allocation): Обработчик Page Fault (INT 14) перехватывает обращения к 0xD0000000 - 0xE0000000, выделяет Zero-filled page и делает return. Процессор аппаратно повторяет инструкцию.
Shared Kernel Space: vmm_create_address_space() клонирует индексы 768-1023 из глобального PD, обеспечивая "общую крышу" для прерываний.
heap.c (Kernel Heap):
Buddy System: Неявное бинарное дерево (tree[TREE_SIZE]).
O(1) Merge: Адрес близнеца вычисляется через buddy = curr ^ 1.
Защита: BlockHeader с magic = 0xDEADBEEF для детекта double-free.

⚙️ Многозадачность (Day 7)
task.c (Scheduler):
PCB (task_t): Хранит PID, State, ESP, CR3, FD Table и FPU State (512 байт, 16-byte aligned).
Round-Robin: Кольцевой двусвязный список. schedule() вызывается из PIT (каждые 20мс) или добровольно (sys_yield).
Lazy FPU: Бит CR0.TS (Task Switched) устанавливается при переключении. При FPU-инструкции возникает #NM (INT 7), который делает fxsave/fxrstor.
Reaper Mechanism: Освобождение памяти DEAD-задач происходит строго после switch_context в schedule().
context_switch.asm:
Сохраняет callee-saved регистры (EBX, ESI, EDI, EBP).
Меняет ESP и загружает новый CR3 (TLB Flush).
Устанавливает CR0.TS (взводит курок для Lazy FPU).

📂 Файловая Система (Day 8)
vfs.c (Virtual File System):
Полиморфизм: vfs_node_t содержит указатели на функции (read, write, readdir). VFS не знает о FAT32 или RAM.
LCRS Tree: Left-Child Right-Sibling для каталогов (отказ от realloc).
3-звенная модель FD: vfs_node_t (Inode) -> open_file_t (offset, ref_count) -> fd_table в PCB.
RBAC: Флаг FS_SYSTEM. Ядро игнорирует его, Ring 3 получает EACCES.
initrd.c (RAM Disk):
Парсит TAR UStar из GRUB Module.
Разворачивает структуру в tmpfs (Heap). Автоматически создает промежуточные директории.

🖥 Графика и Вывод
framebuffer.c:
Double Buffering: Рисование в back_buffer (RAM).
Dirty Rectangles: fb_flush() копирует в LFB только изменившийся бокс через rep movsl.
Unicode: Встроенный UTF-8 State Machine и чтение UCS-2 таблиц из PSF1 шрифтов.
vga.c: Текстовый режим 80x50. Загрузка кастомного шрифта 8x8 в Plane 2 через порты VGA Controller.
klib.c: Паттерн Strategy. output_char() прозрачно маршрутизирует вывод в FB или VGA.

🛡 Прерывания и Железо
gdt.c: Flat Model (4GB), Ring 0/3 Code/Data сегменты, TSS Descriptor.
idt.c: 256 векторов. EOI Lock Bypass: outb(0x20, 0x20) отправляется в PIC ДО вызова C-обработчика, чтобы schedule() не заблокировал IRQ.
tss.c: Настройка ESP0 для аппаратного переключения стека при прерываниях из Ring 3.
syscall.c: INT 0x80 (DPL=3). sys_exit использует Context Hijacking (прямой вызов shell_run() из Ring 0).
timer.c: PIT (1000 Hz). Квант времени = 20 тиков.
keyboard.c: PS/2 (IRQ1). Ring Buffer (Producer-Consumer), обработка Make/Break кодов, Shift/Ctrl/CapsLock.

ОПИСАНИЕ ФУНКЦИЙ БИБЛИОТЕКИ klib.c

klib.c — это стандартная библиотека ядра, заменяющая libc. Она полностью freestanding и не использует системные вызовы.

📦 Работа с памятью
void* k_memset(void* ptr, int value, size_t num) — Заполнение памяти байтом.
void* k_memcpy(void* dest, const void* src, size_t num) — Копирование блока.
int k_memcmp(const void* s1, const void* s2, size_t n) — Побайтовое сравнение.

🔤 Строковые функции
size_t k_strlen(const char* str) — Длина строки.
int k_strcmp(const char* s1, const char* s2) — Полное сравнение.
int k_strncmp(const char* s1, const char* s2, size_t n) — Сравнение первых n символов.

🔢 Конвертация чисел
void k_itoa(int value, char* buf, int base) — Int to ASCII (поддержка base 10/16).
void k_uitoa(unsigned int value, char* buf, int base) — Unsigned Int to ASCII.
int k_atoi(const char* str) — ASCII to Int (игнорирует пробелы, поддерживает знак).
uint32_t k_atoh(const char* str) — ASCII to Hex (поддержка префикса 0x).

🖨 Вывод и Форматирование
void k_print(const char* str) — Печать строки (через Strategy Pattern).
void k_putchar(char c) — Печать одного символа.
void k_clear(void) — Очистка экрана (FB или VGA).
void k_set_color(uint8_t vga_fg, uint8_t vga_bg) — Установка цвета. Маппит 16-цветную палитру VGA в 32-битный RGB для синхронизации с Framebuffer.
void k_printf(const char* fmt, ...) — Форматированный вывод. Поддерживает %d, %u, %x, %p, %s, %c, %%. (Внимание: модификаторы ширины типа %08x не поддерживаются и ломают va_list).
int k_vsprintf(char* buf, const char* fmt, va_list args) — Форматирование в буфер (используется fb_printf).

serial.h 
#ifndef SERIAL_H
#define SERIAL_H

void serial_init(void);
void serial_putc(char c);
void serial_print(const char* str);
// Форматированный вывод в Serial-порт (поддерживает %x, %p, %d, %u, %s, %c)
void serial_printf(const char* fmt, ...);

#endif

5. АРХИТЕКТУРНЫЕ ТОНКОСТИ И TODO

🔄 Порядок инициализации подсистем (Bootstrap)
Строгая последовательность в kernel_main. Нарушение порядка ведет к Triple Fault.
fb_init() (Временный, физический адрес LFB)
gdt_install() (Flat Model + TSS)
idt_install() (256 векторов)
tss_install() (Load TR)
syscall_init() (INT 0x80)
pmm_init() (E820, Safe by Default)
paging_init() (Включение CR0.PG, Direct Map, Reserving)
fb_init() (Resurrect: перепривязка к виртуальному адресу 0xFD000000)
heap_init() (Buddy System в 0xD0000000)
vfs_init() & initrd_init() (Mount tmpfs)
tasking_init() (Создание main_task, FPU setup)
keyboard_install() & timer_init() (Включение IRQ)
shell_run() (Бесконечный цикл CLI)

🗺 Карта памяти (Memory Map)
0x00000000 - 0x00100000 : Lower Memory (IVT, BDA, VGA RAM) -> Зарезервировано PMM.
0x00100000 - 0x01000000 : Kernel Image & Boot Structures (1MB - 16MB) -> Зарезервировано PMM.
0xC0000000 - 0xDFFFFFFF : Higher Half Kernel (Direct Map 512MB RAM).
0xD0000000 - 0xD2000000 : Kernel Heap (32MB, Buddy System, Lazy Alloc).
0xE0000000 - 0xFFFFFFFF : PCI MMIO Hole -> Зарезервировано PMM.
0xFD000000 - 0xFE000000 : Framebuffer LFB (16MB, PAGE_PCD).

⚠️ Критические архитектурные нюансы (Выжимка из Базы Знаний)
VIRT_TO_PHYS Underflow: Секции .boot имеют адреса < 0xC0000000. Макрос VIRT_TO_PHYS обязан содержать проверку addr >= 0xC0000000, иначе произойдет unsigned underflow и загрузка мусора в CR3.
TSS ESP0 Virtual Address: В schedule() при обновлении TSS нужно передавать виртуальный адрес стека ядра (PHYS_TO_VIRT), иначе MMU не найдет стек при прерывании из Ring 3.
EOI Lock: Отправка EOI в PIC должна быть ДО вызова C-обработчика IRQ, иначе schedule() переключит задачу, и линия IRQ заблокируется навсегда.
FXSAVE Trap (#NM Recursion): В обработчике INT 7 (#NM) инструкция clts (сброс бита CR0.TS) должна быть выполнена ДО fxsave, иначе fxsave сам вызовет #NM (бесконечная рекурсия).
All-Zero FXRSTOR: Буфер fpu_state нельзя оставлять нулевым. После fninit нужно сразу сделать fxsave, чтобы сохранить валидный "слепок" FPU, иначе следующий fxrstor вызовет #GP.
16-Byte Alignment: Поле fpu_state[512] должно быть первым в структуре task_t. pmm_alloc_page() возвращает адреса кратные 4096, что гарантирует аппаратное выравнивание для fxsave.
Stack Forging (ABI): При создании задачи стек "подделывается" вручную. Перед первой инструкцией ret в switch_context на стеке должны лежать callee-saved регистры и адрес task_entry_trampoline.
Signed Char Trap: В Shell при фильтрации ввода всегда приводить char к uint8_t, иначе UTF-8 байты (кириллица) интерпретируются как отрицательные числа и отбрасываются.
PSF1 UCS-2: Таблицы Unicode в PSF1 шрифтах закодированы в UTF-16LE. Читать их нужно через uint16_t*, а не посимвольно.

📝 TODO (Технический долг)

Реализовать sys_fork() и sys_exec() (День 9).
Добавить поддержку NX (No-Execute) бита в Page Tables.
Перенести sys_exit с Context Hijacking на полноценное уничтожение процесса через task_exit() и возврат в init (PID 1).
Добавить User Pointer Validation в sys_write (проверка, что buf находится в User Space 0x00000000 - 0xBFFFFFFF).

🚀 Домашнее задание
Сейчас константы вроде USER_STACK_VIRT_ADDR живут в kernel.c. чтобы к следующему коммиту ты перенес все архитектурные лимиты (границы Heap, границы User Space, адреса Lazy Alloc) в отдельный файл include/config.h или include/paging.h. Ядро не должно хардкодить само себя!

6. ПЛАН РАЗВИТИЯ (Дорожная карта)

✅ ЧТО РАБОТАЕТ (Завершено на День 8.1)
День 1-3: Загрузчик, GDT/IDT, VGA, Keyboard, базовый Shell.
День 4: Privilege Separation (Ring 0/3), TSS, Syscalls (INT 0x80), Context Hijacking.
День 5: Оптимизация PMM (__builtin_ctz), Double Buffering, Dirty Rectangles, PSF1 Unicode.
День 6: Higher Half Kernel, E820 Parsing, On-Demand Paging (Page Fault Handler).
День 7: Preemptive Multitasking (Round-Robin), Hardware Memory Isolation (CR3 Switch), Lazy FPU Switching (#NM, fxsave).
День 8.1: VFS (Полиморфизм, LCRS), Initrd (TAR UStar tmpfs), 3-звенная модель File Descriptors, Ring-Based Access Control (RBAC), POSIX Syscalls (ls, cat).

🚀 ЧТО ДЕЛАТЬ ДАЛЬШЕ (Приоритеты)
📅 День 8.2: Storage & FAT32 (Отложено до стабилизации User Space)
ATA PIO Driver: Работа с портами 0x1F0-0x1F7, LBA28, ожидание BSY/DRQ.
MBR & Partitions: Чтение LBA 0, поиск активного раздела.
FAT32 Read-Only: Парсинг BPB, обход цепочек кластеров.
VFAT (LFN): Парсинг Long File Names (UCS-2 -> UTF-8).
VFS Mount: Флаг FS_MOUNTPOINT для "телепортации" по дереву.
📅 День 9: User Space & ELF Loader
ELF Parser: Чтение e_entry, e_phoff, Program Headers (PT_LOAD).
ELF Loader: Выделение User Space памяти, загрузка сегментов .text и .data из VFS (Initrd) в адресное пространство процесса.
/init (PID 1): Запуск первой пользовательской программы по традиции Linux. Реализация fork()/exec().
📅 День 10: Advanced Shell & Debug
Shell Features: History (стрелки вверх/вниз), Tab Completion, Pipes (|), Redirects (>, <).
Debug Tools: Улучшение ps, добавление top, meminfo, dmesg (кольцевой буфер логов ядра).
📅 День 11: Polish & CI/CD
Testing Suite: Unit-тесты для PMM, Heap, VFS. Стресс-тесты планировщика.
CI/CD: GitHub Actions, headless QEMU тесты при каждом git push.
Documentation: Генерация Doxygen для API ядра.
