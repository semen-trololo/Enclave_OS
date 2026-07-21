#include "timer.h"
#include "port_io.h"
#include "isr.h"
#include "klib.h"
#include "pic.h"
// #include "task.h"  ← УДАЛЕНО (DIP-3: L2 не зависит от L6)

#define PIT_BASE_FREQ 1193182
#define PIT_CHANNEL0  0x40
#define PIT_COMMAND   0x43

static volatile uint32_t tick_count = 0;
static uint32_t pit_frequency = 0;

// ============================================================================
// DIP-3 FIX: Callback вместо прямого вызова schedule()/wake_sleepers().
// Timer не знает о задачах. Kernel инжектит handler.
// ============================================================================
static timer_tick_callback_t tick_callback = NULL;

void timer_set_tick_callback(timer_tick_callback_t cb) {
    tick_callback = cb;
}

// Обработчик IRQ0 (INT 32 после remap)
static void pit_handler(struct regs* r) {
    (void)r;
    tick_count++;

    // DIP-3: Вся логика задач (wake_sleepers + schedule) — в callback.
    // Timer только считает тики и дёргает callback.
    if (tick_callback) {
        tick_callback(tick_count);
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
