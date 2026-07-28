// ============================================================================
// arm_user.c — ARM User Mode Setup (Days 43-45)
// ============================================================================
// Responsibilities:
//   - map temporary user code/data sections
//   - enforce W^X at section level
//   - copy raw user test image into user code region
//   - prepare user data region
// ============================================================================

#include <stdint.h>
#include "config.h"
#include "hal/hal_uart.h"

// ============================================================================
// USER TEST IMAGE (defined in arm_user_asm.S)
// ============================================================================

extern uint32_t _user_test_start[];
extern uint32_t _user_test_end[];

// ============================================================================
// TLB INVALIDATION
// ============================================================================

static void arm_tlb_invalidate_all(void)
{
    uint32_t zero = 0;

    // DSB
    __asm__ volatile ("mcr p15, 0, %0, c7, c10, 4" :: "r"(zero) : "memory");

    // Invalidate entire unified TLB
    __asm__ volatile ("mcr p15, 0, %0, c8, c7, 0" :: "r"(zero) : "memory");

    // DSB
    __asm__ volatile ("mcr p15, 0, %0, c7, c10, 4" :: "r"(zero) : "memory");

    // ISB
    __asm__ volatile ("mcr p15, 0, %0, c7, c5, 4" :: "r"(zero) : "memory");
}

// ============================================================================
// MAP USER SECTIONS
// ============================================================================
// Spike mapping:
//
//   VA 0x00100000 -> PA 0x00200000, user RX
//   VA 0x00200000 -> PA 0x00300000, user RW + XN
//
// We read TTBR0 base from CP15 and patch first-level section descriptors.
// This avoids dependency on boot page table symbol name.
// ============================================================================

static void arm_map_user_sections(void)
{
    uint32_t ttbr0;

    __asm__ volatile ("mrc p15, 0, %0, c2, c0, 0" : "=r"(ttbr0));

    // TTBR0 base is bits [31:14].
    volatile uint32_t *tt = (volatile uint32_t *)(ttbr0 & 0xFFFFC000u);

    uint32_t code_index = ARM_USER_CODE_VADDR >> 20;
    uint32_t data_index = ARM_USER_DATA_VADDR >> 20;

    tt[code_index] = ARM_USER_CODE_PADDR | ARM_SECTION_USER_RX;
    tt[data_index] = ARM_USER_DATA_PADDR | ARM_SECTION_USER_RWXN;

    arm_tlb_invalidate_all();

    hal_uart_puts("[MMU] User sections mapped:\r\n");
    hal_uart_puts("      code: VA 0x00100000 -> PA 0x00200000 (RX)\r\n");
    hal_uart_puts("      data: VA 0x00200000 -> PA 0x00300000 (RW, XN)\r\n");
}

// ============================================================================
// COPY USER CODE
// ============================================================================

static void arm_copy_user_code(void)
{
    volatile uint32_t *dst = (volatile uint32_t *)ARM_USER_CODE_VADDR;
    uint32_t *src = _user_test_start;

    while (src < _user_test_end) {
        *dst++ = *src++;
    }

    hal_uart_puts("[USER] Test image copied to VA 0x00100000\r\n");
}

// ============================================================================
// PREPARE USER DATA
// ============================================================================

static void arm_prepare_user_data(void)
{
    static const char msg[] = "Hello ARM user\r\n";

    volatile char *dst = (volatile char *)ARM_USER_DATA_VADDR;

    // Message length is 16 bytes:
    //   "Hello ARM user\r\n"
    //
    // Do not copy terminating NUL into the 16-byte test buffer.
    for (int i = 0; i < 16; i++) {
        dst[i] = msg[i];
    }

    hal_uart_puts("[USER] Data region prepared at VA 0x00200000\r\n");
}

// ============================================================================
// PUBLIC INIT
// ============================================================================

void arm_user_setup(void)
{
    arm_map_user_sections();
    arm_copy_user_code();
    arm_prepare_user_data();
}