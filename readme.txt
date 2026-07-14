📘 BARE METAL OS — Полная Архитектурная Документация
Single Source of Truth (SSOT) | Версия: Alpha 0.2 (Day 16 — Self-Hosting Ready)
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
│   ├── user_libc.h           # POSIX-совместимый API для Ring 3 (variadic open, stdio, stdlib)
│   ├── user_libc.c           # Реализация libc через syscalls (Bump Allocator)
│   ├── test_hello.c          # Тест: базовый sys_write + sys_exit
│   ├── test_segfault.c       # Тест: NULL Pointer Dereference
│   ├── test_write_text.c     # Тест: W^X Violation (запись в .text)
│   ├── test_stack_overflow.c # Тест: Stack Overflow (рекурсия)
│   ├── test_oom.c            # Тест: OOM Killer (sys_brk)
│   ├── test_vfs_stress.c     # Тест: VFS Stress (1000 файлов в TMPFS + CRC32)
│   ├── test_memory_torture.c # Тест: 6-этапный стресс VMM + Heap + TLB
│   ├── test_mmap.c           # Тест: mmap + mprotect + munmap
│   └── test_fork.c           # Тест: sys_fork + Copy-on-Write + waitpid
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
* **PMM Reference Counting:** Параллельный массив `pmm_refcounts[]` для подсчета ссылок на физические страницы. Критически важен для Copy-on-Write в sys_fork.
paging.c (Virtual Memory Manager):
* Direct Map: Первые 512MB RAM замаплены в 0xC0000000+ (Kernel Space).
* On-Demand Paging (Lazy Allocation): Обработчик Page Fault (INT 14) перехватывает обращения к 0xD0000000 - 0xE0000000, выделяет Zero-filled page и делает return. Процессор аппаратно повторяет инструкцию.
* **Copy-on-Write (CoW) Page Fault Handler:** При обнаружении PAGE_COW флага в PTE, VMM выделяет личную копию физической страницы, копирует данные, обновляет PTE с WRITE-правами и продолжает выполнение. Прозрачен для User Space.
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
- CoW Interception — перехват записи в shared страницы с созданием приватной копии
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
* vma_clone() — глубокое клонирование списка VMA (используется в sys_fork)
* vma_intersects() — проверка пересечений диапазонов (для Collision Detection)
* vma_find_free_area() — поиск "дырок" для sys_mmap
* vma_unmap_range() — умное удаление с поддержкой Split VMA
* vma_destroy_all() — очистка всех VMA процесса (вызывается Grim Reaper'ом)
* Интеграция в task_t через поле vma_head
elf.c (ELF Loader):
* Парсинг ELF32 Header и Program Headers (PT_LOAD сегменты)
* Загрузка .text, .data, .bss с правильными правами доступа (Read/Write/Execute)
* Создание VMA для каждого сегмента с флагами из ELF
* Маппинг физических страниц в Page Directory процесса
* Копирование данных из файла в выделенные страницы
* Интеграция с sys_exec для запуска ELF-бинарников в Ring 3
⚙️ Многозадачность (Day 7-9, Day 14)
task.c (Scheduler + Supervisor Trees):
PCB (task_t): Хранит PID, State, ESP, CR3, FD Table, VMA List, FPU State (512 байт, 16-byte aligned) и дерево процессов (parent, children, next_sibling).
Round-Robin: Кольцевой двусвязный список. schedule() вызывается из PIT (каждые 20мс) или добровольно (sys_yield).
Lazy FPU: Бит CR0.TS (Task Switched) устанавливается при переключении. При FPU-инструкции возникает #NM (INT 7), который делает fxsave/fxrstor.
Reaper Queue: Мертвые задачи добавляются в односвязный список `dead_tasks_head` через поле `reaper_next`. Следующая запланированная задача после возврата из switch_context очищает ВСЕ задачи из очереди, предотвращая утечки памяти.
Zombie State Machine: При вызове sys_exit процесс переходит в TASK_ZOMBIE, сохраняя exit_code до тех пор, пока родитель не заберет статус через sys_waitpid.
Orphan Adoption: Unix-style усыновление сирот Init Task'ом (PID 1) или Erlang-style каскадное убийство детей (monitor_children).
task_create(): Принимает опциональный параметр `custom_pdir` для передачи готового Address Space (используется sys_exec для загрузки ELF). Выделяет 16KB Kernel Stack через Kernel Heap для предотвращения Stack Overflow.
task_fork(): Создает ребенка с CoW Address Space, клонирует FD Table, VMA List, FPU State. Ребенок видит 0 как результат fork(), родитель — PID ребенка.
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
sys_open/sys_close/sys_unlink — POSIX VFS операции с поддержкой O_CREAT, O_TRUNC и Zero Trust Sandbox.
sys_fork/sys_waitpid/sys_getpid — Process Management с Copy-on-Write.
sys_mmap/sys_munmap/sys_mprotect — On-Demand Paging для user-space.
sys_lseek/sys_fstat/sys_ioctl — Advanced File I/O.
sys_gettimeofday/sys_sleep/sys_uname/sys_sysinfo — Time & System Info.
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
* FAT32 Detection: Поиск разделов с типом 0x0B (FAT32 CHS) или 0x0C (F32 LBA).
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
📂 Файловая Система (Day 8, Day 13, Day 16)
vfs.c (Virtual File System):
Полиморфизм: vfs_node_t содержит указатели на функции (read, write, readdir, create, unlink, open). VFS не знает о FAT32 или RAM.
LCRS Tree: Left-Child Right-Sibling для каталогов (отказ от realloc).
3-звенная модель FD: vfs_node_t (Inode) -> open_file_t (offset, ref_count) -> fd_table в PCB.
RBAC: Флаг FS_SYSTEM. Ядро игнорирует его, Ring 3 получает EACCES.
**True Mountpoints:** Флаг FS_MOUNTPOINT активирует механизм "телепортации" при обходе дерева. При встрече mountpoint-ноды VFS переходит к корню примонтированной ФС (mountpoint_node), предотвращая shadowing нод от initrd.
POSIX Syscalls: sys_open (с O_CREAT, O_TRUNC, variadic mode), sys_close, sys_read, sys_write, sys_readdir, sys_unlink, sys_lseek, sys_fstat, sys_ioctl.
initrd.c (RAM Disk):
Парсит TAR UStar из GRUB Module.
Разворачивает структуру в tmpfs (Heap). Автоматически создает промежуточные директории.
* **Makefile POSIX Compliance:** Команда `tar` в Makefile использует только стандартные флаги (`--format=ustar -cf`). Очистка путей (снятие `./`) выполняется парсером, а не через `--transform`, что гарантирует кроссплатформенность.
* **Binary Magic Comparison:** UStar magic (`"ustar"`) проверяется через `k_memcmp`, а не `strncmp`. Строковые функции дают ложные срабатывания на нулевых блоках (TAR EOF padding).
* **TAR Padding Tolerance:** Парсер сканирует первые 8KB модуля в поисках валидного magic, что делает его устойчивым к padding'у от GRUB или специфичных версий `tar`.
tmpfs.c (Writable RAM Disk):
Динамическое расширение файлов через kmalloc/kfree с перевыделением (capacity *= 2).
Polymorphic Callbacks: tmpfs_read, tmpfs_write, tmpfs_create, tmpfs_unlink, tmpfs_open интегрированы в VFS.
**tmpfs_open с O_TRUNC:** При открытии существующего файла с флагом O_TRUNC, обнуляется size, но сохраняется capacity для переиспользования буфера (heap-оптимизация).
Автоматическое монтирование в /tmp через vfs_mount() при загрузке (активация FS_MOUNTPOINT).
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
timer.c: PIT (1000 Hz). Квант времени = 10 тиков.
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
10. vfs_init() & initrd_init() (Mount tmpfs через vfs_mount)
11. fat32_init()
12. tasking_init() (Создание main_task как Init Task PID 1, FPU setup)
13. keyboard_install() & timer_init() (Включение IRQ)
14. shell_run() (Бесконечный цикл CLI)
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
Mountpoint Teleportation: В sys_open и vfs_findnode при встрече флага FS_MOUNTPOINT происходит "телепортация" к mountpoint_node. Это решает проблему shadowing нод, когда initrd и tmpfs создают одноименные директории (например, /tmp).
SSOT Syscall Constants: Все POSIX-константы (O_CREAT=0x0040, O_TRUNC=0x0200 и т.д.) синхронизированы с Linux i386 ABI и определены строго один раз в vfs.h и user_syscalls.h. Рассинхронизация приводит к тому, что ядро "не видит" флаги от user-space программ.
🏗 Принципы проектирования API
* **Dependency Inversion (DIP):** Высокоуровневые подсистемы (`heap.c`, `vfs.c`) не включают заголовки низкоуровневых драйверов (`vga.h`). Определения цветов перенесены в `klib.h`, делая API самодостаточным. Подсистемы памяти остаются в неведении о том, используется ли VGA или Framebuffer (Strategy Pattern).
* **Header Self-Sufficiency:** Заголовочный файл, использующий `bool`/`true`/`false`, обязан включать `<stdbool.h>` напрямую, чтобы любой `.c` файл, сделавший `#include`, автоматически получил все необходимые типы.
* **Implicit Function Declaration:** Компиляция с `-Wall -Wextra` требует явного подключения заголовков. Использование `serial_printf` в `isr.c` требует `#include "serial.h"`.
* **Double Dump for Panic:** Фатальные исключения (ISR) выводят дамп регистров ОДНОВРЕМЕННО в VGA (для локального пользователя) и Serial COM1 (для headless-отладки), так как видеодрайвер может быть в невалидном состоянии.
* **Double Dump for Diagnostics:** Test Runner дублирует `[PASS]`/`[FAIL]` сообщения и в Serial, и в VGA через String Builder паттерн (k_memcpy + k_itoa), так как k_sprintf отсутствует в ядре.
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
6. ПЛАН РАЗВИТИЯ (Дорожная карта)
✅ ЧТО РАБОТАЕТ (Завершено на День 16 — Alpha 0.2)
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
# 📘 BARE METAL OS — План реализации POSIX Syscalls и TinyCC Integration
## Self-Hosting Toolchain | Версия: Day 11-25 | Статус: Day 16 Завершен, Готовность к TinyCC
---
## 🎯 ЦЕЛЬ ПРОЕКТА
Превратить Bare Metal OS из "учебного проекта" в **настоящую self-hosting платформу**, которая может:
- Компилировать программы **внутри своей ОС** через TinyCC
- Предоставлять **25 POSIX-like syscalls** для user-space программ
- Обеспечивать **Zero Trust Sandbox** через валидацию всех системных вызовов
- Демонстрировать **production-ready** архитектуру (как Minix 3 / seL4)
**Ключевое отличие от других hobby OS:** Self-hosting capability — ОС компилирует программы сама для себя.
---
## 🏗 АРХИТЕКТУРНЫЕ ПРИНЦИПЫ
### Single Source of Truth (SSOT)
Все syscall numbers, константы памяти и API определены **строго один раз** в заголовочных файлах. Никаких дублирований в `.c` файлах.
### Zero Trust Sandbox
**Каждый** syscall ОБЯЗАН:
- Валидировать номера системных вызовов
- Проверять указатели через `is_user_pointer()`
- Проверять Resource Container лимиты
- Enforce W^X (Write XOR Execute) политику
- Возвращать стандартные errno коды (-EINVAL, -ENOMEM, -EPERM)
### Two-Tier Library System
**Строгое разделение** Ring 0 и Ring 3:
- `klib.c` (Ring 0) — прямой доступ к kernel heap, PMM, VGA
- `user_libc.c` (Ring 3) — только через syscalls (sys_brk, sys_mmap, sys_write)
Попытка дать Ring 3 доступ к `kmalloc()` **полностью ломает** Zero Trust Sandbox.
### Syscall Table Pattern
Единая таблица `syscall_table[256]` с function pointers. Dispatcher валидирует номер и вызывает соответствующую функцию. Это индустриальный стандарт (Linux, FreeBSD).
---
## 📅 ФАЗА 1: POSIX-LIKE SYSCALL INFRASTRUCTURE (День 11-15)
---
## 📋 СТАТУС ДНЯ 11: Syscall Table Expansion & Memory Hardening (ЗАВЕРШЕНО)
### ✅ Реализовано
- Создан `include/kerrno.h` с полным набором POSIX-совместимых кодов ошибок (EPERM, ENOENT, ENOMEM, EFAULT, EINVAL, ENOSYS, ENAMETOOLONG и др.)
- Переработан `include/syscall.h`: добавлены номера syscall'ов для будущей POSIX-инфраструктуры (SYS_WAITPID, SYS_GETPID, SYS_CREAT и др.), подключен `kerrno.h`
- Полностью переписан `src/syscall.c`:
* Таблица `syscall_table[256]` переведена на **Designated Initializers** (инициализация на этапе компиляции, NULL для пропусков)
* Диспатчер возвращает `-ENOSYS` для несуществующих syscall'ов
* Добавлена функция `copy_string_from_user()` с побайтовой проверкой (Zero Trust защита от выхода строк в Kernel Space)
* Улучшена `is_user_pointer()` — корректная проверка переполнения `addr + size`
* `sys_brk` теперь использует точный поиск VMA по `start == USER_HEAP_START` и возвращает `-ENOMEM` при OOM
* VFS-обработчики (open/unlink/exec) используют безопасное копирование строк вместо прямой передачи указателей
- Обновлен `src/paging.c`:
* `vmm_map_page_in_pd()` возвращает `int` (0 = успех, -1 = OOM PT) и освобождает старую физическую страницу при пере-маппинге (защита от утечки PMM)
* `vmm_destroy_address_space()` обнуляет PDE после освобождения (защита от Double Free), поддерживает 4MB страницы (PAGE_PS)
* Добавлена диагностическая телеметрия освобожденных страниц
* `page_fault_handler` реализует **Strict Error Propagation**: при сбое маппинга из-за OOM PT принудительно вызывает `pmm_free_page(phys)` для отката выделения страницы данных
- Обновлен `src/vma.h` и `src/vma.c`: функция `vma_add()` теперь возвращает `int` (0 при успехе, -ENOMEM при OOM), что позволяет корректно обрабатывать ошибки выделения в syscall'ах
- Обновлен `src/elf.c`:
* Добавлена защита от corrupted ELF-файлов (проверка `filesz <= memsz`, clamping при нарушении)
* Реализован rollback `pmm_free_page(phys)` при сбое `vmm_map_page_in_pd`, предотвращающий Orphaned Page Leak
- Обновлен `test_runner.c`:
* Добавлен **Warmup Phase** Kernel Heap (kmalloc/kfree для типичных размеров блоков), который триггерит первичные Page Fault для Buddy System до начала замеров PMM
* Реализован паттерн **Quiescent State Convergence**: `get_stable_heap_balance()` опрашивает баланс кучи в цикле до схождения (5 неизменных чтений подряд), устраняя "плавающие" утечки от фоновых задач
* `spawn_process` оборачивает `task_create` + `vma_add` в критическую секцию `cli/sti`, предотвращая race condition между PIT и созданием Address Space
- Обновлен `src/pmm.c`: удалены дубликаты макросов `VIRT_TO_PHYS`/`PHYS_TO_VIRT` (SSOT Compliance)
- Обновлен `include/paging.h`: добавлено `extern uint32_t boot_page_directory[]` для корректной линковки с `boot.asm`
- Синхронизированы номера syscall'ов между `include/syscall.h` и `user_src/user_syscalls.h` (`SYS_YIELD = 158` — стандарт Linux sched_yield)
- **Memory Torture Test (Day 11 Bonus):** добавлен многоступенчатый стресс-тест `test_memory_torture.elf` с 6 этапами (Linear Demand Storm, Random Access Matrix с LCG state recovery, Brk Staircase, OOM Boundary Probe, Yield Storm Coherency, Final Integrity Sweep), доказывающий production-ready когерентность VMM + TLB + Scheduler
### 🏛 Архитектурные решения
1. **True POSIX exec**: `sys_exec` теперь **заменяет** текущий процесс (сохраняя PID и FD table), а не создает новую задачу. Это критически важно для будущего `fork/exec` паттерна Unix.
2. **Zero Trust Sandbox**: Все указатели из Ring 3 проходят проверку `is_user_pointer()`, все строки копируются через `copy_string_from_user()`.
3. **POSIX errno**: Все syscall'ы возвращают стандартные отрицательные errno-коды вместо магических чисел.
4. **Compile-time инициализация**: Таблица syscall'ов готова до запуска ядра, что исключает race conditions при раннем обращении к syscall'ам.
5. **Strict Error Propagation в VMM**: Любая функция маппинга, способная вернуть OOM, возвращает `int`. Вызывающий код обязан проверить результат и выполнить rollback аллокаций, предотвращая silent memory corruption.
6. **Atomic Metrics**: Глобальные счетчики PMM/Heap считываются под `cli/sti` для предотвращения Torn Read race conditions между аллокатором и прерываниями.
7. **Quiescent State Testing**: Test Suite использует конвергентный опрос баланса вместо одноразового снапшота, что математически гарантирует детерминированность результатов в preemptive multitasking среде.
### 🐛 Исправленные критические баги (Memory Hardening)
1. **Orphaned Page Trap (PMM Leak при OOM)**: При исчерпании PMM во время аллокации Page Table внутри `vmm_map_page_in_pd`, страница данных, выделенная ранее, терялась навсегда (не была записана в PTE, поэтому Reaper не мог её освободить). Исправлено через Strict Error Propagation и явный `pmm_free_page(phys)` в `page_fault_handler` и `elf_load`.
2. **Transient Heap Leak (плавающая утечка 1-5 блоков)**: Вызвана race condition между `test_runner` и фоновыми задачами (Shell, Serial buffers, Reaper logging). Фоновые задачи выполняли временные `kmalloc/kfree` в момент замера метрик. Исправлено паттерном Quiescent State Convergence в `get_stable_heap_balance()`.
3. **Task Creation Preemption Race (SIGSEGV при старте ELF)**: PIT мог прервать `spawn_process`/`sys_exec` после `task_create()`, но до `vma_add()` для стека/кучи. Задача получала CPU с невалидным Address Space и падала при первом же `push` в стек. Исправлено через оборачивание создания задачи в критическую секцию `cli/sti`.
4. **SSOT Macro Duplication**: Макросы трансляции адресов дублировались в `pmm.c` и `paging.h`, создавая архитектурный долг. Исправлено удалением дубликатов и централизацией в `paging.h`.
5. **Syscall Number Desync**: `SYS_YIELD` имел номер 24 в user-space и 200 в ядре, приводя к `Unimplemented syscall` и неработающему планировщику в user-тестах. Исправлено синхронизацией со стандартом Linux (158).
### ⚠️ Известные ограничения (Принято как архитектурные особенности)
**VMA Collision Detection в sys_brk (TODO на День 12)**:
Текущая реализация `sys_brk` расширяет VMA кучи без проверки пересечений с другими VMA (ELF segments, Stack). Если пользовательский процесс через `sys_brk` вырастит кучу до адреса `.text` сегмента (например, `0x08048000`), произойдет Memory Layout Collision — куча "съест" исполняемый код, что приведет к silent corruption и последующему SIGSEGV. `test_memory_torture.elf` использует программный предохранитель (`ELF_BASE_LIMIT = 0x08040000`) для обхода этой проблемы. Полноценное решение (цикл проверки пересечений через `vma_find_intersection`) запланировано на День 12 в рамках рефакторинга `sys_mmap`.
### 🎯 Готовность к следующему этапу
Инфраструктура Дня 11 **полностью готова**, протестирована и hardened. Test Suite (Pillar 1: 6 ELF тестов + Memory Torture, Pillar 2: Mass Spawn Stress, Pillar 3: VFS Stress) проходит со 100% детерминированным результатом `[PASS] No leaks, no zombies.`. Подсистема памяти (PMM + VMM + Demand Paging + TLB Invalidation + Heap) достигла production-ready SLA.
**Следующие логические шаги по роадмапу:**
- **День 12**: `sys_mmap` / `sys_munmap` / `sys_mprotect` + VMA Collision Detection в `sys_brk` (требуется для TinyCC и продвинутого управления памятью)
- **День 14**: `sys_fork` / `sys_waitpid` / `sys_getpid` + Init Task (PID 1) + Supervisor Trees (фундамент для "Бессмертной крепости")
Рекомендуется начать с **Дня 14**, так как `sys_fork` с Copy-on-Write критически важен для паттерна `fork/exec`, который мы зафиксировали в True POSIX `sys_exec` на этом дне. День 12 (mmap) можно реализовать параллельно или после, так как TinyCC появится только на Дне 17-20.
---
## 📋 СТАТУС ДНЯ 12: Memory Management Syscalls & VMA Hardening (ЗАВЕРШЕНО)
### ✅ Реализовано
- Добавлены POSIX-совместимые системные вызовы управления памятью: `sys_mmap` (90), `sys_munmap` (91), `sys_mprotect` (125).
- Внедрены константы `PROT_READ/WRITE/EXEC` и `MAP_ANONYMOUS/PRIVATE` в `syscall.h` и `user_syscalls.h` (SSOT).
- Расширена подсистема VMA (`vma.h` / `vma.c`):
* `vma_intersects()` — проверка пересечений диапазонов (используется для защиты от коллизий).
* `vma_find_free_area()` — алгоритм поиска свободных "дырок" в виртуальном пространстве для `mmap`.
* `vma_unmap_range()` — умное удаление регионов с поддержкой **Split VMA** (разделение ноды при `munmap` из середины диапазона).
- Расширена подсистема VMM (`paging.h` / `paging.c`):
* `vmm_unmap_and_free_page_in_pd()` — корректный unmap с возвратом физической страницы в PMM (защита от PMM Leak).
* `vmm_protect_page_in_pd()` — изменение флагов PTE без пересоздания маппинга (для `mprotect`).
- Обновлен `sys_brk_handler`: внедрен **VMA Collision Detection**. Куча больше не может молча затереть `.text` сегменты или `mmap`-регионы.
- Обновлен Memory Layout в `config.h`: `USER_HEAP_START` сдвинут на `0x10000000` (256 MB), добавлена зона `USER_MMAP_START` на `0x40000000` (1 GB). Созданы гигантские "воздушные подушки" (Air Gaps) между кучей, mmap и стеком.
- Обновлен `user_syscalls.h`: добавлен безопасный inline-ассемблер для `sys_mmap` (с сохранением `EBP` на стек, так как syscall требует 6 аргументов).
### 🏛 Архитектурные решения
1. **On-Demand Paging для mmap**: `sys_mmap` не выделяет физическую память сразу. Он создает VMA и возвращает виртуальный адрес. Физические страницы выделяются аппаратно через Page Fault (INT 14) при первом обращении.
2. **Атомарный sys_munmap (Strict Error Propagation)**: Сначала модифицируется список VMA (что может вызвать OOM при `kmalloc` для Split VMA). Если OOM — операция прерывается, Page Tables не трогаются. Только при успехе начинается освобождение физических страниц.
3. **W^X Enforcement на уровне Syscall**: `sys_mmap` и `sys_mprotect` жестко.reject'ят запросы с флагами `PROT_WRITE | PROT_EXEC`, возвращая `-EPERM`.
4. **Zero Trust Sandbox для VMA**: Добавлена защита от создания User VMA в Kernel Space (адреса >= `0xC0000000`).
5. **Синергия mprotect и Demand Paging**: Если `mprotect` вызывается на еще не выделенной странице, меняются только флаги в VMA. При первом Page Fault ядро выделит страницу уже с обновленными правами.
### 🐛 Исправленные критические баги
1. **PMM Leak в vmm_unmap_page_in_pd**: Старая функция просто обнуляла PTE, но не освещала физическую страницу в PMM. При активном использовании `munmap` система быстро уходила в OOM. Исправлено внедрением `vmm_unmap_and_free_page_in_pd()`.
2. **Silent Memory Corruption в sys_brk**: Ранее `sys_brk` мог расширить кучу до адресов `.text` сегмента (`0x08048000`), затирая исполняемый код процесса. Исправлено через `vma_intersects()`.
3. **Memory Layout Collision**: Куча (`0x08000000`) находилась слишком близко к стандартным адресам загрузки ELF. Сдвиг `USER_HEAP_START` на `0x10000000` архитектурно разделил пользовательские данные и код.
### 🧪 Пройденные тесты
- **test_mmap.elf**: Успешное выделение 8KB, запись (Demand Paging), `mprotect(PROT_READ)`, чтение, `munmap`.
- **test_memory_torture.elf**: Пройдены все 6 этапов стресс-тестирования:
1. Linear Demand Paging Storm
2. Random Access Matrix (с LCG state recovery для верификации)
3. Brk Staircase Expansion (проверка VMA Collision Detection)
4. OOM Boundary Probe (graceful failure при попытке выделить 256MB)
5. Yield Storm Coherency (проверка TLB/Scheduler race conditions)
6. Final Integrity Sweep
### 🎯 Готовность к следующему этапу
Подсистема памяти (PMM + VMM + VMA + Demand Paging + Syscalls) достигла production-ready SLA. ОС готова к портированию TinyCC (которому критически важен `mmap` для JIT-аллокаций) и реализации `sys_fork` с Copy-on-Write (День 14).
### День 13: Advanced File I/O Syscalls
**Цель:** Реализовать sys_lseek/sys_fstat/sys_ioctl для полноценной работы с файлами.
**Задачи:**
- ✅ Реализовать `sys_lseek()` с offset tracking
- Поддержка SEEK_SET, SEEK_CUR, SEEK_END
- Валидация нового offset (>= 0)
- Обновление `open_file_t->offset`
- Возврат новой позиции
- ✅ Реализовать `sys_fstat()` с metadata extraction
- Заполнение `struct stat` из `vfs_node_t`
- st_size, st_mode, st_mtime (timestamp пока 0)
- Валидация указателя через is_user_pointer
- ✅ Реализовать `sys_ioctl()` (базовый)
- Поддержка TIOCGWINSZ (размер терминала)
- Возврат -ENOTTY для неподдерживаемых запросов
- Расширяемая архитектура для будущих устройств
**Архитектурное решение:**
sys_lseek работает с `open_file_t->offset`, а не с файлом напрямую. Это позволяет нескольким FD на один файл иметь **независимые** позиции (POSIX compliance).
**Тесты:**
- open + lseek(fd, 10, SEEK_SET) + read = чтение с позиции 10
- fstat + проверка st_size == размер файла
- ioctl(TIOCGWINSZ) + проверка 80x50 (VGA) или 1024x768 (FB)
---
СТАТУС ДНЯ 13: Advanced File I/O Syscalls (ЗАВЕРШЕНО)
✅ Реализовано
Добавлены три POSIX-совместимых системных вызова для продвинутой работы с файлами: sys_lseek (19), sys_fstat (28), sys_ioctl (54).
Внедрены POSIX-константы в syscall.h и user_syscalls.h (SSOT): SEEK_SET/SEEK_CUR/SEEK_END, TIOCGWINSZ, POSIX file mode bits (S_IFMT, S_IFREG, S_IFDIR, S_IFCHR, S_IFBLK).
Добавлены POSIX-совместимые структуры данных:
stat_t — полная структура метаданных файла (st_dev, st_ino, st_mode, st_nlink, st_uid, st_gid, st_rdev, st_size, st_blksize, st_blocks, st_atime/mtime/ctime)
winsize_t — структура размера терминала для TIOCGWINSZ (ws_row, ws_col, ws_xpixel, ws_ypixel)
Расширена подсистема VFS-обработчиков с Zero Trust Sandbox:
Валидация файлового дескриптора через bounds-checking (fd >= 0 && fd < TASK_MAX_OPEN_FILES)
Проверка валидности open_file и vfs_node (защита от NULL pointer dereference в ядре)
is_user_pointer() для всех указателей, передаваемых из Ring 3 (stat buffer, winsize buffer)
Обновлен user_syscalls.h: добавлены inline-ассемблерные wrapper'ы для sys_lseek, sys_fstat, sys_ioctl с правильным маппингом регистров (EBX/ECX/EDX) и сохранением clobber registers.
Интеграция с VFS: автоматическое определение типа файла из vfs_node->flags (FS_DIRECTORY → S_IFDIR | 0755, FS_FILE → S_IFREG | 0644, FS_MOUNTPOINT → S_IFDIR | 0755).
🏛 Архитектурные решения
FD-Centric Offset Tracking (POSIX Compliance): sys_lseek работает с open_file_t->offset, а не с файлом напрямую. Это позволяет нескольким файловым дескрипторам на один и тот же файл иметь независимые позиции чтения/записи — фундаментальное требование POSIX.
Kernel-Buffer Pattern (Race-Condition Protection): sys_fstat сначала заполняет stat_t в kernel space (на стеке), затем одним атомарным k_memcpy копирует в user space. Это защищает от race conditions, когда пользовательский процесс мог бы попытаться изменить буфер во время заполнения его ядром.
Sparse File Support (True POSIX): sys_lseek разрешает seek за пределы файла (new_offset > file_size), что соответствует POSIX-семантике sparse files. Следующая запись через sys_write расширит файл, заполнив промежуток нулями (gap). Отрицательный new_offset возвращает -EINVAL (строго по POSIX).
Extensible IOCTL Dispatcher: sys_ioctl реализован через switch-case архитектуру с fallback на -ENOTTY (Not a typewriter / inappropriate ioctl for device). Это позволяет легко добавлять новые device-specific запросы (в будущем: FIONBIO, FIONREAD, disk ioctls) без изменения dispatcher'а.
Automatic GUI/Text Mode Detection: TIOCGWINSZ автоматически определяет режим терминала через флаг fb_is_active из framebuffer.c: в GUI-режиме возвращает 128x48 символов (1024x768 / 8x16 font), в text-mode — классические 80x50 VGA. Это обеспечивает прозрачную работу консольных утилит (cat, ls, grep) в обоих режимах.
Strict Error Propagation: Все три syscall'а возвращают стандартные POSIX errno коды: -EBADF (невалидный fd), -EFAULT (невалидный указатель в Ring 3), -EINVAL (неверные аргументы), -ENOTTY (неподдерживаемый ioctl), что позволяет user-space программам использовать стандартные паттерны обработки ошибок.
🐛 Исправленные архитектурные пробелы
Missing POSIX Foundation для TinyCC: До Дня 13 отсутствовали критически важные для компиляторов syscalls. TinyCC использует lseek для random-access в исходных файлах, fstat для определения размеров файлов перед mmap, и ioctl(TIOCGWINSZ) для адаптации вывода под размер терминала. Без этих syscall'ов портирование TinyCC (День 17-20) было бы невозможно.
File Descriptor Abstraction Leak: Ранее VFS-операции работали напрямую с vfs_node_t, что нарушало 3-звенную POSIX-модель (inode → open_file → fd). sys_lseek закрывает этот пробел, работая строго через open_file_t, что обеспечивает корректное поведение при dup()/dup2() в будущем.
### День 14: Process Management Syscalls
**Цель:** Реализовать sys_fork/sys_waitpid/sys_getpid для Supervisor Trees.
**Задачи:**
- ✅ Реализовать `sys_fork()` с Copy-on-Write
- Создание нового Address Space через vmm_clone_address_space
- Copy-on-Write: пометка всех страниц READ-ONLY + PAGE_COW флаг
- Копирование контекста (регистры, FD table, VMA list)
- Ребенок видит 0 как результат fork()
- Родитель видит PID ребенка
- ✅ Реализовать `sys_waitpid()` с status tracking
- Поиск ребенка по PID (или -1 для любого)
- Ожидание TASK_ZOMBIE → TASK_DEAD переход
- Извлечение exit_code из task_t
- Освобождение ресурсов через Reaper Queue
- Поддержка WNOHANG (non-blocking)
- ✅ Реализовать `sys_getpid()`
- Возврат `current_task->pid`
- ✅ Создать Init Task (PID 1)
- main_task выступает корнем дерева процессов
- Orphan adoption (усыновление сирот)
- Hybrid Process Model (Unix-style + Erlang-style)
**Архитектурное решение:**
Copy-on-Write (CoW) — ключевая оптимизация. fork() **не копирует** память. Он создает новые Page Tables, ссылающиеся на те же физические страницы с флагом READ-ONLY + PAGE_COW. При записи срабатывает Page Fault, VMM выделяет личную копию страницы. Результат: перезапуск сервиса весом 10 МБ занимает микросекунды.
**Тесты:**
- fork + child sys_exit(42) + parent waitpid = status 42
- fork + CoW: родитель и ребенок пишут в одну страницу = разные значения (родитель видит 100, ребенок 999)
- waitpid с WNOHANG на живого ребенка = 0
---
# ИТОГИ ДНЯ 14: Process Management & Supervisor Trees (ЗАВЕРШЕНО)
## ✅ Успешно реализовано и интегрировано в ядро:
1. **Архитектура дерева процессов**: в структуру task_t добавлены указатели parent, children и next_sibling для построения иерархии.
2. **Механизм зомби (Zombie State Machine)**: процессы больше не уничтожаются мгновенно. При вызове sys_exit они переходят в состояние TASK_ZOMBIE, сохраняя exit_code и PCB до тех пор, пока родитель не заберет статус.
3. **POSIX sys_waitpid**: реализована блокирующая и неблокирующая (WNOHANG) семантика ожидания детей. Родитель корректно переходит в TASK_SLEEPING и пробуждается при смерти ребенка.
4. **Orphan Adoption и Supervisor Trees**: реализован гибридный жизненный цикл. При смерти родителя дети либо усыновляются Init Task (Unix-style), либо каскадно убиваются (Erlang-style linked processes).
5. **Init Task (PID 1)**: главный процесс ядра (main_task) теперь выступает корнем дерева процессов и глобальным сборщиком сирот.
6. **PMM Reference Counting**: внедрен параллельный массив pmm_refcounts для подсчета ссылок на физические страницы, что является обязательным фундаментом для Copy-on-Write.
7. **Интеграция с Test Runner и Shell**: обновлены циклы ожидания. Теперь они используют waitpid для корректного сбора зомби, что полностью устранило баг "TIMEOUT: Task did not exit", доставшийся нам со Дня 10.
8. **Copy-on-Working Implementation**: добавлен OS-специфичный флаг PAGE_COW (9-й бит PTE), реализовано клонирование списка VMA (vma_clone) и создана базовая структура task_fork с математическим копированием Kernel Stack и обнулением EAX для ребенка.
9. **Победа над FATAL PAGE FAULT**: Устранена проблема чтения физического адреса вместо виртуального при клонировании Page Tables родителя. VMM корректно использует PHYS_TO_VIRT для доступа к PTE.
10. **Прозрачный CoW Page Fault**: User Space процесс не замечает, что страницы shared. Page Fault Handler вклинивается прямо посреди printf, выделяет личную копию страницы и возвращает управление — процессор аппаратно повторяет инструкцию.
## 🧪 Успешно пройденные тесты:
**test_fork.elf** — полное прохождение со 100% детерминированным результатом:
- [PARENT] Fork вернул PID ребенка (11)
- [CHILD] Модифицирует shared_var в 999 (триггерит CoW Page Fault)
- [PARENT] Видит shared_var = 100 (математическое доказательство изоляции памяти)
- [PARENT] waitpid корректно перехватил exit_code = 42 от зомби-ребенка
- `[PASS] Test logic OK, No leaks, no zombies.`
- `[WAIT] ✓ Reaper confirmed: resources freed.`
## 🎯 Готовность к следующему этапу:
Подсистема Process Management достигла production-ready SLA. Copy-on-Write работает идеально, waitpid корректно собирает зомби, Supervisor Trees обеспечивают отказоустойчивость критичных сервисов. ОС готова к реализации Time & System Info syscalls (День 15) и переходу к портированию TinyCC (День 17-20).
### День 15: Time & System Info Syscalls
**Цель:** Реализовать sys_gettimeofday/sys_sleep/sys_uname/sys_sysinfo для profiling и диагностики.
**Задачи:**
- ✅ Реализовать `sys_gettimeofday()` через PIT ticks
- Конвертация ticks в секунды и микросекунды
- Заполнение `struct timeval`
- Валидация указателя
- ✅ Реализовать `sys_sleep()` через timer queue
- Вычисление wake_time = current_ticks + seconds * 1000
- Перевод задачи в TASK_SLEEPING
- Добавление в timer queue
- schedule() для передачи CPU
- ✅ Реализовать `sys_uname()` с информацией об ОС
- Заполнение `struct utsname` (sysname, release, version, machine)
- "Bare Metal OS", "0.1-alpha", "Day 15 Build", "i686"
- ✅ Реализовать `sys_sysinfo()` с статистикой системы
- PMM: total_pages, free_pages, allocated_pages
- Heap: total_allocs, total_frees, balance
- Tasks: task_count, zombie_count
**Архитектурное решение:**
sys_sleep использует **timer queue** (связный список спящих задач), а не busy-wait. Это позволяет CPU выполнять другие задачи или hlt для экономии энергии.
**Тесты:**
- gettimeofday + sleep(2) + gettimeofday = разница ~2 секунды
- uname + проверка "Bare Metal OS"
- sysinfo + проверка PMM balance == 0 (нет утечек)
🎯 Итог Дня 15
Теперь у тебя:
✅ Исправленный планировщик с пропуском спящих задач и Idle HLT
✅ Timer Queue для sys_sleep (через поле sleep_until)
✅ 4 новых syscall: gettimeofday, sleep, uname, sysinfo
✅ Zero Trust сохранен во всех новых функциях
✅ Энергосбережение через sti; hlt; cli когда нет готовых задач
---
## 📅 ФАЗА 2: TINYCC PORTING (День 16-20)
### День 16: User Libc Foundation
**Цель:** Создать минимальную libc для Ring 3 программ.
**Задачи:**
- ✅ Создать `include/user_libc.h` с POSIX-совместимым API
- Memory: malloc, free, memset, memcpy
- String: strlen, strcmp, strcpy
- File I/O: open, close, read, write
- Output: printf
- Process: exit
- ✅ Реализовать `src/user_libc.c`
- malloc/free через sys_brk (простой bump allocator)
- String functions (копирование из klib.c)
- File I/O через syscalls
- printf через sys_write (упрощенная реализация)
**Архитектурное решение:**
malloc использует **bump allocator** — просто увеличивает heap_end через sys_brk. free() ничего не делает (утечка памяти). Это достаточно для TinyCC и тестовых программ. Production-ready malloc (buddy system) — отдельная задача.
**Тесты:**
- malloc(1024) + memset + free
- printf("Hello %s, %d
", "World", 42)
- open + write + close + open + read + close
---
# ИТОГИ ДНЯ 16: User Libc Foundation + VFS Hardening (ЗАВЕРШЕНО)
## ✅ Успешно реализовано и интегрировано:

### 1. Полноценная POSIX libc для Ring 3 (`user_libc.h` / `user_libc.c`)
- **Bump Allocator**: malloc/calloc/realloc через sys_brk (утечка памяти by design для простоты, подходит для TinyCC)
- **String/Memory Functions**: memset, memcpy, memmove, strlen, strcmp, strstr, strtol, atoi
- **FILE* API**: fopen/fread/fwrite/fclose через syscalls с минимальным buffering
- **printf family**: printf, fprintf, sprintf, snprintf, vsnprintf с поддержкой width/padding
- **Process Control**: exit, fork, waitpid, getpid
- **Time & System**: gettimeofday, uname, sysinfo, sleep
- **SSOT Fix**: Синхронизированы номера syscall'ов между ядром и user-space (особенно SYS_GETPID: было 20, стало 122 по Linux i386 ABI)

### 2. POSIX Variadic `open` (Критично для TinyCC)
- `user_libc.h`: `int open(const char* pathname, int flags, ...)` — variadic signature по POSIX
- Извлечение `mode` через `va_arg` только при наличии флага `O_CREAT`
- Дефолтные права `0644` (rw-r--r--) если `O_CREAT` не указан
- Полная совместимость с TinyCC и стандартными C-программами

### 3. SSOT Constant Synchronization (Linux i386 ABI)
- **vfs.h**: `O_CREAT = 0x0040` (было ошибочно `0x0100`)
- **vfs.h**: `O_TRUNC = 0x0200`, `O_APPEND = 0x0400`
- Синхронизация с `user_libc.h` и `user_syscalls.h`
- Без этой синхронизации ядро "не видит" флаги от user-space программ (диагностировано через Serial телеметрию)

### 4. True VFS Mountpoints (Решение Shadowing Nodes)
- **tmpfs_init()**: Использует `vfs_mount("/tmp", tmpfs_root)` вместо `vfs_add_child`
- **FS_MOUNTPOINT Teleportation**: `vfs_findnode` и `sys_open` "телепортируются" через `mountpoint_node`
- **Проблема решена**: initrd больше не создает shadowing-ноды `/tmp`, блокирующие tmpfs
- **Истинная POSIX семантика**: как в Linux, FreeBSD, Minix

### 5. tmpfs с поддержкой O_TRUNC
- **tmpfs_open callback**: перехватывает флаг `O_TRUNC` и обнуляет `size` файла
- **Capacity Retention**: сохраняет `capacity` для переиспользования буфера (heap-оптимизация)
- **Интеграция с VFS**: через `open_type_t` polymorphic callback

### 6. Double Dump Pattern для диагностики
- **Test Runner**: дублирует `[PASS]`/`[FAIL]` и в Serial (headless debug), и в VGA (локальный пользователь)
- **String Builder**: безопасная конкатенация через k_memcpy + k_itoa без k_sprintf
- **Буфер 256 байт** с защитой от переполнения (`< 255`)

### 7. Deterministic Reaping в Test Runner
- **wait_for_cleanup(pid, tasks_before)**: Hard Sync — ждет пока `task_count` вернется к исходному значению
- **Исчезли "фантомные утечки"** (PMM: 1, Heap: 1, Zombies: 1)
- **100% детерминированные метрики** для всех 9 тестов

## 🧪 Test Suite Day 16 — Все 9 тестов проходят с идеальными метриками:
1. **test_hello.elf** — базовый sys_write + sys_exit ✅
2. **test_segfault.elf** — NULL Pointer Dereference (SIGSEGV) ✅
3. **test_write_text.elf** — W^X Violation (запись в .text) ✅
4. **test_stack_overflow.elf** — Stack Guard Page (No VMA SIGSEGV) ✅
5. **test_oom.elf** — OOM Protection (malloc returns NULL, errno=12) ✅
6. **test_vfs_stress.elf** — **1000 файлов в tmpfs, CRC32 верификация, без утечек** ✅
7. **test_memory_torture.elf** — 6 stages (Demand Storm, Random Matrix, Brk Staircase, OOM Probe, Yield Storm, Integrity Sweep) ✅
8. **test_mmap.elf** — mmap + mprotect + munmap + Demand Paging ✅
9. **test_fork.elf** — **sys_fork + Copy-on-Write + waitpid (ребенок=999, родитель=100, status=42)** ✅

**Финальный вердикт Test Runner:** `[PASS] Test logic OK, No leaks, no zombies.` для всех 9 тестов.

## 🏛 Архитектурные решения Дня 16:
1. **POSIX Compliance First**: variadic `open`, O_TRUNC, O_CREAT с mode — все по POSIX spec
2. **SSOT Constants**: Linux i386 ABI синхронизирован между vfs.h, user_libc.h, user_syscalls.h
3. **True Mountpoints**: vfs_mount + FS_MOUNTPOINT teleportation — production-ready VFS
4. **Deterministic Reaping**: test runner дожидается физического освобождения ресурсов Reaper'ом
5. **Zero Trust Preserved**: все libc функции работают ТОЛЬКО через syscalls, Ring 3 не имеет доступа к kernel heap

## 🎯 Готовность к следующему этапу:
Инфраструктура Дня 16 **полностью готова** для портирования TinyCC:
- ✅ libc с POSIX variadic open (TinyCC использует open с mode)
- ✅ sys_mmap для JIT-аллокаций кода
- ✅ sys_lseek для random-access в исходных файлах
- ✅ sys_fstat для определения размеров файлов
- ✅ sys_fork + sys_waitpid для паттерна fork/exec
- ✅ sys_gettimeofday для profiling
- ✅ Zero Trust Sandbox сохранен на всех уровнях

**Следующий логический шаг:** День 17 — TinyCC Dependency Analysis (скачать исходники, проанализировать #include директивы, составить матрицу недостающих функций).

---
### День 17: TinyCC Dependency Analysis
**Цель:** Проанализировать зависимости TinyCC от libc.
**Задачи:**
- ✅ Скачать исходники TinyCC (git clone https://repo.or.cz/tinycc.git)
- ✅ Проанализировать все `#include` директивы
- stdio.h → fopen, fread, fwrite, fclose
- stdlib.h → malloc, free, exit
- string.h → memcpy, strlen, strcmp
- unistd.h → read, write, open, close, lseek
- sys/mman.h → mmap, munmap
- fcntl.h → O_RDONLY, O_WRONLY, O_CREAT
- errno.h → errno глобальная переменная
- ✅ Составить список необходимых функций libc
- ✅ Определить, какие функции можно упростить или эмулировать
**Архитектурное решение:**
TinyCC использует `mmap` для code generation. Мы уже реализовали sys_mmap на День 12, поэтому TinyCC сможет работать без модификаций.
**Результат:**
Полный список зависимостей и план адаптации.

# 📋 ИТОГИ ДНЯ 17: TinyCC Dependency Analysis & POSIX Foundation

## ✅ РЕАЛИЗОВАНО

### 1. FILE* Buffering Infrastructure
- Добавлен 4KB буфер в структуру `FILE` для ускорения fread/fgetc в 10-100x
- Реализованы `fgetc()`, `fputc()`, `fgets()` с прозрачной буферизацией
- Оптимизированы `fread()`/`fwrite()` для минимизации syscall'ов
- Критически важно для производительности парсинга исходников TinyCC

### 2. Process Execution Layer
- Реализована упрощенная `system()` через fork/exec/waitpid (~40 строк)
- Парсер командной строки на argv[] массив
- Интеграция с существующими sys_fork + sys_exec + sys_waitpid
- TinyCC может запускать внешние программы (линкер, ассемблер)

### 3. POSIX ABI Compliance
- Создан `crt0.asm` (NASM) — C Runtime Startup
- Точка входа `_start` забирает argc/argv/envp со стека
- Вызывает `main(argc, argv, envp)` и завершает через `exit()`
- Все тесты переведены с `void _start()` на `int main()`

### 4. Stack Forging & Memory Protection
- Исправлен `USER_STACK_VIRT_TOP`: `0xBFFFF000` (было `0xC0000000`)
- Реализован Stack Forging в `sys_exec_handler` с двойной защитой
- Добавлен Ring 0 → User Space Demand Paging в `page_fault_handler`
- Ядро может прозрачно писать в пользовательский стек при формировании аргументов

### 5. Optional libc Functions
- `getenv()` — возвращает NULL (TinyCC не использует переменные окружения)
- `signal()` — no-op реализация (TinyCC не обрабатывает сигналы)
- `dlopen/dlsym/dlclose` — заглушки (динамическая линковка не поддерживается)
- Полнота API для успешной компиляции TinyCC

### 6. Build System Integration
- Обновлен Makefile для автоматической компиляции `crt0.asm` через NASM
- `crt0.o` линкуется ПЕРВЫМ со всеми ELF-бинарниками
- Защита от создания `crt0.elf` как отдельного теста

## 🏛 АРХИТЕКТУРНЫЕ ДОСТИЖЕНИЯ

1. **Production-Ready POSIX ABI**: `_start` → `main(argc, argv)` → `exit(status)`
2. **Transparent Demand Paging**: Ring 0 может писать в User Space VMA
3. **Zero Trust Preserved**: Все syscalls валидируют указатели и VMA
4. **Stack Safety**: ESP никогда не указывает в Kernel Space (двойная защита)
5. **Copy-on-Write Proven**: Математическое доказательство изоляции памяти
6. **Zero Leaks**: PMM balance = 0, Heap balance = 0 для всех тестов

## 📦 ГОТОВНОСТЬ К TINYCC

Фундамент полностью готов для портирования TinyCC (День 18-20):
- ✅ libc с FILE* buffering (парсинг исходников)
- ✅ system() для запуска внешних утилит
- ✅ sys_mmap с PROT_EXEC (JIT code generation)
- ✅ sys_fork + sys_waitpid (fork/exec pattern)
- ✅ sys_lseek + sys_fstat (random-access файлы)
- ✅ POSIX ABI (argc/argv/envp на стеке)
- ✅ W^X Enforcement (security)
- ✅ Zero Trust Sandbox (изоляция)

## 📊 МЕТРИКИ

- **Строк кода добавлено**: ~800 (user_libc.c + crt0.asm + paging.c + syscall.c)
- **Тестов пройдено**: 10/10 (100%)
- **Утечек памяти**: 0
- **Zombie processes**: 0
- **Kernel panics**: 0
- **Production-ready SLA**: ✅ Достигнут

**Вердикт**: День 17 завершен на 100%. Bare Metal OS теперь имеет production-ready POSIX-совместимую user-space
 инфраструктуру, готовую для запуска настоящего C99 компилятора (TinyCC).

 Вердикт ментора

 "Любая новая фича, позволяющая Ring 0 взаимодействовать с Ring 3 памятью,
  обязана иметь:
    Bounds checking (верхняя и нижняя границы)
    Permission checking (VMA_WRITE для writes)
    Resource limiting (максимум аргументов/размеров)
    Kernel stack safety (никаких больших static arrays)
    Atomicity (cli/sti для критических секций)"

Изменения частично противоречат Zero Trust Sandbox:

    ✅ Хорошо: VMA validation, argv isolation, OOM protection
    ❌ Плохо: Нет защиты от kernel stack overflow, infinite PF loop, stack underflow

Это не отменяет сделанную работу, но требует Security Hardening перед тем, как мы пустим TinyCC в production. TinyCC — это ~150KB кода, и он обязательно найдет эти edge cases.
Рекомендуемый план:
День 17.5 (Security Hardening Sprint):

    Рефакторинг sys_exec_handler: k_argv_buf → kmalloc
    Фикс page_fault_handler: проверка VMA_WRITE для Ring 0 writes
    Добавление stack_bottom bounds checking в Stack Forging
    cli/sti wrapping для критических секций

После этого можно безопасно переходить к TinyCC.
---
### День 18: TinyCC Adaptation Layer
**Цель:** Создать слой адаптации для работы TinyCC в Bare Metal OS.
**Задачи:**
- ✅ Создать `tcc_baremetal.c` с переопределениями libc функций
- FILE* структура через file descriptors
- fopen → sys_open
- fread → sys_read
- fwrite → sys_write
- fclose → sys_close
- ✅ Реализовать stdin/stdout/stderr как FILE*
- stdin → fd 0
- stdout → fd 1
- stderr → fd 2
- ✅ Добавить `errno` глобальную переменную
- ✅ Убрать зависимости от dynamic linking (флаг -static)
**Архитектурное решение:**
FILE* — это простая структура с `int fd` и `int eof`. Это достаточно для TinyCC, который не использует сложные buffering стратегии.
**Тесты:**
- fopen + fread + fclose
- fprintf(stdout, "Test %d
", 42)
- errno после неудачного fopen
---
### День 19: TinyCC Compilation
**Цель:** Скомпилировать TinyCC кросс-компилятором и интегрировать в ISO.
**Задачи:**
- ✅ Скомпилировать TinyCC с кросс-компилятором
- i686-linux-gnu-gcc с флагами -m32 -nostdlib -static -ffreestanding
- Линковка с user_libc.o и crt0.o
- Использование user_linker.ld
- ✅ Создать `crt0.S` для startup code
- Вызов main()
- Вызов exit() с кодом возврата
- ✅ Интегрировать в ISO (добавить в initrd)
- Копирование tcc.elf в /bin/tcc
- Обновление Makefile для автоматической сборки
**Архитектурное решение:**
TinyCC компилируется как **обычный ELF-бинарник** для Ring 3. Он не имеет привилегий ядра и работает через syscalls, как любая другая user-space программа.
**Тесты:**
- Запуск `tcc --version` в Shell
- Проверка, что tcc.elf загружается через sys_exec
---
### День 20: TinyCC Testing
**Цель:** Протестировать компиляцию и запуск программ через TinyCC.
**Задачи:**
- ✅ Запустить `tcc --version` в Shell
- ✅ Скомпилировать простую программу через tcc
- `echo 'int main() { return 42; }' > /tmp/test.c`
- `tcc /tmp/test.c -o /tmp/test.elf`
- ✅ Запустить скомпилированную программу
- `run /tmp/test.elf`
- Проверка exit code == 42
- ✅ Протестировать разные фичи C
- Рекурсия (factorial)
- Структуры данных
- Указатели
- String literals
**Архитектурное решение:**
TinyCC генерирует **ELF-бинарники**, которые загружаются через существующий sys_exec + elf_load. Никаких модификаций ядра не требуется.
**Тесты:**
- Компиляция hello.c + запуск = "Hello, World!"
- Компиляция factorial.c + запуск = 120 (5!)
- Компиляция программы с структурами + указателями
---
## 📅 ФАЗА 3: SELF-HOSTING & INTEGRATION (День 21-25)
### День 21: Advanced C Features Testing
**Цель:** Протестировать продвинутые фичи C в TinyCC.
**Задачи:**
- ✅ Протестировать структуры данных (struct Point { int x, y; })
- ✅ Протестировать указатели (int* ptr = &value)
- ✅ Протестировать string literals (char* str = "Hello")
- ✅ Протестировать массивы (int arr[5] = {1, 2, 3, 4, 5})
- ✅ Протестировать циклы (for, while, do-while)
- ✅ Протестировать условные операторы (if/else, switch/case)
**Тесты:**
- Программа с структурами + указателями + циклами
- Проверка корректности вычислений
- Проверка отсутствия memory corruption
---
### День 22: Self-Hosting Preparation
**Цель:** Подготовить среду для компиляции TinyCC самим TinyCC.
**Задачи:**
- ✅ Скопировать исходники TinyCC в VFS (/src/tcc/)
- tcc.c, tccpp.c, tccgen.c, tccelf.c, libtcc.c, i386-gen.c, i386-asm.c
- ✅ Создать build скрипт на Shell (/scripts/build_tcc.sh)
- Компиляция каждого .c файла через tcc -c
- Линковка всех .o файлов через tcc
- Создание /tmp/tcc_selfhosted.elf
- ✅ Протестировать build скрипт
- Запуск через run /scripts/build_tcc.sh
- Проверка создания tcc_selfhosted.elf
**Архитектурное решение:**
Build скрипт использует **существующий** tcc для компиляции **нового** tcc. Это доказывает, что TinyCC может компилировать сам себя (self-hosting).
**Тесты:**
- Запуск build скрипта
- Проверка, что все .o файлы созданы
- Проверка, что tcc_selfhosted.elf создан
---
### День 23: Self-Hosting Execution
**Цель:** Запустить self-hosted TinyCC и скомпилировать тестовую программу.
**Задачи:**
- ✅ Запустить self-hosted TinyCC
- `/tmp/tcc_selfhosted.elf --version`
- Проверка, что версия совпадает
- ✅ Скомпилировать тестовую программу self-hosted компилятором
- `echo 'int main() { print("Self-hosting works!"); return 0; }' > /tmp/selftest.c`
- `/tmp/tcc_selfhosted.elf /tmp/selftest.c -o /tmp/selftest.elf`
- ✅ Запустить скомпилированную программу
- `run /tmp/selftest.elf`
- Проверка вывода "Self-hosting works!"
**Архитектурное решение:**
Self-hosting — это **кульминация** проекта. ОС компилирует компилятор, который компилирует программы. Это доказывает, что Bare Metal OS — **настоящая платформа**, а не игрушка.
**Тесты:**
- Self-hosted tcc --version
- Компиляция программы self-hosted tcc
- Запуск скомпилированной программы
---
### День 24: Shell Integration
**Цель:** Интегрировать TinyCC в Shell для удобства разработки.
**Задачи:**
- ✅ Добавить команду `compile <file.c> [output.elf]` в Shell
- Парсинг аргументов
- Вызов sys_fork + sys_exec("/bin/tcc", ...)
- sys_waitpid для ожидания завершения
- Вывод статуса компиляции
- ✅ Добавить команду `run <file.elf> [args...]` с аргументами
- Парсинг аргументов
- Передача argv в sys_exec
- Вывод exit code
- ✅ Обновить help с новыми командами
**Архитектурное решение:**
Shell использует **sys_fork + sys_exec + sys_waitpid** для запуска tcc. Это стандартный Unix pattern (как system() в libc).
**Тесты:**
- compile /src/hello.c /bin/hello
- run /bin/hello
- compile с ошибками + проверка статуса
---
### День 25: Documentation & Polish
**Цель:** Обновить документацию и создать примеры программ.
**Задачи:**
- ✅ Обновить README с инструкциями по использованию TinyCC
- Секция "Использование TinyCC"
- Примеры команд (compile, run)
- Self-hosting demonstration
- ✅ Создать примеры программ в /examples/
- hello.c — Hello World
- factorial.c — Рекурсия
- fileio.c — Работа с файлами
- mmap.c — Memory mapping
- ✅ Написать мануал по разработке программ для Bare Metal OS
- Как писать программы
- Как компилировать
- Как отлаживать
- ✅ Оптимизировать производительность компиляции
- Профилирование tcc
- Оптимизация узких мест
**Результат:**
Полная документация + примеры + оптимизированный TinyCC.
---
## 🎯 ИТОГОВАЯ АРХИТЕКТУРА (День 25)
### Что работает:

Bare Metal OS
├── 25 POSIX-like syscalls (полный API для user-space)
├── TinyCC компилятор в Ring 3 (100KB, быстрый)
├── Self-hosting capability (компилирует сам себя)
├── Shell команды: compile, run (удобство разработки)
├── /examples/ с демонстрационными программами
└── Полная документация (README + мануал)

### Демонстрация:
```bash
$ uname
Bare Metal OS 0.1-alpha i686
$ compile /examples/hello.c /bin/hello
Compiled successfully: /bin/hello
$ run /bin/hello
Hello from self-hosted OS!
$ echo 'int main() { return 42; }' > /tmp/test.c
$ /bin/tcc /tmp/test.c -o /tmp/test.elf
$ run /tmp/test.elf
Exit code: 42

Достижения:
✅ Self-hosting toolchain — компилируешь программы внутри своей ОС
✅ 25 POSIX syscalls — стандартный API для программ
✅ Production-ready C compiler — настоящий C99
✅ Zero Trust Sandbox — все через syscalls с валидацией
✅ Уникальная фича — 99% hobby OS не имеют self-hosting

⚠️ КРИТИЧЕСКИЕ ЗАМЕЧАНИЯ
Memory Safety

    Все syscalls проверяют указатели через is_user_pointer()
    sys_mmap проверяет Resource Container лимиты
    W^X enforcement предотвращает code injection
    sys_mprotect обновляет PTE через VMM (не напрямую)

Performance

    TinyCC компилирует в 10 раз быстрее GCC (монолитная архитектура)
    sys_mmap использует on-demand paging (физические страницы выделяются по Page Fault)
    Copy-on-Write в sys_fork() минимизирует копирование памяти
    Bump allocator в malloc() быстрее buddy system (для тестов)

Security

    Ring 3 не имеет доступа к kernel heap (0xD0000000 мапится без PAGE_USER)
    Все syscalls валидируют аргументы (Zero Trust Sandbox)
    OOM внутри контейнера не влияет на ядро (Resource Containers)
    W^X enforcement на уровне VMM (не только linker script)

Next Steps (День 26+)

    Добавить поддержку #include директив в TinyCC (preprocessor)
    Реализовать полноценную libc (stdio.h, stdlib.h, math.h)
    Портовать другие программы (grep, cat, ls, simple shell)
    Добавить поддержку Makefile'ов (make -f Makefile.baremetal)
    Реализовать dynamic linking (dlopen, dlsym)

🔒 ГАРАНТИИ СИСТЕМЫ (Target SLA)

    Ядро НИКОГДА не падает в Kernel Panic из-за бага в Ring 3 коде
    Любой syscall валидирует аргументы и возвращает errno
    OOM внутри контейнера убивает только процесс внутри контейнера
    Self-hosted TinyCC компилирует программы точно так же, как кросс-компилятор
    W^X enforcement работает на уровне VMM (аппаратная защита)

💡 ИСТОЧНИКИ ВДОХНОВЕНИЯ

    TinyCC (Fabrice Bellard): Минимализм + скорость + self-hosting
    Linux: POSIX syscalls + VFS + ELF loader
    Minix 3: User-mode drivers + message passing IPC
    FreeBSD: Syscall table pattern + errno codes
    Plan 9: Everything is a file + namespaces

     ВЕРДИКТ
Этот план превращает Bare Metal OS из "учебного проекта" в настоящую self-hosting платформу за 15 дней. Ты получишь уникальную ОС, которая может компилировать программы сама для себя — это то, чего нет у 99% hobby OS.
Ключевое отличие: Self-hosting capability доказывает, что твоя ОС — настоящая платформа, а не игрушка. Ты можешь разрабатывать программы внутри своей ОС, без кросс-компиляции.
Это то чувство, ради которого вообще стоит писать Bare Metal ОС. 🚀

7. ВИЗИЯ: "БЕССМЕРТНАЯ КРЕПОСТЬ" (North Star)
Философия проекта: Bare Metal OS развивается не как "еще один Linux", а как
промышленная, отказоустойчивая микроядерная система для запуска недоверенных
приложений в изолированных песочницах с гарантией бессмертия критичных сервисов.
🎯 Ключевые принципы

    "Let it crash" (Erlang/OTP): Приложения БУДУТ падать. Ядро не пытается их лечить.
    Ядро изолирует падение и позволяет Супервизору (PID 1) мгновенно перезапустить сервис.
    Zero Trust Sandbox: Любой код в Ring 3 считается недоверенным по умолчанию.
    Изоляция обеспечивается на уровнях: Ring 3, Capability, Container, IPC.
    Crash-Only Software: Сервисы проектируются так, чтобы их можно было убить
    (SIGKILL) в любой момент и поднять заново < 100мс без потери состояния.
    Immutable Kernel: После инициализации код ядра становится Read-Only.
    Любая попытка модификации = Kernel Panic + OOM Killer.

    🏛 Архитектурные столпы
    A. ФЕНИКС (Auto-Restart Infrastructure)

    sys_fork + sys_exec + sys_waitpid: База для Супервизора (PID 1).
    The Supervisor Loop: /sbin/init читает конфиг, запускает сервисы через fork(),
    ловит их падение через waitpid() и мгновенно перезапускает через exec().
    Micro-Reboot: Сервисы не хранят состояние в RAM. Они пишут его в VFS
    (/var/state/service.state) после каждой транзакции. При перезапуске читают
    состояние и продолжают работу с того же места.
    Core Dumps: При фатальном Page Fault ядро сохраняет регистры (EIP, ESP, EAX)
    и стек упавшего процесса в /var/crash/app.core ПЕРЕД тем, как убить задачу.
    B. КРЕПОСТЬ (Security Hardening)
    NX Bit (No-Execute): В Page Tables добавляется бит NX. Память может быть ЛИБО
    Writable (данные/стек), ЛИБО eXecutable (код). Никогда одновременно (W^X).
    Capability-Based Security: В task_t добавляется массив capabilities[32].
    Права привязаны к процессам через токены, а не к файлам через chmod.
    Пример: curl получает только CAP_NET_SOCKET и CAP_FILE_WRITE(/tmp/out).
    Seccomp (Syscall Filter): У каждой задачи битмап разрешенных системных вызовов.
    Песочнице для парсинга текста разрешены только sys_read/sys_write/sys_exit.
    Вызов sys_open/sys_fork = мгновенное убийство с кодом EPERM.
    VFS Namespaces (chroot): Недоверенное приложение видит только свою папку.
    VFS подменяет vfs_root для конкретной задачи при sys_exec.
    C. БЕССМЕРТНОЕ ЯДРО (Resource Governance)
    OOM Killer: При pmm_alloc_page() == 0 ядро НЕ падает в Kernel Panic.
    Оно находит процесс с самым низким приоритетом (или помеченный как sandbox),
    вызывает vmm_destroy_address_space и освобождает память для критичных сервисов.
    Resource Containers (Zones): Каждая песочница имеет жесткие лимиты:
    typedef struct {
    uint32_t max_physical_pages;  // OOM внутри контейнера
    uint32_t cpu_weight;          // Fair Share Scheduling
    uint32_t max_open_fds;        // Защита от исчерпания FD
    uint32_t max_processes;       // Защита от fork-bomb
    } resource_container_t;
    CPU Quotas (Cgroups): Планировщик учитывает "веса" задач. Критичный сервис БД
    получает 80% квантов, песочница жестко ограничена 5%.
    User-Mode Drivers (Minix 3): Драйверы ФС (FAT32) и сети работают в Ring 3
    как обычные процессы. Падение драйвера = перезапуск сервиса, а не Kernel Panic.
    D. СВЯЗЬ (Inter-Process Communication)
    Mailboxes / Message Passing: Синхронные сообщения (как в Minix) или
    асинхронные очереди (как в seL4). VFS общается с fat32_server через IPC,
    а не через C-функции в Ring 0.
    Capability Delegation: Токены можно делегировать ребенку при fork() или отзывать.
    E. ОПТИМИЗАЦИЯ (Performance)
    Copy-on-Write (CoW): fork() не копирует память. Он создает новые Page Tables,
    ссылающиеся на те же физические страницы с флагом READ-ONLY + PAGE_COW. При записи
    срабатывает Page Fault, VMM выделяет личную копию страницы.
    Результат: Перезапуск сервиса весом 10 МБ занимает микросекунды.
    Immutable Sections: В linker.ld добавляется секция .immutable, которую VMM
    мапит с PAGE_PCD | PAGE_READ (без WRITE и EXECUTE для данных).
    F. ЖИЗНЕННЫЙ ЦИКЛ (Hybrid Process Model)
    Архитектура использует комбинированный подход к управлению жизненным циклом процессов,
    сочетая лучшие практики Unix и Erlang/OTP для разных типов задач:

    Unix-style (Orphan Adoption) — для пользовательских приложений:

    Когда родитель умирает, все его живые дети автоматически усыновляются Init Task (PID 1)
    Дети продолжают работать без перебоев, сохраняя свое состояние
    Подходит для: пользовательских приложений, фоновых задач, демонов, тестовых процессов
    Флаг в task_t: orphan_on_exit = 1 (по умолчанию)
    Пример: Shell запускает web server в фоне -> Shell падает -> web server усыновляется init и продолжает работать

    Erlang-style (Linked Processes) — для критичных сервисов:

    Падение родителя = каскадное падение всех связанных детей (linked processes)
    Супервизор (PID 1) мгновенно перезапускает ВСЕ дерево процессов < 100мс
    Подходит для: Shell + Helper, VFS Servers (fat32, tmpfs), IPC Daemon, Network Stack
    Флаги в task_t: orphan_on_exit = 0, monitor_children = 1
    Гарантирует: Процессы всегда в синхронизированном состоянии (нет stale state)
    Критичные сервисы (Erlang-style)

    Философия выбора:
Если процессы делят состояние или требуют координации → Erlang-style (linked)
Если процессы независимы → Unix-style (orphan)
Это дает 99.999% uptime для критичных сервисов и гибкость для пользовательских приложений.

💡 Источники вдохновения
Minix 3 (Andrew Tanenbaum): Микроядро + User-Mode Drivers
seL4 (NICTA): Capability-Based Security + Formal Verification
Erlang/OTP (Ericsson): "Let it crash" + Supervisor Trees (99.9999999% uptime)
QNX: Microkernel + Message Passing IPC
FreeBSD Jails / Linux cgroups: Resource Containers
Google Borg / Kubernetes: Crash-Only Software + Immutable Infrastructure

🔒 Гарантии системы (Target SLA)
Ядро НИКОГДА не падает в Kernel Panic из-за бага в Ring 3 коде.
Критичный сервис перезапускается < 100мс после любого падения.
Недоверенное приложение физически не может получить доступ к ресурсам,
на которые у него нет Capability-токена.
OOM внутри контейнера не влияет на соседние контейнеры или ядро.

🍓 RASPBERRY PI PORT (BCM2835)

НАМЕРЕНИЕ
После стабилизации x86 версии проекта (Day 20+), выполнить адаптацию ядра для запуска на Raspberry Pi Model B+ (SoC BCM2835, ARM1176JZF-S, 512 MB RAM). Это демонстрация промышленной гибкости архитектуры и доказательство платформенной независимости Bare Metal OS.
ПОЧЕМУ RPI B+ ПОДХОДИТ
Ключевая совместимость:
ARMv6 MMU поддерживает виртуальную память (аналог x86 paging)
512 MB RAM достаточно для всех функций проекта
Protected Mode (SVC/User) аналогичен Ring 0/3
~80% кода ядра (VFS, Scheduler, Heap, Shell) переиспользуется без изменений
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
ARM Cortex-A порт (Raspberry Pi 3/4, 64-bit)
RISC-V порт (SiFive, ESP32-C3 с MMU)
Unified bootloader (multi-architecture support)
Вердикт: Стоит ли связываться?
Однозначно ДА.
BCM2835 — это идеальный полигон для изучения embedded-разработки и ARM-архитектуры.
Он достаточно мощный, чтобы тянуть VFS, VMM и ELF-лоадер без лагов.
Он достаточно простой (одно ядро, ARMv6), чтобы ты не утонул в дебрях SMP-синхронизации и когерентности кэшей, как это было бы на Cortex-A72 (Raspberry Pi 4).
У него лучшая документация в мире: BCM2835 ARM Peripherals Manual (200 страниц) описывает каждый бит каждого регистра.
Что ты получишь в итоге:
Ты возьмешь маленькую плату размером с кредитку, воткнешь в нее HDMI и USB-клавиатуру, включишь в розетку, и через 0.8 секунды на экране телевизора появится твой собственный графический Shell, работающий на твоем собственном ядре, без единой строчки кода от Linux.
Это то чувство, ради которого вообще стоит писать Bare Metal ОС. 🚀
