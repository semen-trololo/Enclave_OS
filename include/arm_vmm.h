#ifndef ARM_VMM_H
#define ARM_VMM_H

// ============================================================================
// ARM Virtual Memory Manager (Day 52)
// ============================================================================
// Реализует hal_mmu.h контракт для ARMv6 (ARM1176JZF-S).
//
// Архитектура:
//   - L1 coarse page tables (16 KB, 4096 entries)
//   - L2 small page tables (1 KB, 256 entries per L2)
//   - 4 KB pages для user space
//   - 1 MB sections для kernel space (копируются из boot TTBR0)
//
// Ограничения Day 52 (spike):
//   - L1 tables из статического пула (8 spaces max)
//   - L2 tables из PMM (1 page each, 4 KB)
//   - clone_space / protect_page / kernel_stack — stubs
//   - Нет TTBR1 split (один TTBR0, kernel entries в каждом L1)
// ============================================================================

#include <stdint.h>

// Максимальное количество address spaces (spike).
#define ARM_VMM_MAX_SPACES  8

// Инициализация VMM. Вызывается один раз при boot.
// Сохраняет boot TTBR0 для копирования kernel entries.
void arm_vmm_init(void);

// Получить физический адрес boot TTBR0 (для отладки).
uint32_t arm_vmm_get_boot_ttbr0(void);

#endif // ARM_VMM_H
