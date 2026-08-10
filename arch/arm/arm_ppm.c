// ============================================================================
// arm_pmm.c — ARM Physical Memory Manager (Day 51B)
// ============================================================================
// Bitmap-based page allocator для BCM2835 (Raspberry Pi 1, ARM1176JZF-S).
//
// Дизайн:
//   - 1 бит = 1 страница (4 KB)
//   - Safe-by-default: bitmap = 0xFF (всё занято при init)
//   - O(1) allocation через software ctz32()
//   - IRQ-safe: hal_irq_save/restore вокруг bitmap ops
//   - ATAGS parsing для RAM discovery
//
// ⚠️ ARM1176 (ARMv6): НЕ использовать __builtin_ctz / __builtin_clz.
// ============================================================================

#include <stdint.h>
#include "config.h"
#include "arm_pmm.h"
#include "hal/hal_cpu.h"
#include "hal/hal_uart.h"

// ============================================================================
// BITMAP STRUCTURE
// ============================================================================

// Максимальный поддерживаемый RAM: 512 MB = 131072 страницы.
// Bitmap size: 131072 bits = 16384 bytes = 16 KB.
#define PMM_MAX_PAGES       (512 * 1024 * 1024 / 4096)
#define PMM_BITMAP_SIZE     (PMM_MAX_PAGES / 8)

// Bitmap: 1 = занят, 0 = свободен.
static uint8_t pmm_bitmap[PMM_BITMAP_SIZE] __attribute__((aligned(4)));

// Accounting
static uint32_t pmm_total_pages = 0;
static uint32_t pmm_free_pages = 0;
static uint32_t pmm_allocs = 0;
static uint32_t pmm_frees = 0;

// ============================================================================
// SOFTWARE CTZ (ARMv6 safe)
// ============================================================================

static inline uint32_t ctz32(uint32_t x)
{
    uint32_t n = 0;
    if (!(x & 0x0000FFFFu)) { n += 16; x >>= 16; }
    if (!(x & 0x000000FFu)) { n +=  8; x >>=  8; }
    if (!(x & 0x0000000Fu)) { n +=  4; x >>=  4; }
    if (!(x & 0x00000003u)) { n +=  2; x >>=  2; }
    if (!(x & 0x00000001u)) { n +=  1; }
    return n;
}

// ============================================================================
// ATAGS PARSING
// ============================================================================

#define ATAG_NONE   0x00000000
#define ATAG_CORE   0x54410001
#define ATAG_MEM    0x54410002

struct atag_header {
    uint32_t size;    // Size in words (including header)
    uint32_t tag;     // Tag type
};

struct atag_mem {
    uint32_t size;    // Size in bytes
    uint32_t start;   // Start address
};

// Parse ATAGS to find RAM size.
// Returns RAM size in bytes, or fallback_size if not found.
static uint32_t parse_atags(uint32_t atags_addr, uint32_t fallback_size)
{
    if (atags_addr == 0) {
        hal_uart_puts("[PMM] ATAGS addr = 0, using fallback\r\n");
        return fallback_size;
    }

    struct atag_header *atag = (struct atag_header *)atags_addr;

    while (atag->tag != ATAG_NONE && atag->size != 0) {
        if (atag->tag == ATAG_MEM) {
            struct atag_mem *mem = (struct atag_mem *)((uint8_t *)atag + 8);
            hal_uart_puts("[PMM] ATAG_MEM: size=");
            // Print size in MB
            uint32_t size_mb = mem->size / (1024 * 1024);
            char buf[12];
            int i = 11;
            buf[i] = '\0';
            if (size_mb == 0) {
                hal_uart_puts("0");
            } else {
                while (size_mb > 0 && i > 0) {
                    buf[--i] = (char)('0' + (size_mb % 10));
                    size_mb /= 10;
                }
                hal_uart_puts(&buf[i]);
            }
            hal_uart_puts(" MB, start=0x");
            
            // Print start address
            char hex_buf[9];
            for (int j = 7; j >= 0; j--) {
                hex_buf[j] = "0123456789ABCDEF"[mem->start & 0xF];
                mem->start >>= 4;
            }
            hex_buf[8] = '\0';
            hal_uart_puts(hex_buf);
            hal_uart_puts("\r\n");
            
            return mem->size;
        }

        // Move to next ATAG
        atag = (struct atag_header *)((uint8_t *)atag + atag->size * 4);
    }

    hal_uart_puts("[PMM] ATAG_MEM not found, using fallback\r\n");
    return fallback_size;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void arm_pmm_init(uint32_t atags_addr, uint32_t fallback_size)
{
    hal_uart_puts("[PMM] Initializing ARM Physical Memory Manager...\r\n");

    // Parse ATAGS to get RAM size
    uint32_t ram_size = parse_atags(atags_addr, fallback_size);

    // Calculate total pages
    pmm_total_pages = ram_size / 4096;
    if (pmm_total_pages > PMM_MAX_PAGES) {
        pmm_total_pages = PMM_MAX_PAGES;
        hal_uart_puts("[PMM] RAM size capped at 512 MB\r\n");
    }

    // Safe-by-default: mark all pages as used (bitmap = 0xFF)
    for (uint32_t i = 0; i < PMM_BITMAP_SIZE; i++) {
        pmm_bitmap[i] = 0xFF;
    }

    // Free usable RAM (skip lower 1 MB for now, will be reserved later)
    uint32_t start_page = 0x00100000 / 4096;  // Start after 1 MB
    for (uint32_t page = start_page; page < pmm_total_pages; page++) {
        uint32_t byte_idx = page / 8;
        uint32_t bit_idx = page % 8;
        pmm_bitmap[byte_idx] &= ~(1u << bit_idx);
        pmm_free_pages++;
    }

    hal_uart_puts("[PMM] Total pages: ");
    char buf[12];
    int i = 11;
    buf[i] = '\0';
    uint32_t val = pmm_total_pages;
    if (val == 0) {
        hal_uart_puts("0");
    } else {
        while (val > 0 && i > 0) {
            buf[--i] = (char)('0' + (val % 10));
            val /= 10;
        }
        hal_uart_puts(&buf[i]);
    }

    hal_uart_puts(", free pages: ");
    i = 11;
    buf[i] = '\0';
    val = pmm_free_pages;
    if (val == 0) {
        hal_uart_puts("0");
    } else {
        while (val > 0 && i > 0) {
            buf[--i] = (char)('0' + (val % 10));
            val /= 10;
        }
        hal_uart_puts(&buf[i]);
    }
    hal_uart_puts("\r\n");
}

// ============================================================================
// RESERVATION
// ============================================================================

void arm_pmm_reserve_range(uint32_t start, uint32_t size)
{
    if (start % 4096 != 0) {
        hal_uart_puts("[PMM] WARNING: reserve_range start not 4KB aligned\r\n");
        start &= ~4095u;
    }

    size = (size + 4095) & ~4095u;  // Round up to 4 KB

    uint32_t start_page = start / 4096;
    uint32_t end_page = (start + size) / 4096;

    if (end_page > pmm_total_pages) {
        end_page = pmm_total_pages;
    }

    for (uint32_t page = start_page; page < end_page; page++) {
        uint32_t byte_idx = page / 8;
        uint32_t bit_idx = page % 8;

        if (!(pmm_bitmap[byte_idx] & (1u << bit_idx))) {
            // Page was free, now marking as used
            pmm_bitmap[byte_idx] |= (1u << bit_idx);
            if (pmm_free_pages > 0) {
                pmm_free_pages--;
            }
        }
    }

    hal_uart_puts("[PMM] Reserved 0x");
    char hex_buf[9];
    for (int j = 7; j >= 0; j--) {
        hex_buf[j] = "0123456789ABCDEF"[start & 0xF];
        start >>= 4;
    }
    hex_buf[8] = '\0';
    hal_uart_puts(hex_buf);
    hal_uart_puts(" - 0x");
    
    uint32_t end = start + size;
    for (int j = 7; j >= 0; j--) {
        hex_buf[j] = "0123456789ABCDEF"[end & 0xF];
        end >>= 4;
    }
    hex_buf[8] = '\0';
    hal_uart_puts(hex_buf);
    hal_uart_puts("\r\n");
}

// ============================================================================
// ALLOCATION
// ============================================================================

uint32_t arm_pmm_alloc_page(void)
{
    uint32_t flags = hal_irq_save();

    if (pmm_free_pages == 0) {
        hal_irq_restore(flags);
        hal_uart_puts("[PMM] OOM: no free pages\r\n");
        return 0;
    }

    // Linear scan through bitmap
    for (uint32_t i = 0; i < (pmm_total_pages + 7) / 8; i++) {
        if (pmm_bitmap[i] != 0xFF) {
            // Found a byte with at least one free bit
            uint32_t bit = ctz32(~pmm_bitmap[i]);
            if (bit < 8) {
                uint32_t page = i * 8 + bit;
                if (page < pmm_total_pages) {
                    pmm_bitmap[i] |= (1u << bit);
                    pmm_free_pages--;
                    pmm_allocs++;
                    hal_irq_restore(flags);
                    return page * 4096;
                }
            }
        }
    }

    hal_irq_restore(flags);
    hal_uart_puts("[PMM] OOM: scan failed\r\n");
    return 0;
}

// ============================================================================
// DEALLOCATION
// ============================================================================

void arm_pmm_free_page(uint32_t phys_addr)
{
    if (phys_addr % 4096 != 0) {
        hal_uart_puts("[PMM] ERROR: free_page addr not 4KB aligned\r\n");
        return;
    }

    uint32_t page = phys_addr / 4096;
    if (page >= pmm_total_pages) {
        hal_uart_puts("[PMM] ERROR: free_page addr out of range\r\n");
        return;
    }

    uint32_t flags = hal_irq_save();

    uint32_t byte_idx = page / 8;
    uint32_t bit_idx = page % 8;

    if (pmm_bitmap[byte_idx] & (1u << bit_idx)) {
        // Page was used, now freeing
        pmm_bitmap[byte_idx] &= ~(1u << bit_idx);
        pmm_free_pages++;
        pmm_frees++;
    } else {
        hal_uart_puts("[PMM] WARNING: double-free detected\r\n");
    }

    hal_irq_restore(flags);
}

// ============================================================================
// ACCOUNTING
// ============================================================================

uint32_t arm_pmm_get_free_pages(void)
{
    return pmm_free_pages;
}

uint32_t arm_pmm_get_total_pages(void)
{
    return pmm_total_pages;
}

uint32_t arm_pmm_get_allocs(void)
{
    return pmm_allocs;
}

uint32_t arm_pmm_get_frees(void)
{
    return pmm_frees;
}

void arm_pmm_check_balance(void)
{
    hal_uart_puts("[PMM] Accounting: allocs=");
    char buf[12];
    int i = 11;
    buf[i] = '\0';
    uint32_t val = pmm_allocs;
    if (val == 0) {
        hal_uart_puts("0");
    } else {
        while (val > 0 && i > 0) {
            buf[--i] = (char)('0' + (val % 10));
            val /= 10;
        }
        hal_uart_puts(&buf[i]);
    }

    hal_uart_puts(", frees=");
    i = 11;
    buf[i] = '\0';
    val = pmm_frees;
    if (val == 0) {
        hal_uart_puts("0");
    } else {
        while (val > 0 && i > 0) {
            buf[--i] = (char)('0' + (val % 10));
            val /= 10;
        }
        hal_uart_puts(&buf[i]);
    }

    hal_uart_puts(", free_pages=");
    i = 11;
    buf[i] = '\0';
    val = pmm_free_pages;
    if (val == 0) {
        hal_uart_puts("0");
    } else {
        while (val > 0 && i > 0) {
            buf[--i] = (char)('0' + (val % 10));
            val /= 10;
        }
        hal_uart_puts(&buf[i]);
    }
    hal_uart_puts("\r\n");

    if (pmm_allocs != pmm_frees) {
        hal_uart_puts("[PMM] WARNING: alloc/free mismatch (possible leak)\r\n");
    } else {
        hal_uart_puts("[PMM] Balance OK\r\n");
    }
}
