#include "pmm.h"
#include "klib.h"
#include "multiboot.h"
#include "config.h"   // ✅ SSOT: Подключаем глобальные константы памяти
#include <stdint.h>
#include <stdbool.h>
#include "serial.h"
#include "paging.h"

// ============================================================================
// СТРУКТУРА MULTIBOOT MODULE
// ============================================================================
typedef struct {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t string;
    uint32_t reserved;
} multiboot_module_t;

// ============================================================================
// ГЛОБАЛЬНЫЕ ДАННЫЕ PMM
// ============================================================================
static uint8_t pmm_bitmap[PMM_MAX_PAGES / 8] __attribute__((aligned(4096)));
// ============================================================================
// [ДЕНЬ 14] REFERENCE COUNTING ARRAY (2MB в .bss)
// ============================================================================
// Параллельный массив для подсчета ссылок на физические страницы.
// Используется для Copy-on-Write: при fork() страницы становятся shared,
// и только при первой записи выделяется личная копия.
static uint16_t pmm_refcounts[PMM_MAX_PAGES] __attribute__((aligned(4096)));
static uint32_t total_available_pages = 0;
static uint32_t used_pages = 0;
static uint32_t pmm_max_page = 0; 

// [ДЕНЬ 10] PMM ACCOUNTING
static uint32_t pmm_total_allocs = 0;
static uint32_t pmm_total_frees = 0;

extern uint8_t _boot_start[]; 
extern uint8_t _kernel_start[];
extern uint8_t _kernel_end[];

#define MAX_E820_ENTRIES 64
static e820_entry_t e820_map[MAX_E820_ENTRIES];
static uint32_t e820_count = 0;

// ============================================================================
// IRQ SAFETY HELPERS (Защита критических секций от прерываний)
// ============================================================================
static inline uint32_t read_eflags(void) {
    uint32_t flags;
    __asm__ volatile("pushf ; pop %0" : "=r"(flags));
    return flags;
}

static inline void load_eflags(uint32_t flags) {
    __asm__ volatile("push %0 ; popf" : : "r"(flags));
}

static inline void disable_interrupts(void) {
    __asm__ volatile("cli");
}

// ============================================================================
// БИТМАП ОПЕРАЦИИ
// ============================================================================
static inline void bitmap_set(uint32_t bit) {
    pmm_bitmap[bit / 8] |= (1 << (bit % 8));
}

static inline void bitmap_clear(uint32_t bit) {
    pmm_bitmap[bit / 8] &= ~(1 << (bit % 8));
}

static inline bool bitmap_test(uint32_t bit) {
    return (pmm_bitmap[bit / 8] & (1 << (bit % 8))) != 0;
}

// ============================================================================
// ОСВОБОЖДЕНИЕ РЕГИОНА
// ============================================================================
static void pmm_free_region(uint64_t base, uint64_t length) {
    uint64_t align_base = (base + PMM_PAGE_SIZE - 1) & ~(uint64_t)(PMM_PAGE_SIZE - 1);
    uint64_t align_end = (base + length) & ~(uint64_t)(PMM_PAGE_SIZE - 1);

    if (align_base >= align_end) return;

    uint32_t start_page = (uint32_t)(align_base / PMM_PAGE_SIZE);
    uint32_t end_page = (uint32_t)(align_end / PMM_PAGE_SIZE);

    if (start_page >= pmm_max_page) return;
    if (end_page > pmm_max_page) end_page = pmm_max_page;

    for (uint32_t i = start_page; i < end_page; i++) {
        if (bitmap_test(i)) { 
            bitmap_clear(i);
            total_available_pages++;
        }
    }
}

// ============================================================================
// РЕЗЕРВИРОВАНИЕ РЕГИОНА
// ============================================================================
void pmm_reserve_region(uint64_t base, uint64_t end) {
    if (end <= base) return; 
    
    uint32_t start_page = (uint32_t)(base / PMM_PAGE_SIZE);
    uint32_t end_page = (uint32_t)((end + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE);

    if (start_page >= pmm_max_page) return;
    if (end_page > pmm_max_page) end_page = pmm_max_page;

    for (uint32_t i = start_page; i < end_page; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            if (total_available_pages > 0) {
                total_available_pages--;
            }
        }
    }
}

// ============================================================================
// ИНИЦИАЛИЗАЦИЯ PMM
// ============================================================================
void pmm_init(multiboot_info_t* info) {
    e820_count = 0;
    pmm_max_page = 0;
    pmm_total_allocs = 0;
    pmm_total_frees = 0;
    
    for (uint32_t i = 0; i < sizeof(pmm_bitmap); i++) {
        pmm_bitmap[i] = 0xFF;
    }
    // [ДЕНЬ 14] Обнуляем массив refcounts
    for (uint32_t i = 0; i < PMM_MAX_PAGES; i++) {
        pmm_refcounts[i] = 0;
    }
    total_available_pages = 0;
    used_pages = 0;

    serial_print("[PMM] Starting initialization (Dynamic Sizing)...\n");

    if (info && (info->flags & MULTIBOOT_INFO_MEM_MAP)) {
        serial_print("[PMM] E820 Memory Map detected.\n");
        
        multiboot_memory_map_t* mmap = (multiboot_memory_map_t*)PHYS_TO_VIRT(info->mmap_addr);
        multiboot_memory_map_t* mmap_end = (multiboot_memory_map_t*)PHYS_TO_VIRT(info->mmap_addr + info->mmap_length);

        uint64_t max_addr = 0;

        // ПЕРВЫЙ ПРОХОД
        while (mmap < mmap_end) {
            if (e820_count < MAX_E820_ENTRIES) {
                e820_map[e820_count].addr = mmap->addr;
                e820_map[e820_count].len = mmap->len;
                e820_map[e820_count].type = mmap->type;
                e820_count++;
            }

            if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE) {
                uint64_t end = mmap->addr + mmap->len;
                if (end > max_addr) max_addr = end;
            }
            mmap = (multiboot_memory_map_t*)((uint32_t)mmap + mmap->size + sizeof(uint32_t));
        }

        if (max_addr > 0xFFFFFFFFULL) max_addr = 0xFFFFFFFFULL;
        pmm_max_page = (uint32_t)(max_addr / PMM_PAGE_SIZE);
        
        if (pmm_max_page > PMM_MAX_PAGES) {
            serial_printf("[PMM WARN] RAM exceeds 4GB limit. Truncating to %u pages.\n", PMM_MAX_PAGES);
            pmm_max_page = PMM_MAX_PAGES;
        }
        
        serial_printf("[PMM] Dynamic limit set: %u pages (%u MB max addressable).\n", 
                      pmm_max_page, (pmm_max_page * PMM_PAGE_SIZE) / (1024 * 1024));

        // ВТОРОЙ ПРОХОД
        mmap = (multiboot_memory_map_t*)PHYS_TO_VIRT(info->mmap_addr);
        while (mmap < mmap_end) {
            if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE) {
                pmm_free_region(mmap->addr, mmap->len);
            }
            mmap = (multiboot_memory_map_t*)((uint32_t)mmap + mmap->size + sizeof(uint32_t));
        }

    } else {
        serial_print("[PMM] WARNING: No E820 map found! Fallback to hardcoded 256MB.\n");
        pmm_max_page = (256 * 1024 * 1024) / PMM_PAGE_SIZE;
        pmm_free_region(0x00100000, 256 * 1024 * 1024);
    }

    serial_printf("[PMM] Available pages BEFORE reservations: %u\n", total_available_pages);

    // ✅ Punching Holes - используем SSOT константы из config.h
    pmm_reserve_region(LOWER_MEM_START, LOWER_MEM_END); // 0x00000000 - 0x00100000

    uint32_t phys_k_start = VIRT_TO_PHYS((uint32_t)_kernel_start);
    uint32_t phys_k_end = VIRT_TO_PHYS((uint32_t)_kernel_end);
    pmm_reserve_region(phys_k_start, phys_k_end); // Kernel Image

    if (info) {
        uint32_t mb_phys_start = VIRT_TO_PHYS((uint32_t)info);
        uint32_t mb_phys_end = mb_phys_start + sizeof(multiboot_info_t) + info->mmap_length;
        pmm_reserve_region(mb_phys_start, mb_phys_end); // Multiboot Info
        
        if (info->flags & MULTIBOOT_INFO_MODS) {
            multiboot_module_t* mods = (multiboot_module_t*)PHYS_TO_VIRT(info->mods_addr);
            for (uint32_t i = 0; i < info->mods_count; i++) {
                serial_printf("[PMM] Reserving module %d: 0x%x - 0x%x\n", i, mods[i].mod_start, mods[i].mod_end);
                pmm_reserve_region(mods[i].mod_start, mods[i].mod_end);
            }
        }
    }

    // ✅ PCI MMIO Hole - используем SSOT константы из config.h
    if (pmm_max_page > (PCI_MMIO_HOLE_START / PMM_PAGE_SIZE)) {
        pmm_reserve_region(PCI_MMIO_HOLE_START, PCI_MMIO_HOLE_END);
    }

    serial_printf("[PMM] Available pages AFTER reservations: %u\n", total_available_pages);
    serial_printf("[PMM] Initialized. Available: %u pages (%u MB).\n", 
             total_available_pages, (total_available_pages * PMM_PAGE_SIZE) / (1024 * 1024));
}

// ============================================================================
// АЛЛОКАЦИЯ СТРАНИЦЫ (O(1) BSF + IRQ SAFE)
// ============================================================================
uint32_t pmm_alloc_page(void) {
    uint32_t flags = read_eflags();
    disable_interrupts(); // 🛡️ Защита от Race Conditions

    uint32_t* bitmap_words = (uint32_t*)pmm_bitmap;
    uint32_t word_count = (pmm_max_page + 31) / 32; 
    uint32_t result = 0;
    
    for (uint32_t w = 0; w < word_count; w++) {
        uint32_t word = bitmap_words[w];
        if (word == 0xFFFFFFFF) continue;
        
        uint32_t free_bits = ~word;
        uint32_t b = __builtin_ctz(free_bits);
        uint32_t bit_index = (w << 5) + b; 
        
        if (bit_index >= pmm_max_page) {
            break; // 🛡️ Исправление Edge Case
        }
        
        bitmap_words[w] |= (1U << b);
        used_pages++;
        pmm_total_allocs++; // [ДЕНЬ 10] Accounting
        pmm_refcounts[bit_index] = 1; // [ДЕНЬ 14] Новая страница имеет 1 ссылку
        result = bit_index * PMM_PAGE_SIZE;
        break;
    }
    
    load_eflags(flags); // Восстановление состояния прерываний

    if (result == 0) {
        static uint32_t oom_count = 0;
        if (oom_count < 3) {
            serial_print("[PMM] OOM! Bitmap is full.\n");
            oom_count++;
        }
    }
    return result; 
}

// ============================================================================
// ОСВОБОЖДЕНИЕ СТРАНИЦЫ (IRQ SAFE)
// ============================================================================
void pmm_free_page(uint32_t phys_addr) {
    if (phys_addr % PMM_PAGE_SIZE != 0) return;
    
    uint32_t page_index = phys_addr / PMM_PAGE_SIZE;
    if (page_index >= pmm_max_page) return; 

    uint32_t flags = read_eflags();
    disable_interrupts(); // 🛡️ Защита от Race Conditions
    
    if (!bitmap_test(page_index)) {
        serial_printf("[PMM WARN] Attempt to free already free page: 0x%x\n", phys_addr);
        load_eflags(flags);
        return; 
    }
    
    bitmap_clear(page_index);
    if (used_pages > 0) used_pages--;
    pmm_total_frees++; // [ДЕНЬ 10] Accounting
    pmm_refcounts[page_index] = 0; // [ДЕНЬ 14] Страница свободна
    
    load_eflags(flags);
}
// ============================================================================
// [ДЕНЬ 14] REFERENCE COUNTING API (Copy-on-Write Support)
// ============================================================================
void pmm_inc_ref(uint32_t phys_addr) {
    if (phys_addr % PMM_PAGE_SIZE != 0) return;
    
    uint32_t page_index = phys_addr / PMM_PAGE_SIZE;
    if (page_index >= pmm_max_page) return;
    
    uint32_t flags = read_eflags();
    disable_interrupts();
    
    if (pmm_refcounts[page_index] < 0xFFFF) {
        pmm_refcounts[page_index]++;
    }
    
    load_eflags(flags);
}

void pmm_dec_ref(uint32_t phys_addr) {
    if (phys_addr % PMM_PAGE_SIZE != 0) return;
    
    uint32_t page_index = phys_addr / PMM_PAGE_SIZE;
    if (page_index >= pmm_max_page) return;
    
    uint32_t flags = read_eflags();
    disable_interrupts();
    
    if (pmm_refcounts[page_index] > 0) {
        pmm_refcounts[page_index]--;
        
        // Если счетчик стал 0, страница больше никем не используется
        if (pmm_refcounts[page_index] == 0) {
            pmm_free_page(phys_addr);
        }
    }
    
    load_eflags(flags);
}

// ============================================================================
// СТАТИСТИКА
// ============================================================================
uint32_t pmm_get_used_pages(void) { return used_pages; }
uint32_t pmm_get_free_pages(void) { return total_available_pages - used_pages; }
uint32_t pmm_get_total_pages(void) { return total_available_pages; }
uint32_t pmm_get_max_pages(void) { return pmm_max_page; }

// ============================================================================
// [ДЕНЬ 10] PMM ACCOUNTING API
// ============================================================================
uint32_t pmm_get_alloc_count(void) { return pmm_total_allocs; }
uint32_t pmm_get_free_count(void) { return pmm_total_frees; }
int32_t pmm_check_balance(void) { 
    return (int32_t)(pmm_total_allocs - pmm_total_frees); 
}

// ============================================================================
// E820 MAP ACCESS
// ============================================================================
const e820_entry_t* pmm_get_memory_map(uint32_t* count) {
    if (count) *count = e820_count;
    return e820_map;
}

void pmm_dump_e820(void) {
    serial_print("\n--- [E820 Memory Map] ---\n");
    for (uint32_t i = 0; i < e820_count; i++) {
        const char* type_str = "Unknown";
        if (e820_map[i].type == 1) type_str = "Available RAM";
        else if (e820_map[i].type == 2) type_str = "Reserved";
        else if (e820_map[i].type == 3) type_str = "ACPI Reclaim";
        else if (e820_map[i].type == 4) type_str = "ACPI NVS";
        else if (e820_map[i].type == 5) type_str = "Bad RAM";
        
        uint32_t addr_hi = (uint32_t)(e820_map[i].addr >> 32);
        uint32_t addr_lo = (uint32_t)e820_map[i].addr;
        uint32_t end_hi = (uint32_t)((e820_map[i].addr + e820_map[i].len) >> 32);
        uint32_t end_lo = (uint32_t)(e820_map[i].addr + e820_map[i].len);
        
        serial_printf(" 0x%x:%x - 0x%x:%x | Type: %u (%s)\n",
                 addr_hi, addr_lo, end_hi, end_lo,
                 e820_map[i].type, type_str);
    }
    serial_print("-------------------------\n\n");
}
