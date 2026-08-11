// ============================================================================
// arm_fb.c — ARM Framebuffer Driver (Day 55 video spike)
// ============================================================================
// Минимальный BCM2835 framebuffer driver для Enclave OS ARM port.
//
// Архитектура:
//   1. Отправить property request через BCM2835 mailbox.
//   2. Получить framebuffer physical address, size, pitch.
//   3. Зарезервировать физическую память в PMM.
//   4. Замапить framebuffer 1MB sections в kernel virtual window.
//   5. Рисовать test pattern / fill.
//
// Zero Trust / Enclave notes:
//   - Framebuffer маппится только для kernel.
//   - User space не имеет доступа.
//   - XN enforced: framebuffer не исполняется.
//   - Если mailbox не отвечает, ядро продолжает работать через UART.
// ============================================================================

#include <stdint.h>
#include "config.h"
#include "arm_fb.h"
#include "arm_pmm.h"
#include "arm_vmm.h"
#include "hal/hal_mmu.h"
#include "hal/hal_uart.h"

// ============================================================================
// MMIO ACCESS
// ============================================================================

#define FB_MMIO32(reg) \
    (*(volatile uint32_t *)BCM2835_VIRT(reg))

// ============================================================================
// MAILBOX PROPERTY CONSTANTS
// ============================================================================

#define MBOX_REQ_CODE               0x00000000u
#define MBOX_RESP_SUCCESS           0x80000000u

#define MBOX_TAG_SET_PHYS_WH        0x00048003u
#define MBOX_TAG_SET_VIRT_WH        0x00048004u
#define MBOX_TAG_SET_DEPTH          0x00048005u
#define MBOX_TAG_ALLOCATE_BUFFER    0x00040001u
#define MBOX_TAG_GET_PITCH          0x00040008u

#define MBOX_TIMEOUT                0x00400000u

// ============================================================================
// FRAMEBUFFER STATE
// ============================================================================

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t pitch;
    uint32_t phys;
    uint32_t size;
    uint32_t map_size;
    uint32_t virt;
    uint32_t ready;
} arm_fb_t;

static arm_fb_t fb;

// Property buffer должен быть 16-byte aligned для mailbox.
static uint32_t prop_buf[128] __attribute__((aligned(16)));

// ============================================================================
// SMALL UART HELPERS
// ============================================================================

static void fb_uart_hex(uint32_t v)
{
    char buf[9];

    for (int i = 7; i >= 0; i--) {
        buf[i] = "0123456789ABCDEF"[v & 0xF];
        v >>= 4;
    }

    buf[8] = '\0';
    hal_uart_puts(buf);
}

static void fb_uart_dec(uint32_t v)
{
    char buf[12];
    int i = 11;

    buf[i] = '\0';

    if (v == 0) {
        hal_uart_puts("0");
        return;
    }

    while (v > 0 && i > 0) {
        buf[--i] = (char)('0' + (v % 10));
        v /= 10;
    }

    hal_uart_puts(&buf[i]);
}

// ============================================================================
// CACHE MAINTENANCE
// ============================================================================
// Mailbox property buffer находится в обычной cacheable RAM.
// Перед отправкой GPU нужно вытолкнуть dirty cache lines.
// После ответа GPU нужно инвалидировать cache, чтобы увидеть новые данные.
// ============================================================================

static void dcache_clean_range(uint32_t va, uint32_t size)
{
    if (size == 0)
        return;

    uint32_t start = va & ~31u;
    uint32_t end = (va + size + 31u) & ~31u;

    for (uint32_t addr = start; addr < end; addr += 32) {
        __asm__ volatile (
            "mcr p15, 0, %0, c7, c10, 1"
            :
            : "r"(addr)
            : "memory"
        );
    }

    uint32_t zero = 0;

    // DSB
    __asm__ volatile (
        "mcr p15, 0, %0, c7, c10, 4"
        :
        : "r"(zero)
        : "memory"
    );
}

static void dcache_clean_inv_range(uint32_t va, uint32_t size)
{
    if (size == 0)
        return;

    uint32_t start = va & ~31u;
    uint32_t end = (va + size + 31u) & ~31u;

    for (uint32_t addr = start; addr < end; addr += 32) {
        __asm__ volatile (
            "mcr p15, 0, %0, c7, c14, 1"
            :
            : "r"(addr)
            : "memory"
        );
    }

    uint32_t zero = 0;

    // DSB
    __asm__ volatile (
        "mcr p15, 0, %0, c7, c10, 4"
        :
        : "r"(zero)
        : "memory"
    );
}

// ============================================================================
// MAILBOX LOW LEVEL
// ============================================================================

static int mbox_wait_read_ready(void)
{
    for (volatile uint32_t i = 0; i < MBOX_TIMEOUT; i++) {
        uint32_t status = FB_MMIO32(BCM2835_MBOX0_STATUS);

        if (!(status & BCM2835_MBOX_STATUS_EMPTY))
            return 0;
    }

    return -1;
}

static int mbox_wait_write_ready(void)
{
    for (volatile uint32_t i = 0; i < MBOX_TIMEOUT; i++) {
        uint32_t status = FB_MMIO32(BCM2835_MBOX1_STATUS);

        if (!(status & BCM2835_MBOX_STATUS_FULL))
            return 0;
    }

    return -1;
}

static void mbox_drain(void)
{
    for (volatile uint32_t i = 0; i < 1024; i++) {
        uint32_t status = FB_MMIO32(BCM2835_MBOX0_STATUS);

        if (status & BCM2835_MBOX_STATUS_EMPTY)
            break;

        (void)FB_MMIO32(BCM2835_MBOX0_READ);
    }
}

// ============================================================================
// PROPERTY REQUEST BUILD
// ============================================================================

static void build_fb_request(void)
{
    uint32_t i = 0;

    // Header
    prop_buf[i++] = 0;                  // total size, filled later
    prop_buf[i++] = MBOX_REQ_CODE;

    // Tag: set physical width/height
    prop_buf[i++] = MBOX_TAG_SET_PHYS_WH;
    prop_buf[i++] = 8;                  // value buffer size
    prop_buf[i++] = 0;                  // request indicator
    prop_buf[i++] = fb.width;
    prop_buf[i++] = fb.height;

    // Tag: set virtual width/height
    prop_buf[i++] = MBOX_TAG_SET_VIRT_WH;
    prop_buf[i++] = 8;
    prop_buf[i++] = 0;
    prop_buf[i++] = fb.width;
    prop_buf[i++] = fb.height;

    // Tag: set depth
    prop_buf[i++] = MBOX_TAG_SET_DEPTH;
    prop_buf[i++] = 4;
    prop_buf[i++] = 0;
    prop_buf[i++] = fb.depth;

    // Tag: allocate buffer
    // Request: alignment
    // Response: base address, size
    prop_buf[i++] = MBOX_TAG_ALLOCATE_BUFFER;
    prop_buf[i++] = 8;
    prop_buf[i++] = 0;
    prop_buf[i++] = 0x00100000u;        // 1 MB alignment
    prop_buf[i++] = 0;

    // Tag: get pitch
    prop_buf[i++] = MBOX_TAG_GET_PITCH;
    prop_buf[i++] = 4;
    prop_buf[i++] = 0;
    prop_buf[i++] = 0;

    // End tag
    prop_buf[i++] = 0;

    // Total size in bytes
    prop_buf[0] = i * 4;
}

// ============================================================================
// PROPERTY CALL
// ============================================================================

static int mbox_property_call(uint32_t bus_offset)
{
    uint32_t phys = VIRT_TO_PHYS((uint32_t)prop_buf);
    uint32_t bus = phys | bus_offset;
    uint32_t total = prop_buf[0];

    // Ensure GPU sees our request.
    dcache_clean_range((uint32_t)prop_buf, total);

    if (mbox_wait_write_ready() < 0)
        return -1;

    FB_MMIO32(BCM2835_MBOX1_WRITE) =
        (bus & ~0xFu) | BCM2835_MBOX_CHANNEL_PROP;

    if (mbox_wait_read_ready() < 0)
        return -1;

    uint32_t resp = FB_MMIO32(BCM2835_MBOX0_READ);

    if ((resp & 0xFu) != BCM2835_MBOX_CHANNEL_PROP)
        return -1;

    // Ensure CPU sees GPU response.
    dcache_clean_inv_range((uint32_t)prop_buf, total);

    if (prop_buf[1] != MBOX_RESP_SUCCESS)
        return -1;

    return 0;
}

// ============================================================================
// RESPONSE PARSE
// ============================================================================

static int parse_fb_response(void)
{
    uint32_t total_words = prop_buf[0] / 4;
    uint32_t i = 2;

    fb.phys = 0;
    fb.size = 0;
    fb.pitch = 0;

    while (i + 2 < total_words) {
        uint32_t tag = prop_buf[i];

        if (tag == 0)
            break;

        uint32_t value_bytes = prop_buf[i + 1] & 0xFFFFu;
        uint32_t value_words = (value_bytes + 3) / 4;
        uint32_t value_index = i + 3;

        if (value_index + value_words > total_words)
            break;

        if (tag == MBOX_TAG_ALLOCATE_BUFFER && value_words >= 2) {
            // Bus address may include cache alias bits.
            fb.phys = prop_buf[value_index] & 0x3FFFFFFFu;
            fb.size = prop_buf[value_index + 1];
        } else if (tag == MBOX_TAG_GET_PITCH && value_words >= 1) {
            fb.pitch = prop_buf[value_index];
        }

        i += 3 + value_words;
    }

    if (fb.phys == 0 || fb.size == 0)
        return -1;

    if (fb.pitch == 0)
        fb.pitch = fb.width * (fb.depth / 8);

    return 0;
}

// ============================================================================
// KERNEL MAPPING
// ============================================================================
// Маппим framebuffer в boot TTBR0 1MB sections.
//
// ВАЖНО:
//   Этот код должен вызываться ДО hal_mmu_create_space(),
//   чтобы новые kernel entries были скопированы во все user address spaces.
// ============================================================================

static int arm_fb_map_kernel(void)
{
    if (fb.map_size == 0)
        return -1;

    if (fb.map_size > ARM_FB_MAX_MAP_SIZE)
        return -1;

    if ((fb.phys + fb.map_size) > BCM2835_RAM_SIZE_DEFAULT)
        return -1;

    volatile uint32_t *boot_l1 =
        (volatile uint32_t *)PHYS_TO_VIRT(arm_vmm_get_boot_ttbr0());

    uint32_t l1_idx = ARM_FB_VIRT_BASE >> 20;
    uint32_t sections = fb.map_size >> 20;

    if (sections == 0)
        return -1;

    if ((l1_idx + sections) > ARM_TTBR0_ENTRIES)
        return -1;

    // Safety: do not overwrite existing mappings.
    for (uint32_t i = 0; i < sections; i++) {
        if ((boot_l1[l1_idx + i] & 0x3u) != ARM_L1_TYPE_FAULT) {
            hal_uart_puts("[FB] kernel FB window already mapped\r\n");
            return -1;
        }
    }

    // Framebuffer as kernel-only device memory:
    //   - kernel RW
    //   - user no access
    //   - XN
    //   - non-cacheable/device
    for (uint32_t i = 0; i < sections; i++) {
        uint32_t pa = fb.phys + (i * 0x00100000u);

        uint32_t desc = (pa & 0xFFF00000u)
                      | ARM_L1_SECTION_DOMAIN(ARM_DOMAIN_KERNEL)
                      | ARM_L1_SECTION_AP(ARM_AP_KERNEL_RW)
                      | ARM_L1_SECTION_B
                      | ARM_L1_SECTION_XN
                      | ARM_L1_TYPE_SECTION;

        boot_l1[l1_idx + i] = desc;
    }

    // Make page table updates visible to page table walker.
    dcache_clean_range((uint32_t)(boot_l1 + l1_idx), sections * 4);

    hal_mmu_flush_tlb_all();

    return 0;
}

// ============================================================================
// PUBLIC API
// ============================================================================

int arm_fb_init(void)
{
    fb.ready = 0;
    fb.width = 640;
    fb.height = 480;
    fb.depth = 32;
    fb.pitch = 0;
    fb.phys = 0;
    fb.size = 0;
    fb.map_size = 0;
    fb.virt = 0;

    hal_uart_puts("[FB] Initializing BCM2835 framebuffer...\r\n");

    build_fb_request();

    // Сначала пробуем bus address с offset 0x40000000.
    if (mbox_property_call(BCM2835_MBOX_BUS_OFFSET) < 0) {
        hal_uart_puts("[FB] mailbox failed with bus offset, retrying physical\r\n");

        mbox_drain();
        build_fb_request();

        // Fallback: некоторые эмуляторы могут ожидать чистый physical address.
        if (mbox_property_call(0) < 0) {
            hal_uart_puts("[FB] mailbox timeout / no framebuffer\r\n");
            return -1;
        }
    }

    if (parse_fb_response() < 0) {
        hal_uart_puts("[FB] failed to parse framebuffer response\r\n");
        return -1;
    }

    // Для 1MB section mapping нужен 1MB-aligned physical base.
    if (fb.phys & 0x000FFFFFu) {
        hal_uart_puts("[FB] framebuffer physical base not 1MB aligned\r\n");
        return -1;
    }

    fb.map_size = (fb.size + 0x000FFFFFu) & ~0x000FFFFFu;

    if (fb.map_size == 0)
        fb.map_size = 0x00100000u;

    if (fb.map_size > ARM_FB_MAX_MAP_SIZE) {
        hal_uart_puts("[FB] framebuffer too large\r\n");
        return -1;
    }

    hal_uart_puts("[FB] phys=0x");
    fb_uart_hex(fb.phys);
    hal_uart_puts(" size=0x");
    fb_uart_hex(fb.size);
    hal_uart_puts(" pitch=");
    fb_uart_dec(fb.pitch);
    hal_uart_puts("\r\n");

    // Резервируем физическую память, чтобы PMM не переиспользовал её.
    arm_pmm_reserve_range(fb.phys, fb.map_size);

    if (arm_fb_map_kernel() < 0) {
        hal_uart_puts("[FB] failed to map framebuffer into kernel space\r\n");
        return -1;
    }

    fb.virt = ARM_FB_VIRT_BASE;
    fb.ready = 1;

    hal_uart_puts("[FB] framebuffer ready: ");
    fb_uart_dec(fb.width);
    hal_uart_puts("x");
    fb_uart_dec(fb.height);
    hal_uart_puts("x");
    fb_uart_dec(fb.depth);
    hal_uart_puts(" virt=0x");
    fb_uart_hex(fb.virt);
    hal_uart_puts("\r\n");

    return 0;
}

uint32_t arm_fb_is_ready(void)
{
    return fb.ready;
}

void arm_fb_fill(uint32_t color)
{
    if (!fb.ready)
        return;

    if (fb.depth != 32)
        return;

    volatile uint8_t *base = (volatile uint8_t *)fb.virt;

    for (uint32_t y = 0; y < fb.height; y++) {
        volatile uint32_t *line =
            (volatile uint32_t *)(base + (y * fb.pitch));

        for (uint32_t x = 0; x < fb.width; x++) {
            line[x] = color;
        }
    }
}

void arm_fb_test_pattern(void)
{
    if (!fb.ready)
        return;

    if (fb.depth != 32)
        return;

    // SMPTE-like color bars.
    static const uint32_t colors[8] = {
        0x00FFFFFF,   // white
        0x00FFFF00,   // yellow
        0x0000FFFF,   // cyan
        0x0000FF00,   // green
        0x00FF00FF,   // magenta
        0x00FF0000,   // red
        0x000000FF,   // blue
        0x00000000    // black
    };

    volatile uint8_t *base = (volatile uint8_t *)fb.virt;

    for (uint32_t y = 0; y < fb.height; y++) {
        volatile uint32_t *line =
            (volatile uint32_t *)(base + (y * fb.pitch));

        for (uint32_t x = 0; x < fb.width; x++) {
            uint32_t idx = (x * 8) / fb.width;

            if (idx > 7)
                idx = 7;

            line[x] = colors[idx];
        }
    }

    hal_uart_puts("[FB] test pattern drawn\r\n");
}
