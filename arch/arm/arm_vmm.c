// ============================================================================
// arm_vmm.c — ARM Virtual Memory Manager (Day 52)
// ============================================================================
// Реализует hal_mmu.h контракт для ARMv6 (ARM1176JZF-S).
//
// Two-level page tables (ARMv6 short descriptor):
//   L1: 4096 entries × 4B = 16 KB (coarse → L2, or section → 1 MB)
//   L2:  256 entries × 4B =  1 KB (small page → 4 KB)
//
// Kernel space (entries 3072-4095) копируется из boot TTBR0 в каждый
// новый address space. Это обеспечивает shared kernel mapping.
//
// ⚠️ Day 52 spike: L1 tables из статического пула (8 max).
//    Позже: dynamic L1 allocation через PMM contiguous allocator.
// ============================================================================

#include <stdint.h>
#include "config.h"
#include "arm_pmm.h"
#include "arm_vmm.h"
#include "hal/hal_mmu.h"
#include "hal/hal_cpu.h"
#include "hal/hal_uart.h"

// ============================================================================
// STATIC L1 TABLE POOL (Day 52 spike)
// ============================================================================

typedef struct {
    uint32_t entries[ARM_TTBR0_ENTRIES];
} __attribute__((aligned(ARM_TTBR0_ALIGN))) l1_table_t;

static l1_table_t vmm_l1_pool[ARM_VMM_MAX_SPACES];
static uint8_t vmm_l1_used[ARM_VMM_MAX_SPACES];

// Boot TTBR0 physical address (cached at init).
static uint32_t vmm_boot_ttbr0_phys = 0;

// ============================================================================
// TLB OPERATIONS
// ============================================================================

void hal_mmu_flush_tlb_all(void)
{
    uint32_t zero = 0;
    __asm__ volatile ("mcr p15, 0, %0, c7, c10, 4" :: "r"(zero) : "memory"); // DSB
    __asm__ volatile ("mcr p15, 0, %0, c8, c7, 0" :: "r"(zero) : "memory"); // TLBIALL
    __asm__ volatile ("mcr p15, 0, %0, c7, c10, 4" :: "r"(zero) : "memory"); // DSB
    __asm__ volatile ("mcr p15, 0, %0, c7, c5, 4"  :: "r"(zero) : "memory"); // ISB
}

void hal_mmu_flush_tlb_entry(uint32_t virt)
{
    uint32_t zero = 0;
    __asm__ volatile ("mcr p15, 0, %0, c7, c10, 4" :: "r"(zero) : "memory"); // DSB
    __asm__ volatile ("mcr p15, 0, %0, c8, c7, 1" :: "r"(virt) : "memory"); // TLBIMVA
    __asm__ volatile ("mcr p15, 0, %0, c7, c10, 4" :: "r"(zero) : "memory"); // DSB
    __asm__ volatile ("mcr p15, 0, %0, c7, c5, 4"  :: "r"(zero) : "memory"); // ISB
}

// ============================================================================
// FLAGS TRANSLATION: HAL_PAGE_* → ARM L2 descriptor bits
// ============================================================================

static uint32_t hal_flags_to_l2(uint32_t flags)
{
    uint32_t desc = ARM_L2_TYPE_SMALL_PAGE; // 0x02

    // Memory type
    if (flags & HAL_PAGE_NOCACHE) {
        // Device: TEX=0, C=0, B=1
        desc |= ARM_L2_SMALL_B;
    } else {
        // Normal WB WA: TEX=1, C=1, B=1
        desc |= ARM_L2_SMALL_TEX(1) | ARM_L2_SMALL_C | ARM_L2_SMALL_B;
    }

    // Access permissions
    if (flags & HAL_PAGE_USER) {
        if (flags & HAL_PAGE_WRITE)
            desc |= ARM_L2_SMALL_AP(ARM_AP_KERNEL_RW_USER_RW); // AP=11
        else
            desc |= ARM_L2_SMALL_AP(ARM_AP_KERNEL_RW_USER_RO); // AP=10
    } else {
        desc |= ARM_L2_SMALL_AP(ARM_AP_KERNEL_RW); // AP=01
    }

    // XN (Execute-Never): type 0b10 → 0b11
    if (!(flags & HAL_PAGE_EXEC))
        desc |= ARM_L2_SMALL_XN;

    // Not-Global
    if (!(flags & HAL_PAGE_GLOBAL))
        desc |= ARM_L2_SMALL_NG;

    return desc;
}

// ============================================================================
// L1 ENTRY HELPERS
// ============================================================================

static inline uint32_t l1_type(uint32_t entry)
{
    return entry & 0x3;
}

static inline uint32_t l2_phys_from_l1(uint32_t l1_entry)
{
    return l1_entry & 0xFFFFFC00u;
}

static inline uint32_t l2_index(uint32_t va)
{
    return (va >> 12) & 0xFF;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void hal_mmu_init(void)
{
    arm_vmm_init();
}

void arm_vmm_init(void)
{
    // Read current TTBR0 (set by arm_boot.S)
    uint32_t ttbr0;
    __asm__ volatile ("mrc p15, 0, %0, c2, c0, 0" : "=r"(ttbr0));
    vmm_boot_ttbr0_phys = ttbr0 & 0xFFFFC000u;

    for (int i = 0; i < ARM_VMM_MAX_SPACES; i++)
        vmm_l1_used[i] = 0;

    hal_uart_puts("[VMM] ARM VMM initialized, boot TTBR0=0x");
    char buf[9];
    for (int j = 7; j >= 0; j--) {
        buf[j] = "0123456789ABCDEF"[vmm_boot_ttbr0_phys & 0xF];
        vmm_boot_ttbr0_phys >>= 4;
    }
    buf[8] = '\0';
    hal_uart_puts(buf);
    hal_uart_puts("\r\n");

    // Restore cached value (shifted above)
    __asm__ volatile ("mrc p15, 0, %0, c2, c0, 0" : "=r"(ttbr0));
    vmm_boot_ttbr0_phys = ttbr0 & 0xFFFFC000u;
}

uint32_t arm_vmm_get_boot_ttbr0(void)
{
    return vmm_boot_ttbr0_phys;
}

// ============================================================================
// CREATE ADDRESS SPACE
// ============================================================================

uint32_t *hal_mmu_create_space(void)
{
    // Find free slot in L1 pool
    int slot = -1;
    for (int i = 0; i < ARM_VMM_MAX_SPACES; i++) {
        if (!vmm_l1_used[i]) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        hal_uart_puts("[VMM] OOM: no free L1 slots\r\n");
        return (uint32_t *)0;
    }

    vmm_l1_used[slot] = 1;

    uint32_t *l1 = vmm_l1_pool[slot].entries;

    // Zero entire L1 table
    for (int i = 0; i < ARM_TTBR0_ENTRIES; i++)
        l1[i] = ARM_L1_TYPE_FAULT;

    // Copy kernel entries (3072-4095) from boot TTBR0
    volatile uint32_t *boot_l1 = (volatile uint32_t *)vmm_boot_ttbr0_phys;
    for (int i = 3072; i < 4096; i++)
        l1[i] = boot_l1[i];

    hal_uart_puts("[VMM] Created address space slot=");
    char buf[4];
    buf[0] = '0' + slot;
    buf[1] = '\r';
    buf[2] = '\n';
    buf[3] = '\0';
    hal_uart_puts(buf);

    return l1;
}

// ============================================================================
// DESTROY ADDRESS SPACE
// ============================================================================

void hal_mmu_destroy_space(uint32_t *space_virt)
{
    if (!space_virt) return;

    // Find slot
    int slot = -1;
    for (int i = 0; i < ARM_VMM_MAX_SPACES; i++) {
        if (vmm_l1_pool[i].entries == space_virt) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        hal_uart_puts("[VMM] destroy_space: unknown L1 address\r\n");
        return;
    }

    // Free all L2 tables (user entries 0-3071)
    for (int i = 0; i < 3072; i++) {
        if (l1_type(space_virt[i]) == ARM_L1_TYPE_COARSE) {
            uint32_t l2_pa = l2_phys_from_l1(space_virt[i]);
            arm_pmm_free_page(l2_pa);
        }
    }

    vmm_l1_used[slot] = 0;
    hal_uart_puts("[VMM] Destroyed address space slot=");
    char buf[4];
    buf[0] = '0' + slot;
    buf[1] = '\r';
    buf[2] = '\n';
    buf[3] = '\0';
    hal_uart_puts(buf);
}

// ============================================================================
// SWITCH ADDRESS SPACE
// ============================================================================

void hal_mmu_switch_space(uint32_t phys_space)
{
    __asm__ volatile ("mcr p15, 0, %0, c2, c0, 0" :: "r"(phys_space) : "memory");
    uint32_t zero = 0;
    __asm__ volatile ("mcr p15, 0, %0, c7, c5, 4" :: "r"(zero) : "memory"); // ISB
    hal_mmu_flush_tlb_all();
}

// ============================================================================
// MAP PAGE IN SPACE
// ============================================================================

int hal_mmu_map_page_in_space(uint32_t *space, uint32_t virt,
                              uint32_t phys, uint32_t flags)
{
    if (!space) return -1;
    if (virt & 0xFFF) return -1;  // Not 4 KB aligned
    if (phys & 0xFFF) return -1;

    // W^X check
    if (!hal_mmu_check_wx(flags)) {
        hal_uart_puts("[VMM] W^X violation rejected\r\n");
        return -1;
    }

    uint32_t l1_idx = virt >> 20;

    // Kernel space entries are read-only (shared from boot TTBR0)
    if (l1_idx >= 3072) {
        hal_uart_puts("[VMM] Cannot map into kernel space via VMM\r\n");
        return -1;
    }

    // Ensure L2 table exists
    if (l1_type(space[l1_idx]) == ARM_L1_TYPE_FAULT) {
        // Allocate L2 table from PMM
        uint32_t l2_pa = arm_pmm_alloc_page();
        if (l2_pa == 0) return -1;

        // Zero L2 table
        volatile uint32_t *l2 = (volatile uint32_t *)l2_pa;
        for (int i = 0; i < 256; i++)
            l2[i] = ARM_L2_TYPE_FAULT;

        // Write L1 coarse entry
        uint32_t domain = (flags & HAL_PAGE_USER) ? ARM_DOMAIN_USER
                                                  : ARM_DOMAIN_KERNEL;
        space[l1_idx] = (l2_pa & 0xFFFFFC00u)
                      | ARM_L1_SECTION_DOMAIN(domain)
                      | ARM_L1_TYPE_COARSE;
    }

    if (l1_type(space[l1_idx]) != ARM_L1_TYPE_COARSE) {
        hal_uart_puts("[VMM] L1 entry is not coarse (section conflict?)\r\n");
        return -1;
    }

    // Write L2 small page entry
    uint32_t l2_pa = l2_phys_from_l1(space[l1_idx]);
    volatile uint32_t *l2 = (volatile uint32_t *)l2_pa;
    uint32_t l2_idx = l2_index(virt);

    l2[l2_idx] = (phys & 0xFFFFF000u) | hal_flags_to_l2(flags);

    return 0;
}

// ============================================================================
// MAP PAGE (current space)
// ============================================================================

void hal_mmu_map_page(uint32_t virt, uint32_t phys, uint32_t flags)
{
    uint32_t ttbr0;
    __asm__ volatile ("mrc p15, 0, %0, c2, c0, 0" : "=r"(ttbr0));
    uint32_t *space = (uint32_t *)(ttbr0 & 0xFFFFC000u);

    hal_mmu_map_page_in_space(space, virt, phys, flags);
    hal_mmu_flush_tlb_entry(virt);
}

// ============================================================================
// UNMAP PAGE IN SPACE
// ============================================================================

void hal_mmu_unmap_page_in_space(uint32_t *space, uint32_t virt)
{
    if (!space) return;

    uint32_t l1_idx = virt >> 20;
    if (l1_idx >= 3072) return; // Kernel space

    if (l1_type(space[l1_idx]) != ARM_L1_TYPE_COARSE) return;

    uint32_t l2_pa = l2_phys_from_l1(space[l1_idx]);
    volatile uint32_t *l2 = (volatile uint32_t *)l2_pa;
    uint32_t l2_idx = l2_index(virt);

    l2[l2_idx] = ARM_L2_TYPE_FAULT;
}

// ============================================================================
// UNMAP PAGE (current space)
// ============================================================================

void hal_mmu_unmap_page(uint32_t virt)
{
    uint32_t ttbr0;
    __asm__ volatile ("mrc p15, 0, %0, c2, c0, 0" : "=r"(ttbr0));
    uint32_t *space = (uint32_t *)(ttbr0 & 0xFFFFC000u);

    hal_mmu_unmap_page_in_space(space, virt);
    hal_mmu_flush_tlb_entry(virt);
}

// ============================================================================
// PROTECT PAGE (stub — Day 53+)
// ============================================================================

void hal_mmu_protect_page(uint32_t virt, uint32_t flags)
{
    (void)virt;
    (void)flags;
    hal_uart_puts("[VMM] protect_page: stub (Day 53+)\r\n");
}

// ============================================================================
// CLONE SPACE (stub — Day 53+, CoW)
// ============================================================================

uint32_t *hal_mmu_clone_space(uint32_t *parent_space_virt)
{
    (void)parent_space_virt;
    hal_uart_puts("[VMM] clone_space: stub (Day 53+)\r\n");
    return (uint32_t *)0;
}

// ============================================================================
// KERNEL STACK ALLOCATOR (stub — Day 53+)
// ============================================================================

uint32_t hal_mmu_alloc_kernel_stack(void)
{
    hal_uart_puts("[VMM] alloc_kernel_stack: stub (Day 53+)\r\n");
    return 0;
}

void hal_mmu_free_kernel_stack(uint32_t stack_top)
{
    (void)stack_top;
    hal_uart_puts("[VMM] free_kernel_stack: stub (Day 53+)\r\n");
}

// ============================================================================
// FAULT INFO (ARMv6: DFAR + DFSR)
// ============================================================================

hal_fault_info_t hal_mmu_get_fault_info(uint32_t arch_error_code)
{
    hal_fault_info_t info;
    info.fault_addr = 0;
    info.fault_type = HAL_FAULT_READ;
    info.is_write = 0;
    info.is_exec = 0;
    info.is_user = 0;
    info.is_present = 0;

    // Read DFAR (Data Fault Address Register)
    uint32_t far;
    __asm__ volatile ("mrc p15, 0, %0, c6, c0, 0" : "=r"(far));
    info.fault_addr = far;

    // DFSR: bit [11] = WnR (1 = write)
    info.is_write = (arch_error_code >> 11) & 1;
    info.fault_type = info.is_write ? HAL_FAULT_WRITE : HAL_FAULT_READ;

    // DFSR: FS[3:0] bits [3:0], FS[4] bit [10]
    // FS = 0b00101 = Translation fault (section) → not present
    // FS = 0b00111 = Translation fault (page) → not present
    // FS = 0b01101 = Permission fault (section) → present
    // FS = 0b01111 = Permission fault (page) → present
    uint32_t fs = (arch_error_code & 0xF) | (((arch_error_code >> 10) & 1) << 4);
    if (fs == 0x05 || fs == 0x07)
        info.is_present = 0; // Translation fault
    else if (fs == 0x0D || fs == 0x0F)
        info.is_present = 1; // Permission fault

    return info;
}
