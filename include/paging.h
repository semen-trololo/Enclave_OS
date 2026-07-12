#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

// ============================================================================
// GLOBAL SYMBOLS FROM BOOT.ASM
// ============================================================================
// Корневой Page Directory ядра, созданный на этапе бутстрапа.
// Используется в task.c для создания main_task и в paging.c для клонирования.
extern uint32_t boot_page_directory[];

// ============================================================================
// SINGLE SOURCE OF TRUTH (SSOT) MACROS
// ============================================================================
#ifndef KERNEL_SPACE_START
#define KERNEL_SPACE_START 0xC0000000
#endif

#define VIRT_TO_PHYS(addr) (((uint32_t)(addr) >= KERNEL_SPACE_START) ? ((uint32_t)(addr) - KERNEL_SPACE_START) : (uint32_t)(addr))
#define PHYS_TO_VIRT(addr) ((uint32_t)(addr) + KERNEL_SPACE_START)

// ============================================================================
// PAGE TABLE FLAGS (x86 Architecture)
// ============================================================================
#define PAGE_PRESENT    0x001
#define PAGE_WRITE      0x002
#define PAGE_USER       0x004
#define PAGE_PWT        0x008
#define PAGE_PCD        0x010
#define PAGE_ACCESSED   0x020
#define PAGE_DIRTY      0x040
#define PAGE_PS         0x080  // Page Size Extension (4MB pages)
#define PAGE_GLOBAL     0x100

// ============================================================================
// VMM API
// ============================================================================
struct regs; // Forward declaration for ISR

void paging_init(void);
void page_fault_handler(struct regs* r);

void vmm_map_page(uint32_t virt, uint32_t phys, uint32_t flags);
int vmm_map_page_in_pd(uint32_t* pd_virt, uint32_t virt, uint32_t phys, uint32_t flags);
void vmm_unmap_page_in_pd(uint32_t* pd_virt, uint32_t virt);

uint32_t* vmm_create_address_space(void);
void vmm_switch_pdir(uint32_t phys_pd);
void vmm_destroy_address_space(uint32_t* pdir_virt);

// Освобождает физическую страницу и убирает маппинг (для sys_munmap)
void vmm_unmap_and_free_page_in_pd(uint32_t* pd_virt, uint32_t virt);

// Изменяет флаги прав доступа в PTE без изменения физического адреса (для sys_mprotect)
void vmm_protect_page_in_pd(uint32_t* pd_virt, uint32_t virt, uint32_t flags);

#endif // PAGING_H