// ============================================================================
// arm_timer.c — BCM2835 System Timer (1 MHz)
// ============================================================================
// Реализует hal_timer.h контракт для ARM.
//
// Hardware:
//   BCM2835 System Timer: 4 compare registers (C0-C3), 1 MHz counter (CLO).
//   Используем Compare 1 (C1), GPU IRQ line 1.
//
// Tick model:
//   hal_timer_init(1000) → interval = 1000 us → 1 tick = 1 ms.
//   IRQ handler: clear CS bit → set next C1 = CLO + interval → tick++.
//
// ⚠️ CLO wraps at 2^32 us ≈ 71 min. Для 64-bit us нужен overflow tracking.
//    В Alpha 0.6 spike — достаточно 32-bit. hal_timer_get_us() возвращает
//    raw CLO (wraps). hal_timer_get_ticks() — 64-bit, не wraps.
//
// ⚠️ MMIO: физические адреса (identity mapping). После удаления identity
//    → заменить на BCM2835_VIRT(reg).
// ============================================================================

#include <stdint.h>
#include "config.h"
#include "hal/hal_timer.h"
#include "hal/hal_irq.h"
#include "hal/hal_cpu.h"

// ============================================================================
// MMIO ACCESS
// ============================================================================

static inline void mmio_write(uint32_t addr, uint32_t val)
{
    *(volatile uint32_t*)addr = val;
    hal_dsb();
}

static inline uint32_t mmio_read(uint32_t addr)
{
    uint32_t val = *(volatile uint32_t*)addr;
    hal_dsb();
    return val;
}

// ============================================================================
// STATE
// ============================================================================

static volatile uint64_t tick_count = 0;
static uint32_t timer_hz = 0;
static uint32_t timer_interval_us = 0;   // Microseconds per tick

// ============================================================================
// TIMER IRQ HANDLER
// ============================================================================
// Вызывается из hal_irq_dispatch → irq_handlers[BCM2835_IRQ_TIMER1].
//
// Порядок (ARM level-triggered):
//   1. Clear interrupt source (CS bit 1)
//   2. Set next compare (C1 = CLO + interval)
//   3. Increment tick
//   4. hal_timer_tick() → schedule() (когда scheduler готов)
//
// ⚠️ Если НЕ очистить CS — IRQ сработает повторно (level-triggered).
// ⚠️ Если НЕ установить C1 — не будет следующего тика.
// ============================================================================

static void timer_irq_handler(void* regs, uint32_t line)
{
    (void)regs;
    (void)line;

    // 1. Clear interrupt: write 1 to match bit in CS (Timer 1 = bit 1)
    mmio_write(BCM2835_ST_CS, (1u << BCM2835_IRQ_TIMER1));

    // 2. Set next compare: CLO + interval
    uint32_t clo = mmio_read(BCM2835_ST_CLO);
    mmio_write(BCM2835_ST_C1, clo + timer_interval_us);

    // 3. Increment tick counter
    tick_count++;

    // 4. Scheduler integration (когда task.c будет портирован)
    hal_timer_tick();
}

// ============================================================================
// HAL TIMER INIT
// ============================================================================

void hal_timer_init(uint32_t hz)
{
    if (hz == 0) return;

    timer_hz = hz;
    timer_interval_us = BCM2835_ST_FREQUENCY / hz;  // 1000000 / 1000 = 1000

    // Register handler for System Timer 1 (GPU IRQ line 1)
    hal_irq_register(BCM2835_IRQ_TIMER1, timer_irq_handler);

    // Set first compare: CLO + interval
    uint32_t clo = mmio_read(BCM2835_ST_CLO);
    mmio_write(BCM2835_ST_C1, clo + timer_interval_us);

    // Clear any pending timer 1 interrupt
    mmio_write(BCM2835_ST_CS, (1u << BCM2835_IRQ_TIMER1));

    // Enable IRQ line (unmask in BCM2835 interrupt controller)
    hal_irq_enable_line(BCM2835_IRQ_TIMER1);
}

// ============================================================================
// HAL TIMER QUERIES
// ============================================================================

uint32_t hal_timer_get_hz(void)
{
    return timer_hz;
}

uint64_t hal_timer_get_ticks(void)
{
    // IRQ-safe: 64-bit read на 32-bit ARM не атомарен.
    // Отключаем IRQ на время чтения.
    uint32_t flags = hal_irq_save();
    uint64_t t = tick_count;
    hal_irq_restore(flags);
    return t;
}

uint64_t hal_timer_get_ms(void)
{
    if (timer_hz == 0) return 0;
    uint32_t flags = hal_irq_save();
    uint64_t t = tick_count;
    hal_irq_restore(flags);
    return t * 1000 / timer_hz;
}

uint64_t hal_timer_get_us(void)
{
    // Raw hardware counter (1 MHz = 1 us resolution).
    // ⚠️ Wraps at 2^32 us ≈ 71 min. Для spike — достаточно.
    return (uint64_t)mmio_read(BCM2835_ST_CLO);
}

// ============================================================================
// HAL TIMER TICK (Scheduler Integration)
// ============================================================================
// Вызывается из timer_irq_handler (IRQ context, IRQ disabled).
// Quantum: 10 тиков = 10 мс. По истечении — schedule().
// ============================================================================

#define QUANTUM_TICKS 10

extern void schedule(void);

static uint32_t quantum_counter = 0;

extern void arm_task_check_wakeup(uint64_t current_tick);

void hal_timer_tick(void)
{
    // Check for sleeping tasks that should wake up.
    arm_task_check_wakeup(tick_count);

    quantum_counter++;
    if (quantum_counter >= QUANTUM_TICKS) {
        quantum_counter = 0;
        schedule();
    }
}

// ============================================================================
// ONESHOT TIMER (NOT IMPLEMENTED — Alpha 0.6)
// ============================================================================

int hal_timer_oneshot(uint32_t us, hal_timer_callback_t callback, void* context)
{
    (void)us;
    (void)callback;
    (void)context;
    return -1;  // ENOSYS
}

// ============================================================================
// BUSY-WAIT DELAY (без IRQ, безопасно до hal_timer_init)
// ============================================================================

void hal_timer_delay_us(uint32_t us)
{
    uint32_t start = mmio_read(BCM2835_ST_CLO);
    while ((mmio_read(BCM2835_ST_CLO) - start) < us) {
        // Busy wait. CLO wraps safely: unsigned subtraction handles overflow.
    }
}

void hal_timer_delay_ms(uint32_t ms)
{
    // Разбиваем на chunks по 100ms, чтобы избежать overflow при ms > 4294
    while (ms > 100) {
        hal_timer_delay_us(100000);
        ms -= 100;
    }
    if (ms > 0) {
        hal_timer_delay_us(ms * 1000);
    }
}