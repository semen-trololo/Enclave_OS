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
**Статус**: Дни 1-7 завершены полностью. Ядро поддерживает вытесняющую многозадачность, аппаратную изоляцию памяти (CR3 Switch) и Lazy FPU Switching.

**Новые вехи (День 5 и отладка Дня 6)**:
- **Оптимизация PMM (День 5.1)**: Внедрен `__builtin_ctz` (аппаратная инструкция BSF/TZCNT) для поиска свободных страниц за O(1) тактов.
- **Графическая подсистема (День 5.2)**: Реализован Double Buffering (теневой буфер в RAM) и математика Dirty Rectangles. Скроллинг и отрисовка теперь происходят мгновенно без tearing'а.
- **Локализация и Шрифты (День 5.3)**: Интегрирован парсер бинарных шрифтов PSF1. Реализован UTF-8 State Machine внутри `fb_putc` и поддержка UCS-2 (UTF-16LE) Unicode-таблиц.
- **Hardening VMM (Отладка Дня 6)**: Исправлен фатальный Underflow в макросе `VIRT_TO_PHYS` для `.boot` секций. Разделены Page Tables для Identity Map и Higher Half. Внедрено жесткое резервирование `.boot.bss` в PMM для защиты от перезаписи стека и таблиц ядра аллокатором.

**Новые вехи (День 7: Процессы и Планировщик)**:
- **Preemptive Scheduling (День 7.3)**: Реализована вытесняющая многозадачность. Таймер PIT (IRQ0) насильно отбирает квант времени у задач. Обойдена фатальная ловушка "EOI Lock" (отправка EOI в PIC до вызова C-обработчика).
- **Hardware Memory Isolation (День 7.5)**: Внедрен `CR3 Switch`. Каждая задача получает собственный Page Directory. User Space (0x00000000 - 0xBFFFFFFF) полностью изолирован, а Kernel Space (0xC0000000+) клонируется во все PD ("Shared Kernel Space"), что позволяет прерываниям корректно работать в контексте любой задачи.
- **Lazy FPU Switching (День 7.4)**: Реализован индустриальный стандарт (как в Linux). FPU-регистры не сохраняются при каждом тике таймера. Вместо этого используется аппаратный бит `CR0.TS` (Task Switched). При попытке задачи использовать FPU генерируется исключение `#NM (INT 7)`, которое перехватывается ядром для ленивого сохранения/восстановления контекста через `fxsave`/`fxrstor`.
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
13. `.gitignore`
14. `task.h`, `task.c` - менеджер процессов, PCB, планировщик.
15. `context_switch.asm` - ассемблерное ядро переключения контекста (сохранение ESP, callee-saved регистров).
12. `task.c` - менеджер процессов, PCB, планировщик, FPU handler.
16. `context_switch.asm` - ассемблерное ядро переключения контекста (сохранение ESP, callee-saved регистров, CR3 switch, установка CR0.TS).
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
### 16. БЛОКИРОВКА PIC ПРИ КОНТЕКСТНОМ ПЕРЕКЛЮЧЕНИИ (EOI LOCK)
Если `schedule()` вызывается из обработчика аппаратного прерывания (IRQ0 / PIT), а отправка сигнала **EOI (End of Interrupt)** в контроллер PIC происходит *после* вызова C-обработчика, возникает фатальная ловушка:
1. Планировщик переключает контекст на другую задачу.
2. EOI для текущего IRQ не отправляется (потому что поток ушел в `switch_context`).
3. Контроллер PIC считает, что прерывание всё ещё обрабатывается, и **блокирует** эту линию IRQ.
4. Клавиатура/Таймер перестают работать навсегда.
**РЕШЕНИЕ**: В `irq_handler` (idt.c) отправлять `outb(0x20, 0x20)` в PIC **ДО** вызова C-функции обработчика.

### 17. TASK WRAPPER (ТРАМПЛИН) И ФЛАГ IF
Новые задачи создаются из `task_create` (контекст ядра). Если создание происходит во время обработки IRQ, флаг прерываний `IF` в `EFLAGS` сброшен (прерывания отключены).
`switch_context` делает инструкцию `ret`, которая **НЕ восстанавливает EFLAGS**. Новая задача стартует с отключенными прерываниями и навсегда узурпирует CPU (таймер PIT перестанет генерировать IRQ0).
**РЕШЕНИЕ**: Все новые задачи должны стартовать не в `entry_point`, а в функцию-трамплин (`task_entry_trampoline`), которая первой инструкцией делает `sti`, вызывает саму задачу, а при её возврате вызывает `task_exit()`.

### 18. МАТЕМАТИКА STACK FORGING (ПОДДЕЛКА СТЕКА)
Чтобы новая задача (которая никогда не прерывалась) могла быть запущена через `switch_context` (который ожидает на стеке сохраненные регистры и адрес возврата), её пустой стек должен быть вручную "подделан" при создании.
Порядок записи в пустой стек (растет вниз, от старших адресов к младшим):
1. Аргументы для трамплина (cdecl ABI).
2. Фейковый адрес возврата (например, `task_exit`).
3. Точка входа (`task_entry_trampoline`) — сюда прыгнет `ret`.
4. Нули для регистров `EBP, EDI, ESI, EBX` — их снимет `switch_context` при первом "восстановлении" контекста.

### 19. PHYS_TO_VIRT В HIGHER HALF KERNEL
Функция `pmm_alloc_page()` возвращает **физические** адреса. В архитектуре Higher Half Kernel (День 6) прямое обращение к ним из C-кода (например, для инициализации структуры PCB) вызовет Page Fault, если не настроен Identity Map для первых мегабайт.
**РЕШЕНИЕ**: Всегда использовать макрос `#define PHYS_TO_VIRT(addr) ((uint32_t)(addr) + 0xC0000000)` перед приведением типа указателя для записи/чтения структур ядра.

### 20. PSF1 ФОРМАТ ШРИФТОВ И UCS-2 (UTF-16LE)
Стандартные утилиты Linux (`bdf2psf`) часто генерируют PSF1 шрифты, где Unicode-таблица закодирована не в UTF-8, а в **UCS-2 (UTF-16LE)**. Каждый codepoint занимает 2 байта, а маркером конца глифа служит `0xFFFF`.
**РЕШЕНИЕ**: Благодаря Little-Endian архитектуре x86, таблицу можно читать напрямую через `const uint16_t*`, что аппаратно ускоряет поиск глифа и исключает ручной битовый сдвиг.

### 21. UTF-8 STATE MACHINE В КОНСОЛИ
Функция `fb_putc` должна содержать встроенный State Machine для декодирования UTF-8 байтов, приходящих из `k_printf`.
- Байты `0x00-0x7F` обрабатываются сразу.
- Байты `>= 0x80` накапливаются в `utf8_codepoint` (с учетом битовых масок `0xC0`, `0xE0`, `0xF0`) до завершения последовательности, после чего вызывается `find_glyph_index`.

### 22. ЛОВУШКА ЗНАКОВОГО `char` (SIGNED CHAR TRAP)
В C тип `char` по умолчанию знаковый (signed). При чтении UTF-8 байтов (например, `0xD0` для кириллицы) они интерпретируются как отрицательные числа (например, `-48`).
**ПРОБЛЕМА**: Условие `if (c >= 32 && c < 127)` в Shell молча отбрасывает кириллицу и спецсимволы.
**РЕШЕНИЕ**: Всегда приводить к беззнаковому типу перед сравнением: `uint8_t uc = (uint8_t)c; if (uc >= 32)`.

### 23. VIRT_TO_PHYS UNDERFLOW ДЛЯ .BOOT СЕКЦИЙ
Секции `.boot.bss` и `.boot.data` в `boot.asm` имеют виртуальные адреса **ниже** `0xC0000000` (так как они инициализируются до прыжка в Higher Half).
**ПРОБЛЕМА**: Наивный макрос `#define VIRT_TO_PHYS(addr) (addr - 0xC0000000)` вызывает unsigned integer underflow, превращая адрес `0x00102000` в `0x40102000`. Загрузка этого мусора в `CR3` вызывает Triple Fault.
**РЕШЕНИЕ**: Умный макрос с проверкой:
`#define VIRT_TO_PHYS(addr) (((uint32_t)(addr) >= 0xC0000000) ? ((uint32_t)(addr) - 0xC0000000) : (uint32_t)(addr))`

### 24. ЗАЩИТА ОТ ПЕРЕЗАПИСИ ЯДРА АЛЛОКАТОРОМ (PMM)
При динамическом создании Page Tables в `paging_init()` через `pmm_alloc_page()`, аллокатор может вернуть физические адреса, на которых лежат `boot_stack` или `boot_page_tables`, если они не были явно зарезервированы (линкер часто кладет `.boot.bss` вне диапазона `_kernel_start` / `_kernel_end`).
**РЕШЕНИЕ**: В начале `paging_init()` жестко резервировать диапазон физической памяти, занимаемый ядром (например, `pmm_reserve_region(0x00100000, 0x01000000)`), а также конкретные символы через `VIRT_TO_PHYS`.

### 25. DOUBLE BUFFERING И DIRTY RECTANGLES
Прямая запись в LFB (0xFD000000) вызывает tearing и тормоза из-за задержек шины PCI/AGP.
**РЕШЕНИЕ**: Выделять `back_buffer` в Heap (RAM). Все примитивы рисуют в RAM. `fb_flush()` копирует в LFB только **Dirty Rectangle** (минимальный ограничивающий бокс изменившихся пикселей). Скроллинг превращается в мгновенный `memmove` в RAM.

### 26. FREESTANDING ENVIRONMENT (БЕЛЫЙ СПИСОК ЗАГОЛОВКОВ)
В ядре ОС (`-ffreestanding -nostdlib`) **ЗАПРЕЩЕНО** использовать `<stdio.h>`, `<stdlib.h>`, `<string.h>` (они требуют libc и syscall'ов).
**РАЗРЕШЕНО** (и является стандартом ISO C): `<stdint.h>`, `<stddef.h>`, `<stdbool.h>`, `<stdarg.h>`, `<limits.h>`. Они генерируются встроенными механизмами GCC (builtins) и не требуют линковки с внешними библиотеками.

### 27. РАЗДЕЛЕНИЕ PAGE TABLES (IDENTITY vs HIGHER HALF)
В `boot.asm` нельзя переиспользовать одни и те же Page Tables для Identity Map (0x00000000) и Higher Half (0xC0000000).
**ПРОБЛЕМА**: Модификация PTE в `paging_init()` для Higher Half затрет записи Identity Map, что приведет к потере маппинга физических адресов и Triple Fault.
**РЕШЕНИЕ**: Выделять отдельные массивы в `.boot.bss` (например, `boot_page_tables` и `boot_page_tables_hh`).

### 28. CR3 SWITCH И SHARED KERNEL SPACE (ДЕНЬ 7.5)
При создании новой задачи выделяется пустой Page Directory. Индексы 0-767 (User Space) изолированы. Индексы 768-1023 (Kernel Space) **клонируются** из глобального `boot_page_directory`.
**ЗАЧЕМ**: Это создает "общую крышу". Когда происходит прерывание (IRQ), процессор должен мгновенно найти код ядра и стек прерывания, независимо от того, какая задача сейчас выполняется.

### 29. LAZY FPU SWITCHING И АППАРАТНЫЙ ТРИГГЕР CR0.TS (ДЕНЬ 7.4)
Сохранять 512 байт FPU при каждом переключении — убийство производительности.
**РЕШЕНИЕ**: В `switch.asm` устанавливается бит `CR0.TS` (Task Switched). При попытке задачи выполнить FPU/SSE инструкцию процессор аппаратно генерирует исключение **#NM (INT 7 - Device Not Available)**. Обработчик INT 7 делает `fxsave` для старой задачи, `fxrstor` для новой, снимает `CR0.TS` (инструкцией `clts`) и возвращает управление.

### 30. ЛОВУШКА FXSAVE #NM (СМЕРТЕЛЬНАЯ РЕКУРСИЯ)
Инструкции `fxsave` и `fxrstor` сами по себе считаются FPU-инструкциями и **проверяют бит CR0.TS**.
**ПРОБЛЕМА**: Если вызвать `fxsave` внутри обработчика #NM ДО снятия бита `TS` (до `clts`), процессор сгенерирует НОВОЕ исключение #NM на инструкции `fxsave`. Возникает бесконечная рекурсия, переполнение стека и Triple Fault.
**РЕШЕНИЕ**: Инструкция `clts` должна быть выполнена **В САМОМ НАЧАЛЕ** `device_not_available_handler`, до любых операций с FPU.

### 31. ALL-ZERO FXRSTOR TRAP (НЕВАЛИДНОЕ СОСТОЯНИЕ FPU)
При создании задачи `k_memset` забивает `fpu_state` нулями.
**ПРОБЛЕМА**: Для x87 FPU нулевой буфер — это невалидное состояние (например, Tag Word `0x0000` или MXCSR `0x0000` нарушают спецификацию). `fxrstor` из нулевого буфера генерирует **#GP (General Protection Fault)**.
**РЕШЕНИЕ**: Сразу после `fninit` (первой инициализации FPU задачи) необходимо сделать `fxsave` в `fpu_state`, чтобы сохранить валидный "слепок" чистого FPU.

### 32. CR4.OSFXSR И СКРЫТЫЙ INVALID OPCODE (#UD)
**ПРОБЛЕМА**: Инструкции `fxsave` и `fxrstor` требуют явного разрешения от ОС. Если в регистре `CR4` не установлен бит 9 (`OSFXSR`), процессор генерирует исключение **#UD (Invalid Opcode)** при их вызове.
**РЕШЕНИЕ**: В `tasking_init()` обязательно выставлять биты 9 и 10 в `CR4`.

### 33. ЖЕСТКОЕ ВЫРАВНИВАНИЕ FPU_STATE (16-BYTE ALIGNMENT)
Инструкции `fxsave`/`fxrstor` требуют, чтобы адрес буфера был **строго кратен 16 байтам**. Иначе процессор генерирует #GP.
**РЕШЕНИЕ**: Поле `fpu_state[512]` должно быть первым в структуре `task_t`. Так как `pmm_alloc_page()` всегда возвращает адреса, кратные 4096 (что автоматически кратно 16), это гарантирует идеальное аппаратное выравнивание.

### 34. ДИНАМИЧЕСКАЯ АЛЛОКАЦИЯ MAIN_TASK
**ПРОБЛЕМА**: Объявление `static task_t main_task;` кладет структуру в секцию `.bss` по произвольному адресу (например, `0xC0100124`), который **НЕ кратен 16**. При первом же `fxsave` произойдет #GP.
**РЕШЕНИЕ**: `main_task` должна выделяться динамически через `pmm_alloc_page()` при инициализации планировщика, как и любая другая задача.


АРХИТЕКТУРА ОС (ПОДСИСТЕМЫ)

### АРХИТЕКТУРА ОС (Дополнение)
[МНОГОЗАДАЧНОСТЬ (ДЕНЬ 7)]
- **PCB (task_t)**: Хранит PID, State (READY/RUNNING/DEAD), сохраненный ESP и базу стека ядра.
- **Очередь**: Кольцевой двусвязный список. `schedule()` просто сдвигает `current_task = current_task->next`.
- **TSS Update**: При каждом переключении `schedule()` вызывает `tss_set_kernel_stack()`, обновляя `ESP0`. Это гарантирует, что следующее прерывание из Ring 3 корректно переключит стек на стек ядра *новой* задачи.

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

[МНОГОЗАДАЧНОСТЬ (ДЕНЬ 7)]
- **PCB (task_t)**: Хранит PID, State, сохраненный ESP, CR3 (физ. адрес PD) и буфер FPU (512 байт, 16-byte aligned, первое поле структуры).
- **Очередь**: Кольцевой двусвязный список. `schedule()` сдвигает `current_task`.
- **TSS Update**: При каждом переключении обновляется `ESP0` в TSS.
- **CR3 Switch**: `switch_context` загружает `new_task->cr3` в регистр `CR3`, аппаратно сбрасывая TLB и меняя адресное пространство.
- **FPU Owner**: Глобальная переменная `fpu_owner` отслеживает, чье состояние сейчас в регистрах CPU, чтобы избежать лишнего `fxsave`.
- **Lazy FPU**: `switch.asm` ставит `CR0.TS=1`. При FPU-инструкции срабатывает #NM. Обработчик: `clts` -> `fxsave`/`fxrstor` -> возврат.


ПЛАН РАЗВИТИЯ ОС (ДОРОЖНАЯ КАРТА)
[ДЕНЬ 4: ЗАВЕРШЕНО] Privilege Separation

    4.3.1 Task State Segment (TSS) и ltr.
    4.3.2 Переключение в User Mode (Ring 3) через IRET.
    4.3.3 Системные вызовы (INT 0x80, DPL=3, sys_write/sys_exit).
    4.3.4 Базовая защита памяти (Вариант А: PAGE_USER для всех страниц).
    4.3.5 Context Hijacking (возврат из Ring 3 в Ring 0).

[ДЕНЬ 5: ЗАВЕРШЕНО] Оптимизация графического режима и производительности
5.1 Оптимизация PMM (__builtin_ctz, поиск по 32-битным словам).
5.2 Оптимизация framebuffer (Double buffering, Dirty regions).
5.3 Улучшенный шрифт (PSF1 парсер, UCS-2 таблица, UTF-8 State Machine).

[ДЕНЬ 6 ЗАВЕРШЕНО] Архитектурный рефакторинг и продвинутая память

    6.1 Higher Half Kernel (0xC0000000) и Bulletproof Mapping.
    6.2 Multiboot Memory Map (E820) и Safe by Default.
    6.3 On-demand Paging (Lazy allocation, Zero-filled pages, PF handler).

[ДЕНЬ 7: ЗАВЕРШЕНО] Процессы и планировщик
- 7.1 Process Control Block (PCB) и Linked List.
- 7.2 Context Switching (Stack Forging, save/restore registers).
- 7.3 Round-Robin Scheduler (Preemption по таймеру, обход EOI Lock).
- 7.4 FPU Context Switching (CR0.TS, #NM handler, fxsave/fxrstor, CR4.OSFXSR).
- 7.5 Hardware Memory Isolation (CR3 switch, Shared Kernel Space).

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
