#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

// ============================================================================
// DIP-3 FIX: Callback Pattern.
// Timer (L2) НЕ включает task.h (L6).
// Kernel (L7) инжектит schedule + wake_sleepers через callback.
// ============================================================================
typedef void (*timer_tick_callback_t)(uint32_t tick);

void timer_init(uint32_t freq);
void timer_set_tick_callback(timer_tick_callback_t cb);

uint32_t timer_get_ticks(void);
uint32_t timer_get_frequency(void);
void k_sleep(uint32_t ms);

#endif
