# 📘 Enclave Operating System — Архитектурная Документация

**Версия:** Alpha 0.6-arm-vmm  
**Дата актуализации:** 11 августа 2026   
**Доктрина Enclave:** Zero Trust, Бессмертное Ядро, Crash-Only Userspace

Enclave OS — это минималистичная higher-half операционная система, построенная вокруг идеи **изолированных пользовательских анклавов**. Ядро является бессмертным доверенным контуром, который не доверяет ни одному приложению.

Все программы исполняются в Ring 3 (x86) / USR mode (ARM) и получают доступ к ресурсам только через проверяемые системные вызовы. Память рассматривается как набор явных разрешений, W^X является законом, CoW — контролируемой оптимизацией, а crash приложения — нормальным событием, которое не должно влиять на живучесть системы.

Enclave использует POSIX-подобные интерфейсы не ради клонирования Linux, а ради практичного self-hosting и запуска привычных программных паттернов внутри строгой zero-trust архитектуры.

---
## 1. Доктрина Enclave

### 1.1 Фундаментальные Принципы

| Принцип | Описание |
|---|---|
| **Бессмертное Ядро (Immortal Kernel)** | Ядро является бессмертным доверенным контуром, который не может быть уничтожен пользовательскими процессами |
| **Нулевое Доверие (Zero Trust)** | Ядро не доверяет ни одному приложению, все данные от пользователя проверяются |
| **Crash-Only Userspace** | Падение приложения является нормальным событием, ядро продолжает работать |
| **W^X Принуждение** | Память рассматривается как набор явных разрешений: Write XOR Execute, одновременная запись и исполнение запрещены |
| **Явная Изоляция** | Все программы исполняются в Ring 3 (x86) / USR mode (ARM) |
| **Проверяемый Доступ** | Доступ к ресурсам только через валидируемые системные вызовы |
| **Контролируемый CoW** | Copy-on-Write является контролируемой оптимизацией, не правом пользователя |
| **Прагматичный POSIX** | POSIX-подобные интерфейсы используются для self-hosting, а не для клонирования Linux |

### 1.2 Архитектурный Контракт

- Ядро никогда не падает из-за действий пользователя
- Ошибка пользователя убивает задачу, ядро продолжает работу
- Ошибка ядра — это фатальный баг ядра (halt/panic)
- Весь доступ к ресурсам через VFS/системные вызовы
- Права доступа к памяти явные и принудительно соблюдаются

**Это не игрушка. Это реальная операционная система с доказанными гарантиями безопасности и стабильности.**

### 1.3 Визия: "Бессмертная Крепость"

| Принцип | Реализация |
|---|---|
| **"Let it crash"** (Erlang/OTP) | PID 1 перезапускает упавшие сервисы < 100 мс |
| **Crash-Only Software** | Сервисы не хранят состояние в RAM |
| **Immutable Kernel** | Код ядра Read-Only после инициализации |
| **Zero Trust I/O** | Устройства через VFS (`/dev/console`) |

---

## 2. Статус Проекта

### АКТИВНАЯ РАЗРАБОТКА: ARM Порт
- **Целевое железо:** Raspberry Pi 1 / QEMU `raspi1ap`
- **Процессор:** ARM1176JZF-S, архитектура ARMv6
- **Статус:** Фундамент завершён, активная работа над запуском на реальном железе
- **Фокус:** 4 KB страницы, per-process VMM, ELF загрузчик

### ЗАМОРОЖЕН: x86 Релиз
- **Версия:** Alpha 0.5-rc1
- **Статус:** Полностью функциональная, self-hosting работает
- **Политика:** Никаких новых возможностей, только критические исправления безопасности
- **Назначение:** Эталонная реализация, базовая линия документации

---

## 3. Среда Разработки

### 3.1 Общие Сведения

| Параметр | Значение |
|---|---|
| **Название** | Enclave Operating System (Enclave OS) |
| **Архитектуры** | x86 (основная), ARM (порт) |
| **x86** | 32-битный Protected Mode, Higher Half Kernel (`0xC0000000`) |
| **ARM** | ARM1176JZF-S, ARMv6, Raspberry Pi 1 / QEMU `raspi1ap` |
| **Загрузчик x86** | Multiboot 1 (GRUB) |
| **Дистрибутив x86** | Загрузочный ISO (`grub-mkrescue`) + Initrd (TAR UStar) |
| **Среда** | Linux (Debian) |
| **Эмуляция x86** | QEMU (`qemu-system-i386`) |
| **Эмуляция ARM** | QEMU (`qemu-system-arm -M raspi1ap`) |

### 3.2 x86 Toolchain

| Инструмент | Назначение |
|---|---|
| `i686-linux-gnu-gcc` | Кросс-компилятор ядра |
| `nasm` | Ассемблер |
| `GNU ld` | Линкер |
| `make`, `xorriso`, `grub-pc-bin`, `mtools` | Сборка ISO |
| `git` | Контроль версий |

**Флаги компиляции ядра:**
```makefile
CFLAGS  = -m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Iinclude
CFLAGS += -fno-pie -fno-pic -fno-stack-protector
CFLAGS += -mno-sse -mno-sse2 -mno-mmx -mno-3dnow
CFLAGS += -mincoming-stack-boundary=2 -g
```

**Флаги компиляции пользовательского пространства:**
```makefile
USER_CFLAGS  = -m32 -nostdlib -static -ffreestanding -O2 -Wall -Wextra
USER_CFLAGS += -fno-optimize-sibling-calls
USER_CFLAGS += -fno-pie -fno-pic -fno-stack-protector
```

**Флаги линковки:**
```makefile
KERNEL_LDFLAGS = -T linker.ld -nostdlib -no-pie
LDLIBS         = -lgcc

USER_LDFLAGS = -nostdlib -static -no-pie -T user_src/user_linker.ld
USER_LDLIBS  = -lgcc
```

**Принцип "Голая ОС":** ядро не использует FPU/SSE напрямую. Математика с плавающей точкой доступна только в Ring 3 через Lazy FPU Switching (`#NM`, `fxsave/fxrstor`).

**Запуск x86:**
```bash
make iso && make run
# qemu-system-i386 -cdrom build/enclave_os.iso -m 1024M -serial stdio -no-reboot
```

### 3.3 ARM Toolchain (Raspberry Pi Port)

| Инструмент | Назначение |
|---|---|
| `arm-none-eabi-gcc` | ARM cross-compiler (ARM1176JZF-S, ARMv6) |
| `arm-none-eabi-ld` | ARM linker |
| `arm-none-eabi-objcopy` | ELF → raw binary (`kernel.img`) |
| `qemu-system-arm` | Эмуляция (`-M raspi1ap`) |

**Флаги компиляции ARM:**
```makefile
CPU_FLAGS   = -mcpu=arm1176jzf-s -marm -mabi=aapcs -mno-unaligned-access
CFLAGS      = $(CPU_FLAGS) -std=gnu99 -ffreestanding -nostdlib
CFLAGS     += -O2 -Wall -Wextra -Werror=implicit-function-declaration
CFLAGS     += -fno-pie -fno-pic -fno-stack-protector
CFLAGS     += -fno-builtin -fno-common -Iinclude -DCONFIG_ARCH_ARM=1 -g
```

**Флаги линковки ARM:**
```makefile
LIBGCC  = $(shell $(CC) $(CPU_FLAGS) -print-libgcc-file-name)
LDFLAGS = -T arch/arm/linker_arm.ld -nostdlib -no-pie --gc-sections
```

**Запуск ARM:**
```bash
make -f Makefile.arm run
# qemu-system-arm -M raspi1ap -m 512M -serial stdio -kernel build/arm/kernel.img
```

### 3.4 Флаги Компиляции TinyCC

```makefile
TCC_CFLAGS  = -m32 -nostdlib -static -ffreestanding -O2 -Wall -Wextra
TCC_CFLAGS += -fno-optimize-sibling-calls
TCC_CFLAGS += -fno-pie -fno-pic -fno-stack-protector
TCC_CFLAGS += -DCONFIG_TCC_STATIC -DONE_SOURCE=1
TCC_CFLAGS += -Iuser_src -I$(TOOLCHAIN_INC)
```

| Флаг | Назначение |
|---|---|
| `-DONE_SOURCE=1` | Монолитная компиляция (все `.c` в одном `tcc.c`) |
| `-DCONFIG_TCC_STATIC` | Отключает `dlopen/dlsym` |
| `-fno-optimize-sibling-calls` | Запрет tail-call optimization (`setjmp` safety) |
| `-I$(TOOLCHAIN_INC)` | Доступ к fake POSIX headers при компиляции |

---

## 4. Структура Проекта

```text
Metal/
│
├── 📁 include/                          # Заголовочные файлы ядра (SSOT константы)
│   ├── config.h                         # ⭐ Границы памяти, архитектуры, режимы CPU
│   ├── arm_trap.h                       # ⭐ ARM trap frame layout (SVC/IRQ/Fault)
│   ├── arm_ppm.h                        # ⭐ ARM Physical Memory Manager HAL контракт
│   ├── arm_vmm.h                        # ⭐ ARM Virtual Memory Manager HAL контракт
│   ├── gdt.h, idt.h, isr.h, pic.h, tss.h # x86 дескрипторы и прерывания
│   ├── pmm.h, paging.h, heap.h, vma.h   # Управление памятью
│   ├── task.h, vfs.h, initrd.h, tmpfs.h, devfs.h
│   ├── ata.h, fat32.h                   # Storage (ATA PIO + FAT32)
│   ├── vga.h, framebuffer.h, keyboard.h, timer.h, serial.h
│   ├── klib.h, syscall.h, multiboot.h, port_io.h
│   ├── kerrno.h                         # POSIX errno codes
│   ├── univga_font.h                    # PSF1 шрифт с кириллицей
│   │
│   └── 📁 hal/                          # ⭐ HAL контракты (compile-time)
│       ├── hal_cpu.h                    # irq_save/restore, halt, barriers
│       ├── hal_mmu.h                    # HAL_PAGE_* flags, map/switch/clone
│       ├── hal_irq.h                    # IRQ register/enable/EOI/dispatch
│       ├── hal_timer.h                  # timer_init, get_ticks/ms/us
│       └── hal_uart.h                   # uart_init, putc/getc
│
├── 📁 arch/                             # ⭐ Архитектурно-специфичный код (HAL)
│   └── 📁 arm/                          # ARM порт (активная разработка)
│       ├── arm_boot.S                   # Загрузка, стеки, MMU, VBAR, higher half
│       ├── arm_vectors.S                # Вектора исключений (VBAR), SVC entry
│       ├── arm_context.S                # Переключение контекста ARM
│       ├── arm_user_asm.S               # User trampoline + тестовый образ
│       ├── arm_irq.c                    # BCM2835 IRQ контроллер + dispatch
│       ├── arm_timer.c                  # BCM2835 System Timer (1 MHz, 1 kHz tick)
│       ├── arm_uart.c                   # PL011 UART (BCM2835)
│       ├── arm_main.c                   # ARM kernel_main, планировщик
│       ├── arm_syscall.c                # ⭐ ARM SVC dispatcher
│       ├── arm_user.c                   # Настройка пользовательской памяти
│       ├── arm_elf.c                    # ⭐ ARM ELF loader (Task 2.4)
│       ├── arm_user_test.S              # Минимальный user ELF test image
│       ├── user_test.ld                 # Linker script для user_test.elf
│       ├── arm_ppm.c                    # ⭐ ARM Physical Memory Manager
│       ├── arm_vmm.c                    # ⭐ ARM Virtual Memory Manager
│       └── linker_arm.ld                # LMA 0x10000, VMA 0xC0000000
│
├── boot.asm                             # Multiboot, VBE, Higher Half Trampoline (x86)
├── linker.ld                            # LMA/VMA split (x86)
├── kernel.c                             # kernel_main, Bootstrap (x86)
│
├── descriptors_flush.asm                # lgdt, lidt, ltr
├── isr_asm.asm                          # ISR/IRQ stubs
├── context_switch.asm                   # CR3 switch, CR0.TS
├── usermode.asm                         # IRET в Ring 3
│
├── pmm.c, paging.c, heap.c              # Управление памятью (x86)
├── vma.c, elf.c                         # VMA + ELF Loader
├── task.c                               # Scheduler, Supervisor Trees
├── user_task.c                          # User task integration (x86)
├── vfs.c, initrd.c, tmpfs.c             # VFS + RAM Disks
├── devfs.c                              # ⭐ DevFS /dev/console
├── gdt.c, idt.c, isr.c, pic.c           # Дескрипторы + Прерывания
├── tss.c, syscall.c                     # TSS + Системные вызовы
├── vga.c, framebuffer.c                 # Графика
├── keyboard.c, timer.c                  # PS/2 + PIT
├── serial.c                             # COM1 (headless debug)
├── ata.c, fat32.c                       # ATA PIO + FAT32
├── klib.c                               # Внутренняя библиотека ядра
│
├── 📁 user_src/                         # ⭐ Пользовательское пространство (Ring 3)
│   ├── user_syscalls.h                  # Обёртки системных вызовов (inline asm)
│   ├── user_linker.ld                   # ELF linker script
│   ├── user_libc.h                      # ⭐ Monolithic SSOT header
│   ├── user_libc.c                      # ⭐ POSIX libc (Bump Allocator)
│   ├── setjmp.asm                       # setjmp/longjmp (NASM)
│   ├── crt0.asm                         # C Runtime Startup
│   ├── init.c                           # ⭐ PID 1 (/sbin/init.elf)
│   ├── shell_user.c                     # ⭐ Ring 3 Shell
│   └── config.h                         # Конфигурация TinyCC
│
├── 📁 initrd_src/                       # Исходники для initrd (примеры, тесты)
│   └── 📁 examples/
│       └── stres.c                      # Стресс-тест для компиляции внутри ОС
│
├── 📁 external/                         # Внешние зависимости
│   └── 📁 tcc_src/                      # TinyCC исходники (v0.9.27)
│       ├── tcc.c                        # Монолитный исходник
│       └── libtcc1.c                    # 64-bit math helpers
│
├── 📁 build/                            # Артефакты сборки (не в git)
│
├── Makefile                             # x86 сборка
├── Makefile.arm                         # ⭐ ARM сборка (не трогает x86)
└── readme.txt                           # Этот файл
```

**Ключевые принципы организации:**

1. **`include/hal/`** — контракты аппаратной абстракции, portable kernel code включает только эти заголовки
2. **`arch/`** — архитектурно-специфичная реализация HAL контрактов
3. **`kernel/`** (корневые `.c` файлы) — переносимый код, не знает о конкретной архитектуре
4. **`user_src/`** — всё пользовательское пространство, включая libc и компилятор
5. **`initrd_src/`** — исходники тестов и примеров, компилируются внутри ОС через TinyCC
6. **Раздельные Makefile** — x86 и ARM собираются независимо, не мешают друг другу

---

## 5. Двухуровневая Библиотечная Система (Two-Tier Library System)

Enclave OS использует **строгое разделение** между внутренней библиотекой ядра и пользовательской библиотекой. Это критически важно для Zero Trust Sandbox.

### 5.1 Внутренняя Библиотека Ядра (`klib.c`)

**Назначение:** Вспомогательные функции для кода ядра (Ring 0 / SVC mode)

**Доступ:**
- Прямой доступ к kernel heap (`kmalloc`, `kfree`)
- Прямой доступ к PMM (`pmm_alloc_page`)
- Прямой доступ к VGA/Framebuffer
- Прямой доступ к портам ввода-вывода

**Примеры функций:**
```c
void* kmalloc(size_t size);              // Выделение памяти ядра
void kfree(void* ptr);                   // Освобождение памяти ядра
void k_printf(const char* fmt, ...);     // Вывод в VGA/FB + Serial
uint32_t pmm_alloc_page(void);           // Выделение физической страницы
void outb(uint16_t port, uint8_t val);   // Запись в порт (x86)
```

**Ограничения:**
- **НЕ ДОСТУПНА** из Ring 3 / USR mode
- Попытка вызвать `kmalloc()` из пользовательского кода = нарушение Zero Trust
- Пользовательский код **никогда** не включает `klib.h`

### 5.2 Пользовательская Библиотека (`user_libc.c`)

**Назначение:** POSIX-совместимая стандартная библиотека для Ring 3 / USR mode

**Доступ:**
- **ТОЛЬКО** через системные вызовы (`INT 0x80` / `svc #0`)
- Никакого прямого доступа к памяти ядра
- Никакого прямого доступа к оборудованию

**Примеры функций:**
```c
void* malloc(size_t size);               // Через sys_brk (Bump Allocator)
int printf(const char* fmt, ...);        // Через sys_write
int open(const char* path, int flags);   // Через sys_open
pid_t fork(void);                        // Через sys_fork
```

**Архитектурный контракт:**
```text
Пользовательский код (Ring 3 / USR)
    ↓ вызов
user_libc.c (POSIX обёртки)
    ↓ inline asm
INT 0x80 (x86) / svc #0 (ARM)
    ↓ trap
syscall.c (ядро, Ring 0 / SVC)
    ↓ validation + execution
Возврат значения через eax / r0
```

**Ключевые особенности:**

1. **Monolithic Bypass:** `user_libc.h` определяет все типы через примитивы C, без `#include <stdint.h>`
2. **Bump Allocator:** `malloc` через `sys_brk`, `free = no-op` (by design для TinyCC)
3. **Self-Hosting:** TinyCC компилируется с этой libc, работает в Zero Trust Sandbox

### 5.3 Почему Это Важно

| Сценарий | Правильно | Неправильно |
|---|---|---|
| Ядро выделяет память | `kmalloc()` | ❌ |
| Пользователь выделяет память | `malloc()` → `sys_brk` | ❌ `kmalloc()` |
| Ядро пишет в VGA | `k_printf()` | ❌ |
| Пользователь пишет в консоль | `printf()` → `sys_write` | ❌ `k_printf()` |

**Нарушение этого разделения полностью ломает Zero Trust Sandbox.**

Если пользовательский код получит прямой доступ к `kmalloc()`, он сможет:
- Выделить память ядра
- Записать туда произвольные данные
- Получить контроль над ядром
- Обойти все механизмы безопасности

**Это не теоретическая угроза. Это фундаментальный принцип архитектуры.**

---

## 6. ARM Активная Разработка

### 6.1 Целевое Железо и QEMU

**Аппаратное обеспечение:**
- Raspberry Pi 1 Model B (BCM2835, ARM1176JZF-S)
- Архитектура ARMv6 (нет ARMv7 UAL инструкций)
- 256 MB или 512 MB RAM
- PL011 UART0 для последовательной консоли

**Эмуляция QEMU:**
```bash
qemu-system-arm -M raspi1ap -m 512M -serial stdio -kernel build/arm/kernel.img
```

**Ключевые различия:**
- QEMU загружает ядро по адресу `0x00010000`
- Реальный RPi1 загружает ядро по адресу `0x00008000`
- Оба используют BCM2835 peripheral base `0x20000000`

### 6.2 Контракт Загрузки

**Адреса загрузки:**
- QEMU: `PHYS_BASE = 0x00010000`
- Реальный RPi1: `PHYS_BASE = 0x00008000` (требует параметризации)
- Виртуальный: `KERNEL_VMA = 0xC0000000`

**Последовательность загрузки:**
1. Сохранить `r1` (machine type), `r2` (ATAGS)
2. Войти в SVC mode, отключить IRQ/FIQ
3. Настроить стеки для каждого режима (SVC/IRQ/FIQ/ABT/UND/SYS)
4. Очистить BSS (физические адреса: `phys = virt - 0xC0000000`)
5. Построить единственную TTBR0 таблицу трансляции (identity + higher half)
6. Включить MMU (SCTLR.M = 1)
7. Прыгнуть в higher half (`0xC0000000+`)
8. Установить VBAR = `_vector_table` (CP15 c12)
9. Перезагрузить SP (виртуальный адрес)
10. Вызвать `arm_kernel_main(atags, machine_type)`

**Критические ограничения:**
- Весь код `.boot.text` выполняется по физическим адресам
- Никаких вызовов в `.text` (VMA `0xC0000000+`) до включения MMU
- ARMv6: использовать CP15 barriers, не UAL `isb/dsb/dmb`

**CP15 Barrier Инструкции (ARMv6):**
```text
ISB = mcr p15, 0, r0, c7, c5, 4
DSB = mcr p15, 0, r0, c7, c10, 4
DMB = mcr p15, 0, r0, c7, c10, 5
```

### 6.3 Карта Памяти ARM



**Физическая память:**
```text
0x00000000 ┌──────────────────────┐
           │  RAM (512 MB)        │
0x00010000 │  ← kernel.img load   │  (QEMU)
0x00008000 │  ← kernel.img load   │  (RPi1)
0x20000000 ├──────────────────────┤
           │  Peripherals (16 MB) │
           │  0x20003000: System Timer
           │  0x2000B200: IRQ Controller
           │  0x20200000: GPIO
           │  0x20201000: UART0 (PL011)
0x20FFFFFF └──────────────────────┘
```

**Виртуальная память (текущий spike - одна TTBR0):**
```text
TTBR0 (4096 записей × 4B = 16 KB, 16KB aligned):
[0-255]     0x00000000 → 0x00000000  RAM (256 MB, identity)
[512-527]   0x20000000 → 0x20000000  Peripherals (16 MB, Device)
[3072-3327] 0xC0000000 → 0x00000000  Higher Half RAM (256 MB)
[3584-3599] 0xE0000000 → 0x20000000  Higher Half Peripherals
```

**Дескрипторы секций (1 MB страницы):**
```text
RAM:    PA | 0x140E  (TEX=001, C=1, B=1, AP=01, Section)
Device: PA | 0x416   (XN=1, B=1, AP=01, Section)

User Code: PA | 0x180E (AP=10: kernel RW, user RO, executable)
User Data: PA | 0x1C1E (AP=11: kernel RW, user RW, XN)
```

**Spike маппинг пользовательского режима (временный):**
```text
User code:  VA 0x00100000 → PA 0x00200000  (1 MB, user RO, executable)
User data:  VA 0x00200000 → PA 0x00300000  (1 MB, user RW, XN)

Stack A: 0x00280000 (внутри user data section)
Stack B: 0x002C0000 (внутри user data section)
```

**Будущее (4 KB страницы):**
- L1 coarse tables + L2 small pages
- Per-process TTBR0 switching
- Полноценный VMM с demand paging
- Page-level W^X принуждение

### 6.4 Модель Исключений

**Таблица векторов (VBAR-based):**
- 8 ARM исключений (не 256 как x86 IDT)
- VBAR = адрес `_vector_table` в `.text`
- Нет high vectors (`0xFFFF0000`) - ненужная 1 MB секция

**Типы исключений:**
- **SVC** (Supervisor Call): вход в системный вызов из user mode
- **IRQ**: аппаратное прерывание, preemptive планировщик
- **DAbort**: data abort (page fault, нарушение прав)
- **PAbort**: prefetch abort (ошибка загрузки инструкции)
- **UNDEF**: неопределённая инструкция

**Политика нулевого доверия:**
- Ошибка пользователя → убить задачу, ядро продолжает работу
- Ошибка ядра → фатальный дамп + halt (баг ядра)
- SVC из kernel mode → фатально (нарушение Zero Trust)

**User IRQ Frame (72 байта):**
```text
IRQ из USR mode сохраняет:
  r0-r12, SP_usr, LR_usr, padding, PC, CPSR
```

**Kernel IRQ Frame (64 байта):**
```text
IRQ из SVC mode сохраняет:
  r0-r12, lr_svc, pc, cpsr
```

**Изоляция ошибок (реализовано):**
- Определить прерванный режим через `SPSR & 0x1F`
- Ошибка пользователя → построить 72-byte frame → `arm_task_fault_kill()` → `TASK_FREE`
- Ошибка ядра → фатальный дамп + halt
- Путь обработки ошибки никогда не возвращается к упавшей задаче

### 6.5 ABI Системных Вызовов

**Транспорт:**
```text
svc #0
r7 = номер системного вызова
r0-r6 = аргументы
r0 = возвращаемое значение
```

**Номера системных вызовов:** Заморожены (те же, что и в Enclave x86 ABI)

**Валидация нулевого доверия:**
- Проверить диапазон номера системного вызова
- Проверить указатели пользовательских буферов (`is_user_pointer()`)
- Отклонить указатели ядра (`addr >= 0xC0000000`)
- Принудительно соблюдать W^X права

**Реализованные системные вызовы:**
- `sys_exit` (1): завершить задачу
- `sys_write` (4): записать в fd
- `sys_getpid` (122): получить текущий PID
- `sys_yield` (158): добровольная передача CPU
- `sys_sleep` (230): блокирующий сон (миллисекунды)

**Последовательность входа в системный вызов:**
```text
Пользователь: svc #0
→ CPU входит в SVC mode
→ VBAR + 0x08 (_svc_handler)
→ srsdb sp!, #0x13 (сохранить LR_svc, SPSR)
→ cpsid i (отключить IRQ, атомарный путь syscall)
→ push {r0-r12, lr}
→ bl arm_syscall_entry
→ валидация + выполнение
→ frame->r0 = возвращаемое значение
→ pop {r0-r12, lr}
→ rfeia sp! (восстановить user CPSR, включить IRQ)
→ возврат в USR mode
```

### 6.6 Планировщик и Модель Задач

**Состояния задач:**
```text
TASK_FREE → TASK_READY → TASK_RUNNING → TASK_SLEEPING
                                              ↓
TASK_DEAD ← TASK_ZOMBIE ←─────────────────────┘
```

**Планирование:**
- Round-robin, preemptive
- Квант: 10 ms (10 timer ticks при 1 kHz)
- Таймер: BCM2835 System Timer C1, 1 MHz base, 1 kHz tick

**Архитектура PID:**
- **PID 0**: Kernel idle (бессмертный, SVC mode)
- **PID 1+**: Пользовательские задачи (USR mode, preemptive)

**API управления задачами:**
```c
arm_current_pid()      // получить текущий PID
arm_task_yield()       // добровольное переключение контекста
arm_task_exit()        // завершить (PID 0 immortal guard)
arm_task_fault_kill()  // пометить TASK_FREE после ошибки
```

**User CPSR:**
```text
ARM_CPSR_USER = 0x50
  USR mode | FIQ disabled | IRQ enabled
```

Timer IRQ может прерывать пользовательский код напрямую. IRQ stub сохраняет полный user trap frame, вызывает планировщик, восстанавливает контекст через `rfeia`.

**Блокирующие системные вызовы:**
- Состояние `TASK_SLEEPING` с полем `wakeup_tick`
- Timer tick проверяет `wakeup_tick`, переводит в `TASK_READY`
- Планировщик пропускает спящие задачи

### 6.7 Менеджер Физической Памяти (ARM PPM)

**Статус:** Фундамент завершён (Day 51B)

**Реализация:**
- Bitmap-based page allocator
- ATAGS parsing для обнаружения RAM (`ATAG_MEM`)
- Safe-by-default: bitmap = `0xFF` (всё занято при init)
- O(1) выделение через software `ctz32()` (ARMv6 safe)
- IRQ-safe: `hal_irq_save/restore` вокруг bitmap операций
- Учёт: `pmm_allocs`, `pmm_frees`, `pmm_check_balance()`

**API резервирования:**
```c
arm_pmm_reserve_range(start, size)
```

**Текущие резервирования:**
- Нижние 64 KB
- Образ ядра
- User spike регионы (временные)

**Интеграция:**
- PMM init **ДО** `arm_user_setup()`
- Гарантирует, что пользовательская память comes from managed pool

### 6.8 Чеклист Запуска на Железе

**Критично для первого запуска на реальном железе:**

1. **Linker Script:**
   - `PHYS_BASE = 0x8000` (реальный RPi1) или параметризовать
   - Текущий: `0x10000` (только QEMU)

2. **Подготовка SD карты:**
   ```text
   config.txt:
     core_freq=250
     enable_uart=1
   
   Файлы:
     kernel.img (не kernel7.img)
     bootcode.bin
     start.elf
     fixup.dat
   ```

3. **Подключение UART:**
   - 3.3V USB-UART адаптер
   - TX ↔ RX cross-connect
   - Общий GND

4. **Терминал на хосте:**
   ```bash
   picocom -b 115200 /dev/ttyUSB0
   ```

**Что требует исправления:**
- `linker_arm.ld`: параметризация `PHYS_BASE`
- Скорость UART: зависит от `core_freq=250` в `config.txt`

### 6.9 Критические Правила ARM

**Ограничения ARMv6:**
- **Нет UAL инструкций:** ARMv6 не имеет `isb/dsb/dmb`. Использовать CP15 barriers.
- **Нет аппаратного деления:** ARM1176 генерирует `__aeabi_uidiv`. Использовать libgcc или захардкоженные константы.
- **Нет `__builtin_ctz`:** GCC может выдать ARMv7 `rbit`. Использовать software `ctz32()`.

**AAPCS Stack Alignment:**
- SP mod 8 ДОЛЖЕН БЫТЬ == 0 перед любой `bl` инструкцией
- Всегда push чётное количество регистров (например, `{r0-r12, lr}` = 14 regs = 56 bytes)
- Никогда не push `{r0-r12}` отдельно (13 regs = 52 bytes, нарушает alignment)

**MMU и память:**
- **VMA/LMA relationship:** Каждая секция должна использовать `AT(ADDR(.section) - KERNEL_VMA)`
- **VIRT_TO_PHYS underflow:** Проверять `addr >= 0xC0000000` перед вычитанием
- **TTBR1 не используется:** QEMU raspi1ap даёт Prefetch Abort с TTBR1 N=2. Использовать одну TTBR0.

**Кодирование Immediate:**
- ARM `mov` поддерживает только 8-bit immediate с even rotate
- Большие константы: использовать `ldr Rd, =value` (literal pool)

**Безопасность исключений:**
- VBAR должен быть установлен ДО включения IRQ
- `cpsid i` сразу после `srsdb` в обработчиках исключений (атомарный путь)
- Idle loop явно `cpsie i` для обеспечения продолжения timer tick

**Пользовательский режим:**
- SVC из kernel mode фатален (нарушение Zero Trust)
- Ошибка пользователя никогда не должна возвращаться к упавшей задаче
- WFI может не проснуться на ARM1176/QEMU - использовать NOP sled idle

### 6.10 Известные Ограничения ARM

| ID | Ограничение | Почему допустимо |
|---|---|---|

| ARM-002 | Пользовательский образ - сырой бинарник, не ELF | Proof-of-concept. ELF загрузчик запланирован. |
| ARM-005 | Linker может предупреждать о RWX LOAD segment | ELF segment warning, не runtime проблема. Очистить `linker_arm.ld`. |
| ARM-006 | PHYS_BASE захардкожен на 0x10000 | Работает для QEMU. Реальный RPi1 требует 0x8000 или параметризацию. |
| ARM-007 | Скорость UART захардкожена (IBRD=1, FBRD=40) | Предполагает `core_freq=250` в `config.txt`. Задокументировать требование. |

---

### 6.11 Тестирование Изоляции Ошибок (Day 47 Fault Isolation Verified)
**Статус:** Успешно верифицировано на эталонном краш-тесте.
**Механизм:**
Пользовательский тестовый образ (`arm_user_asm.S`) содержит намеренно вставленную невалидную инструкцию (`.word 0xE7F001F0` — гарантированный `UNDEF` на ARMv6) после выполнения штатных системных вызовов (`sys_getpid`, `sys_write`, `sys_sleep`).
**Результат верификации:**
- CPU генерирует исключение `UNDEF` в USR mode.
- Вектор `_undef_handler` перехватывает управление.
- Ядро определяет, что прерванный режим — USR (`SPSR & 0x1F == 0x10`).
- Формируется 72-байтный user trap frame.
- Вызывается `arm_task_fault_kill()`: задача помечается как `TASK_FREE`, ресурсы освобождаются, планировщик переключается на следующую готовую задачу (или idle).
- **Ядро продолжает работу (Immortal Kernel).**
**Доктрина:** Этот тест намеренно оставлен в базовом образе как перманентное доказательство того, что Enclave OS строго соблюдает принцип *Crash-Only Userspace* и *Zero Trust*. Падение Ring 3 кода никогда не компрометирует ядро.

---

## 7. x86 Замороженный Релиз

### 7.1 Политика Заморозки

**Версия:** Alpha 0.5-rc1  
**Статус:** Полностью функциональная, self-hosting работает

**Правила заморозки:**
- Никаких новых возможностей
- Никакого рефакторинга, кроме критических исправлений безопасности/корректности
- ABI заморожен (16 гарантий, 12 задокументированных отклонений)
- Документация остаётся как эталонная реализация

**Назначение:**
- Базовая линия для архитектурных решений ARM порта
- Доказанная реализация Zero Trust
- Работающий self-hosting toolchain (TinyCC)

### 7.2 Сборка и Запуск

**Toolchain:**
```bash
i686-linux-gnu-gcc -m32 -std=gnu99 -ffreestanding -O2
nasm -f elf32
```

**Сборка:**
```bash
make iso
make run
# qemu-system-i386 -cdrom build/enclave_os.iso -m 1024M -serial stdio
```

**Результат:** Загрузочный ISO с GRUB, ядром, initrd (TAR UStar)

### 7.3 Карта Памяти x86

**Виртуальное адресное пространство:**
```text
0x00000000 ┌─────────────────────────────────────┐
           │  USER SPACE (3 GB)                  │
0x00100000 │    ELF Segments                     │
0x10000000 │    User Heap (64 MB max)            │
0x40000000 │    mmap Region (1 GB)               │
0xBFFEF000 │    User Stack (64 KB)               │
0xBFFFF000 │    Stack Guard (4 KB)               │
0xBFFFFFFF ├─────────────────────────────────────┤
0xC0000000 │  KERNEL SPACE (1 GB)                │
0xC0000000 │    Direct Map (512 MB)              │
0xC8000000 │    Kernel Stack Pool (16 MB)        │
0xD0000000 │    Kernel Heap (128 MB Lazy)        │
0xFD000000 │    Framebuffer (16 MB)              │
0xFFFFFFFF └─────────────────────────────────────┘
```

**Ключевые константы:**
- `KERNEL_SPACE_START = 0xC0000000`
- `USER_STACK_VIRT_TOP = 0xBFFFF000`
- `KERNEL_HEAP_VIRT = 0xD0000000`

### 7.4 Подсистемы Ядра x86

**Управление памятью:**
- PMM: Two-pass E820, bitmap allocator, O(1) allocation
- Paging: Direct map, on-demand paging, CoW, W^X enforcement
- Heap: Buddy system, lazy allocation, guard pages
- VMA: Sorted linked list, collision detection, splitting

**Процессная модель:**
- Scheduler: Round-robin, preemptive, 10 ms quantum
- Task states: READY/RUNNING/SLEEPING/ZOMBIE/DEAD
- Lazy FPU: CR0.TS → #NM → fxsave/fxrstor
- Init respawn: PID 1 restarts from VFS on crash

**Файловая система:**
- VFS: Polymorphic nodes, LCRS tree, 3-tier FD model
- Initrd: TAR UStar parsing from GRUB module
- Tmpfs: Writable RAM disk, dynamic growth
- DevFS: `/dev/console` polymorphic device
- FAT32: Read-only, VFAT LFN support

**Графика и I/O:**
- Framebuffer: Double buffering, dirty rectangles, PSF1 Unicode
- VGA: 80×25 text mode fallback
- Serial: COM1 headless debug
- Keyboard: PS/2 IRQ1, ANSI escape sequences

**Безопасность:**
- Zero Trust: All syscalls validate pointers, enforce W^X
- RBAC: `FS_SYSTEM` flag protects `/boot` from Ring 3
- Orphan semantics: POSIX unlink behavior
- Crash-only: Init respawns Shell < 100 ms

### 7.5 Известные Ограничения x86 (Заморожены)

| ID | Ограничение | Почему допустимо |
|---|---|---|
| LIMIT-001 | Bump allocator (`free = no-op`) | Acceptable для короткоживущих TCC/shell процессов. Для долгоживущих сервисов — mmap-based allocator (post-stabilization). |
| LIMIT-002 | Нет сигналов (`signal = stub`) | Не входит в Enclave POSIX Lite P2. Crash-only model заменяет сигналы. |
| LIMIT-003 | Нет потоков (threads) | Не требуется на данной фазе. Один процесс = один поток. |
| LIMIT-004 | Нет динамической линковки | Static-only by design. `CONFIG_TCC_STATIC`. |
| LIMIT-005 | `waitpid` не Linux-compatible | `status == exit_code` (0..255), не `<< 8`. Enclave ABI (§9). |
| LIMIT-006 | `/boot` не в VFS | ISO-level файлы (GRUB module). RBAC `FS_SYSTEM` проверяется code review. |
| LIMIT-007 | `getcwd()` всегда `"/"` | Нет cwd tracking. Acceptable для текущей фазы. |
| LIMIT-008 | `chdir() = ENOSYS` | Нет cwd tracking. |
| LIMIT-009 | `localtime() = stub` | Нет RTC driver. |
| LIMIT-010 | `getenv() = NULL` | Нет environment. |
| LIMIT-011 | `isatty() = 1` для fd 0/1/2 | Упрощение. Нет `ioctl TIOCGWINSZ` check. |
| LIMIT-012 | `sys_brk` shrink разрешён ядром, но libc не использует | Bump-only policy. Reserved capability. |

---

## 8. Self-Hosting: TinyCC

### 8.1 Архитектурный Обзор

TinyCC (TCC) интегрирован как **полноценный Ring 3 ELF-бинарник** (`/bin/tcc.elf`), скомпилированный из исходников Fabrice Bellard (v0.9.27) кросс-компилятором `i686-linux-gnu-gcc` с флагами `-nostdlib -static -ffreestanding`.

TCC работает внутри Zero Trust Sandbox на общих основаниях:
- Не имеет прямого доступа к памяти ядра
- Все операции через `INT 0x80` (`sys_open`, `sys_read`, `sys_write`, `sys_mmap`, `sys_brk`)
- Crash TCC не влияет на ядро (Init перезапустит Shell, Shell перезапустит компиляцию)
- W^X enforcement применяется к генерируемому коду

### 8.2 Self-Hosting Pipeline

```text
┌─────────────────────────────────────────────────────────────────────┐
│                    SELF-HOSTING PIPELINE                            │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  User Space (Ring 3)                                                │
│  ┌──────────┐    fork+exec    ┌──────────┐    fork+exec             │
│  │  Shell   │ ──────────────→ │ tcc.elf  │ ──────────────→ out.elf  │
│  │ (PID N)  │                 │ (PID N+1)│                 (PID N+2)│
│  └──────────┘                 └──────────┘                          │
│       │                          │                                  │
│       │ waitpid()                │ sys_open("/src/hello.c")         │
│       │                          │ sys_read() → parse → codegen     │
│       │                          │ sys_write("/tmp/hello.elf")      │
│       │                          │ sys_exit(0)                      │
│       ▼                          ▼                                  │
│  ┌──────────────────────────────────────────────────────────┐       │
│  │              INT 0x80 (Zero Trust Boundary)              │       │
│  └──────────────────────────────────────────────────────────┘       │
│                                                                     │
│  Kernel Space (Ring 0)                                              │
│  ┌──────────────────────────────────────────────────────────┐       │
│  │  syscall.c: validation → VFS → PMM → VMM → ELF Loader    │       │
│  └──────────────────────────────────────────────────────────┘       │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

### 8.3 Конфигурация TCC (`user_src/config.h`)

```c
#define TCC_TARGET_I386            1       // 32-bit x86
#define TCC_VERSION                "0.9.27"
#define CONFIG_TCC_STATIC          1       // Нет dlopen/dlsym
#define CONFIG_TCCDIR              "/lib"
#define CONFIG_TCC_SYSINCLUDEPATHS "/include:/usr/include"
#define CONFIG_TCC_LIBPATHS        "/lib:/usr/lib"
#define CONFIG_TCC_CRTPREFIX       "/lib"
#define CONFIG_TCC_ELFINTERP       ""      // Нет динамического линкера
#define HOST_TRIPLET               "i386-pc-enclaveos"
#define CONFIG_TCC_SEMLOCK         0       // Нет семафоров
#define CONFIG_TCC_BACKTRACE       0       // Нет backtrace
#define CONFIG_TCC_BCHECK          0       // Нет bounds checking
#define CONFIG_TCC_USE_LIBGCC      0       // libtcc1.a вместо libgcc
#define CONFIG_TCC_MMAP            0       // malloc вместо mmap
```

### 8.4 Toolchain Injection (Makefile → initrd.tar)

При сборке ISO Makefile автоматически формирует **полный toolchain** внутри initrd для работы TCC в Ring 3:

```text
initrd_root/
├── bin/
│   ├── tcc.elf          ← Компилятор (Ring 3)
│   ├── shell.elf        ← Shell (Ring 3)
│   └── *.elf            ← Тестовые программы
├── sbin/
│   └── init.elf         ← PID 1 Launcher
├── lib/
│   ├── crt0.o           ← C Runtime (из user_src/crt0.asm)
│   ├── crt1.o           ← Алиас crt0.o
│   ├── crti.o           ← Empty stub (section .text)
│   ├── crtn.o           ← Empty stub (section .text)
│   ├── libc.a           ← Архив: user_libc.o + setjmp.o
│   └── libtcc1.a        ← 64-bit math helpers (из tcc_src/libtcc1.c)
├── usr/lib/
│   └── (зеркало /lib/)
├── include/
│   ├── user_libc.h      ← Monolithic SSOT header
│   ├── user_syscalls.h  ← Syscall wrappers
│   ├── stdio.h          ← #include "user_libc.h"
│   ├── stdlib.h         ← #include "user_libc.h"
│   ├── string.h         ← #include "user_libc.h"
│   ├── ...              ← 16 fake POSIX headers
│   └── sys/
│       ├── mman.h       ← #include "../user_libc.h"
│       ├── types.h      ← #include "../user_libc.h"
│       └── ...
└── usr/include/
    └── (зеркало /include/)
```

**Ключевые решения:**
- `libc.a` собирается через `i686-linux-gnu-ar rcs` из двух объектов: `user_libc.o + setjmp.o`
- Fake POSIX headers — однострочные `#include "user_libc.h"` (Monolithic Bypass)
- CRT файлы копируются из объектных файлов сборки (не из исходников)
- `libtcc1.a` содержит хелперы для 64-битной арифметики (`__divdi3`, `__moddi3`, etc.)
- Тесты из `initrd_src/examples/` компилируются внутри ОС через TCC, не на хосте

### 8.5 Команда Shell: `compile`

```c
// shell_user.c — handle_compile()
// Usage: compile <file.c> [output.elf]
// Default output: /tmp/a.out
pid_t pid = fork();
if (pid == 0) {
    const char* tcc_argv[] = { "tcc", input_file, "-o", output_file, NULL };
    exec("/bin/tcc.elf", tcc_argv);
    exit(127);  // exec failed
}
waitpid(pid, &status, 0);
```

---

## 9. POSIX ABI Гарантии

### 9.1 Public User ABI (Ring 3 → Kernel)

**Гарантии (FROZEN — не менять без major version bump):**

| # | Гарантия | Детали |
|---|---|---|
| 1 | Syscall convention | x86: `INT 0x80`, `eax` = номер, `ebx/ecx/edx/esi/edi` = аргументы. ARM: `svc #0`, `r7` = номер, `r0-r6` = аргументы. Return: `eax`/`r0` (`>= 0` success, `< 0` = `-errno`) |
| 2 | POSIX return convention | Все libc-обёртки: при ошибке `return -1` (или `NULL`/`MAP_FAILED`), `errno = -ret` |
| 3 | `fork()` | CoW clone. Child: `fork() == 0`. Parent: `fork() == child_pid`. Error: `-1 + errno` |
| 4 | `exec()` | Заменяет образ. FD table сохраняется. `argv` на user stack. Error: `-1 + errno` (процесс жив) |
| 5 | `waitpid()` status | `status == exit_code` (0..255) при normal exit. `status == -1` при kernel kill. **НЕ Linux-compatible** (нет `<< 8`) |
| 6 | `mmap()` | `MAP_ANONYMOUS`: on-demand paging. `MAP_FAILED` при ошибке. W^X enforced |
| 7 | `mprotect()` | W^X: `PROT_WRITE|PROT_EXEC` → `-EPERM`. Частичное покрытие: VMA splitting |
| 8 | `open()` variadic | `open(path, flags, ...)` — mode через `va_arg` при `O_CREAT` |
| 9 | `close()` / `dup()` / `dup2()` | POSIX semantics. `dup2` атомарно закрывает `newfd` |
| 10 | `unlink()` | Orphan semantics: файл жив пока открыт. `ENOTEMPTY` для непустых директорий |
| 11 | `mkdir()` | RBAC: `FS_SYSTEM` → `EACCES`. `S_IFDIR` → `FS_DIRECTORY` |
| 12 | `fstat()` | `st_size`, `st_mode` (`S_IFREG`/`S_IFDIR`), `st_ino` |
| 13 | `sys_brk` | Bump-only из libc. `brk(0)` = текущий end. `brk(new)` = expand |
| 14 | User Stack | 64 KB, guard page 4 KB. Переполнение → `SIGSEGV` (task kill) |
| 15 | User Heap | 64 MB max. `sys_brk` collision detection с VMA |
| 16 | `fork()` FD inheritance | `ref_count++` для `open_file_t` + `vfs_node_t` |

### 9.2 Осознанные Отклонения от Linux (Зафиксированы, Не Баги)

| # | Область | Enclave OS | Linux | Обоснование |
|---|---|---|---|---|
| D1 | waitpid status | `exit_code` (0..255) | `exit_code << 8` | Упрощение, нет `W*` макросов |
| D2 | waitpid killed | `status == -1` | `WIFSIGNALED` | Нет сигналов |
| D3 | `free()` | no-op | Возврат в heap | Bump allocator (TCC by design) |
| D4 | `mprotect(W|X)` | `-EPERM` | Разрешено | W^X = закон |
| D5 | `getcwd()` | Всегда `"/"` | Реальный cwd | Нет cwd tracking |
| D6 | `chdir()` | `ENOSYS` | Работает | Нет cwd tracking |
| D7 | `signal()` | Stub → `SIG_DFL` | Реальная доставка | Нет подсистемы сигналов |
| D8 | `dlopen/dlsym` | `ENOSYS` | Реальный dl | Static-only OS |
| D9 | `localtime()` | Stub (1 Jan 1970) | Реальное время | Нет RTC driver |
| D10 | `getenv()` | Всегда `NULL` | Реальный env | Нет environment |
| D11 | `isatty()` | `1` для fd 0/1/2 | `ioctl TIOCGWINSZ` | Упрощение |
| D12 | `sys_brk` shrink | Ядро позволяет, libc не вызывает | Уменьшает heap | Reserved capability |

### 9.3 Internal Kernel ABI (НЕ Гарантирован)

| # | Элемент | Статус |
|---|---|---|
| 1 | Номера syscall | Frozen (Linux i386 ABI). Transport: x86 `INT 0x80`, ARM `svc #0` |
| 2 | `vfs_close_fd()` | Internal. Ring 0 НЕ вызывает `sys_close()` |
| 3 | `task_t` layout | Internal. Может меняться |
| 4 | VMA flags (`VMA_COW` и т.д.) | Internal |
| 5 | Page table format | Internal (x86 2-level, ARMv6 short descriptor) |
| 6 | Kernel heap API (`kmalloc`) | Internal. Ring 3 НИКОГДА |

### 9.4 TCC Compatibility Rules

| # | Правило | Обоснование |
|---|---|---|
| T1 | Tagged structs | TCC пишет `struct timeval tv;` → нужен тег. Паттерн: `struct X {...}; typedef struct X X_t;` |
| T2 | `#ifndef CONFIG_TCC_STATIC` guard | TCC определяет static stubs `dl*` с другими сигнатурами |
| T3 | `-fno-optimize-sibling-calls` | `setjmp/longjmp` safety |
| T4 | Heapsort (не Quicksort) | O(1) stack для 64 KB Ring 3 stack |

### 9.5 ABI Change Policy

| Тип изменения | Процедура |
|---|---|
| Добавление syscall | Только в minor version bump |
| Изменение поведения существующего syscall | Только в major version bump |
| Изменение internal kernel ABI | Свободно (не гарантирован) |
| Изменение отклонений D1-D12 | Только в major version bump + миграция |

---

## 10. Кросс-Архитектурные Правила

**Универсальные принципы (применяются к ARM и x86):**

1. **Ядро Никогда Не Доверяет Пользовательским Указателям**
   - Валидировать все аргументы системных вызовов
   - `is_user_pointer()` проверяет границы
   - Отклонять адреса ядра (`>= 0xC0000000`)

2. **W^X Это Закон**
   - Память не может быть одновременно writable и executable
   - Принудительно соблюдается на page/section level
   - `mmap(PROT_WRITE|PROT_EXEC)` → `-EPERM`

3. **Пользовательский Crash Это Норма**
   - Ошибка пользователя убивает задачу, ядро продолжает работу
   - Планировщик переключается на следующую готовую задачу
   - Init перезапускает упавшие сервисы

4. **Ошибка Ядра Это Фатально**
   - Баг ядра → фатальный дамп + halt
   - Нет восстановления из kernel exceptions
   - Triple fault / panic это допустимая смерть ядра

5. **PID 0 Бессмертен**
   - Kernel idle task не может быть убита
   - `sys_exit` из PID 0 игнорируется
   - Система всегда имеет хотя бы одну работающую задачу

6. **Доступ к Ресурсам Через VFS/Syscalls**
   - Нет прямого доступа к оборудованию из Ring 3 / USR mode
   - Все устройства доступны через `/dev/*` VFS nodes
   - Системные вызовы — единственные точки входа в ядро

7. **Явные Права Доступа к Памяти**
   - Каждый маппинг имеет явные R/W/X права
   - Нет неявных прав
   - Guard pages защищают от переполнения стека

8. **IRQ Safety в Критических Секциях**
   - Использовать паттерн `irq_save()/irq_restore()`
   - Избегать безусловных `cli/sti` кроме idle/fatal loops
   - Все операции аллокаторов IRQ-safe

---

## 11. Критические Архитектурные Нюансы

### 11.1 ARM Критические Нюансы

**CP15 Barriers (ARMv6):**
```text
ARMv6 не имеет UAL isb/dsb/dmb инструкций.
Использовать CP15 coprocessor:
  ISB = mcr p15, 0, r0, c7, c5, 4
  DSB = mcr p15, 0, r0, c7, c10, 4
  DMB = mcr p15, 0, r0, c7, c10, 5
```

**PHYS_BASE Различие:**
```text
QEMU raspi1ap: ядро загружается по адресу 0x00010000
Реальный RPi1: ядро загружается по адресу 0x00008000
PHYS_BASE в linker_arm.ld должен совпадать или быть параметризован
```

**VMA/LMA Формула:**
```text
Каждая секция в linker script ДОЛЖНА использовать:
  AT(ADDR(.section) - KERNEL_VMA)
Иначе MMU транслирует в мусорные адреса
```

**VBAR Setup:**
```text
VBAR должен быть установлен ДО включения IRQ
VBAR = адрес _vector_table в .text (kernel virtual)
Не использовать high vectors (0xFFFF0000) - ненужная 1 MB секция
```

**UART Clock Dependency:**
```text
PL011 baud rate зависит от core_freq в config.txt
IBRD=1, FBRD=40 предполагает UARTCLK=3MHz (core_freq=250)
Документировать: config.txt должен содержать core_freq=250
```

**Stack Alignment (AAPCS):**
```text
Перед любой bl инструкцией: SP mod 8 ДОЛЖЕН БЫТЬ == 0
Всегда push чётное количество регистров
Пример: push {r0-r12, lr} = 14 regs = 56 bytes ✅
        push {r0-r12} = 13 regs = 52 bytes ❌
```

**Immediate Encoding:**
```text
ARM mov поддерживает только 8-bit immediate с even rotate
Большие константы: использовать ldr Rd, =value (literal pool)
Пример: mov r7, #999 не работает, использовать mov r7, #99 или ldr r7, =999
```

**SVC Kernel-Mode Fatal:**
```text
SVC из kernel mode это нарушение Zero Trust
svc_handler проверяет SPSR mode
Если SVC из SVC/kernel mode → фатальный дамп + halt
Только SVC из USR mode это валидный syscall
```

**User Fault Never Returns:**
```text
User UNDEF/DABT/PABT → arm_task_fault_kill()
Задача помечается TASK_FREE
Путь обработки ошибки никогда не выполняет rfeia
Планировщик переключается на другую задачу
Упавшая задача никогда не возобновляется
```

**WFI May Not Wake:**
```text
ARM1176 + QEMU: WFI может не проснуться по System Timer IRQ
hal_cpu_idle() использует NOP sled:
  cpsie i
  nop × 4
  cpsid i
Для production Cortex-A: заменить на WFI
```

### 11.2 x86 Критические Нюансы (Замороженная Ссылка)

**IRET Stack Switch Trap:**
```text
popa игнорирует поле ESP
iret читает user ESP из аппаратного стека
Нужно обновлять r->useresp, не r->esp
Иначе возврат на неправильный стек → crash
```

**EOI Lock Bypass:**
```text
Отправить EOI в PIC ДО вызова C обработчика
outb(0x20, 0x20) перед dispatch обработчика
Иначе schedule() переключит задачу, IRQ линия заблокируется навсегда
```

**VIRT_TO_PHYS Underflow:**
```text
.boot секции имеют адреса < 0xC0000000
VIRT_TO_PHYS макрос ДОЛЖЕН проверять:
  if (addr >= KERNEL_SPACE_START)
    return addr - KERNEL_SPACE_START
  else
    return addr
Иначе unsigned underflow → мусор в CR3
```

**CoW Teardown:**
```text
pmm_dec_ref() освобождает страницу только когда refcount == 0
Strict teardown предотвращает use-after-free
CoW-safe mprotect сохраняет PAGE_COW флаг
Если PAGE_COW активен, PAGE_WRITE принудительно снимается
```

**Kernel Heap Isolation:**
```text
Kernel heap по адресу 0xD0000000 маппится без PAGE_USER
Ring 3 доступ → page fault → SIGSEGV
Zero Trust: пользователь не может получить доступ к kernel heap даже с указателем
```

**Stack Guard Pages:**
```text
Kernel stack pool: 1 guard page + 4 data pages
Переполнение стека попадает в guard page → SIGSEGV → task kill
Предотвращает повреждение kernel stack из-за багов пользователя
```

**Framebuffer PCD:**
```text
LFB маппится с PAGE_PCD (Cache Disable)
Без PCD: кэш-артефакты, tearing, corruption
Frame buffer это device memory, не normal RAM
```

**W^X mprotect Enforcement:**
```text
mprotect(PROT_WRITE|PROT_EXEC) → -EPERM
VMA splitting для частичного mprotect
CoW-safe: сохраняет PAGE_COW + PWT/PCD/GLOBAL флаги
Page fault handler принудительно соблюдает права
```

---

## 12. Дорожная Карта ARM

### Фаза 1: Стабилизация Загрузки на Железе ✅ ЗАВЕРШЕНО
- [x] ARM boot spike (Days 36-37)
- [x] IRQ + Timer + Vectors (Days 38-40)
- [x] Переключение контекста + Планировщик (Days 41-42)
- [x] SVC + User mode (Days 43-45)
- [x] IRQ preemption из user mode (Day 46)
- [x] Изоляция ошибок пользователя (Day 47)
- [x] SVC full user frame (Day 48)
- [x] Блокирующие системные вызовы `sys_sleep` (Day 49)
- [x] SVC IRQ hardening (Day 50)
- [x] PMM фундамент (Day 51B)

### Фаза 2: 4 KB Страницы и Реальный VMM 🔄 В ПРОЦЕССЕ
- [x] **Задача 2.1: ARM 4 KB Page Tables**
  - Реализовать L1 coarse tables (1024 записей, 4KB каждая)
  - Реализовать L2 small pages (256 записей, 1KB каждая)
  - Заменить 1 MB section mappings на 4 KB страницы
  - Обновить `build_page_tables` в `arm_boot.S`
  - **Критерий приёмки:** Ядро загружается с 4 KB page granularity

- [x] **Задача 2.2: ARM VMM Per-Process Address Space**
  - Реализовать `arm_vmm_create_address_space()`
  - Реализовать `arm_vmm_destroy_address_space()`
  - Реализовать `arm_vmm_clone_address_space()` для fork
  - Per-process TTBR0 switching
  - **Критерий приёмки:** Несколько пользовательских задач с изолированными address spaces

- [x] **Задача 2.3: Миграция Пользовательских Задач на 4 KB Страницы**
  - Заменить section mappings на 4 KB страницы для user code/data
  - Реализовать demand paging для пользовательского пространства
  - Обновить обработчик ошибок пользователя для page-level granularity
  - **Критерий приёмки:** Пользовательские задачи работают с 4 KB страницами, demand paging работает

- [ ] **Задача 2.4: ELF Загрузчик для ARM**
  - Парсить ELF заголовки (ARM little-endian)
  - Загружать сегменты с правильными правами (R/X для .text, R/W для .data)
  - Настроить entry point и начальный stack
  - Заменить сырой бинарный user image на ELF загрузку
  - **Критерий приёмки:** Может загружать и выполнять ARM ELF бинарники

- [ ] **Задача 2.5: Полное W^X Page-Level Принуждение**
  - Принудительно соблюдать W^X на page level (не section level)
  - Реализовать `sys_mmap` с on-demand paging
  - Реализовать `sys_mprotect` с VMA splitting
  - Обработчик page fault принудительно соблюдает права
  - **Критерий приёмки:** `mmap(PROT_WRITE|PROT_EXEC)` возвращает `-EPERM`

### Фаза 3: Production Возможности 📋 ЗАПЛАНИРОВАНО
- [ ] **Задача 3.1: Параметризация PHYS_BASE**
  - Обнаруживать QEMU vs реальный RPi1 при загрузке
  - Параметризовать `PHYS_BASE` (0x10000 для QEMU, 0x8000 для RPi1)
  - Обновить linker script или runtime relocation
  - **Критерий приёмки:** Один и тот же kernel.img загружается на QEMU и реальном железе

- [ ] **Задача 3.2: ARM Cortex-A Порт (ARMv7/ARMv8)**
  - Портировать на Raspberry Pi 2/3 (Cortex-A7/A53)
  - Использовать UAL `isb/dsb/dmb` инструкции
  - Включить аппаратное деление
  - **Критерий приёмки:** Enclave OS работает на современном ARM железе

- [ ] **Задача 3.3: User-Mode Драйверы (Minix 3 Модель)**
  - Реализовать message passing IPC
  - Переместить UART/timer драйверы в user space
  - Ядро предоставляет минимальные IPC примитивы
  - **Критерий приёмки:** Аппаратные драйверы работают в Ring 3, ядро остаётся минимальным

- [ ] **Задача 3.4: Security Hardening**
  - Реализовать seccomp (syscall filter)
  - Реализовать VFS namespaces (chroot-like изоляция)
  - Capability-based security model
  - **Критерий приёмки:** Fine-grained контроль доступа для пользовательских анклавов

---

**Конец документа.**
