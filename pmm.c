#include "pmm.h"
#include "klib.h"
#include "multiboot.h"
#include <stdint.h>
#include "serial.h"

static uint8_t pmm_bitmap[PMM_PAGES_COUNT / 8] __attribute__((aligned(4096)));

static uint32_t total_available_pages = 0;
static uint32_t used_pages = 0;

extern uint8_t _boot_start[]; 
extern uint8_t _kernel_start[];
extern uint8_t _kernel_end[];

#define MAX_E820_ENTRIES 64
static e820_entry_t e820_map[MAX_E820_ENTRIES];
static uint32_t e820_count = 0;

static inline void bitmap_set(uint32_t bit) {
    pmm_bitmap[bit / 8] |= (1 << (bit % 8));
}

static inline void bitmap_clear(uint32_t bit) {
    pmm_bitmap[bit / 8] &= ~(1 << (bit % 8));
}

static inline uint8_t bitmap_test(uint32_t bit) {
    return pmm_bitmap[bit / 8] & (1 << (bit % 8));
}

static void pmm_free_region(uint64_t base, uint64_t length) {
    uint64_t align_base = (base + PMM_PAGE_SIZE - 1) & ~(uint64_t)(PMM_PAGE_SIZE - 1);
    uint64_t align_end = (base + length) & ~(uint64_t)(PMM_PAGE_SIZE - 1);

    if (align_base >= align_end) return;
    if (align_base >= PMM_MAX_MEMORY_SIZE) return;
    if (align_end > PMM_MAX_MEMORY_SIZE) {
        align_end = PMM_MAX_MEMORY_SIZE;
    }

    uint32_t start_page = (uint32_t)(align_base / PMM_PAGE_SIZE);
    uint32_t end_page = (uint32_t)(align_end / PMM_PAGE_SIZE);

    for (uint32_t i = start_page; i < end_page; i++) {
        if (bitmap_test(i)) { 
            bitmap_clear(i);
            total_available_pages++;
        }
    }
}

static void pmm_reserve_region(uint64_t base, uint64_t end) {
    if (end <= base) return; 
    
    uint32_t start_page = (uint32_t)(base / PMM_PAGE_SIZE);
    uint32_t end_page;
    
    if (end >= 0xFFFFFFFFULL) {
        end_page = PMM_PAGES_COUNT;
    } else {
        end_page = (uint32_t)((end + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE);
    }

    for (uint32_t i = start_page; i < end_page; i++) {
        if (i < PMM_PAGES_COUNT && !bitmap_test(i)) {
            bitmap_set(i);
            if (total_available_pages > 0) {
                total_available_pages--;
            }
        }
    }
}

void pmm_init(multiboot_info_t* info) {
    (void)info; // Временно игнорируем multiboot_info
    e820_count = 0;
    
    for (uint32_t i = 0; i < sizeof(pmm_bitmap); i++) {
        pmm_bitmap[i] = 0xFF;
    }
    total_available_pages = 0;
    used_pages = 0;

    serial_print("[PMM] Starting initialization...\n");
    
    // ========================================================================
    // ВРЕМЕННЫЙ ХАРДКОД (Обходим баг Multiboot/GRUB)
    // ========================================================================
    // QEMU с -m 512M гарантирует непрерывную RAM от 0x100000 (1 МБ).
    // Мы жестко освобождаем 256 МБ физической памяти. 
    // Этого более чем достаточно для ядра, VMM и 32 МБ Heap.
    k_printf("[PMM] Using hardcoded memory map (256 MB RAM) for debugging.\n");
    pmm_free_region(0x00100000, 256 * 1024 * 1024);

    k_printf("[PMM] Available pages BEFORE reservations: %u\n", total_available_pages);

    // А) Резервируем нижний 1 МБ (Real Mode IVT, BIOS Data Area, VGA memory)
    pmm_reserve_region(0x00000000, 0x00100000);

    // Б) ЖЕЛЕЗОБЕТОННОЕ РЕЗЕРВИРОВАНИЕ: Первые 4 МБ физической памяти.
    pmm_reserve_region(0x00100000, 0x00400000);

    // В) ЖЕСТКОЕ РЕЗЕРВИРОВАНИЕ PCI MMIO HOLE (чтобы не затереть фреймбуфер)
    pmm_reserve_region(0xE0000000ULL, 0xFFFFFFFFULL);

    k_printf("[PMM] Available pages AFTER reservations: %u\n", total_available_pages);

    k_printf("[PMM] Safe by Default initialized.\n");
    k_printf("[PMM] Lower 1 MB reserved.\n");
    k_printf("[PMM] Kernel & Bootstrap tables reserved.\n");
    k_printf("[PMM] PCI MMIO Hole reserved.\n");
    k_printf("[PMM] Initialized. Available: %u pages (%u MB).\n", 
             total_available_pages, (total_available_pages * PMM_PAGE_SIZE) / (1024 * 1024));
}

uint32_t pmm_alloc_page(void) {
    uint32_t* bitmap_words = (uint32_t*)pmm_bitmap;
    uint32_t word_count = sizeof(pmm_bitmap) / sizeof(uint32_t);
    
    for (uint32_t w = 0; w < word_count; w++) {
        if (bitmap_words[w] == 0xFFFFFFFF) continue;
        
        uint32_t word = bitmap_words[w];
        for (uint32_t b = 0; b < 32; b++) {
            uint32_t bit_index = w * 32 + b;
            if (bit_index >= PMM_PAGES_COUNT) return 0;
            
            if (!(word & (1 << b))) {
                bitmap_set(bit_index);
                used_pages++;
                return bit_index * PMM_PAGE_SIZE;
            }
        }
    }
    
    // Защита от спама: пишем OOM только первые 3 раза
    static uint32_t oom_count = 0;
    if (oom_count < 3) {
        serial_print("[PMM] OOM! Bitmap is full.\n");
        oom_count++;
    }
    return 0; 
}

void pmm_free_page(uint32_t phys_addr) {
    if (phys_addr % PMM_PAGE_SIZE != 0) return;
    
    uint32_t page_index = phys_addr / PMM_PAGE_SIZE;
    if (page_index >= PMM_PAGES_COUNT) return;
    
    if (!bitmap_test(page_index)) return; 
    
    bitmap_clear(page_index);
    if (used_pages > 0) used_pages--;
}

uint32_t pmm_get_used_pages(void) { return used_pages; }
uint32_t pmm_get_free_pages(void) { return total_available_pages - used_pages; }
uint32_t pmm_get_total_pages(void) { return total_available_pages; }

const e820_entry_t* pmm_get_memory_map(uint32_t* count) {
    if (count) *count = e820_count;
    return e820_map;
}

void pmm_dump_e820(void) {
    k_printf("\n--- [E820 Memory Map] ---\n");
    for (uint32_t i = 0; i < e820_count; i++) {
        const char* type_str = "Unknown";
        if (e820_map[i].type == 1) type_str = "Available RAM";
        else if (e820_map[i].type == 2) type_str = "Reserved";
        else if (e820_map[i].type == 3) type_str = "ACPI Reclaim";
        else if (e820_map[i].type == 4) type_str = "ACPI NVS";
        else if (e820_map[i].type == 5) type_str = "Bad RAM";
        
        k_printf(" %08x%08x - %08x%08x | Type: %u (%s)\n",
                 (uint32_t)(e820_map[i].addr >> 32), (uint32_t)e820_map[i].addr,
                 (uint32_t)((e820_map[i].addr + e820_map[i].len) >> 32),
                 (uint32_t)(e820_map[i].addr + e820_map[i].len),
                 e820_map[i].type, type_str);
    }
    k_printf("-------------------------\n\n");
}