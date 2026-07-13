#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>
#include "config.h" // ✅ SSOT: Подключаем для макросов VIRT_TO_PHYS / PHYS_TO_VIRT

// ============================================================================
// GLOBAL SYMBOLS FROM BOOT.ASM
// ============================================================================
// Корневой Page Directory ядра, созданный на этапе бутстрапа.
// Используется в task.c для создания main_task и в paging.c для клонирования.
extern uint32_t boot_page_directory[];

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
// [ДЕНЬ 14] OS-SPECIFIC PAGE TABLE FLAGS
// ============================================================================
// 9-й бит (Available для ОС) используется для маркировки Copy-on-Write страниц.
// Когда страница помечена как CoW, бит PAGE_WRITE снят (аппаратная защита),
// а PAGE_COW установлен. При попытке записи возникает Page Fault, и ядро
// выделяет личную копию страницы.
#define PAGE_COW        0x200  // Copy-on-Write marker

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

// ============================================================================
// [ДЕНЬ 14] ADDRESS SPACE CLONING (Copy-on-Write)
// ============================================================================
// Клонирует адресное пространство родителя для ребенка при fork().
// Все User Space страницы помечаются как READ-ONLY + PAGE_COW.
// Физические страницы НЕ копируются (только увеличивается refcount).
// Возвращает виртуальный адрес нового Page Directory или NULL при OOM.
uint32_t* vmm_clone_address_space(uint32_t* parent_pd_virt);

#endif // PAGING_H
