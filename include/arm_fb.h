#ifndef ARM_FB_H
#define ARM_FB_H

// ============================================================================
// ARM Framebuffer Driver (Day 55 video spike)
// ============================================================================
// Минимальный BCM2835 framebuffer driver для Enclave OS ARM port.
//
// Назначение:
//   - запросить framebuffer у GPU через mailbox property interface
//   - зарезервировать физическую память в PMM
//   - замапить framebuffer в kernel virtual space
//   - дать kernel early console / test pattern
//
// Ограничения spike:
//   - 32 bpp
//   - 640x480
//   - kernel-only access
//   - без VFS /dev/fb0
//   - без user-mode access
// ============================================================================

#include <stdint.h>

// Инициализировать framebuffer через BCM2835 mailbox.
// Возвращает 0 при успехе, -1 при ошибке.
// При ошибке система продолжает работать через UART.
int arm_fb_init(void);

// Нарисовать тестовые цветные полосы.
// Используется для проверки, что framebuffer реально пишется.
void arm_fb_test_pattern(void);

// Залить framebuffer одним цветом.
// color формат: 0x00RRGGBB для 32bpp.
void arm_fb_fill(uint32_t color);

// 1 если framebuffer готов, 0 если видео недоступно.
uint32_t arm_fb_is_ready(void);

#endif // ARM_FB_H
