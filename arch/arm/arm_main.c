// ============================================================================
// arm_main.c — Enclave OS ARM Kernel (Days 38-40: IRQ + Timer)
// ============================================================================

#include <stdint.h>
#include "config.h"
#include "hal/hal_uart.h"
#include "hal/hal_cpu.h"
#include "hal/hal_irq.h"
#include "hal/hal_timer.h"

// ============================================================================
// Diagnostic counters (временно, для верификации)
// ============================================================================
extern volatile uint32_t diag_irq_entry_count;
extern volatile uint32_t diag_timer_handler_count;

// ============================================================================
// HELPERS
// ============================================================================

static void uart_hex32(uint32_t val)
{
    char hex[9];
    for (int i = 7; i >= 0; i--) {
        hex[i] = "0123456789ABCDEF"[val & 0xF];
        val >>= 4;
    }
    hex[8] = '\0';
    hal_uart_puts(hex);
}

static void uart_dec(uint32_t val)
{
    char buf[12];
    int i = 11;
    buf[i] = '\0';
    if (val == 0) { hal_uart_puts("0"); return; }
    while (val > 0 && i > 0) {
        buf[--i] = '0' + (char)(val % 10);
        val /= 10;
    }
    hal_uart_puts(&buf[i]);
}

// ============================================================================
// KERNEL MAIN (ARM)
// ============================================================================

void arm_kernel_main(uint32_t atags_addr, uint32_t machine_type)
{
    (void)atags_addr;
    (void)machine_type;

    // ------------------------------------------------------------------
    // 1. UART
    // ------------------------------------------------------------------
    hal_uart_init(115200);

    hal_uart_puts("\r\n");
    hal_uart_puts("========================================\r\n");
    hal_uart_puts("  Enclave OS — ARM Port\r\n");
    hal_uart_puts("  BCM2835 / ARM1176JZF-S / ARMv6\r\n");
    hal_uart_puts("  Alpha 0.6-arm-spike\r\n");
    hal_uart_puts("========================================\r\n");
    hal_uart_puts("\r\n");

    // ------------------------------------------------------------------
    // 2. IRQ + Timer init
    // ------------------------------------------------------------------
    hal_uart_puts("[INIT] hal_irq_init()...\r\n");
    hal_irq_init();
    hal_uart_puts("[OK]   IRQ controller ready\r\n");

    hal_uart_puts("[INIT] hal_timer_init(1000)...\r\n");
    hal_timer_init(1000);
    hal_uart_puts("[OK]   System Timer: 1000 Hz, IRQ line 1\r\n");

    // ------------------------------------------------------------------
    // 3. Enable IRQ
    // ------------------------------------------------------------------
    hal_uart_puts("[INIT] Enabling IRQ...\r\n");
    hal_irq_enable();
    hal_uart_puts("[OK]   IRQ enabled.\r\n");
    hal_uart_puts("\r\n");

    // ------------------------------------------------------------------
    // 4. Uptime loop (busy-wait, no WFI)
    // ------------------------------------------------------------------
    hal_uart_puts("[TEST] Uptime counter:\r\n");

    uint32_t uptime_sec = 0;
    uint64_t last_ticks = 0;

    for (;;) {
        uint64_t ticks = hal_timer_get_ticks();

        if (ticks - last_ticks >= 1000) {
            last_ticks += 1000;
            uptime_sec++;

            hal_uart_puts("  uptime: ");
            uart_dec(uptime_sec);
            hal_uart_puts("s  ticks: ");
            uart_hex32((uint32_t)(ticks & 0xFFFFFFFF));
            hal_uart_puts("  irq: ");
            uart_dec(diag_irq_entry_count);
            hal_uart_puts("\r\n");
        }

        // Busy-wait. Timer IRQ fires every 1ms (IRQ enabled globally).
        // No WFI — ARM1176/QEMU errata.
    }
}