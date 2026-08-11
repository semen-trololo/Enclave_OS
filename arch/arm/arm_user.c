// ============================================================================
// arm_user.c — ARM User Mode Setup (Day 54: ELF Loader Integration)
// ============================================================================
// Responsibilities:
//   - Load ARM ELF binary via arm_elf_load()
//   - Setup isolated address space
//   - Return physical address of L1 table and entry point
// ============================================================================

#include <stdint.h>
#include "config.h"
#include "hal/hal_uart.h"
#include "hal/hal_mmu.h"
#include "arm_pmm.h"
#include "arm_elf.h"

// ============================================================================
// EMBEDDED ELF IMAGE (defined in arm_user_asm.S)
// ============================================================================

extern uint8_t _user_elf_start[];
extern uint8_t _user_elf_end[];

// ============================================================================
// CREATE ISOLATED ADDRESS SPACE AND LOAD ELF
// ============================================================================

uint32_t arm_user_create_space_and_load_image(void)
{
    hal_uart_puts("[USER] Loading ELF binary...\r\n");

    arm_elf_load_result_t result;
    
    size_t elf_size = (size_t)(_user_elf_end - _user_elf_start);
    
    if (arm_elf_load(_user_elf_start, elf_size, &result) < 0) {
        hal_uart_puts("[USER] ELF load failed\r\n");
        return 0;
    }

    hal_uart_puts("[USER] ELF loaded, entry=0x");
    char buf[9];
    uint32_t tmp = result.entry_point;
    for (int i = 7; i >= 0; i--) {
        buf[i] = "0123456789ABCDEF"[tmp & 0xF];
        tmp >>= 4;
    }
    buf[8] = '\0';
    hal_uart_puts(buf);
    hal_uart_puts("\r\n");

    // Store entry point and stack pointer for task creation
    // These will be used by arm_task_create_user()
    extern uint32_t g_user_entry_point;
    extern uint32_t g_user_stack_top;
    
    g_user_entry_point = result.entry_point;
    g_user_stack_top = result.user_sp;

    return result.ttbr0_phys;
}
