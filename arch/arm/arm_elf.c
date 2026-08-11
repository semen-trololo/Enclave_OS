// ============================================================================
// arm_elf.c — ARM ELF Loader (Day 54: Task 2.4)
// ============================================================================
// Zero Trust ELF parser and loader for ARM user processes.
//
// Responsibilities:
//   - Validate ELF header (magic, class, machine, type)
//   - Parse PT_LOAD program headers
//   - Enforce W^X at segment level (reject PF_W|PF_X)
//   - Validate segment addresses (must be in user space)
//   - Allocate physical pages via PMM
//   - Map pages with correct HAL_PAGE_* flags via VMM
//   - Copy segment data, zero BSS
//   - Allocate and map user stack
//   - Return entry point and stack pointer
// ============================================================================

#include <stdint.h>
#include <stddef.h>
#include "config.h"
#include "arm_elf.h"
#include "arm_pmm.h"
#include "hal/hal_mmu.h"
#include "hal/hal_uart.h"

// ============================================================================
// VALIDATION HELPERS
// ============================================================================

static int validate_elf_header(const Elf32_Ehdr *ehdr, size_t elf_size)
{
    // Check magic number
    if (ehdr->e_ident[0] != ELFMAG0 ||
        ehdr->e_ident[1] != ELFMAG1 ||
        ehdr->e_ident[2] != ELFMAG2 ||
        ehdr->e_ident[3] != ELFMAG3) {
        hal_uart_puts("[ELF] Invalid magic number\r\n");
        return -1;
    }

    // Check class (32-bit)
    if (ehdr->e_ident[4] != ELFCLASS32) {
        hal_uart_puts("[ELF] Not 32-bit ELF\r\n");
        return -1;
    }

    // Check data encoding (little-endian)
    if (ehdr->e_ident[5] != ELFDATA2LSB) {
        hal_uart_puts("[ELF] Not little-endian\r\n");
        return -1;
    }

    // Check version
    if (ehdr->e_version != EV_CURRENT) {
        hal_uart_puts("[ELF] Invalid version\r\n");
        return -1;
    }

    // Check type (executable)
    if (ehdr->e_type != ET_EXEC) {
        hal_uart_puts("[ELF] Not executable (ET_EXEC)\r\n");
        return -1;
    }

    // Check machine (ARM)
    if (ehdr->e_machine != EM_ARM) {
        hal_uart_puts("[ELF] Not ARM architecture\r\n");
        return -1;
    }

    // Check program header table
    if (ehdr->e_phoff == 0 || ehdr->e_phnum == 0) {
        hal_uart_puts("[ELF] No program headers\r\n");
        return -1;
    }

    // Check program header table fits in file
    if (ehdr->e_phoff + (ehdr->e_phnum * ehdr->e_phentsize) > elf_size) {
        hal_uart_puts("[ELF] Program headers out of bounds\r\n");
        return -1;
    }

    // Check program header entry size
    if (ehdr->e_phentsize != sizeof(Elf32_Phdr)) {
        hal_uart_puts("[ELF] Invalid program header size\r\n");
        return -1;
    }

    return 0;
}

static int validate_segment(const Elf32_Phdr *phdr, size_t elf_size)
{
    // Check segment type
    if (phdr->p_type != PT_LOAD) {
        // Skip non-LOAD segments
        return 1;
    }

    // W^X check: reject segments with both Write and Execute
    if ((phdr->p_flags & PF_W) && (phdr->p_flags & PF_X)) {
        hal_uart_puts("[ELF] W^X violation: segment has W|X\r\n");
        return -1;
    }

    // Check segment must be readable
    if (!(phdr->p_flags & PF_R)) {
        hal_uart_puts("[ELF] Segment not readable\r\n");
        return -1;
    }

    // Check segment size consistency
    if (phdr->p_filesz > phdr->p_memsz) {
        hal_uart_puts("[ELF] p_filesz > p_memsz\r\n");
        return -1;
    }

    // Check segment file offset
    if (phdr->p_offset + phdr->p_filesz > elf_size) {
        hal_uart_puts("[ELF] Segment data out of bounds\r\n");
        return -1;
    }

    // Check segment virtual address is in user space
    if (phdr->p_vaddr >= KERNEL_SPACE_START) {
        hal_uart_puts("[ELF] Segment in kernel space\r\n");
        return -1;
    }

    // Check segment end is in user space
    uint32_t vaddr_end = phdr->p_vaddr + phdr->p_memsz;
    if (vaddr_end > USER_SPACE_END || vaddr_end < phdr->p_vaddr) {
        hal_uart_puts("[ELF] Segment exceeds user space\r\n");
        return -1;
    }

    // Check alignment (must be page-aligned)
    if (phdr->p_vaddr & 0xFFF) {
        hal_uart_puts("[ELF] Segment not page-aligned\r\n");
        return -1;
    }

    return 0;
}

// ============================================================================
// SEGMENT LOADING
// ============================================================================

static uint32_t segment_flags_to_hal(uint32_t p_flags)
{
    uint32_t flags = HAL_PAGE_PRESENT | HAL_PAGE_USER;

    if (p_flags & PF_W) {
        flags |= HAL_PAGE_WRITE;
    }

    if (p_flags & PF_X) {
        flags |= HAL_PAGE_EXEC;
    }

    return flags;
}

static int load_segment(uint32_t *space, const uint8_t *elf_image,
                        const Elf32_Phdr *phdr)
{
    uint32_t seg_va   = phdr->p_vaddr;
    uint32_t filesz   = phdr->p_filesz;
    uint32_t memsz    = phdr->p_memsz;
    uint32_t fileoff  = phdr->p_offset;
    uint32_t flags    = segment_flags_to_hal(phdr->p_flags);

    // Segment memory range.
    uint32_t seg_end = seg_va + memsz;

    // Page-aligned mapping range.
    uint32_t map_start = seg_va & ~0xFFFu;
    uint32_t map_end   = (seg_end + 0xFFFu) & ~0xFFFu;

    if (map_end <= map_start)
        return 0;

    uint32_t num_pages = (map_end - map_start) / 0x1000;

    hal_uart_puts("[ELF] Loading PT_LOAD segment\r\n");

    for (uint32_t page = 0; page < num_pages; page++) {
        uint32_t page_va = map_start + (page * 0x1000);

        // Allocate physical page.
        uint32_t page_pa = arm_pmm_alloc_page();
        if (page_pa == 0) {
            hal_uart_puts("[ELF] OOM: cannot allocate segment page\r\n");
            return -1;
        }

        // Map page into target address space.
        if (hal_mmu_map_page_in_space(space, page_va, page_pa, flags) < 0) {
            hal_uart_puts("[ELF] Failed to map segment page\r\n");
            arm_pmm_free_page(page_pa);
            return -1;
        }

        // Access page through kernel direct map.
        volatile uint8_t *page_virt = (volatile uint8_t *)PHYS_TO_VIRT(page_pa);

        // Zero whole page first. This handles BSS and padding.
        for (uint32_t i = 0; i < 0x1000; i++) {
            page_virt[i] = 0;
        }

        // Copy file-backed part of segment.
        //
        // Segment file data covers virtual addresses:
        //   [seg_va, seg_va + filesz)
        //
        // Current page covers:
        //   [page_va, page_va + 0x1000)
        //
        // We copy intersection of these two ranges.

        uint32_t file_data_end = seg_va + filesz;

        uint32_t copy_start = (page_va > seg_va) ? page_va : seg_va;

        uint32_t page_end = page_va + 0x1000;
        uint32_t copy_end = (page_end < file_data_end) ? page_end : file_data_end;

        if (copy_start < copy_end) {
            uint32_t src_offset = fileoff + (copy_start - seg_va);
            uint32_t dst_offset = copy_start - page_va;
            uint32_t copy_size  = copy_end - copy_start;

            const uint8_t *src = elf_image + src_offset;
            volatile uint8_t *dst = page_virt + dst_offset;

            for (uint32_t i = 0; i < copy_size; i++) {
                dst[i] = src[i];
            }
        }
    }

    return 0;
}

// ============================================================================
// STACK ALLOCATION
// ============================================================================

static int allocate_user_stack(uint32_t *space, uint32_t *stack_top)
{
    // Allocate stack pages (USER_STACK_SIZE = 64 KB = 16 pages)
    uint32_t stack_pages = USER_STACK_SIZE / 0x1000;
    uint32_t stack_vaddr = USER_STACK_VIRT_TOP - USER_STACK_SIZE;

    for (uint32_t page = 0; page < stack_pages; page++) {
        uint32_t page_vaddr = stack_vaddr + (page * 0x1000);
        
        uint32_t page_paddr = arm_pmm_alloc_page();
        if (page_paddr == 0) {
            hal_uart_puts("[ELF] OOM: cannot allocate stack page\r\n");
            return -1;
        }

        // Stack: RW, no execute
        if (hal_mmu_map_page_in_space(space, page_vaddr, page_paddr, 
                                       HAL_PAGE_USER_DATA) < 0) {
            hal_uart_puts("[ELF] Failed to map stack page\r\n");
            arm_pmm_free_page(page_paddr);
            return -1;
        }

        // Zero stack page
        volatile uint8_t *page_virt = (volatile uint8_t *)PHYS_TO_VIRT(page_paddr);
        for (uint32_t i = 0; i < 4096; i++) {
            page_virt[i] = 0;
        }
    }

    *stack_top = USER_STACK_VIRT_TOP;
    return 0;
}

// ============================================================================
// MAIN ELF LOADER
// ============================================================================

int arm_elf_load(const uint8_t *elf_image, size_t elf_size,
                 arm_elf_load_result_t *result)
{
    hal_uart_puts("[ELF] Loading ARM ELF binary...\r\n");

    // Validate ELF header
    if (elf_size < sizeof(Elf32_Ehdr)) {
        hal_uart_puts("[ELF] File too small for ELF header\r\n");
        return -1;
    }

    const Elf32_Ehdr *ehdr = (const Elf32_Ehdr *)elf_image;
    if (validate_elf_header(ehdr, elf_size) < 0) {
        return -1;
    }

    hal_uart_puts("[ELF] Header validated\r\n");

    // Create new address space
    uint32_t *space = hal_mmu_create_space();
    if (!space) {
        hal_uart_puts("[ELF] OOM: cannot create address space\r\n");
        return -1;
    }

    // Get program headers
    const Elf32_Phdr *phdr_table = (const Elf32_Phdr *)(elf_image + ehdr->e_phoff);

    // Load each PT_LOAD segment
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        const Elf32_Phdr *phdr = &phdr_table[i];

        int ret = validate_segment(phdr, elf_size);
        if (ret < 0) {
            hal_uart_puts("[ELF] Segment validation failed\r\n");
            hal_mmu_destroy_space(space);
            return -1;
        }

        if (ret == 1) {
            // Skip non-LOAD segment
            continue;
        }

        if (load_segment(space, elf_image, phdr) < 0) {
            hal_uart_puts("[ELF] Failed to load segment\r\n");
            hal_mmu_destroy_space(space);
            return -1;
        }
    }

    // Allocate user stack
    uint32_t stack_top = 0;
    if (allocate_user_stack(space, &stack_top) < 0) {
        hal_uart_puts("[ELF] Failed to allocate stack\r\n");
        hal_mmu_destroy_space(space);
        return -1;
    }

    // Fill result structure
    result->ttbr0_phys = VIRT_TO_PHYS((uint32_t)space);
    result->entry_point = ehdr->e_entry;
    result->user_sp = stack_top;

    hal_uart_puts("[ELF] ELF loaded successfully\r\n");
    hal_uart_puts("[ELF] Entry point: 0x");
    
    char buf[9];
    uint32_t tmp = result->entry_point;
    for (int i = 7; i >= 0; i--) {
        buf[i] = "0123456789ABCDEF"[tmp & 0xF];
        tmp >>= 4;
    }
    buf[8] = '\0';
    hal_uart_puts(buf);
    hal_uart_puts("\r\n");

    return 0;
}
