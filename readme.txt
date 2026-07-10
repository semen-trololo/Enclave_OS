📘 BARE METAL OS — Полная Архитектурная Документация
Single Source of Truth (SSOT) | Версия: Alpha 0.1 (Day 10)

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

User Space CFLAGS:
USER_CFLAGS = -m32 -nostdlib -static -ffreestanding -O2 -Wall -Wextra -fno-optimize-sibling-calls

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
│   ├── pmm.h, paging.h, heap.h, vma.h, elf.h
│   ├── task.h, vfs.h, initrd.h, tmpfs.h
│   ├── vga.h, framebuffer.h, keyboard.h, timer.h, serial.h
│   ├── klib.h, shell.h, syscall.h, multiboot.h, port_io.h
│   ├── config.h              # Single Source of Truth для всех границ памяти
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
├── vma.c, elf.c              # Virtual Memory Areas и ELF Loader
├── task.c                    # PCB, Round-Robin, Lazy FPU, Reaper Queue
├── vfs.c, initrd.c, tmpfs.c  # Файловая система и RAM-диски
├── gdt.c, idt.c, isr.c       # Дескрипторы и прерывания
├── pic.c, tss.c, syscall.c   # Железо и системные вызовы
├── vga.c, framebuffer.c      # Графика (Text 80x50 + GUI 1024x768)
├── keyboard.c, timer.c       # Драйверы PS/2 и PIT
├── serial.c                  # COM1 (Headless debug)
├── ata.c                     # ATA PIO Driver & MBR Parser (Day 8.2)
├── klib.c, shell.c           # Утилиты и CLI
├── Makefile                  # Автоматизация сборки
│
├── user_src/                 # User Space программы
│   ├── user_syscalls.h       # API системных вызовов для Ring 3
│   ├── user_linker.ld        # Linker script для ELF-бинарников
│   ├── test_hello.c          # Тест: базовый sys_write + sys_exit
│   ├── test_segfault.c       # Тест: NULL Pointer Dereference
│   ├── test_write_text.c     # Тест: W^X Violation (запись в .text)
│   ├── test_stack_overflow.c # Тест: Stack Overflow (рекурсия)
│   ├── test_oom.c            # Тест: OOM Killer (sys_brk)
│   └── test_vfs_stress.c     # Тест: VFS Stress (1000 файлов в TMPFS)
│
└── .gitignore

3. СТРУКТУРА ФАЙЛОВ И СИСТЕМ (Глубокое погружение)

🚀 Загрузчик и Инициализация

boot.asm:
* Содержит Multiboot Header (magic 0x1BADB002).
* Инициализирует Bochs VBE (1024x768x32bpp) через порты 0x01CE/0x01CF.
* Higher Half Trampoline: Создает Identity Map (0-512MB) и Higher Half Map (0xC0000000+). Использует раздельные Page Tables (boot_page_tables и boot_page_tables_hh) для предотвращения затирания PTE.
* Маппит Framebuffer (0xFD000000) с флагом PAGE_PCD (Cache Disable).
* Сохраняет eax (magic) и ebx (mmap) в глобальные переменные .boot.data, избегая уязвимостей стека.
* **Defensive Handover:** Переход в `kernel_main` осуществляется через `call`, а не `jmp`. Это гарантирует, что при случайном `return` из ядра процессор корректно попадет в `.halt_loop`, а не получит Triple Fault.
* **Multiboot Flags Trap:** В заголовке ядра биты 3-15 ЗАРЕЗЕРВИРОВАНЫ (обязаны быть 0). Установка бита 3 (`MBOOT_INFO_MODS`) здесь приведет к отказу GRUB грузить ядро. Флаг модулей выставляется GRUB'ом автоматически в структуре `multiboot_info_t`, если в `grub.cfg` есть директива `module`.
* **NASM Local Labels:** Локальные метки (начинающиеся с точки, например `.halt_loop`) привязаны к последней глобальной метке. Дублирование локальных меток в одном скоупе вызывает ошибку ассемблера `inconsistently redefined`.

linker.ld:
Разделяет секции на физические (.boot*) и виртуальные (.text, .data, .bss).
Использует AT(ADDR(...) - 0xC0000000) для корректной LMA (Load Memory Address).
Экспортирует символы _boot_start, _kernel_start, _kernel_end для PMM.

kernel.c:
Точка входа kernel_main. Реализует жесткую последовательность Bootstrap.
Содержит стресс-тесты: Day 6.3 (On-Demand Paging), Ring 3 Transition, x87 FPU Math Task.

🧠 Управление Памятью (Memory Management)

config.h (Single Source of Truth):
* Все глобальные константы памяти собраны в одном файле
* USER_SPACE_START/END — границы пользовательского пространства (0x00000000 - 0xBFFFFFFF)
* KERNEL_SPACE_START — начало ядра (0xC0000000)
* LOWER_MEM_START/END — нижняя память (0x00000000 - 0x00100000)
* PCI_MMIO_HOLE_START/END — PCI MMIO (0xE0000000 - 0xFFFFFFFF)
* USER_STACK_VIRT_TOP/SIZE/GUARD_SIZE — стек пользователя (64KB + 4KB Guard Page)
* USER_HEAP_START/MAX_SIZE — куча процесса (64MB максимум)
* KERNEL_HEAP_VIRT/SIZE/END — куча ядра (32MB виртуальный пул)
* FB_VIRT_BASE/PHYS_BASE/SIZE_MB — фреймбуфер (16MB)

pmm.c (Physical Memory Manager):
* Safe by Default: Изначально вся память помечена как занятая.
* E820 Parsing & Dynamic Sizing: Читает карту памяти от GRUB. Статический битмап рассчитан на 4GB (128KB в .bss), но динамическая переменная `pmm_max_page` ограничивает сканирование только реальным объемом RAM, найденным в E820. Это предотвращает выход за пределы физически существующей памяти.
* Punching Holes: Резервирует нижний 1MB, образ ядра, Multiboot info, PCI MMIO Hole (использует константы из config.h).
* O(1) Allocation: Использует битмап и аппаратную инструкцию __builtin_ctz (BSF/TZCNT).
* **Two-Pass E820 Parsing:** Сканирование карты памяти выполняется в два прохода. Pass 1 находит `max_addr` и вычисляет `pmm_max_page`. Pass 2 освобождает доступные регионы. Объединение в один проход приводит к OOM, так как `pmm_free_region()` вызывается при `pmm_max_page == 0`.
* **Initrd Memory Protection:** Физические страницы, занятые модулями GRUB (например, `initrd.tar`), ОБЯЗАТЕЛЬНО резервируются в PMM сразу после резервирования ядра. Иначе VMM при создании Page Tables затрет TAR-архив, что приведет к монтированию пустой ФС.
* **IRQ Safety:** Все операции с битмапом защищены cli/sti для предотвращения race conditions.
* **PMM Accounting:** Глобальные счетчики `pmm_total_allocs` и `pmm_total_frees` для детекции утечек памяти. API: `pmm_check_balance()` возвращает 0, если все ресурсы освобождены.

paging.c (Virtual Memory Manager):
* Direct Map: Первые 512MB RAM замаплены в 0xC0000000+ (Kernel Space).
* On-Demand Paging (Lazy Allocation): Обработчик Page Fault (INT 14) перехватывает обращения к 0xD0000000 - 0xE0000000, выделяет Zero-filled page и делает return. Процессор аппаратно повторяет инструкцию.
* Security Fix: Диапазон Kernel Heap (0xD0000000) мапится без флага PAGE_USER, чтобы защитить память ядра от доступа из Ring 3.
* Deep Destroy: `vmm_destroy_address_space()` корректно освобождает не только Page Tables, но и сами физические страницы данных (PTE), предотвращая утечки памяти при завершении процессов.
* Shared Kernel Space: vmm_create_address_space() клонирует индексы 768-1023 из глобального PD.
* **PAGE_PS Hardware Check:** 7-й бит в PDE аппаратно называется PS (Page Size Extension). VMM проверяет `pde & PAGE_PS` перед созданием Page Tables, чтобы предотвратить коррупцию памяти внутри 4MB регионов.
* **SSOT Macros:** Макросы трансляции адресов (`VIRT_TO_PHYS`, `PHYS_TO_VIRT`) определены СТРОГО ОДИН РАЗ в `paging.h`. Переопределение их в `.c` файлах нарушает Single Source of Truth.
* **Paranoid Page Fault Handler (Zero Trust Sandbox):**
  - NULL Pointer Guard — мгновенный SIGSEGV при обращении к 0x00000000
  - Kernel Space Protection — SIGSEGV при попытке Ring 3 доступа к ядру
  - VMA Enforcement — проверка наличия VMA перед выделением страниц
  - W^X Enforcement — защита от записи в Read-Only память
  - OOM Trap — реактивное убийство процесса при исчерпании RAM

heap.c (Kernel Heap):
* Buddy System: Неявное бинарное дерево (tree[TREE_SIZE]). O(1) Merge через XOR (buddy = curr ^ 1).
* Zero-Cost Lazy Heap: Heap больше не "съедает" 32 МБ физической RAM на старте. Он только резервирует виртуальный диапазон. Физические страницы выделяются аппаратно через Page Fault (INT 14) только в момент первой записи (например, при сохранении BlockHeader).
* Защита: BlockHeader с magic = 0xDEADBEEF для детекта double-free и повреждения границ.
* IRQ Safety: Все операции с деревом защищены cli/sti.
* Bounds Checking: kfree() проверяет, что указатель принадлежит диапазону Heap'а.
* Heap Accounting: Глобальные счетчики `heap_total_allocs` и `heap_total_frees` для детекции утечек. API: `heap_check_balance()`.

vma.c (Virtual Memory Areas):
* Сортированный связный список VMA для каждого процесса
* vma_add() — добавление VMA с автоматической сортировкой по start адресу
* vma_find() — линейный поиск VMA, содержащей заданный адрес
* vma_destroy_all() — очистка всех VMA процесса (вызывается Grim Reaper'ом)
* Интеграция в task_t через поле vma_head

elf.c (ELF Loader):
* Парсинг ELF32 Header и Program Headers (PT_LOAD сегменты)
* Загрузка .text, .data, .bss с правильными правами доступа (Read/Write/Execute)
* Создание VMA для каждого сегмента с флагами из ELF
* Маппинг физических страниц в Page Directory процесса
* Копирование данных из файла в выделенные страницы
* Интеграция с sys_exec для запуска ELF-бинарников в Ring 3

⚙️ Многозадачность (Day 7-9)

task.c (Scheduler):
PCB (task_t): Хранит PID, State, ESP, CR3, FD Table, VMA List и FPU State (512 байт, 16-byte aligned).
Round-Robin: Кольцевой двусвязный список. schedule() вызывается из PIT (каждые 20мс) или добровольно (sys_yield).
Lazy FPU: Бит CR0.TS (Task Switched) устанавливается при переключении. При FPU-инструкции возникает #NM (INT 7), который делает fxsave/fxrstor.
Reaper Queue: Мертвые задачи добавляются в односвязный список `dead_tasks_head` через поле `reaper_next`. Следующая запланированная задача после возврата из switch_context очищает ВСЕ задачи из очереди, предотвращая утечки памяти.
task_create(): Принимает опциональный параметр `custom_pdir` для передачи готового Address Space (используется sys_exec для загрузки ELF). Выделяет 16KB Kernel Stack через Kernel Heap для предотвращения Stack Overflow.
task_kill_current(): Принудительное убийство процесса из Page Fault Handler (включает прерывания перед вызовом task_exit).
Task Accounting: Глобальный счетчик `task_count` для детекции zombie processes. API: `task_get_count()`.

context_switch.asm:
Сохраняет callee-saved регистры (EBX, ESI, EDI, EBP).
Меняет ESP и загружает новый CR3 (TLB Flush).
Устанавливает CR0.TS (взводит курок для Lazy FPU).

usermode.asm:
Готовит стек для IRET в Ring 3.
Загружает пользовательские сегменты (SS=0x23, CS=0x1B).
Включает прерывания (IF bit в EFLAGS).
Делает iret для перехода в Ring 3.

syscall.c (System Calls):
INT 0x80 (DPL=3) — точка входа для Ring 3.
sys_exit — вызывает task_exit(), который запускает Grim Reaper.
sys_write/sys_read — проверка указателей через is_user_pointer(), делегирование в VFS.
sys_yield — добровольная передача CPU через schedule().
sys_brk — динамическое управление кучей процесса (расширение VMA без физического выделения).
sys_open/sys_close/sys_unlink — POSIX VFS операции с поддержкой O_CREAT и Zero Trust Sandbox.
sys_exec — загрузка и запуск ELF-бинарников в Ring 3:
1. Создание нового Address Space через vmm_create_address_space()
2. Загрузка ELF через elf_load() (создает VMA для сегментов)
3. Создание задачи через task_create() с передачей готового pdir_virt
4. Добавление VMA для стека и кучи в новый процесс

💾 Storage & ATA (Day 8.2)

ata.c (ATA PIO Driver + MBR Parser + FAT32):
ATA PIO Driver:
* Port I/O: Работа с регистрами Primary IDE Bus (0x1F0-0x1F7).
* Polling Mode: Ожидание BSY/DRQ через циклы с io_delay() (без IRQ14 для простоты).
* IDENTIFY Command: Чтение 512-байтной структуры с информацией о диске (модель, сериал, LBA capacity).
* LBA28 Addressing: Чтение секторов через 28-битный LBA (лимит 128 GB).
* ATAPI Detection: Проверка регистров LBA_MID (0x14) и LBA_HI (0xEB) для отличия ATA от ATAPI (CD-ROM).
* Byte-Swap Fix: ASCII строки в IDENTIFY (model, serial, firmware) хранятся в byte-swapped формате, требуют обмена байтов перед выводом.
* BSY/DRQ Timeout Protection: Защита от зависания на неисправных дисках (100000 итераций с io_delay).
* Sector Count Edge Case: Поддержка sector_count=0 как 256 секторов (ATA спецификация).
* Error Handling: Чтение регистра ATA_REG_ERROR при сбое, детальное логирование в Serial.

MBR Parser (внутри ata.c):
* MBR Signature: Проверка magic 0xAA55 в последних 2 байтах сектора 0.
* Partition Table: Парсинг 4-х записей по 16 байт (offset 446-509).
* FAT32 Detection: Поиск разделов с типом 0x0B (FAT32 CHS) или 0x0C (FAT32 LBA).
* Partition Registry: Глобальный массив partition_info_t[] для хранения LBA-адресов начала разделов.

FAT32 Read-Only Driver (fat32.c):
* BPB Parsing: Чтение BIOS Parameter Block из Boot Sector (первый сектор раздела).
* Cluster Math: Вычисление first_data_sector = reserved_sectors + (num_fats * fat_size_32).
* Cluster → LBA Translation: Формула lba = partition_lba + first_data_sector + (cluster - 2) * sectors_per_cluster.
* FAT Caching: Статический буфер fat_sector_buffer[512] для кэширования текущего сектора FAT (избегаем повторных чтений).
* Chain Traversal: Функция fat32_next_cluster() читает следующий кластер из FAT-таблицы (маскирует верхние 4 бита).
* EOF Detection: Проверка cluster >= 0x0FFFFFF8 (End of File).
* VFS Integration: Каждый vfs_node_t хранит fat32_node_data_t (start_cluster, size, fs pointer).
* Polymorphic Callbacks: fat32_read(), fat32_readdir(), fat32_finddir() интегрированы в VFS через указатели на функции.

VFAT (Long File Names) Support:
* LFN Entry Structure: Парсинг 32-байтных записей с атрибутом 0x0F (READ_ONLY|HIDDEN|SYSTEM|VOLUME_ID).
* Reverse Order: LFN записи идут в обратном порядке (последний фрагмент первым, bit 6 в order = last entry).
* UCS-2 Accumulation: Накопление символов из name1[5], name2[6], name3[2] (всего 13 UCS-2 символов на запись).
* UCS-2 → UTF-8 Conversion: Функция ucs2_to_utf8() конвертирует 16-бит Unicode в variable-length UTF-8 (1-3 байта).
* Checksum Verification: Функция lfn_checksum() вычисляет контрольную сумму 8.3 имени для верификации LFN записей.
* 8.3 Fallback: Если LFN отсутствует или checksum не совпадает, используется классическое 8.3 имя (space-padded).
* Cyrillic Support: Кириллица в именах файлов корректно отображается благодаря UTF-8 кодировке.
* Volume Label Filter: Игнорирование записей с атрибутом FAT32_ATTR_VOLUME_ID (метка тома).
* Deleted Entry Filter: Игнорирование записей с первым байтом 0xE5 (удаленные файлы).

⚠️ Архитектурное решение (День 8.2):
MBR Parser интегрирован в ata.c для упрощения отладки и снижения связанности.
Разделение на отдельный partition.c планируется на День 16 (User-Mode Drivers),
когда ATA драйвер будет вынесен в Ring 3 как ata_server процесс, а partition_scan()
станет отдельным IPC-сервисом или библиотечной функцией.

📂 Файловая Система (Day 8)

vfs.c (Virtual File System):
Полиморфизм: vfs_node_t содержит указатели на функции (read, write, readdir, create, unlink). VFS не знает о FAT32 или RAM.
LCRS Tree: Left-Child Right-Sibling для каталогов (отказ от realloc).
3-звенная модель FD: vfs_node_t (Inode) -> open_file_t (offset, ref_count) -> fd_table в PCB.
RBAC: Флаг FS_SYSTEM. Ядро игнорирует его, Ring 3 получает EACCES.
POSIX Syscalls: sys_open (с O_CREAT), sys_close, sys_read, sys_write, sys_readdir, sys_unlink.

initrd.c (RAM Disk):
Парсит TAR UStar из GRUB Module.
Разворачивает структуру в tmpfs (Heap). Автоматически создает промежуточные директории.
* **Makefile POSIX Compliance:** Команда `tar` в Makefile использует только стандартные флаги (`--format=ustar -cf`). Очистка путей (снятие `./`) выполняется парсером, а не через `--transform`, что гарантирует кроссплатформенность.
* **Binary Magic Comparison:** UStar magic (`"ustar"`) проверяется через `k_memcmp`, а не `strncmp`. Строковые функции дают ложные срабатывания на нулевых блоках (TAR EOF padding).
* **TAR Padding Tolerance:** Парсер сканирует первые 8KB модуля в поисках валидного magic, что делает его устойчивым к padding'у от GRUB или специфичных версий `tar`.

tmpfs.c (Writable RAM Disk):
Динамическое расширение файлов через kmalloc/kfree с перевыделением (capacity *= 2).
Polymorphic Callbacks: tmpfs_read, tmpfs_write, tmpfs_create, tmpfs_unlink интегрированы в VFS.
Автоматическое монтирование в /tmp при загрузке.

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
char* k_strncpy(char* dest, const char* src, size_t n) — Безопасное копирование не более n символов.
Если src короче n, остаток буфера dest принудительно заполняется нулями ('\0').
Критично для предотвращения утечки данных из стека/кучи (например, при парсинге FAT32 LFN имен и передаче их в структуры VFS dirent_t).

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
1. fb_init() (Временный, физический адрес LFB)
2. gdt_install() (Flat Model + TSS)
3. idt_install() (256 векторов)
4. tss_install() (Load TR)
5. syscall_init() (INT 0x80)
6. pmm_init() (Two-Pass E820 + Reserve Modules, Safe by Default)
7. paging_init() (Включение CR0.PG, Direct Map, Reserving)
8. fb_init() (Resurrect: перепривязка к виртуальному адресу 0xFD000000)
9. heap_init() (Buddy System в 0xD0000000)
10. vfs_init() & initrd_init() (Mount tmpfs)
11. fat32_init()
12. tasking_init() (Создание main_task, FPU setup)
13. keyboard_install() & timer_init() (Включение IRQ)
14. shell_run() (Бесконечный цикл CLI)

🗺 Карта памяти (Memory Map)
0x00000000 - 0x00100000 : Lower Memory (IVT, BDA, VGA RAM) -> Зарезервировано PMM.
0x00100000 - 0x01000000 : Kernel Image & Boot Structures (1MB - 16MB) -> Зарезервировано PMM.
0xC0000000 - 0xDFFFFFFF : Higher Half Kernel (Direct Map 512MB RAM).
0xD0000000 - 0xD2000000 : Kernel Heap (32MB Virtual Pool, Buddy System). Физически не выделен на старте, бэкапится страницами по требованию (On-Demand Paging).
0xE0000000 - 0xFFFFFFFF : PCI MMIO Hole -> Зарезервировано PMM.
0xFD000000 - 0xFE000000 : Framebuffer LFB (16MB, PAGE_PCD).

⚠️ Критические архитектурные нюансы (Выжимка из Базы Знаний)

Framebuffer PCD (Page Cache Disable): При маппинге LFB (Linear Framebuffer) в boot.asm и paging.c ОБЯЗАТЕЛЬНО использовать флаг PAGE_PCD (0x10). Без него CPU кэширует записи в видеопамять, что вызывает артефакты, тиринг и падение FPS.

Virtual Stack Switch: Сразу после включения CR0.PG в boot.asm необходимо выполнить mov esp, stack_top, чтобы переключиться с временного физического стека (16KB) на полноценный виртуальный стек в Higher Half (256KB). Иначе ядро упадет в Triple Fault при отключении Identity Map.

Context Hijacking is Dead: sys_exit больше не запускает shell_run() напрямую. Он вызывает task_exit(), что гарантирует освобождение стека, Page Directory и FD таблицы через механизм Grim Reaper в schedule().

Reaper Queue Pattern: Освобождение ресурсов TASK_DEAD задачи невозможно в её собственном контексте (так как switch_context использует её стек для выхода). Мертвые задачи добавляются в глобальную очередь `dead_tasks_head` через поле `reaper_next`, которая очищается следующей запланированной задачей сразу после возврата из switch_context. Это предотвращает потерю задач при race conditions.

Scheduler IRQ Safety: schedule() обязан сохранять EFLAGS и выполнять cli на входе, чтобы предотвратить повреждение связного списка задач, если schedule() вызван добровольно (sys_yield) при активных прерываниях.

Heap-VMM Synergy (Lazy Write): `kmalloc()` возвращает виртуальный адрес, у которого нет физической страницы (PTE пуст). Физическая страница аллоцируется из PMM только когда ядро попытается записать туда данные (например, `header->magic = 0xDEADBEEF`), что триггерит INT 14. Это экономит десятки мегабайт RAM.

VMM Deep Free Trap: При уничтожении адресного пространства (смерть процесса) недостаточно освободить только Page Directory и Page Tables. Необходимо пройтись по всем валидным PTE и вызвать `pmm_free_page()` для самих страниц данных, иначе система быстро упадет в OOM из-за утечки физической памяти.

Kernel Heap Isolation: Обработчик Page Fault для диапазона 0xD0000000 (Kernel Heap) ОБЯЗАН мапить страницы без флага `PAGE_USER`. Иначе пользовательский процесс сможет легально читать/писать в кучу ядра, просто обратившись по этому адресу.

VIRT_TO_PHYS Underflow: Секции .boot имеют адреса < 0xC0000000. Макрос VIRT_TO_PHYS обязан содержать проверку addr >= 0xC0000000, иначе произойдет unsigned underflow и загрузка мусора в CR3.

TSS ESP0 Virtual Address: В schedule() при обновлении TSS нужно передавать виртуальный адрес стека ядра (PHYS_TO_VIRT), иначе MMU не найдет стек при прерывании из Ring 3.

EOI Lock: Отправка EOI в PIC должна быть ДО вызова C-обработчика IRQ, иначе schedule() переключит задачу, и линия IRQ заблокируется навсегда.

FXSAVE Trap (#NM Recursion): В обработчике INT 7 (#NM) инструкция clts (сброс бита CR0.TS) должна быть выполнена ДО fxsave, иначе fxsave сам вызовет #NM (бесконечная рекурсия).

All-Zero FXRSTOR: Буфер fpu_state нельзя оставлять нулевым. После fninit нужно сразу сделать fxsave, чтобы сохранить валидный "слепок" FPU, иначе следующий fxrstor вызовет #GP.

16-Byte Alignment: Поле fpu_state[512] должно быть первым в структуре task_t. pmm_alloc_page() возвращает адреса кратные 4096, что гарантирует аппаратное выравнивание для fxsave.

Stack Forging (ABI): При создании задачи стек "подделывается" вручную. Перед первой инструкцией ret в switch_context на стеке должны лежать callee-saved регистры и адрес task_entry_trampoline.

Signed Char Trap: В Shell при фильтрации ввода всегда приводить char к uint8_t, иначе UTF-8 байты (кириллица) интерпретируются как отрицательные числа и отбрасываются.

PSF1 UCS-2: Таблицы Unicode в PSF1 шрифтах закодированы в UTF-16LE. Читать их нужно через uint16_t*, а не посимвольно.

sys_exec Address Space Handover: sys_exec создает новый Address Space, загружает в него ELF (создавая VMA для сегментов), и передает готовый pdir_virt в task_create(). Это предотвращает двойное создание Page Directory и утечки памяти.

16KB Kernel Stack: Kernel Stack выделяется через kmalloc(16384) вместо pmm_alloc_page(). Это гарантирует виртуальную смежность и защиту от Kernel Stack Overflow при глубокой вложенности вызовов ядра (serial_printf + VFS + Scheduler).

Tail Call Optimization Trap: User Space программы компилируются с флагом -fno-optimize-sibling-calls для предотвращения превращения рекурсии в бесконечный цикл (что ломает тесты Stack Overflow).

W^X Enforcement: user_linker.ld явно разделяет сегменты на Read+Execute (.text) и Read+Write (.data) с ALIGN(4096) для физической изоляции кода от данных.

🏗 Принципы проектирования API

* **Dependency Inversion (DIP):** Высокоуровневые подсистемы (`heap.c`, `vfs.c`) не включают заголовки низкоуровневых драйверов (`vga.h`). Определения цветов перенесены в `klib.h`, делая API самодостаточным. Подсистемы памяти остаются в неведении о том, используется ли VGA или Framebuffer (Strategy Pattern).

* **Header Self-Sufficiency:** Заголовочный файл, использующий `bool`/`true`/`false`, обязан включать `<stdbool.h>` напрямую, чтобы любой `.c` файл, сделавший `#include`, автоматически получил все необходимые типы.

* **Implicit Function Declaration:** Компиляция с `-Wall -Wextra` требует явного подключения заголовков. Использование `serial_printf` в `isr.c` требует `#include "serial.h"`.

* **Double Dump for Panic:** Фатальные исключения (ISR) выводят дамп регистров ОДНОВРЕМЕННО в VGA (для локального пользователя) и Serial COM1 (для headless-отладки), так как видеодрайвер может быть в невалидном состоянии.

* **Single Source of Truth (SSOT):** Все глобальные константы памяти (границы User/Kernel Space, Heap, Stack, Framebuffer) определены СТРОГО ОДИН РАЗ в `include/config.h`. Любой файл, использующий эти константы, обязан делать `#include "config.h"`.

📅 День 9: User Space & Memory Protection

✅ Реализовано
Инфраструктура VMA (Virtual Memory Areas):
* include/config.h — Single Source of Truth для всех границ памяти (User Space, Kernel Heap, Stack, Framebuffer)
* include/vma.h + src/vma.c — подсистема VMA с сортированным связным списком
* Интеграция VMA в task_t и автоматическая очистка через Grim Reaper

Защита памяти (Zero Trust Sandbox):
* Параноидальный page_fault_handler с проверкой VMA перед выделением страниц
* NULL Pointer Guard — мгновенный SIGSEGV при обращении к 0x00000000
* Kernel Space Protection — SIGSEGV при попытке Ring 3 доступа к ядру
* W^X Enforcement — защита от записи в .text секции
* OOM Trap — проактивная проверка в sys_brk и реактивное убийство процесса при исчерпании RAM

Системные вызовы:
* sys_brk — динамическое управление кучей (расширение VMA без физического выделения)
* sys_exec — загрузка и запуск ELF-бинарников в Ring 3

ELF Loader:
* include/elf.h + src/elf.c — парсинг ELF Header и Program Headers (PT_LOAD)
* Загрузка сегментов .text, .data, .bss с правильными правами доступа
* Создание VMA для каждого сегмента ELF
* Интеграция с task_create() через передачу готового Address Space

Инфраструктура сборки:
* Переделан Makefile — все артефакты в build/, автоматическая компиляция user-space программ
* user_linker.ld — linker script для ELF-бинарников
* Тестовый бинарник user_src/hello.asm — Hello World через sys_write

🎯 Итоговая архитектура памяти процесса
0xFFFFFFFF ┌─────────────────────────┐
           │   Kernel Space          │ (Shared, Read-Only для Ring 3)
0xC0000000 ├─────────────────────────┤
           │   Stack (64 KB)         │ VMA: READ | WRITE
           │   Guard Page (4 KB)     │ NO VMA (Stack Overflow Trap)
0xBFFEF000 ├─────────────────────────┤
           │   [Свободное пространство]         │
0x08000000 ├─────────────────────────┤
           │   Heap (sys_brk)        │ VMA: READ | WRITE
0x01000000 ├─────────────────────────┤
           │   .bss / .data / .text  │ VMA с правами из ELF
0x00001000 ├─────────────────────────┤
           │   NULL Guard Page       │ NO VMA (NULL Pointer Trap)
0x00000000 └─────────────────────────┘

6. ПЛАН РАЗВИТИЯ (Дорожная карта)

✅ ЧТО РАБОТАЕТ (Завершено на День 10 - Alpha 0.1)

День 1-3: Загрузчик, GDT/IDT, VGA, Keyboard, базовый Shell.

День 4: Privilege Separation (Ring 0/3), TSS, Syscalls (INT 0x80), Context Hijacking.

День 5: Оптимизация PMM (__builtin_ctz), Double Buffering, Dirty Rectangles, PSF1 Unicode.

День 6: Higher Half Kernel, E820 Parsing, On-Demand Paging (Page Fault Handler).

День 7: Preemptive Multitasking (Round-Robin), Hardware Memory Isolation (CR3 Switch), Lazy FPU Switching (#NM, fxsave).

День 8.1: VFS (Полиморфизм, LCRS), Initrd (TAR UStar tmpfs), 3-звенная модель File Descriptors, Ring-Based Access Control (RBAC), POSIX Syscalls (ls, cat).
* **Robust Initrd Parser:** Автоматический поиск UStar magic, защита от пустых блоков, корректная работа с GNU tar и bsdtar.
* **PMM Module Protection:** Резервирование физических страниц GRUB-модулей предотвращает Memory Corruption при создании Page Tables.

День 8.2: Storage & FAT32 (ATA PIO + VFAT)
* ATA PIO Driver: Работа с портами 0x1F0-0x1F7, LBA28 addressing, Polling Mode (без IRQ14).
* IDENTIFY Command: Чтение 512-байтной структуры диска (модель, сериал, firmware, LBA capacity).
* ATAPI Detection: Проверка регистров LBA_MID/LBA_HI для отличия ATA от CD-ROM.
* Byte-Swap Fix: Корректная обработка ASCII строк в IDENTIFY (модель, сериал хранятся в byte-swapped формате).
* BSY/DRQ Timeout Protection: Защита от зависания на неисправных дисках (100000 итераций с io_delay).
* MBR Parser: Парсинг таблицы разделов (4 записи по 16 байт, offset 446-509), валидация сигнатуры 0xAA55.
* FAT32 Read-Only: Парсинг BPB (BIOS Parameter Block), вычисление first_data_sector, fat1_lba.
* Cluster Chain Navigation: Чтение FAT-таблицы, обход цепочек кластеров через fat32_next_cluster().
* VFAT (Long File Names): Парсинг LFN записей (атрибут 0x0F), накопление UCS-2 символов.
* UCS-2 → UTF-8 Conversion: Поддержка кириллицы и Unicode в именах файлов (до 255 символов).
* LFN Checksum Verification: Проверка контрольной суммы 8.3 имени для верификации LFN записей.
* VFS Mount: Флаг FS_MOUNTPOINT для "телепортации" по дереву (transparent mount).

День 9: User Space & Memory Protection
* VMA (Virtual Memory Areas): Инфраструктура для управления виртуальной памятью процессов
* Zero Trust Sandbox: Параноидальная защита памяти (NULL Guard, W^X, OOM Trap)
* ELF Loader: Загрузка и запуск ELF-бинарников в Ring 3
* sys_brk/sys_exec: Динамическое управление памятью и запуск процессов
* Reaper Queue: Безопасное освобождение ресурсов мертвых процессов

День 10: Testing Suite (Alpha 0.1 Release)
* **Pillar 1: ELF Test Suite** — 6 тестовых бинарников (test_hello, test_segfault, test_write_text, test_stack_overflow, test_oom, test_vfs_stress)
* **Pillar 2: Stress Tests** — Mass Spawn 1000 kernel-level задач с проверкой Reaper Queue
* **Pillar 3: VFS Stress** — Создание/удаление 1000 файлов в TMPFS с CRC32 верификацией
* **PMM/Heap/Task Accounting** — Глобальные счетчики alloc/free для детекции утечек памяти
* **Test Runner** — Shell-команды run_tests и stress spawn для автоматизации
* **POSIX VFS Syscalls** — sys_open (с O_CREAT), sys_close, sys_unlink с Zero Trust Sandbox
* **TMPFS Dynamic Growth** — Writable RAM Disk с автоматическим расширением через kmalloc
* **16KB Kernel Stack** — Переход с PMM на Kernel Heap для предотвращения Stack Overflow
* **W^X Enforcement** — user_linker.ld с явным разделением Read+Execute и Read+Write сегментов
* **Tail Call Optimization Fix** — Флаг -fno-optimize-sibling-calls для корректной работы рекурсии

🚀 ЧТО ДЕЛАТЬ ДАЛЬШЕ (Приоритеты)

📅 День 11: Process Lifecycle
- sys_fork, sys_exec, sys_waitpid
- Copy-on-Write (CoW) для оптимизации fork
Цель: Фундамент для Супервизора (PID 1)

📅 День 12: Security & Hardening
- NX Bit (No-Execute) в Page Tables
- W^X Enforcement (Write XOR Execute)
- Core Dumps при Segfault
Цель: Защита от инъекций кода и телеметрия падений

📅 День 13: The Supervisor (PID 1)
- Написание /sbin/init с конфигом:
  [service:shell]
  exec=/bin/sh
  restart=always
- Auto-Restart при падении
Цель: Реализация философии "Let it crash"

📅 День 14: Sandboxing
- VFS chroot (подмена vfs_root для задачи)
- Seccomp (фильтр системных вызовов)
- Resource Containers + OOM Killer
Цель: Изоляция недоверенных приложений

📅 День 15: IPC & Microkernel
- Mailboxes (sys_send, sys_recv)
- Shared Memory Rings
Цель: Подготовка к User-Mode Drivers

📅 День 16+: User-Mode Drivers
- Вынос fat32_server в Ring 3
- Capability к I/O портам (0x1F0-0x1F7)
- VFS <-> fat32_server через IPC
Цель: Микроядерная архитектура (Minix 3 style)

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

🍓 RASPBERRY PI PORT (BCM2835)

НАМЕРЕНИЕ
После стабилизации x86 версии проекта (Day 20+), выполнить адаптацию ядра для запуска на Raspberry Pi Model B+ (SoC BCM2835, ARM1176JZF-S, 512 MB RAM). Это демонстрация промышленной гибкости архитектуры и доказательство платформенной независимости Bare Metal OS.

ПОЧЕМУ RPI B+ ПОДХОДИТ
Ключевая совместимость:
- ARMv6 MMU поддерживает виртуальную память (аналог x86 paging)
- 512 MB RAM достаточно для всех функций проекта
- Protected Mode (SVC/User) аналогичен Ring 0/3
- ~80% кода ядра (VFS, Scheduler, Heap, Shell) переиспользуется без изменений

КОГДА
Day 30-35, после завершения x86 версии и внедрения Hardware Abstraction Layer (HAL).

ПЛАН ПОРТИРОВАНИЯ
Day 25: Внедрение HAL в x86 код (абстрактный интерфейс для железа)
Day 30: ARM boot code + UART (serial output на реальном железе)
Day 31: PMM + VMM (ARM Translation Tables вместо x86 Page Tables)
Day 32: Interrupts + Timer (ARM Exception Vectors, BCM2835 VIC)
Day 33-34: Framebuffer (через Mailbox) + SD Card driver
Day 35: Интеграция, запуск всех тестов Day 10 на ARM

РЕЗУЛЬТАТ
Одна кодовая база работает на двух архитектурах (x86 + ARM). Это позиционирует проект как промышленную, платформенно-независимую систему, а не учебное упражнение. Открывает путь к embedded applications (IoT, robotics).

СЛЕДУЮЩИЕ ШАГИ (Day 40+)
- ARM Cortex-A порт (Raspberry Pi 3/4, 64-bit)
- RISC-V порт (SiFive, ESP32-C3 с MMU)
- Unified bootloader (multi-architecture support)

Вердикт: Стоит ли связываться?
Однозначно ДА.

BCM2835 — это идеальный полигон для изучения embedded-разработки и ARM-архитектуры.
Он достаточно мощный, чтобы тянуть VFS, VMM и ELF-лоадер без лагов.
Он достаточно простой (одно ядро, ARMv6), чтобы ты не утонул в дебрях SMP-синхронизации и когерентности кэшей, как это было бы на Cortex-A72 (Raspberry Pi 4).
У него лучшая документация в мире: BCM2835 ARM Peripherals Manual (200 страниц) описывает каждый бит каждого регистра.

Что ты получишь в итоге:
Ты возьмешь маленькую плату размером с кредитку, воткнешь в нее HDMI и USB-клавиатуру, включишь в розетку, и через 0.8 секунды на экране телевизора появится твой собственный графический Shell, работающий на твоем собственном ядре, без единой строчки кода от Linux.

Это то чувство, ради которого вообще стоит писать Bare Metal ОС. 🚀
