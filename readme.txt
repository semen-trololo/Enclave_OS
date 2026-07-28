````markdown
# 📘 Enclave Operating System — Полная Архитектурная Документация

**Версия:** Alpha 0.6-arm-preempt
**Дата актуализации:** 28 июля 2026
**Статус:** ARM User IRQ Preemption Complete + Validation Pass (Day 46)

**Enclave Doctrine:** Zero Trust, Immortal Kernel, Crash-Only Userspace.

Enclave OS — это минималистичная x86 higher-half operating system, построенная вокруг
идеи изолированных пользовательских анклавов. Ядро является бессмертным доверенным
контуром, который не доверяет ни одному приложению.

Все программы исполняются в Ring 3 и получают доступ к ресурсам только через
проверяемые системные вызовы. Память рассматривается как набор явных разрешений,
W^X является законом, CoW — контролируемой оптимизацией,
а crash приложения — нормальным событием, которое не должно влиять на живучесть
системы. Enclave использует POSIX-подобные интерфейсы не ради клонирования Linux,
а ради практичного self-hosting и запуска привычных программных паттернов внутри
строгой zero-trust архитектуры.

---

## 📑 СОДЕРЖАНИЕ

1. [Среда разработки](#1-среда-разработки)
2. [Структура проекта](#2-структура-проекта)
3. [Архитектурные принципы](#3-архитектурные-принципы)
4. [Карта памяти](#4-карта-памяти)
5. [Подсистемы ядра](#5-подсистемы-ядра)
6. [Многозадачность и процессная модель](#6-многозадачность-и-процессная-модель)
7. [Системные вызовы](#7-системные-вызовы)
8. [User Space и Self-Hosting](#8-user-space-и-self-hosting)
9. [Гарантии системы (SLA)](#9-гарантии-системы-sla)
10. [Известные проблемы и Roadmap](#10-известные-проблемы-и-roadmap)
11. [Приложения](#-приложение-a-критические-архитектурные-нюансы)

---

## 1. СРЕДА РАЗРАБОТКИ

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

### 🛠 x86 Toolchain

| Инструмент | Назначение |
|---|---|
| `i686-linux-gnu-gcc` | Кросс-компилятор ядра |
| `nasm` | Ассемблер |
| `GNU ld` | Линкер |
| `make`, `xorriso`, `grub-pc-bin`, `mtools` | Сборка ISO |
| `git` | Контроль версий |

### ⚙️ x86 Флаги компиляции

**Kernel CFLAGS:**

```makefile
CFLAGS  = -m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Iinclude
CFLAGS += -fno-pie -fno-pic -fno-stack-protector
CFLAGS += -mno-sse -mno-sse2 -mno-mmx -mno-3dnow
CFLAGS += -mincoming-stack-boundary=2 -g
```

**User Space CFLAGS:**

```makefile
USER_CFLAGS = -m32 -nostdlib -static -ffreestanding -O2 -Wall -Wextra
USER_CFLAGS += -fno-optimize-sibling-calls
```

**LDFLAGS:**

```makefile
LDFLAGS = -T linker.ld -nostdlib -no-pie -lgcc
```

**Принцип "Голая ОС":** ядро не использует FPU/SSE напрямую. Математика с плавающей
точкой доступна только в Ring 3 через Lazy FPU Switching (`#NM`, `fxsave/fxrstor`).

**Запуск x86:**

```bash
make iso && make run
# qemu-system-i386 -cdrom build/metal_os.iso -m 1024M -serial stdio -no-reboot
```

### 🛠 ARM Toolchain (Raspberry Pi Port)

| Инструмент | Назначение |
|---|---|
| `arm-none-eabi-gcc` | ARM cross-compiler (ARM1176JZF-S, ARMv6) |
| `arm-none-eabi-ld` | ARM linker |
| `arm-none-eabi-objcopy` | ELF → raw binary (`kernel.img`) |
| `qemu-system-arm` | Эмуляция (`-M raspi1ap`) |

**ARM CFLAGS:**

```makefile
CPU_FLAGS = -mcpu=arm1176jzf-s -marm -mabi=aapcs -mno-unaligned-access
CFLAGS   += -ffreestanding -nostdlib -O2 -fno-pie -fno-pic
CFLAGS   += -DCONFIG_ARCH_ARM=1
```

**Запуск ARM:**

```bash
make -f Makefile.arm run
# qemu-system-arm -M raspi1ap -m 512M -serial stdio -kernel build/arm/kernel.img
```

---

### 1.3 POSIX ABI Policy (Frozen, Day 32)

#### Public User ABI (Ring 3 → Kernel)

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

**Осознанные отклонения от Linux (зафиксированы, не баги):**

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

#### Internal Kernel ABI (НЕ гарантирован)

| # | Элемент | Статус |
|---|---|---|
| 1 | Номера syscall | Frozen (Linux i386 ABI). Transport: x86 `INT 0x80`, ARM `svc #0` |
| 2 | `vfs_close_fd()` | Internal. Ring 0 НЕ вызывает `sys_close()` |
| 3 | `task_t` layout | Internal. Может меняться |
| 4 | VMA flags (`VMA_COW` и т.д.) | Internal |
| 5 | Page table format | Internal (x86 2-level, ARMv6 short descriptor) |
| 6 | Kernel heap API (`kmalloc`) | Internal. Ring 3 НИКОГДА |

#### TCC Compatibility Rules

| # | Правило | Обоснование |
|---|---|---|
| T1 | Tagged structs | TCC пишет `struct timeval tv;` → нужен тег. Паттерн: `struct X {...}; typedef struct X X_t;` |
| T2 | `#ifndef CONFIG_TCC_STATIC` guard | TCC определяет static stubs `dl*` с другими сигнатурами |
| T3 | `-fno-optimize-sibling-calls` | `setjmp/longjmp` safety |
| T4 | Heapsort (не Quicksort) | O(1) stack для 64 KB Ring 3 stack |

#### ABI Change Policy

| Тип изменения | Процедура |
|---|---|
| Добавление syscall | Только в minor version bump |
| Изменение поведения существующего syscall | Только в major version bump |
| Изменение internal kernel ABI | Свободно (не гарантирован) |
| Изменение отклонений D1-D12 | Только в major version bump + миграция |

---

### 1.4 Release Checklist: Alpha 0.5-rc1 (x86)

- [x] SSOT version fixed (Alpha 0.5-rc1)
- [x] ABI policy fixed (§1.3: 16 гарантий, 12 отклонений)
- [x] All critical bugs closed (5/5 libc, 0 kernel)
- [x] All fixed bugs tested (68/68 PASS)
- [x] Memory balance clean (pmm 117→117, heap 242→242)
- [x] FD balance clean (`heap_balance` стабилен)
- [x] Process teardown clean (`fork→exit→reap`: 0 утечек)
- [x] CoW teardown proven (15 zombie cascade, 25 fork bomb)
- [x] TCC self-hosting works (compile + run + error recovery)
- [x] W^X enforced (`mmap` + `mprotect` reject)
- [x] RBAC enforced (`FS_SYSTEM` → `EACCES`)
- [x] Stress tests pass (fork bomb, 1000 files, FD exhaustion, heap exhaustion)
- [x] Serial log clean (0 FAIL, 0 PANIC, 0 TRIPLE FAULT)
- [x] Known limitations documented (12 пунктов)
- [x] Changelog written
- [x] Git tag: `alpha-0.5-rc1`

#### Итого

| Метрика | Значение |
|---|---|
| Тестов | 55 → **68** (+13) |
| Багов в ядре | **0** |
| Багов в libc | **5** (все исправлены) |
| Багов в тестах | **2** (исправлены / удалены) |
| Утечек PMM | **0** |
| Утечек heap | **0** |
| Triple Fault | **0** |
| Kernel Panic | **0** |

---

### 1.5 ARM Hardware Testing Notes

#### Сводная таблица

```text
#  Файл              Проблема                    QEMU   RPi1    Критичность
─  ────              ────────                    ────   ────    ───────────
1  linker_arm.ld     PHYS_BASE 0x10000           ✅     💥      КРИТИЧНО
2  arm_uart.c        baud без core_freq=250      ✅     💥      КРИТИЧНО
3  config.h          RAM default 512 MB          ✅     ⚠️*     ВАЖНО (*когда будет PMM)
4  arm_boot.S        маппинг 256 MB              ✅     ✅      OK для spike
5  hal_cpu.h         WFI → busy-wait             ✅     ⚠️      Субоптимально
6  arm_boot.S        caches off                  ✅     ⚠️      Субоптимально
7  arm_boot.S        branch pred off             ✅     ⚠️      Субоптимально
8  arm_main.c        machine type не парсится    ✅     ⚠️*     ВАЖНО (*для PMM)
```

#### Минимальный чеклист для первого запуска на железе

```text
1. linker_arm.ld: PHYS_BASE = 0x8000 (или параметризация)
2. SD карта: config.txt с core_freq=250, enable_uart=1
3. SD карта: kernel.img (не kernel7.img)
4. SD карта: bootcode.bin, start.elf, fixup.dat (firmware)
5. UART: 3.3V переходник, TX↔RX cross, GND
6. Хост: picocom -b 115200 /dev/ttyUSB0
```

Пункты 1-2 — без них **не запустится**.  
Пункты 3-6 — без них не увидим вывод.

#### Что НЕ нужно менять

```text
✅ arm_vectors.S (VBAR, b handler, SRS/RFE) — работает на hardware
✅ arm_irq.c (BCM2835 IRQ controller) — identical hardware
✅ arm_timer.c (BCM2835 System Timer) — identical hardware
✅ arm_context.S (context switch) — pure CPU, no hardware dependency
✅ ctz32() — software, no hardware dependency
✅ AAPCS alignment — pure ABI, no hardware dependency
✅ CPSR save/restore — pure CPU
✅ MMIO volatile + DSB — работает на hardware (даже важнее чем в QEMU)
```

---

### 1.6 Changelog: ARM Boot Spike (Days 36–37, July 2026)

#### Day 36: HAL Design + Build Infrastructure

- Спроектирован HAL: 6 модулей (`cpu`, `mmu`, `irq`, `timer`, `uart`, `mem`)
- Compile-time abstraction (`#ifdef CONFIG_ARCH_ARM`), НЕ runtime vtable
- Написаны HAL контракты: `hal_cpu.h`, `hal_mmu.h`, `hal_irq.h`, `hal_timer.h`, `hal_uart.h`
- Расширен `config.h`: ARM CPSR modes, BCM2835 MMIO, ARMv6 MMU descriptors
- `Makefile.arm`: отдельный build, не трогает x86 Makefile
- `linker_arm.ld`: LMA `0x10000`, VMA `0xC0000000`, `AT(ADDR - KERNEL_VMA)`

#### Day 37: ARM Boot Milestone

- `arm_boot.S`: SVC mode, stacks, BSS clear, TTBR0, MMU enable, higher half jump
- `arm_uart.c`: PL011 UART init + `putc/getc` (GPIO ALT0, 115200 8N1)
- `arm_main.c`: banner + halt (placeholder)

**Решённые проблемы:**

- `isb` → CP15 `mcr` (ARMv6 не имеет UAL)
- `PHYS_BASE 0x8000 → 0x10000` (QEMU raspi1ap)
- VMA/LMA mismatch → `AT(ADDR(.section) - KERNEL_VMA)`
- TTBR1 Prefetch Abort → один TTBR0 (identity + HH)
- `__aeabi_uidiv` → hardcoded baud + libgcc
- QEMU RAM `256M → 512M` (raspi1ap requirement)

**Результат:** Banner по UART в QEMU raspi1ap ✅

#### Итого за ARM Boot Spike

| Метрика | Значение |
|---|---|
| Файлов HAL | 5 контрактов + `config.h` расширение |
| Файлов ARM | `arm_boot.S`, `arm_uart.c`, `arm_main.c`, `linker_arm.ld` |
| Build | `Makefile.arm` (отдельный, x86 не тронут) |
| QEMU | `raspi1ap`, 512M, PL011 UART |
| Boot | SVC → MMU → Higher Half → UART banner ✅ |
| x86 regression | 0 (Makefile не тронут) |

---

### 1.7 Changelog: ARM IRQ + Timer + Vectors (Days 38–40, July 2026)

#### Day 38: HAL IRQ + Timer Implementation

- `hal_irq.h`: 72 IRQ линии (64 GPU + 8 Basic), namespace `HAL_IRQ_GPU_*` / `HAL_IRQ_BASIC_*`
- `hal_irq.h`: `hal_irq_dispatch` задокументирован как arch-specific (EOI порядок)
- `hal_cpu.h`: `hal_irq_restore` (ARM) восстанавливает только I-bit, не mode bits
- `hal_timer.h`: добавлен `hal_timer_get_hz()`, `hal_timer_oneshot` помечен NOT IMPLEMENTED
- `config.h`: `BCM2835_PERIPH_VIRT_BASE` + `BCM2835_VIRT(reg)` макрос
- `config.h`: `BCM2835_RAM_SIZE_DEFAULT = 512 MB` (QEMU raspi1ap)

#### Day 39: ARM Exception Vectors + IRQ Controller

- `arm_vectors.S`: VBAR-based vector table (8 entries, `b handler`)
- `arm_vectors.S`: IRQ stub (`srsdb/cps/push/bl/pop/rfeia`)
- `arm_vectors.S`: Exception stubs (UNDEF, SVC, PABT, DABT) с context dump
- `arm_irq.c`: BCM2835 IRQ controller (init, register, enable/disable, dispatch)
- `arm_irq.c`: `arm_irq_entry` — read PEND1/PEND2/BASIC, `ctz32()`, dispatch
- `arm_irq.c`: `arm_exception_entry` — fatal dump (`r0-r12`, `LR`, `fault_addr`, `SPSR`)

#### Day 40: ARM System Timer + Integration

- `arm_timer.c`: BCM2835 System Timer C1, 1 MHz, 1 kHz tick
- `arm_timer.c`: `timer_irq_handler` (clear CS, next C1, `tick_count++`)
- `arm_timer.c`: `hal_timer_delay_us/ms` (hardware CLO polling)
- `arm_main.c`: полная интеграция (IRQ init → Timer init → enable → uptime loop)
- `arm_boot.S`: VBAR setup в `_higher_half_entry`
- `arm_uart.c`: удалены дублирующиеся `hal_timer_delay_us/ms` (владелец: `arm_timer.c`)
- `Makefile.arm`: добавлены `arm_vectors.S`, `arm_irq.c`, `arm_timer.c`

#### Критические баги Days 38-40

| # | Баг | Корень | Фикс |
|---|---|---|---|
| B1 | `__builtin_ctz` → `rbit` (ARMv7) | GCC codegen для ARMv6 | Software `ctz32()` |
| B2 | WFI не просыпается | ARM1176 + QEMU errata | NOP sled idle |
| B3 | `push {r0-r12}` = 52 bytes | AAPCS SP mod 8 = 4 | `push {r0-r12, lr}` = 56 bytes |

#### Итого за Days 38-40

| Метрика | Значение |
|---|---|
| Новых файлов | `arm_vectors.S`, `arm_irq.c`, `arm_timer.c` |
| Обновлённых файлов | `arm_boot.S`, `arm_main.c`, `arm_uart.c`, `Makefile.arm` |
| Обновлённых HAL | `hal_irq.h`, `hal_cpu.h`, `hal_timer.h`, `config.h` |
| IRQ dispatch | 1000 IRQ/sec, 0 пропусков, 0 сбоев |
| Uptime test | 9+ секунд стабильно |
| x86 regression | 0 (Makefile не тронут) |

---

### 1.8 Changelog: ARM Context Switch + Scheduler (Days 41–42, July 2026)

#### Day 41: ARM Context Switch

- `arm_context.S`: `context_switch` (`r4-r11` + CPSR + LR, SP switch)
- `arm_main.c`: 3 задачи (idle + task_a + task_b), preemptive round-robin
- Forged stack: 10 regs, 8-byte aligned, CPSR=`0x13` (SVC, I=0)

#### Day 42: Preemptive Scheduler Integration

- `arm_timer.c`: `hal_timer_tick()` → `quantum_counter` → `schedule()`
- Quantum: 10 ticks (10 ms)
- `schedule()`: round-robin, `arm_context_switch()`

#### Критический баг Days 41-42

| # | Баг | Корень | Фикс |
|---|---|---|---|
| B4 | CPSR не сохраняется в `context_switch` | Новая задача наследует I=1 из IRQ handler | `mrs/msr cpsr_c` + forged CPSR=`0x13` |

#### Итого за Days 41-42

| Метрика | Значение |
|---|---|
| Новых файлов | `arm_context.S` |
| Задач | 3 (idle, task_a, task_b) |
| Quantum | 10 ms |
| Uptime test | 51 секунда, 0 сбоев |
| Context switches | ~5100 (3 задачи × 100 Hz × 51s / 3) |

---

### 1.9 Changelog: ARM SVC + User Mode (Days 43–45, July 2026)

#### Day 43: ARM SVC Syscall Entry

- `arm_vectors.S`: SVC vector переключён с fatal dump на syscall entry
- `arm_vectors.S`: SVC frame = `srsdb` + `push {r0-r12, lr}` = 64 bytes
- `include/arm_trap.h`: общий ARM trap frame layout
- `arch/arm/arm_syscall.c`: SVC syscall dispatcher
- ARM syscall ABI:
  - `r7` = syscall number
  - `r0-r6` = arguments
  - `r0` = return value
- Реализованы:
  - `sys_exit`
  - `sys_write`
  - `sys_getpid`
  - `sys_yield`
- Invalid syscall → `-ENOSYS`
- Zero Trust:
  - validation syscall number
  - validation fd
  - validation user buffer range
  - rejection of kernel pointers

#### Day 44: ARM User Mode Entry

- `arch/arm/arm_user_asm.S`: `arm_user_trampoline`
- `arch/arm/arm_user_asm.S`: `arm_enter_user_first`
- `arch/arm/arm_user_asm.S`: raw user test image `_user_test_start/_user_test_end`
- `arch/arm/arm_user.c`: user memory setup
- `config.h`: W^X section descriptors:
  - `ARM_SECTION_USER_RX = 0x180E`
  - `ARM_SECTION_USER_RWXN = 0x1C1E`
- User code mapping:
  - VA `0x00100000` → PA `0x00200000`
  - user RO, executable
- User data mapping:
  - VA `0x00200000` → PA `0x00300000`
  - user RW, XN
- User CPSR cooperative:
  - `0xD0 = USR | FIQ disabled | IRQ disabled`

#### Day 45: Cooperative Scheduler Integration

- `arm_main.c`: user task creation through scheduler
- `arm_main.c`: `TASK_FREE`-aware `schedule()`
- `arm_main.c`: task control API:
  - `arm_current_pid()`
  - `arm_task_yield()`
  - `arm_task_exit()`
- `arm_syscall.c` больше не имеет прямого доступа к `current_task` или `tasks[]`
- `sys_yield` интегрирован с round-robin scheduler
- `sys_exit` интегрирован с task teardown
- PID 0 immortal guard:
  - `arm_task_exit()` игнорирует попытку убить idle

#### Критические баги Days 43-45

| # | Баг | Корень | Фикс |
|---|---|---|---|
| B5 | `invalid constant (3e7)` | `mov r7, #999` не кодируется ARM immediate | Заменено на `mov r7, #99` |
| B6 | `hal_irq_enable` implicit declaration | Не включён `hal/hal_cpu.h` | Добавлен include |
| B7 | `undefined reference to current_task` | `current_task` static, прямой extern из syscall | Введён task control API |
| B8 | `ARM_SECTION_USER_RX` redefined | Два user-memory блока в `config.h` | Оставлен единый SSOT блок |
| B9 | Potential zombie task | `schedule()` мог вернуть `TASK_FREE` в `TASK_READY` | `schedule()` проверяет состояние |

#### Результат

```text
User code executes in USR mode
svc #0 syscall transport works
sys_write prints from user buffer
invalid syscall returns -ENOSYS
sys_yield switches tasks
sys_exit kills user task
kernel remains alive
idle remains immortal
x86 regression: 0
```
### 1.10 Changelog: ARM User IRQ Preemption (Day 46, July 2026)

#### Day 46: IRQ Preemption from User Mode

- `include/arm_trap.h`: добавлен `struct arm_user_irq_frame`
- `include/arm_trap.h`: добавлены offsets для user IRQ frame
- `arch/arm/arm_vectors.S`: IRQ stub разделён на два пути:
  - IRQ from SVC/kernel mode
  - IRQ from USR/user mode
- `arch/arm/arm_user_asm.S`: user CPSR изменён с `0xD0` на `ARM_CPSR_USER`
- `arch/arm/arm_main.c`: `hal_irq_enable()` перенесён до idle loop
- `arch/arm/arm_main.c`: ручной cooperative `schedule()` loop удалён

#### User CPSR

До Day 46:

```text
0xD0 = USR mode | FIQ disabled | IRQ disabled

Kernel IRQ frame
IRQ from SVC/kernel mode сохраняет прежний 64-byte frame:

offset  0: r0
offset  4: r1
offset  8: r2
offset 12: r3
offset 16: r4
offset 20: r5
offset 24: r6
offset 28: r7
offset 32: r8
offset 36: r9
offset 40: r10
offset 44: r11
offset 48: r12
offset 52: lr_svc
offset 56: pc
offset 60: cpsr

User IRQ frame
IRQ from USR mode сохраняет расширенный 72-byte frame:

offset  0: r0
offset  4: r1
offset  8: r2
offset 12: r3
offset 16: r4
offset 20: r5
offset 24: r6
offset 28: r7
offset 32: r8
offset 36: r9
offset 40: r10
offset 44: r11
offset 48: r12
offset 52: sp_usr
offset 56: lr_usr
offset 60: padding
offset 64: pc
offset 68: cpsr

#### QEMU smoke test

```bash
make -f Makefile.arm clean
make -f Makefile.arm run
```

#### Итого за Days 43-45

| Метрика | Значение |
|---|---|
| Новых файлов | `arm_syscall.c`, `arm_user.c`, `arm_user_asm.S`, `arm_trap.h` |
| Обновлённых файлов | `arm_vectors.S`, `arm_main.c`, `config.h`, `Makefile.arm` |
| User mode | USR, cooperative |
| Syscalls | `exit`, `write`, `getpid`, `yield` |
| W^X | section-level enforced |
| User tasks | 2 (`user_a`, `user_b`) |
| Kernel crash | 0 |
| x86 regression | 0 |

---

## 2. СТРУКТУРА ПРОЕКТА

```text
project_root/
├── isodir/                         # Корневая директория ISO (x86)
│   └── boot/
│       ├── grub/grub.cfg           # Multiboot конфигурация
│       ├── kernel.bin              # Ядро
│       └── initrd.tar              # RAM-диск (TAR UStar)
│
├── include/                        # Заголовочные файлы ядра
│   ├── config.h                    # ⭐ SSOT границ памяти
│   ├── arm_trap.h                  # ⭐ ARM trap frame layout (SVC/kernel IRQ + user IRQ)
│   ├── gdt.h, idt.h, isr.h, pic.h, tss.h
│   ├── pmm.h, paging.h, heap.h, vma.h, elf.h
│   ├── task.h, vfs.h, initrd.h, tmpfs.h, devfs.h
│   ├── ata.h, fat32.h              # Storage (Day 8.2)
│   ├── vga.h, framebuffer.h, keyboard.h, timer.h, serial.h
│   ├── klib.h, syscall.h, multiboot.h, port_io.h
│   ├── kerrno.h                    # POSIX errno codes
│   ├── univga_font.h               # PSF1 шрифт с кириллицей
│   │
│   └── hal/                        # ⭐ HAL Contracts (compile-time)
│       ├── hal_cpu.h               # irq_save/restore, halt, barriers
│       ├── hal_mmu.h               # HAL_PAGE_* flags, map/switch/clone
│       ├── hal_irq.h               # IRQ register/enable/EOI/dispatch
│       ├── hal_timer.h             # timer_init, get_ticks/ms/us
│       └── hal_uart.h              # uart_init, putc/getc
│
├── arch/                           # ⭐ Architecture-Specific (HAL)
│   └── arm/
│       ├── arm_boot.S              # Entry, stacks, MMU, VBAR, higher half
│       ├── arm_vectors.S           # Exception vectors (VBAR), SVC syscall entry, user/kernel IRQ stubs
│       ├── arm_context.S           # ARM context switch (r4-r12/CPSR/LR)
│       ├── arm_irq.c               # BCM2835 IRQ controller + dispatch
│       ├── arm_timer.c             # BCM2835 System Timer (1 MHz, 1 kHz tick)
│       ├── arm_uart.c              # PL011 UART (BCM2835)
│       ├── arm_main.c              # ARM kernel_main, scheduler, user task integration
│       ├── arm_syscall.c           # ⭐ ARM SVC syscall dispatcher (Days 43-45)
│       ├── arm_user.c              # ⭐ User memory setup + user image copy
│       ├── arm_user_asm.S          # ⭐ User trampoline + raw user test image
│       └── linker_arm.ld           # LMA 0x10000, VMA 0xC0000000
│
├── boot.asm                        # Multiboot, VBE, Higher Half Trampoline (x86)
├── linker.ld                       # LMA/VMA split (x86)
├── kernel.c                        # kernel_main, Bootstrap (x86)
│
├── descriptors_flush.asm           # lgdt, lidt, ltr
├── isr_asm.asm                     # ISR/IRQ stubs
├── context_switch.asm              # CR3 switch, CR0.TS
├── usermode.asm                    # IRET в Ring 3
│
├── pmm.c, paging.c, heap.c         # Memory Management
├── vma.c, elf.c                    # VMA + ELF Loader
├── task.c                          # Scheduler, Supervisor Trees
├── vfs.c, initrd.c, tmpfs.c        # VFS + RAM Disks
├── devfs.c                         # ⭐ DevFS /dev/console
├── gdt.c, idt.c, isr.c, pic.c      # Descriptors + Interrupts
├── tss.c, syscall.c                # TSS + Syscalls
├── vga.c, framebuffer.c            # Graphics
├── keyboard.c, timer.c             # PS/2 + PIT
├── serial.c                        # COM1 (headless debug)
├── ata.c, fat32.c                  # ATA PIO + FAT32
├── klib.c                          # Kernel utilities
│
├── Makefile                        # x86 build
├── Makefile.arm                    # ⭐ ARM build (не трогает x86)
│
└── user_src/                       # ⭐ User Space (Ring 3)
    ├── user_syscalls.h             # Syscall wrappers (inline asm)
    ├── user_linker.ld              # ELF linker script
    ├── user_libc.h                 # ⭐ Monolithic SSOT header
    ├── user_libc.c                 # POSIX libc (Bump Allocator)
    ├── setjmp.asm                  # setjmp/longjmp (NASM)
    ├── crt0.asm                    # C Runtime Startup
    ├── init.c                      # ⭐ PID 1 (/sbin/init.elf)
    ├── shell_user.c                # ⭐ Ring 3 Shell
    └── config.h                    # Config TinyCC
```

---

## 3. АРХИТЕКТУРНЫЕ ПРИНЦИПЫ

### 3.1 Single Source of Truth (SSOT)

Все глобальные константы памяти определены **строго один раз** в `include/config.h`.
Любой файл, использующий эти константы, обязан делать `#include "config.h"`.

> ⚠️ **Нахождение ревью [C1]:** макросы `VIRT_TO_PHYS`/`PHYS_TO_VIRT` были перенесены
> в `config.h`, но SSOT-документация указывала на `paging.h`.
> **Решение:** `config.h` является единственным источником, `paging.h` делает
> `#include "config.h"`.

---

### 3.2 Zero Trust Sandbox

**Каждый** syscall ОБЯЗАН:

- Валидировать номера системных вызовов
- Проверять указатели через `is_user_pointer()`
- Копировать строки через `copy_string_from_user()`
- Enforce W^X (Write XOR Execute) политику
- Возвращать стандартные errno коды

---

### 3.3 Two-Tier Library System

| Уровень | Библиотека | Доступ |
|---|---|---|
| Ring 0 | `klib.c` | Прямой доступ к kernel heap, PMM, VGA/FB |
| Ring 3 | `user_libc.c` | **Только** через syscalls |

Попытка дать Ring 3 доступ к `kmalloc()` **полностью ломает** Zero Trust Sandbox.

---

### 3.4 Dependency Inversion (DIP)

Высокоуровневые подсистемы (`heap.c`, `vfs.c`) не включают заголовки низкоуровневых
драйверов (`vga.h`). Определения цветов перенесены в `klib.h`. Подсистемы памяти не
знают, используется ли VGA или Framebuffer (**Strategy Pattern**).

---

### 3.5 Header Self-Sufficiency

Заголовочный файл, использующий `bool`/`true`/`false`, обязан включать
`<stdbool.h>` напрямую. `user_libc.h` использует **Monolithic Bypass** — все типы
определяются через примитивы C и `__builtin_va_list`, без `#include <stdint.h>`.

---

### 3.6 Double Dump Pattern

Фатальные исключения выводят дамп **одновременно** в VGA/FB (локальный пользователь)
и Serial COM1 (headless-отладка).

---

### 3.7 Визия: "Бессмертная Крепость"

| Принцип | Реализация |
|---|---|
| **"Let it crash"** (Erlang/OTP) | PID 1 перезапускает упавшие сервисы < 100 мс |
| **Crash-Only Software** | Сервисы не хранят состояние в RAM |
| **Immutable Kernel** | Код ядра Read-Only после инициализации |
| **Zero Trust I/O** | Устройства через VFS (`/dev/console`) |

---

### 3.8 HAL — Hardware Abstraction Layer (Compile-Time)

Enclave OS поддерживает несколько архитектур через compile-time HAL.
Один бинарник = одна архитектура. Runtime polymorphism (vtable) НЕ используется.

| HAL модуль | x86 реализация | ARM реализация |
|---|---|---|
| `hal_cpu` | GDT, TSS, `cli/sti/hlt` | CP15, `cpsid/cpsie/wfi` |
| `hal_mmu` | 2-level PD (CR3) | ARMv6 Translation Table (TTBR0) |
| `hal_irq` | IDT + PIC 8259 (16 линий) | VBAR vectors + BCM2835 IRQ (72 линии: 64 GPU + 8 Basic) |
| `hal_timer` | PIT 8254 (1193182 Hz) | BCM2835 System Timer C1 (1 MHz, 1 kHz tick) |
| `hal_uart` | COM1 (16550A, `0x3F8`) | PL011 (MMIO `0x20201000`) |
| `hal_mem` | E820 | ATAGS (`r2` при boot) |

**Принцип:** portable kernel code (`task.c`, `vfs.c`, `heap.c`) включает ТОЛЬКО
`hal_*.h`. Arch-specific code (`arch/x86/`, `arch/arm/`) реализует контракты.

> ⚠️ **x86 НЕ рефакторится** до стабильности ARM. ARM пишется параллельно.

**Syscall ABI:** Enclave номера (Frozen, §1.3).

На x86:

```text
int 0x80
eax = syscall number
ebx/ecx/edx/esi/edi = arguments
eax = return value
```

На ARM:

```text
svc #0
r7 = syscall number
r0-r6 = arguments
r0 = return value
```

Номера syscall НЕ меняются. `user_libc.c` меняет только inline asm обёртку.

---

## 4. КАРТА ПАМЯТИ

### 4.1 Виртуальное адресное пространство (x86)

```text
0x00000000 ┌─────────────────────────────────────┐
           │  USER SPACE (3 GB)                  │
           │  ┌─────────────────────────────┐    │
0x00000000 │  │ NULL Guard (4 KB)           │    │
0x00100000 │  │ ELF Segments (.text/.data)  │    │
           │  │ ...                         │    │
0x10000000 │  │ User Heap (64 MB max)       │    │ ← USER_HEAP_START
0x14000000 │  │ ...                         │    │
           │  │ Air Gap (704 MB)            │    │
0x40000000 │  │ mmap Region (1 GB)          │    │ ← USER_MMAP_START
0x80000000 │  │ ...                         │    │
           │  │ Air Gap                     │    │
0xBFFEF000 │  │ User Stack (64 KB)          │    │ ← USER_STACK_VIRT_TOP - SIZE
0xBFFFF000 │  │ Stack Guard (4 KB)          │    │
0xBFFFFFFF │  └─────────────────────────────┘    │
           ├─────────────────────────────────────┤ ← KERNEL_SPACE_START
0xC0000000 │  KERNEL SPACE (1 GB)                │
           │  ┌─────────────────────────────┐    │
0xC0000000 │  │ Direct Map (512 MB)         │    │ ← KERNEL_DIRECT_MAP
0xC8000000 │  │ Kernel Stack Pool (16 MB)   │    │ ← KERNEL_STACK_POOL_START
0xC9000000 │  │ ...                         │    │
0xD0000000 │  │ Kernel Heap (128 MB Lazy)   │    │ ← KERNEL_HEAP_VIRT
0xD8000000 │  │ ...                         │    │
0xE0000000 │  │ PCI MMIO Hole (Reserved)    │    │ ← PCI_MMIO_HOLE_START
0xFD000000 │  │ Framebuffer (16 MB, PCD)    │    │ ← FB_VIRT_BASE
0xFE000000 │  │ ...                         │    │
0xFFFFFFFF │  └─────────────────────────────┘    │
           └─────────────────────────────────────┘
```

---

### 4.2 SSOT константы (`config.h`)

| Константа | Значение | Назначение |
|---|---|---|
| `USER_SPACE_START` | `0x00000000` | Начало User Space |
| `USER_SPACE_END` | `0xBFFFFFFF` | Конец User Space |
| `KERNEL_SPACE_START` | `0xC0000000` | Начало Kernel Space |
| `LOWER_MEM_START/END` | `0x00000000 / 0x00100000` | Нижняя 1 MB (reserved) |
| `PCI_MMIO_HOLE_START/END` | `0xE0000000 / 0xFFFFFFFF` | PCI MMIO |
| `USER_STACK_VIRT_TOP` | `0xBFFFF000` | Вершина User Stack |
| `USER_STACK_SIZE` | `64 KB` | Размер User Stack |
| `USER_STACK_GUARD_SIZE` | `4 KB` | Guard Page |
| `USER_HEAP_START` | `0x10000000` | Начало User Heap |
| `USER_HEAP_MAX_SIZE` | `64 MB` | Лимит User Heap |
| `KERNEL_HEAP_VIRT` | `0xD0000000` | Начало Kernel Heap |
| `KERNEL_HEAP_SIZE` | `128 MB` | Виртуальный пул (Lazy) |
| `KERNEL_STACK_POOL_START` | `0xC8000000` | Пул Kernel Stacks |
| `KERNEL_STACK_POOL_SIZE` | `16 MB` | ~819 задач |
| `KERNEL_STACK_SLOT_PAGES` | `5` | 1 Guard + 4 Data |
| `FB_VIRT_BASE` | `0xFD000000` | Framebuffer |
| `FB_SIZE_MB` | `16` | Размер FB |
| `USER_MMAP_START` | `0x40000000` | mmap зона |
| `USER_MMAP_MAX_SIZE` | `1 GB` | Лимит mmap |

---

### 4.3 Макросы трансляции адресов

```c
// config.h — SSOT
#define VIRT_TO_PHYS(addr) \
    (((uint32_t)(addr) >= KERNEL_SPACE_START) ? \
     ((uint32_t)(addr) - KERNEL_SPACE_START) : (uint32_t)(addr))

#define PHYS_TO_VIRT(addr) ((uint32_t)(addr) + KERNEL_SPACE_START)
```

> ⚠️ **VIRT_TO_PHYS Underflow:** секции `.boot` имеют адреса < `0xC0000000`.
> Макрос **обязан** содержать проверку `addr >= 0xC0000000`, иначе unsigned
> underflow → мусор в CR3.

---

### 4.4 ARM Memory Map (BCM2835, Raspberry Pi 1)

**Physical:**

```text
0x00000000 ┌──────────────────────┐
           │  RAM (512 MB QEMU)   │
0x00010000 │  ← kernel.img load   │  (QEMU raspi1ap)
0x00008000 │  ← kernel.img load   │  (реальный RPi1)
0x20000000 ├──────────────────────┤
           │  Peripherals (16 MB) │
           │  0x20003000: System Timer (1 MHz)
           │  0x2000B000: ARM Timer
           │  0x2000B200: Interrupt Controller
           │  0x20200000: GPIO
           │  0x20201000: UART0 (PL011)
0x20FFFFFF └──────────────────────┘
```

**Virtual (после MMU, один TTBR0):**

```text
TTBR0 (4096 entries × 4B = 16 KB):
  [0-255]     0x00000000 → 0x00000000  RAM (256 MB, identity)
  [512-527]   0x20000000 → 0x20000000  Peripherals (16 MB, Device)
  [3072-3327] 0xC0000000 → 0x00000000  Higher Half RAM (256 MB)
  [3584-3599] 0xE0000000 → 0x20000000  Higher Half Peripherals
```

> ⚠️ **TTBR1:** планировался TTBR0/TTBR1 split (Linux/seL4 стандарт),
> но QEMU raspi1ap даёт Prefetch Abort при TTBR1 N=2.
> Spike использует один TTBR0. TTBR0/TTBR1 split — в `arm_mmu.c` (Фаза 2).

> ⚠️ **VBAR:** exception vectors находятся в `.text` (kernel virtual address).
> VBAR устанавливается в `arm_boot.S` (`_higher_half_entry`) ДО включения IRQ.
> Не используется `0xFFFF0000` (high vectors) — нет необходимости маппить
> дополнительную 1 MB section. VBAR = адрес `_vector_table` в `.text`.

> ⚠️ **BCM2835 MMIO:** `config.h` определяет физические адреса (`0x20000000+`).
> Макрос `BCM2835_VIRT(reg)` транслирует в kernel virtual (`0xE0000000+`).
> Сейчас (spike): identity mapping активен, используются физические адреса.
> Позже (HH-only): заменить на `BCM2835_VIRT()` во всех ARM-файлах.

**ARM Section Descriptor (1 MB page):**

```text
RAM:    PA | 0x140E  (TEX=001, C=1, B=1, AP=01, Section)
Device: PA | 0x416   (XN=1, B=1, AP=01, Section)
```

**User Mode Spike Mapping (Days 43-45):**

```text
TTBR0 temporary user sections:

[1]   0x00100000 → 0x00200000   User code   (1 MB, user RO, executable)
[2]   0x00200000 → 0x00300000   User data   (1 MB, user RW, XN)
```

Section descriptors:

```text
User code:  PA | 0x180E
            Section | TEX=001 | C=1 | B=1 | AP=10 | XN=0
            AP=10: privileged RW, user RO

User data:  PA | 0x1C1E
            Section | TEX=001 | C=1 | B=1 | AP=11 | XN=1
            AP=11: privileged RW, user RW
```

W^X:

```text
user code = R/X, no W
user data = R/W, no X
```

User stacks:

```text
user_a stack top = 0x00280000
user_b stack top = 0x002C0000
```

Обе стековые вершины находятся внутри user data section.

---

## 5. ПОДСИСТЕМЫ ЯДРА

### 5.1 Загрузчик и инициализация

#### `boot.asm` — Multiboot + Higher Half Trampoline (x86)

- Multiboot Header: magic `0x1BADB002`, flags `0x3` (`PAGE_ALIGN | MEM_INFO`)
- Bochs VBE инициализация (1024×768×32bpp) через порты `0x01CE/0x01CF`
- **Раздельные Page Tables:** `boot_page_tables` (Identity Map) +
  `boot_page_tables_hh` (Higher Half Map)
- Framebuffer `0xFD000000` с флагом `PAGE_PCD` (Cache Disable)
- Сохранение `eax`/`ebx` в `.boot.data` (защита от уязвимостей стека)
- **Defensive Handover:** `call kernel_main` (не `jmp`) → `.halt_loop` при случайном return
- **Virtual Stack Switch:** `mov esp, stack_top` после включения CR0.PG

#### `linker.ld` — LMA/VMA Split (x86)

- Физическая загрузка: `0x00100000` (1 MB)
- `_boot_start`, `_kernel_start`, `_kernel_end` экспортированы для PMM
- `AT(ADDR(...) - 0xC0000000)` для корректной LMA
- `. += 0xC0000000` для перехода в Higher Half

#### `kernel.c` — Bootstrap Sequence (x86)

**Строгая последовательность (нарушение → Triple Fault):**

| Шаг | Функция | Назначение |
|---|---|---|
| 1 | `serial_init()` | Headless debug |
| 2 | Multiboot magic check | `0x2BADB002` |
| 3 | `fb_init()` | Временный (физический LFB) |
| 4 | `gdt_install()` | Flat Model + TSS |
| 5 | `idt_install()` | 256 векторов |
| 6 | `tss_install()` | Load TR |
| 7 | `syscall_init()` | INT 0x80 |
| 8 | `pmm_init()` | Two-Pass E820 + Reserve Modules |
| 9 | `paging_init()` | Direct Map, Page Fault Handler |
| 10 | `fb_init()` | Resurrect (виртуальный `0xFD000000`) |
| 11 | `heap_init()` | Buddy System (Lazy) |
| 12 | `ata_init()` + `fat32_init()` | Storage |
| 13 | `vfs_init()` + `devfs_init()` + `initrd_init()` | VFS + DevFS + Initrd |
| 14 | `tmpfs_init()` | Writable RAM Disk |
| 15 | `tasking_init()` | Main Task (PID 0), FPU setup |
| 16 | `keyboard_install()` + `timer_init(1000)` | IRQ |
| 17 | **Launch PID 1** (`/sbin/init.elf`) | Ring 3 Init |
| 18 | **Kernel Idle Loop** | `sti; hlt; cli` |

---

#### ARM Boot Sequence (`arch/arm/arm_boot.S`)

**Строгая последовательность (нарушение → Prefetch Abort loop):**

| Шаг | Действие | Назначение |
|---|---|---|
| 1 | Сохранить `r1` (machine), `r2` (ATAGS) | Boot params |
| 2 | `cpsid if, #0x13` | SVC mode, IRQ/FIQ off |
| 3 | Invalidate caches + TLB | Clean state |
| 4 | Disable MMU/caches (SCTLR) | Clean boot |
| 5 | Setup stacks (SVC/IRQ/FIQ/ABT/UND/SYS) | Mode-specific SP |
| 6 | Clear BSS (`phys = VMA - 0xC0000000`) | Zero `.bss` |
| 7 | Build TTBR0 (identity + higher half) | Page tables |
| 8 | Set TTBR0, DACR (Domain 0 = Manager) | MMU config |
| 9 | Enable MMU (SCTLR.M = 1) | MMU ON |
| 10 | `ldr pc, =_higher_half_entry` | Jump to `0xC0000000+` |
| 11 | Set VBAR = `_vector_table` (CP15 c12) | Exception vectors in `.text` |
| 12 | ISB (CP15 `c7,c5,4`) | Ensure VBAR write completes |
| 13 | Reload SP (virtual address) | SVC stack in HH |
| 14 | `bl arm_kernel_main(atags, machine)` | C kernel |

> ⚠️ **ARMv6: НЕТ инструкций `isb/dsb/dmb` (UAL, ARMv7+).**
>
> Используем CP15:
>
> ```text
> ISB = mcr p15, 0, r0, c7, c5, 4
> DSB = mcr p15, 0, r0, c7, c10, 4
> DMB = mcr p15, 0, r0, c7, c10, 5
> ```

> ⚠️ **QEMU raspi1ap:** kernel load address = `0x10000` (не `0x8000`).
> `PHYS_BASE` в `linker_arm.ld = 0x00010000`.

> ⚠️ **Linker script:** VMA = LMA + KERNEL_VMA для КАЖДОЙ секции.
> Формула: `AT(ADDR(.section) - KERNEL_VMA)`.

---

#### ARM IRQ + Timer Sequence (Days 38-40)

Последовательность инициализации прерываний (`arm_main.c`):

| Шаг | Действие | Назначение |
|---|---|---|
| 1 | `hal_uart_init(115200)` | PL011 UART (polling) |
| 2 | `hal_irq_init()` | BCM2835: disable all lines, clear FIQ routing |
| 3 | `hal_timer_init(1000)` | System Timer C1, 1 kHz, register handler, enable line 1 |
| 4 | `hal_irq_enable()` | `cpsie i` — global IRQ enable |
| 5 | Uptime loop (busy-wait) | `hal_timer_get_ticks()`, print every 1000 ticks |

IRQ dispatch chain:

```text
Timer match (CLO == C1)
→ BCM2835 PEND1 bit 1
→ CPU IRQ exception (VBAR + 0x18)
→ b _irq_handler (arm_vectors.S)
→ sub lr, lr, #4 / srsdb / cps #0x13 / push {r0-r12, lr}
→ bl arm_irq_entry (arm_irq.c)
→ read PEND1/PEND2/BASIC → ctz32() → hal_irq_dispatch()
→ timer_irq_handler (arm_timer.c)
→ clear CS bit 1, set next C1, tick_count++
→ pop {r0-r12, lr} / rfeia sp!
→ return to interrupted code
```

> ⚠️ **ARM1176 (ARMv6): ЗАПРЕЩЕНО использовать `__builtin_ctz` / `__builtin_clz`.**
> GCC может сгенерировать `rbit` (ARMv7-only) → Undefined Instruction.
> Использовать software `ctz32()` (`arm_irq.c`).

> ⚠️ **AAPCS Stack Alignment:** перед любой `bl` из ассемблера SP mod 8 MUST == 0.
>
> ```text
> SRS (8 bytes) + push {r0-r12, lr} (56 bytes) = 64 → 64 mod 8 = 0 ✅
> push {r0-r12} (52 bytes) → 52 mod 8 = 4 → НАРУШЕНИЕ ❌
> ```
>
> Всегда `push {r0-r12, lr}` (14 регистров), НЕ `push {r0-r12}` (13).

> ⚠️ **WFI на ARM1176/QEMU:** не просыпается по System Timer IRQ.
> `hal_cpu_idle()` использует NOP sled (`cpsie i / nop×4 / cpsid i`).
> Для production (Cortex-A) — заменить на WFI.

---

#### ARM SVC + User Mode Sequence (Days 43-45)

Последовательность user-mode spike (`arm_main.c`):

| Шаг | Действие | Назначение |
|---|---|---|
| 1 | `hal_uart_init(115200)` | PL011 UART |
| 2 | `hal_irq_init()` | BCM2835 IRQ controller |
| 3 | `hal_timer_init(1000)` | System Timer C1, 1 kHz |
| 4 | `arm_user_setup()` | Map user sections, copy user image |
| 5 | Create PID 0 idle | Boot SVC stack |
| 6 | Create PID 1 `user_a` | Kernel stack + user PC/SP |
| 7 | Create PID 2 `user_b` | Kernel stack + user PC/SP |

User task first-time entry:

```text
arm_context_switch()
→ pop forged kernel context
→ bx arm_user_trampoline
→ set SP_usr
→ set SPSR = ARM_CPSR_USER (0x50: USR | FIQ disabled | IRQ enabled)
→ clear r0-r12
→ movs pc, lr
→ user code at 0x00100000
```

Syscall path:

```text
user code: svc #0
→ CPU enters SVC mode
→ VBAR + 0x08 (_svc_handler)
→ srsdb sp!, #0x13
→ push {r0-r12, lr}
→ bl arm_syscall_entry
→ validate syscall number
→ validate user buffer
→ execute handler
→ frame->r0 = return value
→ pop {r0-r12, lr}
→ rfeia sp!
→ return to USR mode
```

> ⚠️ **Current user mode is preemptive (Day 46).**
>
> User CPSR:
>
> ```text
> 0x50 = USR mode | FIQ disabled | IRQ enabled
> ```
>
> Timer IRQ может прервать user code напрямую.
> IRQ stub сохраняет полный user trap frame (72 bytes), вызывает scheduler,
> затем восстанавливает user context через `rfeia`.
>
> Fault isolation ещё не реализован:
> user Data Abort / Prefetch Abort / Undefined Instruction пока fatal dump
> (см. `ARM-LIMIT-003`).

---

### 5.2 Управление памятью

#### `pmm.c` — Physical Memory Manager

| Фича | Описание |
|---|---|
| **Safe by Default** | Вся память изначально занята (bitmap = `0xFF`) |
| **Two-Pass E820** | Pass 1: `max_addr` → `pmm_max_page`. Pass 2: освобождение |
| **Dynamic Sizing** | `pmm_max_page` ограничивает сканирование реальной RAM |
| **Punching Holes** | Lower 1 MB, Kernel Image, Multiboot Info, Modules, PCI MMIO |
| **O(1) Allocation** | `__builtin_ctz` (BSF/TZCNT) |
| **IRQ Safety** | `cli/sti` вокруг всех операций с bitmap |
| **Reference Counting** | `pmm_refcounts[]` для Copy-on-Write |
| **Accounting** | `pmm_total_allocs/frees`, `pmm_check_balance()` |

#### `paging.c` — Virtual Memory Manager

| Фича | Описание |
|---|---|
| **Direct Map** | 512 MB → `0xC0000000+` |
| **On-Demand Paging** | Page Fault (INT 14) выделяет страницы по запросу |
| **CoW Page Fault** | `PAGE_COW` → личная копия при записи |
| **Kernel Heap Isolation** | `0xD0000000` без `PAGE_USER` |
| **Kernel Stack Pool** | Guard Pages (Day 16) |
| **Deep Destroy** | `vmm_destroy_address_space()` освобождает PTE + PT + PD |
| **Strict CoW Teardown** | `pmm_dec_ref()` → free только при refcount == 0 (Day 24) |
| **PAGE_PS Check** | Защита от 4 MB регионов |
| **CoW-safe mprotect** | `vmm_protect_page_in_pd` сохраняет `PAGE_COW + PWT/PCD/GLOBAL` (Day 31, S1 fix) |

**Paranoid Page Fault Handler (Zero Trust):**

1. NULL Pointer Guard (< `0x1000` → SIGSEGV)
2. Kernel Space Protection (Ring 3 → Kernel → SIGSEGV)
3. Kernel Stack Overflow Guard (Guard Page → SIGSEGV)
4. Kernel Heap Demand Paging (`0xD0000000`, no `PAGE_USER`)
5. Ring 0 → User Space Demand Paging (Stack Forging)
6. CoW Interception (`PAGE_COW` → copy page)
7. User Space Demand Paging (VMA check)
8. W^X Enforcement (write to Read-Only → SIGSEGV)
9. OOM Trap (`pmm_alloc_page == 0` → `task_kill_current`)

#### `heap.c` — Kernel Heap (Buddy System)

| Фича | Описание |
|---|---|
| **Buddy System** | Неявное бинарное дерево, O(1) Merge через XOR |
| **Zero-Cost Lazy Heap** | Виртуальный пул 128 MB, физические страницы по Page Fault |
| **BlockHeader** | magic `0xDEADBEEF` для детекта double-free |
| **Bounds Checking** | `kfree()` проверяет диапазон |
| **IRQ Safety** | `cli/sti` вокруг операций с деревом |
| **Accounting** | `heap_total_allocs/frees`, `heap_check_balance()` |

#### `vma.c` — Virtual Memory Areas

- Сортированный связный список VMA для каждого процесса
- `vma_add()` — добавление с автоматической сортировкой
- `vma_find()` — линейный поиск по адресу
- `vma_clone()` — глубокое клонирование (`sys_fork`)
- `vma_intersects()` — Collision Detection
- `vma_find_free_area()` — поиск "дырок" для mmap
- `vma_unmap_range()` — Split VMA при munmap
- `vma_destroy_all()` — очистка (Reaper)
- `vma_protect_range()` — Split VMA при mprotect (Day 31, S1 fix), сохраняет `VMA_COW`

---

### 5.3 Файловая система

#### `vfs.c` — Virtual File System

| Фича | Описание |
|---|---|
| **Полиморфизм** | `vfs_node_t` с function pointers (`read`, `write`, `readdir`, `create`, `unlink`, `open`) |
| **LCRS Tree** | Left-Child Right-Sibling для каталогов |
| **3-звенная модель FD** | `vfs_node_t` (Inode) → `open_file_t` (offset, ref_count) → `fd_table` |
| **RBAC** | Флаг `FS_SYSTEM` — Ring 3 получает `EACCES` |
| **True Mountpoints** | `FS_MOUNTPOINT` → "телепортация" к `mountpoint_node` |
| **MAX_MOUNT_HOPS** | 16 (защита от Kernel Stack Overflow) |
| **Orphan Nodes** | POSIX unlink semantics (`is_unlinked + ref_count`) |

#### `initrd.c` — RAM Disk (TAR UStar)

- Парсинг TAR UStar из GRUB Module
- **Robust Magic Search:** сканирование первых 8 KB
- **Binary Magic Comparison:** `k_memcmp` (не `strncmp`)
- **TAR Padding Tolerance:** защита от padding GRUB
- **Prefix + Name:** UStar 256-байтные пути
- **FS_SYSTEM для /boot:** RBAC наследование

#### `tmpfs.c` — Writable RAM Disk

| Фича | Описание |
|---|---|
| **Dynamic Growth** | `capacity *= 2` (< 1 MB) или `+25%` (> 1 MB) |
| **OOM Protection** | `TMPFS_MAX_FILE_SIZE = 25 MB` |
| **O_TRUNC** | Обнуление size, сохранение capacity |
| **Sparse Files** | Заполнение "дырок" нулями |
| **Size Synchronization** | `node->size = new_size` после write |
| **Atomic Commit** | `kmalloc private_data` до link в дерево |
| **True Mountpoint** | `vfs_mount("/tmp", tmpfs_root)` |
| **mkdir Support** | `tmpfs_create` с `S_IFDIR` → `FS_DIRECTORY` + callbacks (Day 31) |
| **ENOTEMPTY Guard** | `tmpfs_unlink` отвергает удаление непустых директорий (Day 31) |

#### `devfs.c` — Device File System

| Фича | Описание |
|---|---|
| **`/dev/console`** | Полиморфная VFS-нода для клавиатуры + экрана |
| **Exclusive Access** | `console_open_count` (только PID 1) |
| **ANSI State Machine** | CSI parsing (SGR, CUP, ED) |
| **RAW MODE** | Line Discipline в Ring 3 (Shell) |
| **Private Mode** | `\033[?25h/l` безопасно игнорируются |
| **Double Dump** | `serial_putc + k_putchar` |

#### `ata.c` + `fat32.c` — Storage

- **ATA PIO Driver:** порты `0x1F0-0x1F7`, LBA28, Polling Mode
- **IDENTIFY Command:** модель, сериал, firmware, LBA capacity
- **ATAPI Detection:** `LBA_MID/LBA_HI`
- **MBR Parser:** 4 записи, сигнатура `0xAA55`
- **FAT32 Read-Only:** BPB, Cluster Chain, FAT Caching
- **VFAT (LFN):** UCS-2 → UTF-8, кириллица, checksum verification

---

### 5.4 Графика и вывод

#### `framebuffer.c`

| Фича | Описание |
|---|---|
| **Double Buffering** | `back_buffer` в Kernel Heap (3 MB) |
| **Dirty Rectangles** | `fb_flush()` копирует только изменённый бокс |
| **PSF1 Unicode** | UCS-2 tables, UTF-8 State Machine |
| **Fallback Font** | 8×16 ASCII (32-126) |
| **rep movsl / rep stosl** | Аппаратное копирование/заполнение |

#### `klib.c` — Strategy Pattern

```c
static void output_char(char c) {
    if (fb_is_available())
        fb_putc(c);
    else
        vga_putc(c);
}
```

- `k_set_color()` — VGA → RGB mapping (16 цветов)
- `k_printf()` — C99 compliant formatter
- `k_set_cursor()` — Strategy Pattern (FB/VGA)

---

### 5.5 Прерывания и железо (x86)

| Файл | Назначение |
|---|---|
| `gdt.c` | Flat Model (4 GB), Ring 0/3, TSS Descriptor |
| `idt.c` | 256 векторов, **EOI Lock Bypass** |
| `isr.c` + `isr_asm.asm` | ISR/IRQ stubs, `pusha`, segment swap |
| `pic.c` | PIC Remap (ICW1-ICW4), `irq_set_mask` |
| `tss.c` | ESP0 для Ring 3 → Ring 0 переходов |
| `timer.c` | PIT 1000 Hz, квант = 10 тиков (50 Hz) |
| `keyboard.c` | PS/2 IRQ1, Ring Buffer 256, ANSI escape для стрелок |
| `serial.c` | COM1 (headless debug) |

**IRQ Save/Restore Primitive:**

```text
irq_save()    → сохраняет EFLAGS и выполняет cli
irq_restore() → восстанавливает EFLAGS
```

Все критические секции должны использовать `irq_save()/irq_restore()` вместо
безусловных `cli/sti`. Безусловные `cli/sti` допустимы только для явного управления
прерываниями: idle wait, fatal loops, task trampoline.

> ⚠️ **EOI Lock Bypass:** `outb(0x20, 0x20)` отправляется в PIC **ДО** вызова
> C-обработчика. Иначе `schedule()` переключит задачу, и линия IRQ заблокируется
> навсегда.

---

## 6. МНОГОЗАДАЧНОСТЬ И ПРОЦЕССНАЯ МОДЕЛЬ

### 6.1 PID Architecture

```text
┌─────────────────────────────────────────────────────────────┐
│  PID 0: Kernel Idle (Ring 0)                                │
│  • Бессмертный Ring 0 поток                                 │
│  • Единственная задача: sti; hlt; cli                       │
│  • Никогда не падает из-за багов в Ring 3                   │
├─────────────────────────────────────────────────────────────┤
│  PID 1: /sbin/init.elf (Ring 3)                             │
│  • Launcher + Supervisor                                    │
│  • Запускает Shell через fork + exec                        │
│  • Respawn при падении Shell (< 100 мс)                     │
│  • Orphan Adoption (усыновление сирот)                      │
│  • Если PID 1 падает → Reaper перезапускает из VFS          │
├─────────────────────────────────────────────────────────────┤
│  PID 2+: /bin/shell.elf, /bin/tcc.elf, ... (Ring 3)         │
│  • Zero Trust Sandbox                                       │
│  • Только через syscalls                                    │
│  • Crash-Only (Init перезапустит)                           │
└─────────────────────────────────────────────────────────────┘
```

### Init Respawn Model

Init — это Ring 3 watchdog для Shell.

Kernel отвечает только за respawn PID 1:

```text
PID 1 dies → Reaper → load /sbin/init.elf → new PID 1
```

Init отвечает за respawn Shell:

```text
Shell dies → init waitpid → fork/exec /bin/shell.elf
```

Если Init умирает, его прямой ребёнок Shell убивается через
`monitor_children = 1`, чтобы новый Init мог чисто перезапустить
Shell и заново открыть `/dev/console`.

---

### 6.2 `task.c` — Scheduler + Supervisor Trees

| Фича | Описание |
|---|---|
| **PCB (`task_t`)** | PID, State, ESP, CR3, FD Table, VMA List, FPU State (512 B), Process Tree |
| **Round-Robin** | Кольцевой двусвязный список, квант 10 мс |
| **Lazy FPU** | CR0.TS → `#NM` (INT 7) → `fxsave/fxrstor` |
| **Reaper Queue** | `dead_tasks_head + reaper_next` |
| **Zombie State Machine** | `TASK_ZOMBIE → waitpid → TASK_DEAD → Reaper` |
| **Orphan Adoption** | Unix-style (`orphan_on_exit=1`) + Erlang-style (`monitor_children=1`) |
| **Kernel Stack Pool** | 16 KB + Guard Page (Day 16) |
| **Init Respawn** | Reaper перезапускает PID 1 из VFS (Day 24) |

---

### 6.3 Состояния задач

```text
TASK_READY ──→ TASK_RUNNING ──→ TASK_SLEEPING
    ↑               │               │
    │               ▼               │
    └────────── TASK_ZOMBIE ←───────┘
                    │
                    ▼ (waitpid)
               TASK_DEAD ──→ Reaper Queue ──→ Freed
```

---

### 6.4 Context Switch (`context_switch.asm`, x86)

1. Сохранение callee-saved (EBX, ESI, EDI, EBP)
2. `*old_esp = ESP`
3. `mov ESP, new_esp` (телепортация)
4. `mov CR3, new_cr3` (TLB Flush)
5. `pop EBP/EDI/ESI/EBX`
6. `or CR0, TS` (Lazy FPU trigger)
7. `ret` (прыжок в EIP новой задачи)

---

### 6.5 Copy-on-Write

```text
fork():
  1. vmm_clone_address_space() → новый PD
  2. Kernel Space (768-1023): клонируется из boot_page_directory
  3. User Space (0-767): PTE помечаются READ-ONLY + PAGE_COW
  4. pmm_inc_ref() для каждой физической страницы
  5. vma_clone() → deep copy VMA list
  6. FD inheritance: ref_count++ для open_file_t + vfs_node_t
  7. Kernel Stack copy (16 KB)
  8. child_r->eax = 0 (ребёнок видит fork() == 0)
  9. Stack Forging: [child_r][ret_from_fork][EBX][ESI][EDI][EBP]

Write to CoW page:
  1. Page Fault (INT 14, rw=1, present=1)
  2. if refcount == 1: снять PAGE_COW, восстановить WRITE
  3. if refcount > 1: pmm_alloc_page(), k_memcpy(), pmm_dec_ref()
  4. Обновить PTE, invlpg
  5. return (процессор повторяет инструкцию)
```

---

## 7. СИСТЕМНЫЕ ВЫЗОВЫ

### 7.1 Syscall Table

Номера syscall frozen (Linux i386 ABI).

Transport:

```text
x86: int 0x80
ARM: svc #0
```

| # | Syscall | Описание |
|---|---|---|
| 1 | `sys_exit` | Завершение процесса → `task_exit()` |
| 2 | `sys_fork` | CoW клонирование процесса |
| 3 | `sys_read` | Чтение из FD (Zero Trust) |
| 4 | `sys_write` | Запись в FD (ANSI State Machine для fd 1/2) |
| 5 | `sys_open` | Открытие файла (`O_CREAT`, `O_TRUNC`, variadic mode) |
| 6 | `sys_close` | Закрытие FD |
| 7 | `sys_waitpid` | Ожидание ребёнка (`WNOHANG`) |
| 10 | `sys_unlink` | Удаление файла (Orphan Semantics) |
| 11 | `sys_exec` | Замена образа процесса (ELF load) |
| 19 | `sys_lseek` | Позиционирование (`SEEK_SET/CUR/END`) |
| 28 | `sys_fstat` | Метаданные файла (`struct stat`) |
| 39 | `sys_mkdir` | Создание директории (Day 31, RBAC + ENOTEMPTY) |
| 41 | `sys_dup` | Дублирование FD (Day 28) |
| 45 | `sys_brk` | Управление кучей (VMA Collision Detection) |
| 54 | `sys_ioctl` | `TIOCGWINSZ` (размер терминала) |
| 63 | `sys_dup2` | Атомарное дублирование FD (Day 28) |
| 78 | `sys_gettimeofday` | Время с момента загрузки |
| 90 | `sys_mmap` | On-Demand Paging (`MAP_ANONYMOUS`) |
| 91 | `sys_munmap` | Освобождение памяти |
| 116 | `sys_sysinfo` | Статистика системы |
| 122 | `sys_getpid` | PID текущего процесса |
| 125 | `sys_mprotect` | Изменение прав (W^X enforcement) |
| 141 | `sys_readdir` | Чтение директории |
| 158 | `sys_yield` | Добровольная передача CPU |
| 164 | `sys_uname` | Информация об ОС |
| 230 | `sys_nanosleep` / `sys_sleep` | Сон (миллисекунды) |

---

### 7.2 Zero Trust Validation

```c
// Каждый syscall ОБЯЗАН:
static inline bool is_user_pointer(const void* ptr, size_t size) {
    uint32_t addr = (uint32_t)ptr;

    if (!ptr)
        return false;

    if (addr >= KERNEL_SPACE_START)
        return false;

    if (size == 0)
        return true;

    if (addr > USER_SPACE_END - size + 1)
        return false;

    return true;
}

static int copy_string_from_user(char* dest, const char* user_src, size_t max_len) {
    // Побайтовое копирование с проверкой Kernel Space boundary
}
```

---

### 7.3 `sys_exec` — Stack Forging + IRET Stack Switch

```text
1. copy_string_from_user(filename)
2. vfs_findnode(filename) → ELF node
3. vmm_create_address_space() → новый PD
4. elf_load(node, &temp_task) → entry_point + VMA
5. Сохранить FD table (POSIX exec сохраняет FD)
6. vmm_destroy_address_space(old PD)
7. vmm_switch_pdir(new CR3)
8. Stack Forging: argc, argv[], NULL на User Stack
9. r->useresp = stack_ptr  ← КРИТИЧНО (IRET Stack Switch Trap)
10. r->eip = entry_point
11. return 0 (IRET восстановит новый контекст)
```

> ⚠️ **IRET Stack Switch Trap (Day 25):** `popa` **игнорирует** поле ESP.
> `iret` читает User ESP из аппаратного стека. Обновление `r->esp` не имеет
> эффекта — необходимо обновлять `r->useresp`.

---

## 8. USER SPACE И SELF-HOSTING

### 8.1 `user_libc.h` — Monolithic SSOT Header

**Monolithic Bypass:** полный отказ от `#include <stdint.h>`, `<stddef.h>`,
`<stdarg.h>`. Все типы определяются через примитивы C и `__builtin_va_list`.

```c
typedef __builtin_va_list va_list;
typedef signed char        int8_t;
typedef unsigned int       uint32_t;
typedef unsigned int       size_t;
typedef int                ssize_t;
typedef int                pid_t;
// ... 50+ типов и 200+ прототипов
```

---

### 8.2 `user_libc.c` — Ring 3 Standard Library

Единый файл реализации Ring 3 libc.

| Компонент | Описание |
|---|---|
| **Bump Allocator** | `malloc` через `sys_brk`, `free = no-op` (by design для TinyCC) |
| **malloc_header_t** | magic `0xA110CA7E`, size |
| **FILE* API** | 4 KB read + 4 KB write buffers |
| **printf family** | `vsnprintf` (C99 compliant) |
| **POSIX variadic open** | `open(path, flags, ...)` с mode через `va_arg` |
| **Process Control** | `fork`, `exec`, `waitpid`, `exit` (с `fflush`) |
| **system()** | `fork + exec(/bin/cmd) + waitpid` |
| **mmap/munmap** | Обёртки над `sys_mmap/sys_munmap` |
| **Sort/Search** | `qsort` (Heapsort, O(1) stack), `bsearch` |
| **Float parsing** | `strtod`, `strtof`, `strtold`, `ldexp` |
| **Time** | `gettimeofday`, `clock`, `localtime`, `sleep`, `usleep` |
| **glibc compat** | `__errno_location`, `__isoc23_strtol/strtoul/strtoll/strtoull` |
| **Day 29** | `getline`, `strdup`, `strerror`, `perror`, `time`, `ioctl` |
| **Day 31** | `mkdir`, `fstat` — POSIX обёртки над `sys_mkdir / sys_fstat` |
| **Day 32** | Консолидация `tcc_lib_os.c` → `user_libc.c` (единый файл) |

---

### 8.3 `crt0.asm` — C Runtime Startup

```asm
_start:
    mov eax, [esp]              ; argc
    lea ebx, [esp + 4]          ; argv
    lea ecx, [esp + eax*4 + 8]  ; envp
    push ecx
    push ebx
    push eax
    call main
    add esp, 12
    push eax                    ; exit_code
    call exit                   ; sys_exit (never returns)

.halt_loop:
    cli
    hlt
    jmp .halt_loop
```

---

### 8.4 Self-Hosting Pipeline

```text
┌─────────────────────────────────────────────────────────────┐
│  1. Shell: compile /examples/hello.c /tmp/hello.elf         │
│  2. fork() → child                                          │
│  3. exec("/bin/tcc.elf", ["tcc", "hello.c", "-o", ...])     │
│  4. TinyCC (Ring 3) → sys_open, sys_read, sys_mmap          │
│  5. TinyCC generates ELF → sys_write("/tmp/hello.elf")      │
│  6. waitpid() → status = 0                                  │
│  7. Shell: run /tmp/hello.elf                               │
│  8. fork() → child                                          │
│  9. exec("/tmp/hello.elf", ["hello"])                       │
│  10. hello.elf (Ring 3) → printf("Hello, World!\n")         │
│  11. exit(0) → waitpid() → status = 0                       │
│  12. SELF-HOSTING SUCCESS ✅                                │
└─────────────────────────────────────────────────────────────┘
```

---

### 8.5 Shell (Ring 3)

| Команда | Описание |
|---|---|
| `help` | Справка |
| `clear` | Очистка экрана (`\033[2J\033[H`) |
| `ls [path]` | Список файлов (ANSI colors: dir=blue, ELF=green, `.c`=yellow) |
| `cat <file>` | Вывод содержимого файла |
| `mkdir <path>` | Создание директории (`sys_mkdir`, Day 31) |
| `rm <path>` | Удаление файла |
| `run <elf> [args]` | Запуск ELF (`fork + exec + waitpid`) |
| `compile <c> [out]` | Компиляция через TinyCC |
| `uptime` | Время работы системы |
| `sysinfo` | Статистика (RAM, процессы) |
| `exit` | Выход (Init respawn) |

**Readline:**

- Arrow Keys (CSI A/B/C/D) — навигация по истории и строке
- Home/End (CSI H/F) — начало/конец строки
- Delete (CSI 3~) — удаление символа
- Ctrl+A/E — Home/End
- Ctrl+K/U — kill line (вправо/влево)
- Ctrl+L — clear screen
- Ctrl+C — cancel
- Ctrl+D — EOF
- History (32 записи)

---

### 8.6 Интеграция TinyCC — Self-Hosting Toolchain

#### 8.6.1 Архитектурный обзор

TinyCC (TCC) интегрирован как **полноценный Ring 3 ELF-бинарник** (`/bin/tcc.elf`),
скомпилированный из исходников Fabrice Bellard (v0.9.27) кросс-компилятором
`i686-linux-gnu-gcc` с флагами `-nostdlib -static -ffreestanding`.

TCC работает внутри Zero Trust Sandbox на общих основаниях:

- Не имеет прямого доступа к памяти ядра
- Все операции через INT 0x80 (`sys_open`, `sys_read`, `sys_write`, `sys_mmap`, `sys_brk`)
- Crash TCC не влияет на ядро (Init перезапустит Shell, Shell перезапустит компиляцию)
- W^X enforcement применяется к генерируемому коду

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

#### 8.6.2 Компоненты интеграции

| Компонент | Файл | Назначение |
|---|---|---|
| **TCC Source** | `external/tcc_src/tcc.c` | Монолитный исходник (`ONE_SOURCE=1`) |
| **TCC Config** | `user_src/config.h` | Fake `config.h` (заменяет `./configure`) |
| **Consolidated libc** | `user_src/user_libc.c` | POSIX libc + все функции адаптации для TCC |
| **setjmp/longjmp** | `user_src/setjmp.asm` | NASM i386 реализация (error recovery TCC) |
| **Syscall Wrappers** | `user_src/user_syscalls.h` | Inline asm обёртки INT 0x80 |
| **Monolithic libc** | `user_src/user_libc.h` | SSOT header (Monolithic Bypass) |
| **libc Implementation** | `user_src/user_libc.c` | `printf`, `malloc` (bump), FILE* API |
| **Shell Integration** | `user_src/shell_user.c` | Команда `compile <file.c> [out]` |
| **Makefile** | `Makefile` | Сборка TCC + Toolchain Injection |

#### 8.6.3 Конфигурация TCC (`user_src/config.h`)

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

#### 8.6.4 Консолидация libc (Day 32)

Все функции адаптации для TCC перенесены из `tcc_lib_os.c` в `user_libc.c`.
Файл `tcc_lib_os.c` удалён из проекта. `user_libc.c` является единственным
источником Ring 3 libc-функций.

**Порядок секций в `user_libc.c`:**

1. Includes
2. Globals (`errno`, `stdin/stdout/stderr`)
3. Memory ops (`memset`, `memcpy`, `memcmp`, `memmove`)
4. Heap (`malloc`, `calloc`, `realloc`, `free` — Bump Allocator)
5. String ops
6. Number conversion (`atoi`, `strtol`, `strtoul`, `strtoll`, `strtoull`, `itoa`)
7. Float parsing (`strtod`, `strtof`, `strtold`, `ldexp`)
8. FILE* I/O (`fopen..remove`)
9. Low-level I/O (`open..unlink`, `isatty`, `getcwd`, `chdir`, `realpath`)
10. Printf family
11. Process control (`fork`, `exec`, `exit`, `system`, `atexit`, `abort`)
12. Sort/Search (heapsort `qsort`, `bsearch`)
13. Time & System (`gettimeofday`, `uname`, `sysinfo`, `sleep`, `clock`, `time`)
14. Signals & Dynamic Linking stubs
15. POSIX extensions (`getline`, `strdup`, `strerror`, `perror`, `ioctl`, `mmap`)
16. glibc compat (`__errno_location`, `__isoc23_*`, `__assert_fail`, `_setjmp`)
17. Environment (`getenv`, `environ`)

> ⚠️ **Heapsort вместо Quicksort:** выбран для гарантии O(1) стековой памяти.
> В Ring 3 User Stack = 64 KB, рекурсивный Quicksort может вызвать
> Stack Overflow Guard (SIGSEGV) на больших массивах.

#### 8.6.5 setjmp/longjmp (NASM, i386)

Критично для **error recovery** TinyCC: при синтаксической ошибке
TCC вызывает `longjmp()` для возврата к точке `setjmp()` без
раскрутки стека.

```text
jmp_buf layout (24 bytes):
  [0]  EBX    (callee-saved)
  [4]  ESI    (callee-saved)
  [8]  EDI    (callee-saved)
  [12] EBP    (frame pointer)
  [16] ESP    (stack pointer, скорректирован на return address)
  [20] EIP    (return address из стека)
```

POSIX compliance: `longjmp(env, 0)` возвращает 1.

#### 8.6.6 Toolchain Injection (Makefile → initrd.tar)

При сборке ISO Makefile автоматически формирует **полный toolchain**
внутри initrd для работы TCC в Ring 3:

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

- `libc.a` собирается через `i686-linux-gnu-ar rcs` из двух объектов:
  `user_libc.o + setjmp.o`
- Fake POSIX headers — однострочные `#include "user_libc.h"` (Monolithic Bypass)
- CRT файлы копируются из объектных файлов сборки (не из исходников)
- `libtcc1.a` содержит хелперы для 64-битной арифметики (`__divdi3`, `__moddi3`, etc.)

#### 8.6.7 Флаги компиляции TCC

```makefile
TCC_CFLAGS = -m32 -nostdlib -static -ffreestanding -O2 -Wall -Wextra \
             -fno-optimize-sibling-calls \
             -DCONFIG_TCC_STATIC \
             -DONE_SOURCE=1 \
             -Iuser_src \
             -I$(INITRD_ROOT)/include
```

| Флаг | Назначение |
|---|---|
| `-DONE_SOURCE=1` | Монолитная компиляция (все `.c` в одном `tcc.c`) |
| `-DCONFIG_TCC_STATIC` | Отключает `dlopen/dlsym` |
| `-fno-optimize-sibling-calls` | Запрет tail-call optimization (`setjmp` safety) |
| `-I$(INITRD_ROOT)/include` | Доступ к fake POSIX headers при компиляции |

#### 8.6.8 Команда Shell: `compile`

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

## 9. ГАРАНТИИ СИСТЕМЫ (SLA)

| # | Гарантия | Механизм |
|---|---|---|
| 1 | **Бессмертное Ядро** | PID 0 физически не может упасть из-за Ring 3 |
| 2 | **Бессмертный Init** | Reaper перезапускает PID 1 из `/sbin/init.elf` |
| 3 | **Crash-Only Shell** | Init перезапускает Shell < 100 мс |
| 4 | **Изоляция CoW** | Strict CoW Teardown (`pmm_dec_ref`) |
| 5 | **Zero Trust I/O** | Устройства через `open("/dev/console")` |
| 6 | **W^X Enforcement** | VMM + `sys_mmap/sys_mprotect` reject `WRITE+EXEC` |
| 7 | **OOM Governance** | OOM Trap убивает процесс, не ядро |
| 8 | **Kernel Stack Protection** | Guard Pages (Day 16) |
| 9 | **POSIX Compliance** | Orphan Nodes, FD inheritance, variadic open |
| 10 | **Self-Hosting** | TinyCC компилирует программы внутри ОС |
| 11 | **68/68 Stress Test** | Omni Stress: VMM(10), FPU(5), Process(6), VFS(11), C(6), Syscall(8), Exec(2), Memory(3), Libc(6), System(2), Hardening(5), Security(4) |

### Математически доказанные гарантии (x86)

| Тест | Что доказано |
|---|---|
| `vmm_mprotect_sigsegv` | W^X Enforcement работает |
| `vmm_mprotect_partial` | S1 fix: VMA splitting при частичном mprotect |
| `vmm_mprotect_partial_sigsegv` | S1 fix: SIGSEGV при записи в частично защищённую страницу |
| `proc_fork_bomb` (25 fork'ов) | Scheduler production-ready |
| `proc_zombie_cascade` (15 зомби) | Supervisor Trees работают |
| `vfs_1000_files` | tmpfs stable под нагрузкой |
| `fpu_context_switch` | Lazy FPU Switching корректен |
| `heap_exhaustion` | `sys_brk` защитил от OOM |
| `stack_overflow_guard` | Guard Page убил процесс |
| `fd_exhaustion` (256 FD) | `fd_table` лимит работает |
| `unlink_open_file` | POSIX Orphan Semantics |
| `directory_ops` | `sys_mkdir + S_IFDIR + readdir + ENOTEMPTY` (Day 31) |
| `syscall_enosys` | Invalid syscall number → `-ENOSYS`, не crash |
| `syscall_eacces_rbac` | `FS_SYSTEM` RBAC защищает `/boot` от Ring 3 |
| `syscall_efault_null` | NULL pointer → `-EFAULT` |
| `syscall_efault_kernel` | Kernel pointer (`0xC0000000`) → `-EFAULT` |
| `vmm_wx_mprotect_reject` | `mprotect(W|X)` → `-EPERM` (SLA #6) |
| `vfs_dup_dup2` | FD duplication + irq_safe refcount |
| `vfs_fstat_size` | Inode metadata: size, `S_IFREG / S_IFDIR` |
| `vfs_readdir_list` | Index-based directory iteration |
| `proc_exec_argv` | exec + Stack Forging argv + self-hosting compile |
| `proc_exec_enoent` | exec nonexistent → `-ENOENT`, процесс жив |
| `proc_waitpid_wnohang` | Non-blocking reap (`WNOHANG`) |
| `libc_printf_edge` | `INT_MIN`, NULL string, hex — без UB |
| `libc_snprintf_overflow` | Buffer truncation safety |
| `sys_uname_sysinfo` | System identity + resource accounting |
| `syscall_ebadf` | `close(-1)/read(-1)/write(-1)/lseek(-1)` → `EBADF` (Day 31) |
| `proc_rapid_fork_exit` | 20× fork+exit без Triple Fault (T5 regression) |

---

## 10. ИЗВЕСТНЫЕ ПРОБЛЕМЫ И ROADMAP

### 10.1 Known Limitations (x86, Alpha 0.5-rc1)

| ID | Ограничение | Почему допустимо |
|---|---|---|
| LIMIT-001 | Bump allocator (`free = no-op`) | Acceptable для короткоживущих TCC/shell процессов. Для долгоживущих сервисов — mmap-based allocator (post-stabilization). |
| LIMIT-002 | Нет сигналов (`signal = stub`) | Не входит в Enclave POSIX Lite P2. Crash-only model заменяет сигналы. |
| LIMIT-003 | Нет потоков (threads) | Не требуется на данной фазе. Один процесс = один поток. |
| LIMIT-004 | Нет динамической линковки | Static-only by design. `CONFIG_TCC_STATIC`. |
| LIMIT-005 | `waitpid` не Linux-compatible | `status == exit_code` (0..255), не `<< 8`. Enclave ABI (§1.3). |
| LIMIT-006 | `/boot` не в VFS | ISO-level файлы (GRUB module). RBAC `FS_SYSTEM` проверяется code review. |
| LIMIT-007 | `getcwd()` всегда `"/"` | Нет cwd tracking. Acceptable для текущей фазы. |
| LIMIT-008 | `chdir() = ENOSYS` | Нет cwd tracking. |
| LIMIT-009 | `localtime() = stub` | Нет RTC driver. |
| LIMIT-010 | `getenv() = NULL` | Нет environment. |
| LIMIT-011 | `isatty() = 1` для fd 0/1/2 | Упрощение. Нет `ioctl TIOCGWINSZ` check. |
| LIMIT-012 | `sys_brk` shrink разрешён ядром, но libc не использует | Bump-only policy. Reserved capability. |

---

### 10.2 ARM Known Limitations (Alpha 0.6-arm-user)

| ID | Ограничение | Почему допустимо |
|---|---|---|
| ARM-LIMIT-001 | CLOSED: User tasks run with IRQ enabled (`CPSR = 0x50`) | IRQ stub сохраняет полный user trap frame (72 bytes). Day 46 validation pass. |
| ARM-LIMIT-002 | User memory mapped as 1 MB sections | Spike mapping. 4 KB pages и real ARM VMM — позже. |
| ARM-LIMIT-003 | DAbort/PAbort/UNDEF пока fatal dump | Fault isolation и task kill — следующий этап. |
| ARM-LIMIT-004 | User image is raw binary, not ELF | ELF loader на ARM будет позже. Сейчас proof-of-concept user mode. |
| ARM-LIMIT-005 | `sys_write` validates only explicit user regions | Полная VMA-based validation появится вместе с ARM VMM. |
| ARM-LIMIT-006 | Нет per-process address space на ARM | Один временный user mapping для spike. |
| ARM-LIMIT-007 | Linker может предупреждать о RWX LOAD segment | Это warning уровня ELF segments, не runtime W^X user memory. Требует cleanup `linker_arm.ld`. |
| ARM-LIMIT-008 | Нет PMM на ARM | Физические user region зарезервированы вручную. PMM — позже. |
| ARM-LIMIT-009 | SVC handler не сохраняет `SP_usr` / `LR_usr` | Безопасно для текущих non-blocking syscalls. Scheduling syscalls (`sys_yield`, future sleep/wait) с user stack требуют full SVC user frame. Fix planned before production user-space. |

---

### 10.3 Roadmap

| Days | Milestone | Status |
|---|---|---|
| **36-37** | 🍓 ARM Boot Spike | ✅ Complete |
| **38-40** | 🍓 ARM IRQ + Timer + Vectors | ✅ Complete |
| **41-42** | 🍓 ARM Context Switch + Scheduler | ✅ Complete |
| **43-45** | 🍓 ARM SVC + User Mode + Integration | ✅ Complete |
| **45+** | ARM IRQ Preemption from User Mode + Fault Isolation | ✅ IRQ Preemption Complete + Validation Pass (Day 46); 🔄 Fault Isolation Next |
| **46+** | ARM 4 KB Pages + Real ARM VMM | 📋 Planned |
| **47+** | User-Mode Drivers (Minix 3), IPC (Message Passing) | 📋 Planned |
| **48+** | Seccomp (Syscall Filter), VFS Namespaces (chroot) | 📋 Planned |
| **50+** | RISC-V порт, ARM Cortex-A (64-bit) | 📋 Planned |

---

### 10.4 Источники вдохновения

| Проект | Что заимствовано |
|---|---|
| **TinyCC** (Fabrice Bellard) | Минимализм + скорость + self-hosting |
| **Linux** | POSIX syscalls + VFS + ELF loader |
| **Minix 3** (Tanenbaum) | User-mode drivers + message passing IPC |
| **seL4** (NICTA) | Capability-Based Security + Formal Verification |
| **Erlang/OTP** (Ericsson) | "Let it crash" + Supervisor Trees |
| **QNX** | Microkernel + Message Passing IPC |
| **FreeBSD Jails / Linux cgroups** | Resource Containers |

---

## 📎 ПРИЛОЖЕНИЕ A: КРИТИЧЕСКИЕ АРХИТЕКТУРНЫЕ НЮАНСЫ

### x86 Nuances

| Нюанс | Описание |
|---|---|
| **Framebuffer PCD** | LFB мапится с `PAGE_PCD` (`0x10`), иначе кэш-артефакты |
| **Virtual Stack Switch** | `mov esp, stack_top` после CR0.PG |
| **Reaper Queue Pattern** | Мертвые задачи очищаются следующей задачей |
| **Scheduler IRQ Safety** | `cli/sti` с сохранением EFLAGS |
| **IRQ Save/Restore Primitive** | `irq_save()` сохраняет EFLAGS и выполняет `cli`; `irq_restore()` восстанавливает EFLAGS.<br>Все критические секции должны использовать `irq_save()/irq_restore()` вместо безусловных `cli/sti`.<br>Безусловные `cli/sti` допустимы только для idle wait, fatal loops, task trampoline. |
| **Heap-VMM Synergy** | `kmalloc` → Page Fault → физическая страница |
| **VMM Deep Free Trap** | Освобождать PTE + PT + PD |
| **Kernel Heap Isolation** | `0xD0000000` без `PAGE_USER` |
| **VIRT_TO_PHYS Underflow** | Проверка `addr >= 0xC0000000` |
| **TSS ESP0 Virtual Address** | `PHYS_TO_VIRT` для стека ядра |
| **EOI Lock Bypass** | EOI ДО вызова C-обработчика |
| **FXSAVE Trap** | `clts` ДО `fxsave` |
| **All-Zero FXRSTOR** | `fninit + fxsave` сразу |
| **16-Byte Alignment** | `fpu_state[512]` первым в `task_t` |
| **Stack Forging (ABI)** | callee-saved + trampoline на стеке |
| **Signed Char Trap** | `uint8_t` для UTF-8 байтов |
| **PSF1 UCS-2** | `uint16_t*` для Unicode tables |
| **IRET Stack Switch Trap** | Обновлять `r->useresp`, не `r->esp` |
| **Tail Call Optimization** | `-fno-optimize-sibling-calls` для User Space |
| **W^X Enforcement** | `user_linker.ld` + VMM |
| **Mountpoint Teleportation** | `FS_MOUNTPOINT → mountpoint_node` |
| **SSOT Syscall Constants** | Linux i386 ABI (`O_CREAT=0x0040`) |
| **Ring 0 CoW Write** | Ring 0 может писать в user CoW только через `vmm_handle_user_write_fault()`.<br>Проверки: VMA exists, `VMA_WRITE`, no W^X, refcount-safe, `invlpg`.<br>Blanket Ring 0 write access запрещён. |
| **vfs_close_fd()** | Internal kernel API для закрытия FD любой задачи.<br>Ring 0 НЕ вызывает `sys_close()`.<br>`sys_close()` — тонкая обёртка над `vfs_close_fd(current_task, fd)`. |
| **sys_mkdir RBAC** | `sys_mkdir_handler` проверяет `FS_SYSTEM` на родителе → `EACCES` для `/boot`.<br>Ring 3 не может создавать файлы в защищённых директориях. |
| **tmpfs S_IFDIR** | `tmpfs_create` с `mode & S_IFDIR` → `FS_DIRECTORY` + `readdir/finddir/create/unlink/open/close` callbacks.<br>`private_data = NULL` для директорий. |
| **ENOTEMPTY Guard** | `tmpfs_unlink` проверяет `FS_DIRECTORY && first_child != NULL` → `-ENOTEMPTY`.<br>POSIX compliance для rmdir semantics. |
| **waitpid Status Encoding** | `status == exit_code` (normal exit, 0..255); `status == -1` (killed by kernel: SIGSEGV, OOM, guard page).<br>НЕ Linux-compatible (нет `<< 8`). |
| **VMA Splitting (mprotect)** | `vma_protect_range()` разделяет VMA при частичном покрытии (5 случаев).<br>`VMA_COW` сохраняется.<br>Паттерн идентичен `vma_unmap_range()`. |
| **CoW-safe mprotect** | `vmm_protect_page_in_pd()` сохраняет `PAGE_COW + PWT/PCD/GLOBAL`.<br>Если `PAGE_COW` активен — `PAGE_WRITE` принудительно снимается (аппаратная защита CoW). |
| **POSIX Return Convention** | Все POSIX-обёртки в `user_libc.c` при ошибке возвращают `-1` (или `MAP_FAILED` для mmap) и устанавливают `errno = -ret`.<br>Raw errno НЕ возвращается пользователю. |
| **TCC Tagged Struct Rule** | Все структуры в `user_libc.h`, используемые TCC, обязаны быть tagged (`struct timeval`, не `typedef struct {...} timeval_t`).<br>TCC пишет `struct timeval tv;` — anonymous struct → incomplete type → compilation error.<br>Паттерн: `struct X {...}; typedef struct X X_t;`. |
| **TCC dl* Guard** | Объявления `dlopen/dlsym/dlclose/dlerror` + `RTLD_*` обёрнуты в `#ifndef CONFIG_TCC_STATIC`.<br>TCC определяет собственные static stubs с несовместимыми сигнатурами.<br>Без guard → conflicting types error. |
| **CoW ref==0 Safety** | `vmm_handle_user_write_fault`: если `pmm_get_refcount() == 0` (теоретически corruption), трактуется как last-owner: COW снимается, WRITE восстанавливается.<br>Безопаснее чем crash. |
| **RBAC FS_SYSTEM** | `sys_open`: `(FS_SYSTEM && !FS_DIRECTORY) → EACCES`.<br>`sys_mkdir`: `parent->flags & FS_SYSTEM → EACCES`.<br>`/boot` не существует в VFS (ISO-level, не initrd) — RBAC проверяется code review, не Ring 3 тестом. |
| **vfs_close_fd Orphan Semantics** | `of->ref_count--` → 0 → `node->ref_count--` → 0 && `is_unlinked` → `close()` + `kfree(private_data)` + `kfree(node)` + `kfree(of)`.<br>IRQ-safe.<br>Ring 0 НЕ вызывает `sys_close()`. |
| **tmpfs ENOTEMPTY** | `tmpfs_unlink`: `FS_DIRECTORY && first_child → -ENOTEMPTY`.<br>POSIX rmdir semantics. |

### ARM Nuances

| Нюанс | Описание |
|---|---|
| **ARM ISB/DSB/DMB** | ARMv6 не имеет UAL `isb/dsb/dmb`.<br>Используем CP15:<br>`ISB = mcr p15,0,r0,c7,c5,4`<br>`DSB = c7,c10,4`<br>`DMB = c7,c10,5` |
| **ARM PHYS_BASE** | QEMU raspi1ap загружает kernel по `0x10000` (не `0x8000`).<br>Реальный RPi1: `0x8000`.<br>`PHYS_BASE` в `linker_arm.ld`. |
| **ARM VMA/LMA** | VMA = LMA + KERNEL_VMA для каждой секции.<br>`AT(ADDR(.section) - KERNEL_VMA)`.<br>Иначе MMU транслирует в мусор. |
| **ARM TTBR1 QEMU** | QEMU raspi1ap: Prefetch Abort при TTBR1 N=2.<br>Spike: один TTBR0 (identity + HH).<br>TTBR0/TTBR1 split — позже. |
| **ARM Section Map** | Начальный маппинг: Section 1 MB (не 4 KB pages).<br>RAM = `PA|0x140E`, Device = `PA|0x416`.<br>Fine-grained — в `arm_mmu.c`. |
| **ARM No Hardware Divide** | ARM1176 не имеет div.<br>GCC генерирует `__aeabi_uidiv` (libgcc).<br>Решение: hardcoded константы + libgcc в LDFLAGS. |
| **ARM SVC vs INT 0x80** | ARM syscall: `svc #0` (не `int 0x80`).<br>Номера syscall — Enclave (Frozen).<br>`user_syscalls.h` меняет только asm обёртку. |
| **ARM Exception Vectors** | 8 векторов (не 256 как x86 IDT).<br>VBAR = адрес `_vector_table` в `.text` (не `0xFFFF0000`).<br>`b handler` (не `ldr pc` — нет literal pool).<br>SVC = syscall, DAbort = page fault, IRQ = hardware. |
| **ARM PL011 UART** | BCM2835 UART0 (PL011) по `0x20201000`.<br>GPIO 14/15 ALT0.<br>Baud: IBRD=1, FBRD=40 (3 MHz UARTCLK). |
| **ARM hal_timer_delay** | `hal_timer_delay_us/ms` реализованы в `arm_timer.c` (BCM2835 CLO polling, 1 MHz).<br>Hardware counter тикает с power-on, работает ДО `hal_timer_init()`.<br>Calibrated loop (Day 37 stub) удалён из `arm_uart.c` — hardware timer точнее. |
| **ARM SVC SP_usr Limitation** | SVC frame не сохраняет `SP_usr` / `LR_usr`.<br>Это безопасно для текущих non-blocking syscalls, но требует full user frame для scheduling syscalls, использующих user stack (см. `ARM-LIMIT-009`). |
| **ARM Syscall ABI** | `svc #0`; `r7` = syscall number; `r0-r6` = args; `r0` = return.<br>Номера syscall совпадают с x86 Enclave ABI. |
| **ARM Preemptive User CPSR** | `0x50 = USR | FIQ disabled | IRQ enabled`.<br>Используется для user tasks начиная с Day 46. |
| **ARM User IRQ Trap Frame** | IRQ from USR mode сохраняет 72-byte frame: `r0-r12`, `SP_usr`, `LR_usr`, padding, `PC`, `CPSR`.<br>IRQ from SVC mode сохраняет 64-byte kernel frame.<br>Mode detection: `SPSR_irq & 0x1F == ARM_MODE_USR`. |
| **ARM User IRQ Return** | `SP_usr` восстанавливается через SYS mode.<br>`r0-r12` и `LR_usr` восстанавливаются через `ldmia sp, {r0-r12, lr}^`.<br>`PC/CPSR` восстанавливаются через `rfeia sp!`. |
| **ARM W^X Section Flags** | User code: `0x180E` (user RO, executable).<br>User data: `0x1C1E` (user RW, XN). |
| **ARM Scheduler TASK_FREE** | `schedule()` не должен возвращать мёртвую задачу в `TASK_READY`.<br>`TASK_FREE` — терминальное состояние до очистки. |
| **ARM PID 0 Immortal Guard** | `arm_task_exit()` игнорирует `current_task == 0`.<br>Idle не может быть убит через `sys_exit`. |
| **ARM Task Control API** | `arm_syscall.c` не имеет прямого доступа к `tasks[]` или `current_task`.<br>Используются `arm_current_pid()`, `arm_task_yield()`, `arm_task_exit()`. |
| **ARM Immediate Encoding** | `mov Rd, #imm` поддерживает только 8-bit immediate с even rotate.<br>Большие константы через `ldr Rd, =value`. |
| **ARM User Entry** | `arm_enter_user_first`: set `SP_usr`, set `SPSR = ARM_CPSR_USER (0x50)`, clear `r0-r12`, `movs pc, lr`. |

---

## 📎 ПРИЛОЖЕНИЕ B: ВЕРДИКТ МЕНТОРА

> *"Любая новая фича, позволяющая Ring 0 взаимодействовать с Ring 3 памятью, обязана иметь:*
>
> - *Bounds checking (верхняя и нижняя границы)*
> - *Permission checking (`VMA_WRITE` для writes)*
> - *Resource limiting (максимум аргументов/размеров)*
> - *Kernel stack safety (никаких больших static arrays)*
> - *Atomicity (`cli/sti` для критических секций)"*

---

**Конец документа.**
````