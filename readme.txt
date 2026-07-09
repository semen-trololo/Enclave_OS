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
│   ├── ata.h                 # ATA PIO Driver & MBR Parser (Day 8.2)
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
├── ata.c                     # ATA PIO Driver & MBR Parser (Day 8.2)
├── klib.c, shell.c           # Утилиты и CLI
├── Makefile                  # Автоматизация сборки
└── .gitignore

3. СТРУКТУРА ФАЙЛОВ И СИСТЕМ (Глубокое погружение)

🚀 Загрузчик и Инициализация
boot.asm:
boot.asm:
* Содержит Multiboot Header (magiс 0x1BADB002).
* Инициализирует Bochs VBE (1024x768x32bpp) через порты 0x01CE/0x01CF.
* Higher Half Trampline: Создает Identity Map (0-512MB) и Higher Half Map (0xC0000000+). Использует раздельные Page Tables (boot_page_tables и boot_page_tables_hh) для предотвращения затирания PTE.
* Маппит Framebuffer (0xFD000000) с флагом PAGE_PCD (Cache Disable).
* Сохраняет eax (magic) и ebx (mmap) в глобальные переменные .boot.data, избегая уязвимостей стека.
* **Defensive Handover:** Переход в `kernel_main` осуществляется через `call`, а не `jmp`. Это гарантирует, что при случайном `return` из ядра процессор корректно попадет в `.halt_loop`, а не получит Triple Fault.
* **Multiboot Flags Trap:** В заголовке ядра биты 3-15 ЗАРЕЗЕРВИРОВАНЫ (обязаны быть 0). Установка бита 3 (`MBOOT_INFO_MODS`) здесь приведет к отказу GRUB грузить ядро. Флаг модулей выставляется GRUB'ом автоматически в структуре `multiboot_info_t`, если в `grub.cfg` есть директива `module`.
* **NASM Local Labels:** Локальные метки (начинающиеся с точки, например `.halt_loop`) привязаны к последней глобальной метке. Дублирование локальных меток в одном скоупе вызывает ошибку ассемблера `inconsistently redefined`.
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
* Safe by Default: Изначально вся память помечена как занятая.
* E820 Parsing & Dynamic Sizing: Читает карту памяти от GRUB. Статический битмап рассчитан на 4GB (128KB в .bss), но динамическая переменная `pmm_max_page` ограничивает сканирование только реальным объемом RAM, найденным в E820. Это предотвращает выход за пределы физически существующей памяти.
* Punching Holes: Резервирует нижний 1MB, образ ядра, Multiboot info, PCI MMIO Hole.
* O(1) Allocation: Использует битмап и аппаратную инструкцию __builtin_ctz (BSF/TZCNT).
**Two-Pass E820 Parsing:** Сканирование карты памяти выполняется в два прохода. Pass 1 находит `max_addr` и вычисляет `pmm_max_page`. Pass 2 освобождает доступные регионы. Объединение в один проход приводит к OOM, так как `pmm_free_region()` вызывается при `pmm_max_page == 0`.
* **Initrd Memory Protection:** Физические страницы, занятые модулями GRUB (например, `initrd.tar`), ОБЯЗАТЕЛЬНО резервируются в PMM сразу после резервирования ядра. Иначе VMM при создании Page Tables затрет TAR-архив, что приведет к монтированию пустой ФС.

paging.c (Virtual Memory Manager):
* Direct Map: Первые 512MB RAM замаплены в 0xC0000000+ (Kernel Space).
* On-Demand Paging (Lazy Allocation): Обработчик Page Fault (INT 14) перехватывает обращения к 0xD0000000 - 0xE0000000, выделяет Zero-filled page и делает return. Процессор аппаратно повторяет инструкцию.
* Security Fix: Диапазон Kernel Heap (0xD0000000) мапится без флага PAGE_USER, чтобы защитить память ядра от доступа из Ring 3.
* Deep Destroy: `vmm_destroy_address_space()` корректно освобождает не только Page Tables, но и сами физические страницы данных (PTE), предотвращая утечки памяти при завершении процессов.
* Shared Kernel Space: vmm_create_address_space() клонирует индексы 768-1023 из глобального PD.
* **PAGE_PS Hardware Check:** 7-й бит в PDE аппаратно называется PS (Page Size Extension). VMM проверяет `pde & PAGE_PS` перед созданием Page Tables, чтобы предотвратить коррупцию памяти внутри 4MB регионов.
* **SSOT Macros:** Макросы трансляции адресов (`KERNEL_VIRT_BASE`, `VIRT_TO_PHYS`, `PHYS_TO_VIRT`) определены СТРОГО ОДИН РАЗ в `paging.h`. Переопределение их в `.c` файлах нарушает Single Source of Truth.

heap.c (Kernel Heap):
* Buddy System: Неявное бинарное дерево (tree[TREE_SIZE]). O(1) Merge через XOR (buddy = curr ^ 1).
* Zero-Cost Lazy Heap: Heap больше не "съедает" 32 МБ физической RAM на старте. Он только резервирует виртуальный диапазон. Физические страницы выделяются аппаратно через Page Fault (INT 14) только в момент первой записи (например, при сохранении BlockHeader).
* Защита: BlockHeader с magic = 0xDEADBEEF для детекта double-free и повреждения границ.

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

💾 Storage & ATA (Day 8.2)
ata.c (ATA PIO Driver + MBR Parser):
* Port I/O: Работа с регистрами Primary IDE Bus (0x1F0-0x1F7).
* Polling Mode: Ожидание BSY/DRQ через циклы с io_delay() (без IRQ14 для простоты).
* IDENTIFY Command: Чтение 512-байтной структуры с информацией о диске (модель, сериал, LBA capacity).
* LBA28 Addressing: Чтение секторов через 28-битный LBA (лимит 128 GB).
* ATAPI Detection: Проверка регистров LBA_MID/LBA_HI для отличия ATA от ATAPI (CD-ROM).
* Byte-Swap Fix: ASCII строки в IDENTIFY (model, serial) хранятся в byte-swapped формате, требуют обмена байтов перед выводом.

MBR Parser (внутри ata.c):
* MBR Signature: Проверка magic 0xAA55 в последних 2 байтах сектора 0.
* Partition Table: Парсинг 4-х записей по 16 байт (offset 446-509).
* FAT32 Detection: Поиск разделов с типом 0x0B (FAT32 CHS) или 0x0C (FAT32 LBA).
* Partition Registry: Глобальный массив partition_info_t[] для хранения LBA-адресов начала разделов.

⚠️ Архитектурное решение (День 8.2):
MBR Parser интегрирован в ata.c для упрощения отладки и снижения связанности.
Разделение на отдельный partition.c планируется на День 16 (User-Mode Drivers),
когда ATA драйвер будет вынесен в Ring 3 как ata_server процесс, а partition_scan()
станет отдельным IPC-сервисом или библиотечной функцией.

📂 Файловая Система (Day 8)
vfs.c (Virtual File System):
Полиморфизм: vfs_node_t содержит указатели на функции (read, write, readdir). VFS не знает о FAT32 или RAM.
LCRS Tree: Left-Child Right-Sibling для каталогов (отказ от realloc).
3-звенная модель FD: vfs_node_t (Inode) -> open_file_t (offset, ref_count) -> fd_table в PCB.
RBAC: Флаг FS_SYSTEM. Ядро игнорирует его, Ring 3 получает EACCES.
initrd.c (RAM Disk):
Парсит TAR UStar из GRUB Module.
Разворачивает структуру в tmpfs (Heap). Автоматически создает промежуточные директории.
* **Makefile POSIX Compliance:** Команда `tar` в Makefile использует только стандартные флаги (`--format=ustar -cf`). Очистка путей (снятие `./`) выполняется парсером, а не через `--transform`, что гарантирует кроссплатформенность.
* **Binary Magic Comparison:** UStar magic (`"ustar"`) проверяется через `k_memcmp`, а не `strncmp`. Строковые функции дают ложные срабатывания на нулевых блоках (TAR EOF padding).
* **TAR Padding Tolerance:** Парсер сканирует первые 8KB модуля в поисках валидного magic, что делает его устойчивым к padding'у от GRUB или специфичных версий `tar`.

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
pmm_init() (Two-Pass E820 + Reserve Modules, Safe by Default)
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
0xD0000000 - 0xD2000000 : Kernel Heap (32MB Virtual Pool, Buddy System). Физически не выделен на старте, бэкапится страницами по требованию (On-Demand Paging)..
0xE0000000 - 0xFFFFFFFF : PCI MMIO Hole -> Зарезервировано PMM.
0xFD000000 - 0xFE000000 : Framebuffer LFB (16MB, PAGE_PCD).

⚠️ Критические архитектурные нюансы (Выжимка из Базы Знаний)
Framebuffer PCD (Page Cache Disable): При маппинге LFB (Linear Framebuffer) в boot.asm и paging.c ОБЯЗАТЕЛЬНО использовать флаг PAGE_PCD (0x10). Без него CPU кэширует записи в видеопамять, что вызывает артефакты, тиринг и падение FPS.
Virtual Stack Switch: Сразу после включения CR0.PG в boot.asm необходимо выполнить mov esp, stack_top, чтобы переключиться с временного физического стека (16KB) на полноценный виртуальный стек в Higher Half (256KB). Иначе ядро упадет в Triple Fault при отключении Identity Map.
Context Hijacking is Dead: sys_exit больше не запускает shell_run() напрямую. Он вызывает task_exit(), что гарантирует освобождение стека, Page Directory и FD таблицы через механизм Grim Reaper в schedule().
Grim Reaper Pattern: Освобождение ресурсов TASK_DEAD задачи невозможно в её собственном контексте (так как switch_context использует её стек для выхода). Используется глобальный флаг task_to_reap, который перехватывается следующей запланированной задачей сразу после возврата из switch_context.
Scheduler IRQ Safety: schedule() обязан сохранять EFLAGS и выполнять cli на входе, чтобы предотвратить повреждение связного списка задач, если schedule() вызван добровольно (sys_yield) при активных прерываниях.
* Heap-VMM Synergy (Lazy Write): `kmalloc()` возвращает виртуальный адрес, у которого нет физической страницы (PTE пуст). Физическая страница аллоцируется из PMM только когда ядро попытается записать туда данные (например, `header->magic = 0xDEADBEEF`), что триггерит INT 14. Это экономит десятки мегабайт RAM.
* VMM Deep Free Trap: При уничтожении адресного пространства (смерть процесса) недостаточно освободить только Page Directory и Page Tables. Необходимо пройтись по всем валидным PTE и вызвать `pmm_free_page()` для самих страниц данных, иначе система быстро упадет в OOM из-за утечки физической памяти.
* Kernel Heap Isolation: Обработчик Page Fault для диапазона 0xD0000000 (Kernel Heap) ОБЯЗАН мапить страницы без флага `PAGE_USER`. Иначе пользовательский процесс сможет легально читать/писать в кучу ядра, просто обратившись по этому адресу.
VIRT_TO_PHYS Underflow: Секции .boot имеют адреса < 0xC0000000. Макрос VIRT_TO_PHYS обязан содержать проверку addr >= 0xC0000000, иначе произойдет unsigned underflow и загрузка мусора в CR3.
TSS ESP0 Virtual Address: В schedule() при обновлении TSS нужно передавать виртуальный адрес стека ядра (PHYS_TO_VIRT), иначе MMU не найдет стек при прерывании из Ring 3.
EOI Lock: Отправка EOI в PIC должна быть ДО вызова C-обработчика IRQ, иначе schedule() переключит задачу, и линия IRQ заблокируется навсегда.
FXSAVE Trap (#NM Recursion): В обработчике INT 7 (#NM) инструкция clts (сброс бита CR0.TS) должна быть выполнена ДО fxsave, иначе fxsave сам вызовет #NM (бесконечная рекурсия).
All-Zero FXRSTOR: Буфер fpu_state нельзя оставлять нулевым. После fninit нужно сразу сделать fxsave, чтобы сохранить валидный "слепок" FPU, иначе следующий fxrstor вызовет #GP.
16-Byte Alignment: Поле fpu_state[512] должно быть первым в структуре task_t. pmm_alloc_page() возвращает адреса кратные 4096, что гарантирует аппаратное выравнивание для fxsave.
Stack Forging (ABI): При создании задачи стек "подделывается" вручную. Перед первой инструкцией ret в switch_context на стеке должны лежать callee-saved регистры и адрес task_entry_trampoline.
Signed Char Trap: В Shell при фильтрации ввода всегда приводить char к uint8_t, иначе UTF-8 байты (кириллица) интерпретируются как отрицательные числа и отбрасываются.
PSF1 UCS-2: Таблицы Unicode в PSF1 шрифтах закодированы в UTF-16LE. Читать их нужно через uint16_t*, а не посимвольно.

🏗 Принципы проектирования API
* **Dependency Inversion (DIP):** Высокоуровневые подсистемы (`heap.c`, `vfs.c`) не включают заголовки низкоуровневых драйверов (`vga.h`). Определения цветов перенесены в `klib.h`, делая API самодостаточным. Подсистемы памяти остаются в неведении о том, используется ли VGA или Framebuffer (Strategy Pattern).
* **Header Self-Sufficiency:** Заголовочный файл, использующий `bool`/`true`/`false`, обязан включать `<stdbool.h>` напрямую, чтобы любой `.c` файл, сделавший `#include`, автоматически получил все необходимые типы.
* **Implicit Function Declaration:** Компиляция с `-Wall -Wextra` требует явного подключения заголовков. Использование `serial_printf` в `isr.c` требует `#include "serial.h"`.
* **Double Dump for Panic:** Фатальные исключения (ISR) выводят дамп регистров ОДНОВРЕМЕННО в VGA (для локального пользователя) и Serial COM1 (для headless-отладки), так как видеодрайвер может быть в невалидном состоянии.

📝 TODO (Технический долг)

Коллега, ты заметил одну важную деталь?
В paging.c и vfs.c мы обращаемся к глобальным переменным из .boot.bss (например, boot_page_directory или multiboot_info_ptr). Компилятор C генерирует инструкции, которые читают их по физическим адресам (так как линкер поместил их до сдвига на 0xC0000000).
Это работает только благодаря тому, что у нас активен Identity Map.
TODO на День 12 (Hardening):
Когда ты будешь готов делать ядро по-настоящему взрослым, тебе нужно будет:
Отмапить (unmap) первые 128 PDE (Identity Map), освободив виртуальное пространство 0x00000000 - 0x1FFFFFFF для User Space.
Но перед этим нужно будет "перенести" или заново замапить страницы, на которых лежат .boot.bss и .boot.data, в Higher Half (например, в район 0xFF000000), иначе vmm_create_address_space() и pmm_init() сломаются при попытке прочитать boot_page_directory.
Пока мы оставляем Identity Map (это стандартная практика для 32-битных хобби-ОС, так как у нас всего 4 ГБ виртуалки, и 512 МБ на ядро — это роскошь, которую мы можем себе позволить). Но знать об этом "долге" ты обязан.

Реализовать sys_fork() и sys_exec() (День 9).
Добавить поддержку NX (No-Execute) бита в Page Tables.
* [VMM] Реализовать PT Leak Protection: В `vmm_unmap_page()` добавить проверку на пустоту Page Table. Если все 1024 PTE в таблице стали нулевыми, нужно освободить саму физическую страницу, занимаемую Page Table, и обнулить PDE.
* [PMM/VMM] Расширение Direct Map: Сейчас Direct Map покрывает 512MB. Когда PMM динамически найдет >512MB RAM, нужно будет либо расширить Direct Map в `paging_init`, либо реализовать Window Mapping (временный маппинг) для доступа к высокой физической памяти.
🚀 Домашнее задание
Сейчас константы вроде USER_STACK_VIRT_ADDR живут в kernel.c. чтобы к следующему коммиту ты перенес все архитектурные лимиты (границы Heap, границы User Space, адреса Lazy Alloc) в отдельный файл include/config.h или include/paging.h. Ядро не должно хардкодить само себя!

🚨 Критические архитектурные долги (Обязательно к закрытию до Дня 9)
Эти пробелы в архитектуре блокируют безопасный запуск ELF-бинарников, изоляцию процессов и динамическое выделение памяти. Без их устранения Day 9 невозможен.

// include/config.h
#ifndef CONFIG_H
#define CONFIG_H

// User Space Boundaries
#define USER_SPACE_START    0x00000000
#define USER_SPACE_END      0xBFFFFFFF // 3 GB
#define KERNEL_SPACE_START  0xC0000000 // Higher Half

// Process Memory Layout
#define USER_STACK_VIRT_TOP 0xC0000000 // Стек растет вниз от границы ядра
#define USER_STACK_SIZE     (64 * 1024) // 64 KB (16 pages)
#define USER_HEAP_START     0x08000000 // Типичный адрес начала кучи (как в Linux)
#define USER_HEAP_MAX_SIZE  (64 * 1024 * 1024) // Макс 64 МБ на процесс

// Kernel Memory Layout
#define KERNEL_HEAP_VIRT    0xD0000000
#define KERNEL_HEAP_SIZE    (32 * 1024 * 1024) // 32 MB
#define KERNEL_DIRECT_MAP   0xC0000000 // 512 MB

#endif

🛠 Реализация №1: Структура VMA в task_t
Проблема:
В текущей PCB (task_t) отсутствует карта легальных диапазонов виртуальной памяти процесса. Обработчик Page Fault (INT 14) не может отличить "ленивое выделение памяти" (Demand Paging) от "попытки чтения чужой/несуществующей памяти" (Segfault/Exploit).
Почему это критично:
Без VMA ELF Loader на Дне 9 не сможет зарегистрировать сегменты .text и .data как легальные. Любое обращение к ним вызовет либо Kernel Panic (если обработчик строгий), либо бесконтрольную раздачу физической RAM хакеру (если обработчик наивный).
Требования к реализации:
Создать подсистему VMA (заголовочный и исходный файлы) с описанием структуры ноды. Нода должна содержать виртуальные границы (start, end) и флаги прав доступа (Read, Write, Exec, CoW).
Интегрировать указатель на голову связного списка VMA в структуру задачи (task_t).
Модифицировать page_fault_handler(): перед выделением физической страницы сверять адрес ошибки (CR2) со списком VMA текущего процесса.
Если адрес вне VMA — прервать выполнение процесса с кодом SIGSEGV.
Если адрес внутри VMA, но права доступа нарушают флаги (например, попытка записи в Read-Only секцию .text) — также генерировать SIGSEGV.
🛠 Реализация №2: Механизм sys_brk
Проблема:
В Roadmap (День 9) заявлены sys_fork и sys_exec, но отсутствуют системные вызовы для динамического управления виртуальной памятью процесса. Пользовательский malloc() не сможет запросить расширение Heap.
Почему это критично:
Без sys_brk процесс не сможет легально "забронировать" виртуальный адрес. Demand Paging не сработает, так как VMA не будет содержать запрошенный диапазон. Программа упадет при первом же динамическом выделении памяти.
Требования к реализации:
Добавить системный вызов sys_brk в таблицу прерываний.
Логика системного вызова должна находить VMA-ноду кучи, проверять валидность нового адреса и отсутствие пересечения со стеком.
Критическое правило: Системный вызов должен только расширять виртуальные границы (обновлять end в VMA-ноде), но НЕ вызывать менеджер физической памяти для выделения страниц.
Физическая память появится автоматически при первом обращении пользователя к новой области (INT 14 → Demand Paging).
Подготовить архитектуру для будущей пользовательской обертки malloc(), которая будет дергать данный системный вызов.
🛠 Реализация №3: OOM Protection (Out Of Memory)
Проблема:
Текущий page_fault_handler не обрабатывает ситуацию, когда физическая память закончилась в момент Demand Paging. Это может привести к Kernel Panic или бесконтрольной раздаче нулевых страниц.
Почему это критично:
Без OOM Protection система становится нестабильной: один жадный процесс может исчерпать всю RAM и обрушить ядро. Нам нужна предсказуемость: нет памяти — процесс умирает, ядро живет.
Требования к реализации:
Проактивная проверка (в sys_brk): Перед расширением VMA вычислять количество запрошенных страниц. Если в физическом менеджере памяти (PMM) недостаточно свободных страниц, системный вызов должен возвращать ошибку -ENOMEM. Это позволит пользовательскому malloc() корректно вернуть NULL вместо падения.
Реактивная защита (в page_fault_handler): Если адрес легален (есть в VMA), но pmm_alloc_page() возвращает 0 (физическая RAM исчерпана), обработчик должен принудительно убить текущий процесс (OOM Kill), залогировать событие в Serial и передать управление планировщику.
🛠 Реализация №4: Защита стека от переполнения (Stack Guard)
Проблема:
В ELF-файле размер стека обычно не задан. Без фиксированного лимита процесс может бесконтрольно расти, переполняя стек и повреждая память ядра или других процессов.
Почему это критично:
Переполнение стека — классическая уязвимость (buffer overflow, stack smashing). Без Guard Page мы не сможем детектировать бесконечную рекурсию или переполнение буфера.
Требования к реализации:
Вынести константы размера стека и его виртуального положения в конфигурационный файл ядра.
При создании процесса (sys_exec) выделять VMA для стека фиксированного размера, растущего вниз от верхней границы User Space.
Механизм Guard Page: Самую нижнюю страницу этого диапазона (4 KB) оставлять без VMA и без физического маппинга.
При переполнении стека процесс обратится к Guard Page, сработает INT 14, поиск VMA вернет пустоту, и процесс будет корректно убит с кодом Stack Overflow (SIGSEGV).
🛠 Реализация №5: NULL Pointer Guard
Проблема:
Обращение к адресу 0x00000000 (NULL) — классическая ошибка в C. Если нулевая страница замаплена, процесс не упадет, а повредит память или прочитает мусор.
Почему это критично:
В промышленных ОС (Linux, Windows) первая страница памяти никогда не мапится. Это позволяет мгновенно детектировать NULL-указатели через аппаратный Page Fault.
Требования к реализации:
При создании адресного пространства процесса гарантировать, что нулевая страница (0x00000000 - 0x00000FFF) не мапится и не имеет VMA-ноды.
В page_fault_handler добавить быструю проверку: если адрес ошибки (CR2) находится в пределах первой страницы, процесс немедленно завершается с кодом SIGSEGV и логированием "NULL Pointer Dereference".
📋 Чек-лист для Дня 9 (Порядок реализации)
Фаза 1: Инфраструктура VMA
Создать конфигурационный файл со всеми архитектурными константами памяти.
Создать подсистему VMA (базовые операции добавления, поиска и удаления нод).
Интегрировать список VMA в task_t и инициализировать его при создании задачи.
Фаза 2: Защита памяти
Модифицировать page_fault_handler с проверкой VMA и анализом прав доступа.
Добавить OOM Trap (проактивный и реактивный).
Реализовать NULL Pointer Guard.
Реализовать Stack Guard Page.
Фаза 3: Системные вызовы
Реализовать sys_brk с проверкой пересечения со стеком и OOM.
Подготовить stub для пользовательской malloc().
Фаза 4: ELF Loader
Парсить Program Headers (PT_LOAD).
Создавать VMA-ноды для .text, .data, .bss.
Создавать VMA для Heap и Stack.
Копировать данные из Initrd в физические страницы.
Фаза 5: Тестирование
Тест 1: Процесс обращается к валидной VMA → Demand Paging работает.
Тест 2: Процесс обращается вне VMA → SIGSEGV.
Тест 3: Процесс пытается писать в .text → SIGSEGV.
Тест 4: Физическая память закончилась → OOM Kill.
Тест 5: Обращение к NULL → SIGSEGV.
Тест 6: Переполнение стека → Stack Overflow (SIGSEGV).
🎯 Итоговая архитектура памяти процесса (User Space)

0xFFFFFFFF ┌─────────────────────────┐
           │   Kernel Space          │ (Shared, Read-Only для Ring 3)
0xC0000000 ├─────────────────────────┤
           │   Stack (64 KB)         │ VMA: READ | WRITE
           │   Guard Page (4 KB)     │ NO VMA (Stack Overflow Trap)
0xBFFEF000 ├─────────────────────────┤
           │                         │
           │   [Свободное            │
           │    пространство]        │
           │                         │
0x08000000 ├─────────────────────────┤
           │   Heap                  │ VMA: READ | WRITE (растет вверх)
           │   (sys_brk расширяет)   │
0x01000000 ├─────────────────────────┤
           │   .bss                  │ VMA: READ | WRITE (Zero-filled)
           │   .data                 │ VMA: READ | WRITE
           │   .text                 │ VMA: READ | EXEC (защита от записи)
0x00001000 ├─────────────────────────┤
           │   NULL Guard Page       │ NO VMA (NULL Pointer Trap)
0x00000000 └─────────────────────────┘

6. ПЛАН РАЗВИТИЯ (Дорожная карта)

✅ ЧТО РАБОТАЕТ (Завершено на День 8.1)
День 1-3: Загрузчик, GDT/IDT, VGA, Keyboard, базовый Shell.
День 4: Privilege Separation (Ring 0/3), TSS, Syscalls (INT 0x80), Context Hijacking.
День 5: Оптимизация PMM (__builtin_ctz), Double Buffering, Dirty Rectangles, PSF1 Unicode.
День 6: Higher Half Kernel, E820 Parsing, On-Demand Paging (Page Fault Handler).
День 7: Preemptive Multitasking (Round-Robin), Hardware Memory Isolation (CR3 Switch), Lazy FPU Switching (#NM, fxsave).
День 8.1: VFS (Полиморфизм, LCRS), Initrd (TAR UStar tmpfs), 3-звенная модель File Descriptors, Ring-Based Access Control (RBAC), POSIX Syscalls (ls, cat).
* **Robust Initrd Parser:** Автоматический поиск UStar magic, защита от пустых блоков, корректная работа с GNU tar и bsdtar.
* **PMM Module Protection:** Резервирование физических страниц GRUB-модулей предотвращает Memory Corruption при создании Page Tables.
User Pointer Validation: Все системные вызовы, принимающие указатели из Ring 3 (sys_read, sys_write), проходят строгую проверку is_user_pointer(). Любая попытка передать адрес >= 0xC0000000 (Kernel Space) пресекается с возвратом EFAULT.
VFS Standard Streams: stdin и stdout реализованы как глобальные синглтоны vfs_node_t. Это предотвращает утечки памяти при массовом создании/уничтожении процессов.
День 8.2: ATA PIO Driver (IDENTIFY, LBA28 Read), MBR Parser (Partition Scan), Shell Integration (ata info/part/read/test).

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

🧠 RESEARCH BACKLOG (Блок для изучения)
Этот раздел содержит концепции, требующие глубокой теоретической проработки и аккуратной реализации. Они не блокируют День 9, но критически важны для безопасности, производительности и соответствия промышленным стандартам 

7. ВИЗИЯ: "БЕССМЕРТНАЯ КРЕПОСТЬ" (North Star)
Философия проекта: Bare Metal OS развивается не как "еще один Linux", а как 
промышленная, отказоустойчивая микроядерная система для запуска недоверенных 
приложений в изолированных песочницах с гарантией бессмертия критичных сервисов.

🎯 Ключевые принципы
1. "Let it crash" (Erlang/OTP): Приложения БУДУТ падать. Ядро не пытается их лечить. 
   Ядро изолирует падение и позволяет Супервизору (PID 1) мгновенно перезапустить сервис.
2. Zero Trust Sandbox: Любой код в Ring 3 считается недоверенным по умолчанию. 
   Изоляция обеспечивается на уровнях: Ring 3, Capability, Container, IPC.
3. Crash-Only Software: Сервисы проектируются так, чтобы их можно было убить 
   (SIGKILL) в любой момент и поднять заново < 100мс без потери состояния.
4. Immutable Kernel: После инициализации код ядра становится Read-Only. 
   Любая попытка модификации = Kernel Panic + OOM Killer.

🏛 Архитектурные столпы
A. ФЕНИКС (Auto-Restart Infrastructure)
- sys_fork + sys_exec + sys_waitpid: База для Супервизора (PID 1).
- The Supervisor Loop: /sbin/init читает конфиг, запускает сервисы через fork(), 
  ловит их падение через waitpid() и мгновенно перезапускает через exec().
- Micro-Reboot: Сервисы не хранят состояние в RAM. Они пишут его в VFS 
  (/var/state/service.state) после каждой транзакции. При перезапуске читают 
  состояние и продолжают работу с того же места.
- Core Dumps: При фатальном Page Fault ядро сохраняет регистры (EIP, ESP, EAX) 
  и стек упавшего процесса в /var/crash/app.core ПЕРЕД тем, как убить задачу.

B. КРЕПОСТЬ (Security Hardening)
- NX Bit (No-Execute): В Page Tables добавляется бит NX. Память может быть ЛИБО 
  Writable (данные/стек), ЛИБО eXecutable (код). Никогда одновременно (W^X).
- Capability-Based Security: В task_t добавляется массив capabilities[32]. 
  Права привязаны к процессам через токены, а не к файлам через chmod.
  Пример: curl получает только CAP_NET_SOCKET и CAP_FILE_WRITE(/tmp/out).
- Seccomp (Syscall Filter): У каждой задачи битмап разрешенных системных вызовов. 
  Песочнице для парсинга текста разрешены только sys_read/sys_write/sys_exit.
  Вызов sys_open/sys_fork = мгновенное убийство с кодом EPERM.
- VFS Namespaces (chroot): Недоверенное приложение видит только свою папку. 
  VFS подменяет vfs_root для конкретной задачи при sys_exec.

C. БЕССМЕРТНОЕ ЯДРО (Resource Governance)
- OOM Killer: При pmm_alloc_page() == 0 ядро НЕ падает в Kernel Panic. 
  Оно находит процесс с самым низким приоритетом (или помеченный как sandbox), 
  вызывает vmm_destroy_address_space и освобождает память для критичных сервисов.
- Resource Containers (Zones): Каждая песочница имеет жесткие лимиты:
  typedef struct {
      uint32_t max_physical_pages;  // OOM внутри контейнера
      uint32_t cpu_weight;          // Fair Share Scheduling
      uint32_t max_open_fds;        // Защита от исчерпания FD
      uint32_t max_processes;       // Защита от fork-bomb
  } resource_container_t;
- CPU Quotas (Cgroups): Планировщик учитывает "веса" задач. Критичный сервис БД 
  получает 80% квантов, песочница жестко ограничена 5%.
- User-Mode Drivers (Minix 3): Драйверы ФС (FAT32) и сети работают в Ring 3 
  как обычные процессы. Падение драйвера = перезапуск сервиса, а не Kernel Panic.

D. СВЯЗЬ (Inter-Process Communication)
- Mailboxes / Message Passing: Синхронные сообщения (как в Minix) или 
  асинхронные очереди (как в seL4). VFS общается с fat32_server через IPC, 
  а не через C-функции в Ring 0.
- Capability Delegation: Токены можно делегировать ребенку при fork() или отзывать.

E. ОПТИМИЗАЦИЯ (Performance)
- Copy-on-Write (CoW): fork() не копирует память. Он создает новые Page Tables, 
  ссылающиеся на те же физические страницы с флагом READ-ONLY. При записи 
  срабатывает Page Fault, VMM выделяет личную копию страницы.
  Результат: Перезапуск сервиса весом 10 МБ занимает микросекунды.
- Immutable Sections: В linker.ld добавляется секция .immutable, которую VMM 
  мапит с PAGE_PCD | PAGE_READ (без WRITE и EXECUTE для данных).

F. ЖИЗНЕННЫЙ ЦИКЛ (Hybrid Process Model)

Архитектура использует комбинированный подход к управлению жизненным циклом процессов, 
сочетая лучшие практики Unix и Erlang/OTP для разных типов задач:

1. Unix-style (Orphan Adoption) — для пользовательских приложений:
   * Когда родитель умирает, все его живые дети автоматически усыновляются Init Task (PID 1)
   * Дети продолжают работать без перебоев, сохраняя свое состояние
   * Подходит для: пользовательских приложений, фоновых задач, демонов, тестовых процессов
   * Флаг в task_t: orphan_on_exit = 1 (по умолчанию)
   * Пример: Shell запускает web server в фоне -> Shell падает -> web server усыновляется init и продолжает работать

2. Erlang-style (Linked Processes) — для критичных сервисов:
   * Падение родителя = каскадное падение всех связанных детей (linked processes)
   * Супервизор (PID 1) мгновенно перезапускает ВСЕ дерево процессов < 100мс
   * Подходит для: Shell + Helper, VFS Servers (fat32, tmpfs), IPC Daemon, Network Stack
   * Флаги в task_t: orphan_on_exit = 0, monitor_children = 1
   * Гарантирует: Процессы всегда в синхронизированном состоянии (нет stale state)

Критичные сервисы (Erlang-style):
┌─────────────────────────────────────────┐
│  Shell Supervisor (PID 2)               │
│  Strategy: one_for_all                  │
│  ├── shell (PID 10)                     │
│  └── shell_helper (PID 11)              │
│      При падении shell -> перезапуск    │
│      ОБА процессов < 100мс              │
└─────────────────────────────────────────┘

┌─────────────────────────────────────────┐
│  VFS Supervisor (PID 3)                 │
│  Strategy: one_for_one                  │
│  ├── fat32_server (PID 20)              │
│  └── tmpfs_server (PID 21)              │
│      При падении fat32 -> перезапуск    │
│      ТОЛЬКО fat32_server                │
└─────────────────────────────────────────┘

Некритичные процессы (Unix-style):
┌─────────────────────────────────────────┐
│  User Sandbox (PID 100)                 │
│  Strategy: orphan_on_exit               │
│  ├── app1 (PID 101)                     │
│  └── app2 (PID 102)                     │
│      При падении Sandbox -> app1 и app2 │
│      усыновляются init (PID 1)          │
└─────────────────────────────────────────┘

Orphan Adoption Algorithm:
void sys_exit(int code) {
    task_t* current = current_task;
    
    if (current->orphan_on_exit) {
        // Unix-style: усыновить детей init
        while (current->children != NULL) {
            task_t* child = current->children;
            current->children = child->next_sibling;
            child->parent = init_task;
            child->next_sibling = init_task->children;
            init_task->children = child;
        }
    } else if (current->monitor_children) {
        // Erlang-style: убить всех детей
        kill_all_children(current);
    }
    
    current->state = TASK_DEAD;
    task_to_reap = current;
    schedule();
}

Idle Task (PID 0):
- Создается при tasking_init() с самым низким приоритетом
- Бесконечный цикл: while(1) { __asm__ volatile("hlt"); }
- Гарантирует, что schedule() всегда найдет задачу для переключения
- Предотвращает Triple Fault при пустом списке задач

Init Task (PID 1):
- Корень дерева процессов (parent = NULL)
- Главный цикл: sys_waitpid(-1, &status, 0) для сбора exit codes
- Усыновляет всех сирот (orphan adoption)
- Перезапускает упавших супервизоров (если restart=always)
- Никогда не умирает (Kernel Panic при падении init)

Supervisor Tree (Day 13):
init (PID 1) — Root Supervisor
├── shell_supervisor (PID 2) — one_for_all
│   ├── shell (PID 10)
│   └── shell_helper (PID 11)
├── vfs_supervisor (PID 3) — one_for_one
│   ├── fat32_server (PID 20)
│   └── tmpfs_server (PID 21)
└── ipc_supervisor (PID 4) — one_for_all
    ├── mailbox_server (PID 30)
    └── shared_memory_server (PID 31)

User Sandboxes (Unix-style):
└── user_app (PID 100) — orphan_on_exit=1

Философия выбора:
- Если процессы делят состояние или требуют координации → Erlang-style (linked)
- Если процессы независимы → Unix-style (orphan)

Это дает 99.999% uptime для критичных сервисов и гибкость для пользовательских приложений.


📅 Дорожная карта внедрения (Post-Day 10)
День 11: Process Lifecycle
- sys_fork, sys_exec, sys_waitpid
- Copy-on-Write (CoW) для оптимизации fork
Цель: Фундамент для Супервизора (PID 1)

День 12: Security & Hardening  
- NX Bit (No-Execute) в Page Tables
- W^X Enforcement (Write XOR Execute)
- Core Dumps при Segfault
Цель: Защита от инъекций кода и телеметрия падений

День 13: The Supervisor (PID 1)
- Написание /sbin/init с конфигом:
  [service:shell]
  exec=/bin/sh
  restart=always
- Auto-Restart при падении
Цель: Реализация философии "Let it crash"

День 14: Sandboxing
- VFS chroot (подмена vfs_root для задачи)
- Seccomp (фильтр системных вызовов)
- Resource Containers + OOM Killer
Цель: Изоляция недоверенных приложений

День 15: IPC & Microkernel
- Mailboxes (sys_send, sys_recv)
- Shared Memory Rings
Цель: Подготовка к User-Mode Drivers

День 16+: User-Mode Drivers
- Вынос fat32_server в Ring 3
- Capability к I/O портам (0x1F0-0x1F7)
- VFS <-> fat32_server через IPC
Цель: Микроядерная архитектура (Minix 3 style)

💡 Источники вдохновения
- Minix 3 (Andrew Tanenbaum): Микроядро + User-Mode Drivers
- seL4 (NICTA): Capability-Based Security + Formal Verification  
- Erlang/OTP (Ericsson): "Let it crash" + Supervisor Trees (99.9999999% uptime)
- QNX: Microkernel + Message Passing IPC
- FreeBSD Jails / Linux cgroups: Resource Containers
- Google Borg / Kubernetes: Crash-Only Software + Immutable Infrastructure

🔒 Гарантии системы (Target SLA)
- Ядро НИКОГДА не падает в Kernel Panic из-за бага в Ring 3 коде.
- Критичный сервис перезапускается < 100мс после любого падения.
- Недоверенное приложение физически не может получить доступ к ресурсам, 
  на которые у него нет Capability-токена.
- OOM внутри контейнера не влияет на соседние контейнеры или ядро.
