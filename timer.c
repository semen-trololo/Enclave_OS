#include "timer.h"
#include "port_io.h"
#include "isr.h"
#include "klib.h"
#include "pic.h"
#include "task.h"

#define PIT_BASE_FREQ 1193182
#define PIT_CHANNEL0  0x40
#define PIT_COMMAND   0x43

static volatile uint32_t tick_count = 0;
static uint32_t pit_frequency = 0;

// ✅ [ДЕНЬ 15] Пробуждение задач, спящих по таймеру
static void wake_sleepers(void) {
    if (!current_task) return;
    task_t* t = current_task;
    do {
        // Будим только тех, у кого sleep_until > 0 (игнорируем waitpid)
        if (t->state == TASK_SLEEPING && t->sleep_until > 0) {
            if (tick_count >= t->sleep_until) {
                t->state = TASK_READY;
                t->sleep_until = 0;
            }
        }
        t = t->next;
    } while (t != current_task);
}

// Обработчик IRQ0 (INT 32 после remap)
static void pit_handler(struct regs* r) {
    (void)r; 
    tick_count++;
    
    wake_sleepers(); // ✅ Пробуждаем спящих перед планированием

    // Квантование времени: переключаем задачи каждые 20 миллисекунд (50 Гц)
    if (tick_count % 20 == 0) {
        schedule();
    }
}

void timer_init(uint32_t freq) {
    pit_frequency = freq;
    uint32_t divisor = PIT_BASE_FREQ / freq;

    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));

    irq_register_handler(0, pit_handler);
    irq_clear_mask(0);

    k_printf("[PIT] Timer initialized at %d Hz (divisor: %d)\n", freq, divisor);
}

uint32_t timer_get_ticks(void) {
    return tick_count;
}

uint32_t timer_get_frequency(void) {
    return pit_frequency;
}

void k_sleep(uint32_t ms) {
    uint32_t start = tick_count;
    uint32_t target = start + ms;  
    
    while (tick_count < target) {
        __asm__ volatile("hlt");
    }
}
