#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// USER SPACE BOUNDARIES
// ============================================================================
#define USER_SPACE_START    0x00000000
#define USER_SPACE_END      0xBFFFFFFF

// ============================================================================
// KERNEL SPACE BOUNDARIES
// ============================================================================
#define KERNEL_SPACE_START  0xC0000000
#define KERNEL_DIRECT_MAP   0xC0000000

// ============================================================================
// SSOT ADDRESS TRANSLATION MACROS (Перенесено из paging.h и pmm.c)
// ============================================================================
// VIRT_TO_PHYS: учитывает, что секции .boot имеют адреса < 0xC0000000
#define VIRT_TO_PHYS(addr) (((uint32_t)(addr) >= KERNEL_SPACE_START) ? ((uint32_t)(addr) - KERNEL_SPACE_START) : (uint32_t)(addr))
// PHYS_TO_VIRT: нижняя память (0-16MB) замаплена в Higher Half (Direct Map)
#define PHYS_TO_VIRT(addr) ((uint32_t)(addr) + KERNEL_SPACE_START)

// ============================================================================
// LOWER MEMORY (Reserved by PMM)
// ============================================================================
#define LOWER_MEM_START     0x00000000
#define LOWER_MEM_END       0x00100000

// ============================================================================
// PCI MMIO HOLE (Reserved by PMM)
// ============================================================================
#define PCI_MMIO_HOLE_START 0xE0000000
#define PCI_MMIO_HOLE_END   0xFFFFFFFF

// ============================================================================
// PROCESS MEMORY LAYOUT (User Space)
// ============================================================================
// ✅ ИСПРАВЛЕНО: Стек должен быть СТРОГО ниже KERNEL_SPACE_START
// Стек растет ВНИЗ от stack_top, поэтому граница = KERNEL_SPACE_START - 4KB
#define USER_STACK_VIRT_TOP 0xBFFFF000  // Было 0xC0000000
#define USER_STACK_SIZE     (64 * 1024)
#define USER_STACK_GUARD_SIZE (4 * 1024)

#define USER_HEAP_START 0x10000000
#define USER_HEAP_MAX_SIZE  (64 * 1024 * 1024)

// ============================================================================
// KERNEL MEMORY LAYOUT
// ============================================================================
#define KERNEL_HEAP_VIRT    0xD0000000
#define KERNEL_HEAP_SIZE (128 * 1024 * 1024)  // 🛡️ Expanded for 40MB+ tmpfs files (Lazy Heap)
#define KERNEL_HEAP_END     (KERNEL_HEAP_VIRT + KERNEL_HEAP_SIZE)

// ============================================================================
// LAZY ALLOCATION (On-Demand Paging для ядра)
// ============================================================================
#define LAZY_ALLOC_START    KERNEL_HEAP_VIRT
#define LAZY_ALLOC_END      KERNEL_HEAP_END

// ============================================================================
// FRAMEBUFFER
// ============================================================================
#define FB_VIRT_BASE        0xFD000000
#define FB_PHYS_BASE        0xFD000000
#define FB_SIZE_MB          16

// ============================================================================
// MMAP REGION (Day 12)
// ============================================================================
#define USER_MMAP_START      0x40000000  // 1 GB (База для mmap)
#define USER_MMAP_MAX_SIZE   0x40000000  // 1 GB (Максимальный размер пула mmap)

// ============================================================================
// KERNEL STACK POOL (Day 16 Security Hardening)
// ============================================================================
// Пул виртуальных адресов для выделения Kernel Stacks с Hardware Guard Page.
// Каждый слот занимает 5 страниц (20 KB): 
// [Page 0: Guard (Unmapped)] [Page 1-4: Data (16 KB)]
// Стек растет вниз. При переполнении ESP уходит в Page 0, вызывая PF.
#define KERNEL_STACK_POOL_START 0xC8000000
#define KERNEL_STACK_POOL_SIZE  (16 * 1024 * 1024) // 16 MB (хватит на ~819 задач)
#define KERNEL_STACK_SLOT_PAGES 5
#define KERNEL_STACK_USABLE_SIZE (4 * 4096) // 16 KB usable data space

#endif // CONFIG_H
