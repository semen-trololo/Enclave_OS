#include "pmm.h"
#include "klib.h"
#include "multiboot.h"
#include <stdint.h>
#include <stdbool.h>
#include "serial.h"

// ============================================================================
// МАКРОСЫ ДЛЯ РАБОТЫ С HIGHER HALF (SSOT)
// ============================================================================
#define KERNEL_VIRT_BASE 0xC0000000

// Если адрес >= 0xC0000000, это Higher Half (вычитаем базу).
// Если адрес < 0xC0000000, это уже физический адрес (секции .boot), возвращаем как есть.
#define VIRT_TO_PHYS(addr) (((uint32_t)(addr) >= KERNEL_VIRT_BASE) ? ((uint32_t)(addr) - KERNEL_VIRT_BASE) : (uint32_t)(addr))

// Физический адрес всегда превращаем в виртуальный Higher Half
#define PHYS_TO_VIRT(addr) ((uint32_t)(addr) + KERNEL_VIRT_BASE)

// ============================================================================
// СТРУКТУРА MULTIBOOT MODULE (для резервирования initrd в PMM)
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
// Статический битмап на максимум 4GB (128 KB). 
// Это безопасно для .bss и избавляет от chicken-and-egg проблемы динамической аллокации.
// Выравнивание на 4096 для потенциального использования как Page Table.
static uint8_t pmm_bitmap[PMM_MAX_PAGES / 8] __attribute__((aligned(4096)));

static uint32_t total_available_pages = 0;
static uint32_t used_pages = 0;
static uint32_t pmm_max_page = 0; // ДИНАМИЧЕСКИЙ ЛИМИТ (вычисляется из E820)

// Linker symbols (экспортируются из linker.ld)
extern uint8_t _boot_start[]; 
extern uint8_t _kernel_start[];
extern uint8_t _kernel_end[];

// Кэш E820 карты (для shell команды 'meminfo')
#define MAX_E820_ENTRIES 64
static e820_entry_t e820_map[MAX_E820_ENTRIES];
static uint32_t e820_count = 0;

// ============================================================================
// БИТМАП ОПЕРАЦИИ (O(1) Hardware Accelerated)
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
// ОСВОБОЖДЕНИЕ РЕГИОНА (Mark as Available)
// ============================================================================
static void pmm_free_region(uint64_t base, uint64_t length) {
    // Выравнивание по границам страниц (4KB)
    uint64_t align_base = (base + PMM_PAGE_SIZE - 1) & ~(uint64_t)(PMM_PAGE_SIZE - 1);
    uint64_t align_end = (base + length) & ~(uint64_t)(PMM_PAGE_SIZE - 1);

    if (align_base >= align_end) return;

    uint32_t start_page = (uint32_t)(align_base / PMM_PAGE_SIZE);
    uint32_t end_page = (uint32_t)(align_end / PMM_PAGE_SIZE);

    // 🛡️ Жесткое ограничение динамическим лимитом
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
// РЕЗЕРВИРОВАНИЕ РЕГИОНА (Mark as Reserved)
// ============================================================================
void pmm_reserve_region(uint64_t base, uint64_t end) {
    if (end <= base) return; 
    
    uint32_t start_page = (uint32_t)(base / PMM_PAGE_SIZE);
    uint32_t end_page = (uint32_t)((end + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE);

    // 🛡️ Жесткое ограничение динамическим лимитом
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
// ИНИЦИАЛИЗАЦИЯ PMM (Dynamic Sizing + Safe by Default)
// ============================================================================
void pmm_init(multiboot_info_t* info) {
    e820_count = 0;
    pmm_max_page = 0;
    
    // ШАГ 1: Safe by Default — помечаем ВСЮ память как ЗАНЯТУЮ
    // Это защита от использования памяти до её явного освобождения через E820.
    for (uint32_t i = 0; i < sizeof(pmm_bitmap); i++) {
        pmm_bitmap[i] = 0xFF;
    }
    total_available_pages = 0;
    used_pages = 0;

    serial_print("[PMM] Starting initialization (Dynamic Sizing)...\n");

    // ШАГ 2: Парсинг E820 и поиск максимального адреса
    if (info && (info->flags & MULTIBOOT_INFO_MEM_MAP)) {
        serial_print("[PMM] E820 Memory Map detected.\n");
        
        multiboot_memory_map_t* mmap = (multiboot_memory_map_t*)PHYS_TO_VIRT(info->mmap_addr);
        multiboot_memory_map_t* mmap_end = (multiboot_memory_map_t*)PHYS_TO_VIRT(info->mmap_addr + info->mmap_length);

        uint64_t max_addr = 0;

        // 🛡️ ПЕРВЫЙ ПРОХОД: Находим максимальный адрес RAM
        while (mmap < mmap_end) {
            // Кэшируем E820 карту для shell команды 'meminfo'
            if (e820_count < MAX_E820_ENTRIES) {
                e820_map[e820_count].addr = mmap->addr;
                e820_map[e820_count].len = mmap->len;
                e820_map[e820_count].type = mmap->type;
                e820_count++;
            }

            // Ищем самый высокий адрес доступной RAM
            if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE) {
                uint64_t end = mmap->addr + mmap->len;
                if (end > max_addr) max_addr = end;
            }

            // Переход к следующей записи (Multiboot spec: size + sizeof(uint32_t))
            mmap = (multiboot_memory_map_t*)((uint32_t)mmap + mmap->size + sizeof(uint32_t));
        }

        // 🛡️ Устанавливаем динамический лимит ПЕРЕД освобождением регионов
        // Ограничиваем 4GB для 32-битной системы без PAE
        if (max_addr > 0xFFFFFFFFULL) max_addr = 0xFFFFFFFFULL;
        pmm_max_page = (uint32_t)(max_addr / PMM_PAGE_SIZE);
        
        // Страховка: если pmm_max_page превышает размер статического массива
        if (pmm_max_page > PMM_MAX_PAGES) {
            serial_printf("[PMM WARN] RAM exceeds 4GB limit. Truncating to %u pages.\n", PMM_MAX_PAGES);
            pmm_max_page = PMM_MAX_PAGES;
        }
        
        serial_printf("[PMM] Dynamic limit set: %u pages (%u MB max addressable).\n", 
                      pmm_max_page, (pmm_max_page * PMM_PAGE_SIZE) / (1024 * 1024));

        // 🛡️ ВТОРОЙ ПРОХОД: Освобождаем доступные регионы
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

    // ШАГ 3: Punching Holes (резервирование критических регионов)
    pmm_reserve_region(0x00000000, 0x00100000); // Lower 1MB (IVT, BDA, VGA RAM)

    uint32_t phys_k_start = VIRT_TO_PHYS((uint32_t)_kernel_start);
    uint32_t phys_k_end = VIRT_TO_PHYS((uint32_t)_kernel_end);
    pmm_reserve_region(phys_k_start, phys_k_end); // Kernel Image

    if (info) {
        uint32_t mb_phys_start = VIRT_TO_PHYS((uint32_t)info);
        uint32_t mb_phys_end = mb_phys_start + sizeof(multiboot_info_t) + info->mmap_length;
        pmm_reserve_region(mb_phys_start, mb_phys_end); // Multiboot Info
        
        // 🛡️ КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ: Резервируем модули (initrd.tar) в PMM
        // Если этого не сделать, PMM затрет TAR-архив при выделении Page Tables!
        if (info->flags & MULTIBOOT_INFO_MODS) {
            multiboot_module_t* mods = (multiboot_module_t*)PHYS_TO_VIRT(info->mods_addr);
            for (uint32_t i = 0; i < info->mods_count; i++) {
                serial_printf("[PMM] Reserving module %d: 0x%x - 0x%x\n", i, mods[i].mod_start, mods[i].mod_end);
                pmm_reserve_region(mods[i].mod_start, mods[i].mod_end);
            }
        }
    }

    // PCI MMIO Hole (только если он попадает в pmm_max_page)
    if (pmm_max_page > (0xE0000000 / PMM_PAGE_SIZE)) {
        pmm_reserve_region(0xE0000000ULL, 0xFFFFFFFFULL);
    }

    serial_printf("[PMM] Available pages AFTER reservations: %u\n", total_available_pages);
    serial_printf("[PMM] Initialized. Available: %u pages (%u MB).\n", 
             total_available_pages, (total_available_pages * PMM_PAGE_SIZE) / (1024 * 1024));
}

// ============================================================================
// АЛЛОКАЦИЯ СТРАНИЦЫ (O(1) BSF - Bit Scan Forward)
// ============================================================================
uint32_t pmm_alloc_page(void) {
    uint32_t* bitmap_words = (uint32_t*)pmm_bitmap;
    
    // Считаем только до динамического лимита
    uint32_t word_count = (pmm_max_page + 31) / 32; 
    
    for (uint32_t w = 0; w < word_count; w++) {
        uint32_t word = bitmap_words[w];
        
        // Fast-Forwarding: если все 32 бита заняты, пропускаем
        if (word == 0xFFFFFFFF) continue;
        
        // Инвертируем слово: 0 = занято, 1 = свободно
        uint32_t free_bits = ~word;
        
        // __builtin_ctz (Count Trailing Zeros) = BSF (Bit Scan Forward)
        // Находит позицию первого свободного бита
        uint32_t b = __builtin_ctz(free_bits);
        uint32_t bit_index = (w << 5) + b; 
        
        // Защита от выхода за пределы
        if (bit_index >= pmm_max_page) return 0;
        
        // Помечаем страницу как занятую
        bitmap_words[w] |= (1U << b);
        used_pages++;
        return bit_index * PMM_PAGE_SIZE;
    }
    
    // OOM Protection: логируем только первые 3 раза
    static uint32_t oom_count = 0;
    if (oom_count < 3) {
        serial_print("[PMM] OOM! Bitmap is full.\n");
        oom_count++;
    }
    return 0; 
}

// ============================================================================
// ОСВОБОЖДЕНИЕ СТРАНИЦЫ
// ============================================================================
void pmm_free_page(uint32_t phys_addr) {
    if (phys_addr % PMM_PAGE_SIZE != 0) return;
    
    uint32_t page_index = phys_addr / PMM_PAGE_SIZE;
    if (page_index >= pmm_max_page) return; // Динамическая проверка
    
    if (!bitmap_test(page_index)) {
        serial_printf("[PMM WARN] Attempt to free already free page: 0x%x\n", phys_addr);
        return; 
    }
    
    bitmap_clear(page_index);
    if (used_pages > 0) used_pages--;
}

// ============================================================================
// СТАТИСТИКА (Для Shell и отладки)
// ============================================================================
uint32_t pmm_get_used_pages(void) { return used_pages; }
uint32_t pmm_get_free_pages(void) { return total_available_pages - used_pages; }
uint32_t pmm_get_total_pages(void) { return total_available_pages; }
uint32_t pmm_get_max_pages(void) { return pmm_max_page; }

// ============================================================================
// ДОСТУП К E820 КАРТЕ (Для Shell команды 'meminfo')
// ============================================================================
const e820_entry_t* pmm_get_memory_map(uint32_t* count) {
    if (count) *count = e820_count;
    return e820_map;
}

// ============================================================================
// ОТЛАДОЧНЫЙ ВЫВОД E820
// ============================================================================
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