#ifndef HAL_TIMER_H
#define HAL_TIMER_H

// ============================================================================
// HAL TIMER — Hardware Abstraction Layer: System Timer
// ============================================================================
// x86: PIT 8254 (Channel 0, IRQ 0, 1193182 Hz base)
// ARM: BCM2835 System Timer (1 MHz, IRQ line 1) или ARM Timer (SP804)
//
// Portable kernel code включает ТОЛЬКО этот header.
// ============================================================================

#include <stdint.h>
#include "config.h"

// ============================================================================
// INITIALIZATION
// ============================================================================

// Инициализация системного таймера.
// hz: частота тиков (обычно 1000 = 1 мс тик)
// x86: PIT Channel 0, divisor = 1193182 / hz, IRQ 0
// ARM: BCM2835 System Timer Compare 1, IRQ line BCM2835_IRQ_TIMER1
//
// После вызова: hal_timer_get_ticks() инкрементируется hz раз в секунду.
void hal_timer_init(uint32_t hz);

// ============================================================================
// [M3] TIMER FREQUENCY QUERY
// ============================================================================

// Возвращает configured tick frequency (Hz).
// После hal_timer_init(1000) → 1000.
// До hal_timer_init() → 0.
uint32_t hal_timer_get_hz(void);

// ============================================================================
// TICK COUNTER
// ============================================================================

// Текущее количество тиков с момента boot.
// Инкрементируется в IRQ handler таймера.
// При hz=1000: 1 тик = 1 мс.
uint64_t hal_timer_get_ticks(void);

// Миллисекунды с момента boot.
// = hal_timer_get_ticks() * 1000 / hz
// При hz=1000: идентично get_ticks().
uint64_t hal_timer_get_ms(void);

// Микросекунды с момента boot (high-resolution).
// x86: ticks * (1000000 / hz) — грубо, но достаточно для nanosleep
// ARM: BCM2835 System Timer CLO (1 MHz counter) — точно
uint64_t hal_timer_get_us(void);

// ============================================================================
// SCHEDULER INTEGRATION
// ============================================================================

// Вызывается из IRQ handler таймера (hal_timer_tick_handler).
// Инкрементирует tick counter + вызывает schedule() если квант истёк.
//
// ⚠️ x86: вызывается ПОСЛЕ EOI (EOI Lock Bypass).
// ⚠️ ARM: вызывается ПОСЛЕ clear timer interrupt source.
void hal_timer_tick(void);

// ============================================================================
// ONESHOT TIMER (для nanosleep / timeout)
// ============================================================================

// Одноразовый таймер: вызвать callback через us микросекунд.
// x86: PIT Channel 0 в one-shot mode (или software timer wheel)
// ARM: BCM2835 System Timer Compare register
//
// Возвращает 0 при успехе, -1 если таймер занят.
// callback вызывается в контексте IRQ (Ring 0, прерывания выключены).
//
// ============================================================================
// [P5] NOT IMPLEMENTED in Alpha 0.6. Reserved for Days 41+.
// Returns -1 (ENOSYS) until implemented.
// ============================================================================
typedef void (*hal_timer_callback_t)(void* context);
int hal_timer_oneshot(uint32_t us, hal_timer_callback_t callback, void* context);

// ============================================================================
// BUSY-WAIT DELAY (для hardware init, до запуска IRQ)
// ============================================================================

// Busy-wait задержка в микросекундах.
// НЕ использует IRQ. Безопасно до hal_timer_init().
// x86: PIT Channel 2 (speaker port) или calibrated loop
// ARM: BCM2835 System Timer CLO polling
void hal_timer_delay_us(uint32_t us);

// Busy-wait задержка в миллисекундах.
void hal_timer_delay_ms(uint32_t ms);

#endif // HAL_TIMER_H