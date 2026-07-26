#ifndef HAL_MMU_H
#define HAL_MMU_H

// ============================================================================
// HAL MMU — Hardware Abstraction Layer: Memory Management Unit
// ============================================================================
// Контракт для arch/x86/ и arch/arm/ реализаций.
//
// x86: 2-level Page Directory (CR3), 4KB pages, PAGE_* flags
// ARM: ARMv6 Translation Tables (TTBR0/TTBR1), Section/Page, AP bits
//
// Portable kernel code использует HAL_PAGE_* флаги.
// Arch-specific код транслирует их в аппаратные биты.
// ============================================================================

#include <stdint.h>
#include "config.h"

// ============================================================================
// ABSTRACT PAGE FLAGS (Portable)
// ============================================================================
// Эти флаги НЕ являются аппаратными. Каждая arch-реализация
// транслирует их в x86 PTE bits или ARM L1/L2 descriptor bits.
//
// ⚠️ x86: маппинг на PAGE_PRESENT|PAGE_WRITE|PAGE_USER|... из paging.h
// ⚠️ ARM: маппинг на AP[2:0], XN, Domain, TEX/C/B
// ============================================================================

#define HAL_PAGE_PRESENT        (1 << 0)   // Страница замаплена
#define HAL_PAGE_WRITE          (1 << 1)   // Разрешена запись
#define HAL_PAGE_USER           (1 << 2)   // Доступна из Ring 3 / User Mode
#define HAL_PAGE_EXEC           (1 << 3)   // Разрешено исполнение (XN=0 на ARM)
#define HAL_PAGE_NOCACHE        (1 << 4)   // Non-cacheable (PCD на x86, Device на ARM)
#define HAL_PAGE_WRITETHROUGH   (1 << 5)   // Write-through (PWT на x86)
#define HAL_PAGE_GLOBAL         (1 << 6)   // Global (не инвалидируется при CR3/TTBR switch)
#define HAL_PAGE_COW            (1 << 7)   // Copy-on-Write (OS-specific, не аппаратный)

// ============================================================================
// COMPOSITE FLAGS (удобные пресеты)
// ============================================================================

// Kernel code: Present + Exec (RO, no user)
#define HAL_PAGE_KERNEL_CODE    (HAL_PAGE_PRESENT | HAL_PAGE_EXEC | HAL_PAGE_GLOBAL)

// Kernel data: Present + Write (no exec, no user)
#define HAL_PAGE_KERNEL_DATA    (HAL_PAGE_PRESENT | HAL_PAGE_WRITE | HAL_PAGE_GLOBAL)

// Kernel heap: Present + Write + NoCache (demand paging)
#define HAL_PAGE_KERNEL_HEAP    (HAL_PAGE_PRESENT | HAL_PAGE_WRITE | HAL_PAGE_GLOBAL)

// User code: Present + Exec + User (RO, W^X)
#define HAL_PAGE_USER_CODE      (HAL_PAGE_PRESENT | HAL_PAGE_EXEC | HAL_PAGE_USER)

// User data: Present + Write + User (no exec, W^X)
#define HAL_PAGE_USER_DATA      (HAL_PAGE_PRESENT | HAL_PAGE_WRITE | HAL_PAGE_USER)

// User CoW: Present + User + COW (Write снят аппаратно)
#define HAL_PAGE_USER_COW       (HAL_PAGE_PRESENT | HAL_PAGE_USER | HAL_PAGE_COW)

// Framebuffer / MMIO: Present + Write + NoCache + Global
#define HAL_PAGE_MMIO           (HAL_PAGE_PRESENT | HAL_PAGE_WRITE | HAL_PAGE_NOCACHE | HAL_PAGE_GLOBAL)

// ============================================================================
// MMU INITIALIZATION
// ============================================================================

// Инициализация MMU (вызывается один раз при boot).
// x86: paging_init() — Direct Map 512MB, Page Fault handler
// ARM: arm_mmu_init() — TTBR0/TTBR1 setup, enable MMU, Domain config
void hal_mmu_init(void);

// ============================================================================
// PAGE MAPPING
// ============================================================================

// Маппинг одной страницы (4KB).
// virt: виртуальный адрес (выровнен на 4KB)
// phys: физический адрес (выровнен на 4KB)
// flags: HAL_PAGE_* (абстрактные)
void hal_mmu_map_page(uint32_t virt, uint32_t phys, uint32_t flags);

// Маппинг в конкретном Page Directory / Translation Table (не текущем).
// pd: виртуальный адрес PD (x86) или TTBR0 table (ARM)
// Возвращает 0 при успехе, -1 при OOM.
int hal_mmu_map_page_in_space(uint32_t* space, uint32_t virt, uint32_t phys, uint32_t flags);

// Анмаппинг одной страницы.
void hal_mmu_unmap_page(uint32_t virt);

// Анмаппинг в конкретном address space.
void hal_mmu_unmap_page_in_space(uint32_t* space, uint32_t virt);

// Изменение прав доступа (для mprotect).
// flags: новые HAL_PAGE_* (PRESENT и адрес сохраняются).
void hal_mmu_protect_page(uint32_t virt, uint32_t flags);

// ============================================================================
// ADDRESS SPACE MANAGEMENT
// ============================================================================

// Создание нового address space (для fork/exec).
// x86: выделяет Page Directory, копирует kernel PDEs (768-1023)
// ARM: выделяет TTBR0 table (16KB), TTBR1 shared (не копируется)
// Возвращает виртуальный адрес нового space или NULL при OOM.
uint32_t* hal_mmu_create_space(void);

// Переключение address space.
// x86: mov CR3, phys_pd
// ARM: mov TTBR0, phys_ttbr0 (+ TLB flush)
void hal_mmu_switch_space(uint32_t phys_space);

// Уничтожение address space (для reaper).
// Освобождает: PTEs + Page Tables + PD (x86) / L2 tables + TTBR0 (ARM)
void hal_mmu_destroy_space(uint32_t* space_virt);

// Клонирование address space (для fork, CoW).
// User pages: READ-ONLY + HAL_PAGE_COW, refcount++
// Kernel space: shared (x86: copy PDEs, ARM: TTBR1 shared)
// Возвращает виртуальный адрес нового space или NULL при OOM.
uint32_t* hal_mmu_clone_space(uint32_t* parent_space_virt);

// ============================================================================
// TLB MANAGEMENT
// ============================================================================

// Инвалидация одной TLB entry.
// x86: invlpg [virt]
// ARM: MCR p15, 0, Rd, c8, c7, 1 (TLBIMVA)
void hal_mmu_flush_tlb_entry(uint32_t virt);

// Полная инвалидация TLB.
// x86: reload CR3
// ARM: MCR p15, 0, Rd, c8, c7, 0 (TLBIALL)
void hal_mmu_flush_tlb_all(void);

// ============================================================================
// KERNEL STACK ALLOCATOR (Hardware Guard Page)
// ============================================================================

// Выделяет KERNEL_STACK_USABLE_SIZE (16KB) стека + Guard Page.
// Возвращает виртуальный адрес ВЕРШИНЫ стека (top) или 0 при OOM.
uint32_t hal_mmu_alloc_kernel_stack(void);

// Освобождает стек ядра.
void hal_mmu_free_kernel_stack(uint32_t stack_top);

// ============================================================================
// PAGE FAULT INFO (для обработчика)
// ============================================================================

typedef struct hal_fault_info {
    uint32_t fault_addr;    // Адрес, вызвавший fault (CR2 на x86, FAR на ARM)
    uint32_t fault_type;    // Тип доступа
    int      is_write;      // 1 = write, 0 = read
    int      is_exec;       // 1 = instruction fetch
    int      is_user;       // 1 = Ring 3 / User Mode
    int      is_present;    // 1 = page present (protection fault), 0 = not present
} hal_fault_info_t;

// Fault type constants
#define HAL_FAULT_READ          0
#define HAL_FAULT_WRITE         1
#define HAL_FAULT_EXEC          2

// Извлечение информации о fault из аппаратных регистров.
// x86: CR2 + error code из ISR stack
// ARM: FAR (DFAR/IFAR) + FSR (DFSR/IFSR)
hal_fault_info_t hal_mmu_get_fault_info(uint32_t arch_error_code);

// ============================================================================
// W^X ENFORCEMENT HELPER
// ============================================================================

// Проверяет, что комбинация флагов не нарушает W^X.
// Возвращает 1 если комбинация валидна, 0 если нарушение.
static inline int hal_mmu_check_wx(uint32_t flags) {
    if ((flags & HAL_PAGE_WRITE) && (flags & HAL_PAGE_EXEC))
        return 0;  // W^X violation
    return 1;
}

#endif // HAL_MMU_H