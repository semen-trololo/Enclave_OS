#include "pmm.h"
#include "klib.h"
#include "multiboot.h"
#include <stdint.h>
#include "serial.h"

// ============================================================================
// МАКРОСЫ ДЛЯ РАБОТЫ С HIGHER HALF (ИСПРАВЛЕНО ОТ UNDERFLOW)
// ============================================================================
#define KERNEL_VIRT_BASE 0xC0000000

// Если адрес >= 0xC0000000, это Higher Half (вычитаем базу).
// Если адрес < 0xC0000000, это уже физический адрес (секции .boot), возвращаем как есть.
#define VIRT_TO_PHYS(addr) (((uint32_t)(addr) >= KERNEL_VIRT_BASE) ? ((uint32_t)(addr) - KERNEL_VIRT_BASE) : (uint32_t)(addr))

// Физический адрес всегда превращаем в виртуальный Higher Half
#define PHYS_TO_VIRT(addr) ((uint32_t)(addr) + KERNEL_VIRT_BASE)

// ============================================================================
// ГЛОБАЛЬНЫЕ ДАННЫЕ PMM
// ============================================================================
static uint8_t pmm_bitmap[PMM_PAGES_COUNT / 8] __attribute__((aligned(4096)));

static uint32_t total_available_pages = 0;
static uint32_t used_pages = 0;

// Linker symbols (виртуальные адреса из Higher Half)
extern uint8_t _boot_start[]; 
extern uint8_t _kernel_start[];
extern uint8_t _kernel_end[];

// Кэш E820 карты для команды `memmap` в Shell
#define MAX_E820_ENTRIES 64
static e820_entry_t e820_map[MAX_E820_ENTRIES];
static uint32_t e820_count = 0;

// ============================================================================
// БИТМАП ОПЕРАЦИИ (Inline для производительности)
// ============================================================================
static inline void bitmap_set(uint32_t bit) {
    pmm_bitmap[bit / 8] |= (1 << (bit % 8));
}

static inline void bitmap_clear(uint32_t bit) {
    pmm_bitmap[bit / 8] &= ~(1 << (bit % 8));
}

static inline uint8_t bitmap_test(uint32_t bit) {
    return pmm_bitmap[bit / 8] & (1 << (bit % 8));
}

// ============================================================================
// ОСВОБОЖДЕНИЕ РЕГИОНА (Punching Holes - шаг 1)
// ============================================================================
static void pmm_free_region(uint64_t base, uint64_t length) {
    // Выравнивание по границам страниц (4 KB)
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
        // Освобождаем только если страница ещё не свободна (защита от дубликатов в E820)
        if (bitmap_test(i)) { 
            bitmap_clear(i);
            total_available_pages++;
        }
    }
}

// ============================================================================
// РЕЗЕРВИРОВАНИЕ РЕГИОНА (Punching Holes - шаг 2)
// ============================================================================
void pmm_reserve_region(uint64_t base, uint64_t end) {
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

// ============================================================================
// ИНИЦИАЛИЗАЦИЯ PMM (Safe by Default + E820 Parsing)
// ============================================================================
void pmm_init(multiboot_info_t* info) {
    e820_count = 0;
    
    // ШАГ 1: Safe by Default — помечаем ВСЮ память как ЗАНЯТУЮ
    for (uint32_t i = 0; i < sizeof(pmm_bitmap); i++) {
        pmm_bitmap[i] = 0xFF;
    }
    total_available_pages = 0;
    used_pages = 0;

    serial_print("[PMM] Starting initialization (Safe by Default)...\n");

    // ШАГ 2: Парсинг E820 карты от GRUB
    if (info && (info->flags & MULTIBOOT_INFO_MEM_MAP)) {
        serial_print("[PMM] E820 Memory Map detected.\n");
        
        // mmap_addr — ФИЗИЧЕСКИЙ адрес. Благодаря Direct Map он доступен напрямую.
        multiboot_memory_map_t* mmap = (multiboot_memory_map_t*)(uintptr_t)info->mmap_addr;
        multiboot_memory_map_t* mmap_end = (multiboot_memory_map_t*)(uintptr_t)(info->mmap_addr + info->mmap_length);

        while (mmap < mmap_end) {
            // Сохраняем в наш внутренний массив для команды `memmap` в Shell
            if (e820_count < MAX_E820_ENTRIES) {
                e820_map[e820_count].addr = mmap->addr;
                e820_map[e820_count].len = mmap->len;
                e820_map[e820_count].type = mmap->type;
                e820_count++;
            }

            // Освобождаем ТОЛЬКО реальную RAM (type == 1)
            if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE) {
                pmm_free_region(mmap->addr, mmap->len);
            }

            // Переход к следующей записи (Нюанс 14: size не включает само поле size!)
            mmap = (multiboot_memory_map_t*)((uint32_t)mmap + mmap->size + sizeof(uint32_t));
        }
    } else {
        serial_print("[PMM] WARNING: No E820 map found! Fallback to hardcoded 256MB.\n");
        pmm_free_region(0x00100000, 256 * 1024 * 1024);
    }

    k_printf("[PMM] Available pages BEFORE reservations: %u\n", total_available_pages);

    // ШАГ 3: Punching Holes — жесткое резервирование критических зон
    
    // А) Нижние 1 МБ (IVT, BIOS Data Area, VGA RAM) — НИКОГДА не трогать!
    pmm_reserve_region(0x00000000, 0x00100000);

    // Б) Образ ядра (используем Linker Symbols + VIRT_TO_PHYS)
    // _kernel_start/_kernel_end — виртуальные адреса (Higher Half), переводим в физические
    uint32_t phys_k_start = VIRT_TO_PHYS((uint32_t)_kernel_start);
    uint32_t phys_k_end = VIRT_TO_PHYS((uint32_t)_kernel_end);
    pmm_reserve_region(phys_k_start, phys_k_end);

    // В) Структура Multiboot info и E820 массив (защита от кривых BIOS)
    if (info) {
        uint32_t mb_phys_start = (uint32_t)info;
        uint32_t mb_phys_end = mb_phys_start + sizeof(multiboot_info_t) + info->mmap_length;
        pmm_reserve_region(mb_phys_start, mb_phys_end);
    }

    // Г) PCI MMIO Hole (0xE0000000 - 0xFFFFFFFF) — защита фреймбуфера и устройств
    pmm_reserve_region(0xE0000000ULL, 0xFFFFFFFFULL);

    k_printf("[PMM] Available pages AFTER reservations: %u\n", total_available_pages);
    k_printf("[PMM] Initialized. Available: %u pages (%u MB).\n", 
             total_available_pages, (total_available_pages * PMM_PAGE_SIZE) / (1024 * 1024));
}

// ============================================================================
// АЛЛОКАЦИЯ СТРАНИЦЫ (Hardware-Accelerated BSF / TZCNT)
// ============================================================================
uint32_t pmm_alloc_page(void) {
    uint32_t* bitmap_words = (uint32_t*)pmm_bitmap;
    uint32_t word_count = sizeof(pmm_bitmap) / sizeof(uint32_t);
    
    for (uint32_t w = 0; w < word_count; w++) {
        uint32_t word = bitmap_words[w];
        
        // Fast-Forwarding: пропускаем полностью занятые блоки (32 страницы)
        if (word == 0xFFFFFFFF) continue;
        
        // Инвертируем слово: теперь 1 означает "свободно", 0 - "занято"
        uint32_t free_bits = ~word;
        
        // Аппаратное ускорение: __builtin_ctz (Count Trailing Zeros)
        // Компилятор i686-elf-gcc превратит это в одну инструкцию BSF/TZCNT.
        // Возвращает индекс первого свободного бита (от 0 до 31).
        uint32_t b = __builtin_ctz(free_bits);
        
        // w << 5 эквивалентно w * 32, но работает быстрее
        uint32_t bit_index = (w << 5) + b; 
        
        // Защита от выхода за пределы, если PMM_PAGES_COUNT не кратно 32
        if (bit_index >= PMM_PAGES_COUNT) return 0;
        
        // Помечаем страницу как занятую напрямую в слове (O(1), без деления по модулю)
        bitmap_words[w] |= (1U << b);
        
        used_pages++;
        return bit_index * PMM_PAGE_SIZE;
    }
    
    // OOM fallback: защита от спама в лог
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
    // Валидация: адрес должен быть выровнен по странице
    if (phys_addr % PMM_PAGE_SIZE != 0) return;
    
    uint32_t page_index = phys_addr / PMM_PAGE_SIZE;
    if (page_index >= PMM_PAGES_COUNT) return;
    
    // Защита от double-free: освобождаем только если страница занята
    if (!bitmap_test(page_index)) return; 
    
    bitmap_clear(page_index);
    if (used_pages > 0) used_pages--;
}

// ============================================================================
// СТАТИСТИКА
// ============================================================================
uint32_t pmm_get_used_pages(void) { return used_pages; }
uint32_t pmm_get_free_pages(void) { return total_available_pages - used_pages; }
uint32_t pmm_get_total_pages(void) { return total_available_pages; }

// ============================================================================
// ДОСТУП К E820 КАРТЕ (для команды `memmap` в Shell)
// ============================================================================
const e820_entry_t* pmm_get_memory_map(uint32_t* count) {
    if (count) *count = e820_count;
    return e820_map;
}

// ============================================================================
// ОТЛАДОЧНЫЙ ВЫВОД E820 (ИСПРАВЛЕННЫЙ k_printf)
// ============================================================================
void pmm_dump_e820(void) {
    k_printf("\n--- [E820 Memory Map] ---\n");
    for (uint32_t i = 0; i < e820_count; i++) {
        const char* type_str = "Unknown";
        if (e820_map[i].type == 1) type_str = "Available RAM";
        else if (e820_map[i].type == 2) type_str = "Reserved";
        else if (e820_map[i].type == 3) type_str = "ACPI Reclaim";
        else if (e820_map[i].type == 4) type_str = "ACPI NVS";
        else if (e820_map[i].type == 5) type_str = "Bad RAM";
        
        // Нюанс 16: k_printf НЕ поддерживает %08x! Делим 64-битные адреса на части.
        uint32_t addr_hi = (uint32_t)(e820_map[i].addr >> 32);
        uint32_t addr_lo = (uint32_t)e820_map[i].addr;
        uint32_t end_hi = (uint32_t)((e820_map[i].addr + e820_map[i].len) >> 32);
        uint32_t end_lo = (uint32_t)(e820_map[i].addr + e820_map[i].len);
        
        k_printf(" %x%x - %x%x | Type: %u (%s)\n",
                 addr_hi, addr_lo, end_hi, end_lo,
                 e820_map[i].type, type_str);
    }
    k_printf("-------------------------\n\n");
}