#include "syscall.h"
#include "idt.h"
#include "isr.h"
#include "klib.h"
#include <stdint.h>

// Тип функции системного вызова
// Принимает указатель на struct regs (где лежат аргументы из EBX, ECX, EDX)
// Возвращает результат, который будет записан обратно в EAX
typedef int (*syscall_func_t)(struct regs* r);

// Таблица системных вызовов
#define MAX_SYSCALLS 256
static syscall_func_t syscall_table[MAX_SYSCALLS];

// ========================================================================
// Реализации системных вызовов
// ========================================================================

// sys_exit: завершение процесса и возврат управления ядру
static int sys_exit(struct regs* r) {
    uint32_t exit_code = r->ebx;
    k_printf("\n [SYSCALL] sys_exit called with code %u. Returning to kernel.\n", exit_code);

    // КРИТИЧЕСКИ ВАЖНО: Включаем прерывания (sti)!
    // При входе в прерывание CPU автоматически сбрасывает IF (делает cli).
    // Если мы не включим их обратно, hlt в k_getchar() зависнет навсегда.
    __asm__ volatile("sti");

    // Запускаем Shell прямо здесь, в контексте sys_exit.
    // Мы уже в Ring 0, на безопасном стеке ядра (переключенном через TSS при int 0x80).
    // Это временное, но абсолютно рабочее решение до Дня 7 (Планировщик), 
    // где мы будем делать правильный context switch и fork/exec.
    extern void shell_run(void);
    shell_run();

    // Fallback (никогда не выполнится, так как shell_run - бесконечный цикл)
    while(1) {
        __asm__ volatile("hlt");
    }
    return 0;
}
    
// sys_write: вывод строки
// Аргументы: EBX = fd, ECX = const char* buf, EDX = size_t count
static int sys_write(struct regs* r) {
    uint32_t fd = r->ebx;
    const char* buf = (const char*)r->ecx;
    uint32_t count = r->edx;

    // Базовая проверка (User Pointer Validation)
    // В будущем (День 6) здесь будет проверка, что buf находится в User Space
    if (buf == (const char*)0) {
        return -1; // EFAULT
    }

    if (fd == 1) { // stdout
        for (uint32_t i = 0; i < count; i++) {
            if (buf[i] == '\0') break; // Защита от не-null-terminated строк
            k_putchar(buf[i]);
        }
        return count;
    }

    return -1; // EBADF (неверный fd)
}

// sys_yield: добровольная отдача кванта времени
static int sys_yield(struct regs* r) {
    (void)r;
    // Пока нет планировщика, просто возвращаем управление
    return 0;
}

// ========================================================================
// Диспетчер системных вызовов
// ========================================================================

// Вызывается из isr_handler при срабатывании INT 0x80
static void syscall_dispatcher(struct regs* r) {
    uint32_t syscall_num = r->eax;

    if (syscall_num >= MAX_SYSCALLS || syscall_table[syscall_num] == 0) {
        k_printf("\n [SYSCALL] Invalid syscall number: %u\n", syscall_num);
        r->eax = (uint32_t)-1; // -ENOSYS
        return;
    }

    // Вызываем нужный системный вызов и сохраняем результат в EAX
    r->eax = (uint32_t)syscall_table[syscall_num](r);
}

// ========================================================================
// Инициализация
// ========================================================================

void syscall_init(void) {
    // 1. Обнуляем таблицу
    for (int i = 0; i < MAX_SYSCALLS; i++) {
        syscall_table[i] = 0;
    }

    // 2. Регистрируем системные вызовы
    syscall_table[SYS_EXIT]  = sys_exit;
    syscall_table[SYS_WRITE] = sys_write;
    syscall_table[SYS_YIELD] = sys_yield;

    // 3. Регистрируем обработчик INT 0x80 в IDT
    // ВАЖНО: Флаг 0xEE (Present=1, DPL=3, Type=1110b - 32-bit Interrupt Gate)
    // DPL=3 позволяет вызывать INT 0x80 из Ring 3 (User Mode)
    extern void isr128(); // Объявляем внешнюю ASM-функцию
    idt_set_gate(128, (uint32_t)isr128, 0x08, 0xEE);

    // 4. Регистрируем C-обработчик
    isr_register_handler(128, syscall_dispatcher);
}
