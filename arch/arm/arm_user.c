// ============================================================================
// arm_user.c — ARM User Mode Setup (Day 52: 4KB Pages + Per-Process Isolation)
// ============================================================================
// Responsibilities:
//   - allocate fresh L1 address space via VMM
//   - allocate 4KB physical pages for code and data via PMM
//   - map pages with strict W^X permissions
//   - copy raw user test image into isolated code region
// ============================================================================

#include <stdint.h>
#include "config.h"
#include "hal/hal_uart.h"
#include "hal/hal_mmu.h"
#include "arm_pmm.h"

// ============================================================================
// USER TEST IMAGE (defined in arm_user_asm.S)
// ============================================================================

extern uint32_t _user_test_start[];
extern uint32_t _user_test_end[];

// ============================================================================
// CREATE ISOLATED ADDRESS SPACE
// ============================================================================
// Allocates L1 table, 4KB code page, 4KB data page.
// Returns PHYSICAL address of L1 table (to be stored in task->ttbr0_phys).
// Returns 0 on OOM.
// ============================================================================

uint32_t arm_user_create_space_and_load_image(void)
{
    // 1. Allocate fresh L1 table (copies kernel entries automatically)
    uint32_t *l1_virt = hal_mmu_create_space();
    if (!l1_virt) {
        hal_uart_puts("[USER] OOM: no L1 space\r\n");
        return 0;
    }

    // 2. Allocate physical 4KB pages
    uint32_t code_pa = arm_pmm_alloc_page();
    uint32_t data_pa = arm_pmm_alloc_page();

    if (!code_pa || !data_pa) {
        hal_uart_puts("[USER] OOM: no physical pages\r\n");
        // Note: L1 table leaked here, proper teardown needed in production
        return 0;
    }

    // 3. Map pages with strict W^X permissions
    hal_mmu_map_page_in_space(l1_virt, ARM_USER_CODE_VA_4K, code_pa, HAL_PAGE_USER_CODE);
    hal_mmu_map_page_in_space(l1_virt, ARM_USER_DATA_VA_4K, data_pa, HAL_PAGE_USER_DATA);

    // 4. Copy user test image
    // Identity mapping is torn down, so we MUST use PHYS_TO_VIRT for destination.
    uint32_t *src = _user_test_start;
    volatile uint32_t *dst = (volatile uint32_t *)PHYS_TO_VIRT(code_pa);
    while (src < _user_test_end) {
        *dst++ = *src++;
    }

    // 5. Prepare user data region
    volatile char *data_virt = (volatile char *)PHYS_TO_VIRT(data_pa);
    static const char msg[] = "Hello ARM user\r\n";
    for (int i = 0; i < 16; i++) {
        data_virt[i] = msg[i];
    }

    hal_uart_puts("[USER] Isolated 4KB address space created\r\n");

    // Return physical address of L1 table for TTBR0
    return VIRT_TO_PHYS((uint32_t)l1_virt);
}