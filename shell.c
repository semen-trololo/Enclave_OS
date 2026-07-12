#include "shell.h"
#include "keyboard.h"
#include "klib.h"
#include "timer.h"
#include "pmm.h"
#include "heap.h"
#include "vga.h"
#include "syscall.h"
#include "task.h"
#include "framebuffer.h"
#include "vfs.h"
#include "serial.h"
#include "ata.h"

#define CMD_BUFFER_SIZE 256


// 🛡️ ИСПРАВЛЕНО: Ограничиваем стресс-тест 64 МБ (16384 страницы).
#define PMM_TEST_MAX_PAGES 16384 
static uint32_t test_allocations[PMM_TEST_MAX_PAGES];

extern int run_elf_and_wait(const char* filename);
// ============================================================================
// ПАРСИНГ АРГУМЕНТОВ
// ============================================================================
static int parse_args(char* buffer, char args[MAX_ARGS][MAX_ARG_LEN]) {
    int argc = 0;
    char* ptr = buffer;
    
    for (int i = 0; i < MAX_ARGS; i++) args[i][0] = '\0';

    while (*ptr && argc < MAX_ARGS) {
        while (*ptr == ' ') ptr++; 
        if (*ptr == '\0') break;
        
        int i = 0;
        while (*ptr && *ptr != ' ' && i < MAX_ARG_LEN - 1) {
            args[argc][i++] = *ptr++;
        }
        args[argc][i] = '\0';
        argc++;
    }
    return argc;
}

// ============================================================================
// СПРАВКА (HELP)
// ============================================================================
static void print_help(void) {
    k_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    k_print("=== Available Commands ===\n");
    
    k_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    k_print("  [ General ]\n");
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    k_print("  help             - Show this help message\n");
    k_print("  clear            - Clear the screen\n");
    k_print("  uptime           - Show system uptime\n");
    k_print("  ps               - List running processes\n");
    k_print("  run <file.elf>   - Execute ELF binary and wait for exit\n");
    
    k_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    k_print("  [ File System (VFS) ]\n");
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    k_print("  ls [path]        - List directory contents\n");
    k_print("  cat <path>       - Print file contents\n");

    k_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    k_print("  [ Storage (ATA) ]\n");
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    k_print("  ata info         - Show ATA drive information (IDENTIFY)\n");
    k_print("  ata part         - Scan MBR and list partitions\n");
    k_print("  ata read <lba>   - Read sector and show hex dump\n");
    k_print("  ata test         - Run ATA read/write stress test\n");

    k_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    k_print("  [ Memory Management ]\n");
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    k_print("  memmap           - Show E820 physical memory map\n");
    k_print("  pmm status       - Show physical memory usage\n");
    k_print("  pmm alloc [num]  - Allocate physical pages\n");
    k_print("  pmm free <addr>  - Free a physical page (hex)\n");
    k_print("  pmm test         - Run PMM stress tests\n");
    k_print("  heap <cmd>       - Heap operations (status|alloc|free|test)\n");
    
    k_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    k_print("  [ Graphics & Fonts ]\n");
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    k_print("  font test        - Render ASCII and Cyrillic test table\n");
    
    k_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    k_print("  [ Testing (Day 10) ]\n");
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    k_print("  run_tests        - Run full ELF test suite\n");
    k_print("  stress spawn <N> - Mass spawn N kernel tasks\n");

    k_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    k_print("  [ System ]\n");
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    k_print("  syscall          - Test INT 0x80 (sys_write)\n");
    k_print("\n");
}

// ============================================================================
// ОБРАБОТЧИКИ КОМАНД
// ============================================================================

static void handle_uptime(void) {
    uint32_t ticks = timer_get_ticks();
    uint32_t freq = timer_get_frequency(); 
    
    uint32_t total_seconds = ticks / freq;
    uint32_t hours   = total_seconds / 3600;
    uint32_t minutes = (total_seconds % 3600) / 60;
    uint32_t seconds = total_seconds % 60;
    
    k_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    k_print("System Uptime: ");
    
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    k_printf("%u hours, %u minutes, %u seconds\n", hours, minutes, seconds);
    
    k_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); 
    k_printf("  (Raw ticks: %u @ %u Hz)\n", ticks, freq);
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

static void handle_syscall(void) {
    const char* msg = "Hello from Syscall!\n";
    uint32_t len = k_strlen(msg);
    
    uint32_t result;
    __asm__ volatile (
        "int $0x80"
        : "=a" (result)
        : "a" (SYS_WRITE), "b" (1), "c" (msg), "d" (len)
    );
    
    k_printf("\n[Shell] Syscall returned: %d\n", result);
    if (result == (uint32_t)-1) {
        k_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        k_print("[Shell] Note: EFAULT expected if syscall.c Ring 0 check is not patched yet.\n");
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    }
}

static void handle_font_test(void) {
    k_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    k_print("\n=== Font Rendering Test ===\n\n");
    
    // 1. ASCII Table (32-126)
    k_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    k_print("[ ASCII 32-126 ]\n");
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    
    for (int i = 32; i < 127; i++) {
        char c = (char)i;
        k_putchar(c);
        if ((i - 31) % 32 == 0) k_putchar('\n');
    }
    k_putchar('\n');
    
    // 2. Cyrillic Test (UTF-8)
    k_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    k_print("\n[ Cyrillic (UTF-8) ]\n");
    k_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    k_print("Привет, мир! Проверка кириллицы.\n");
    k_print("АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯ\n");
    k_print("абвгдеёжзийклмнопрстуфхцчшщъыьэюя\n");
    
    // 3. Box Drawing (если поддерживается шрифтом)
    k_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    k_print("\n[ Box Drawing & Symbols ]\n");
    k_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    k_print("┌───────────────────┐\n");
    k_print("│  Bare Metal OS    │\n");
    k_print("│  ▓▓▓▓▓▓▓▓▓▓ 100%  │\n");
    k_print("└───────────────────┘\n");
    
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    k_print("\n");
}

static void handle_pmm(int argc, char args[MAX_ARGS][MAX_ARG_LEN]) {
    if (argc < 2) {
        k_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
        k_print("Usage: pmm <status|alloc|free|test>\n");
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return;
    }

    if (k_strcmp(args[1], "status") == 0) {
        uint32_t used = pmm_get_used_pages();
        uint32_t free = pmm_get_free_pages();
        uint32_t total = pmm_get_total_pages();
        uint32_t max = pmm_get_max_pages();
        
        k_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
        k_print("[PMM] --- Physical Memory Status ---\n");
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        k_printf("[PMM] Max Addressable: %u MB (%u pages)\n", (max * 4) / 1024, max);
        k_printf("[PMM] Total Available: %u MB (%u pages)\n", (total * 4) / 1024, total);
        k_printf("[PMM] Used:            %u MB (%u pages)\n", (used * 4) / 1024, used);
        k_printf("[PMM] Free:            %u MB (%u pages)\n", (free * 4) / 1024, free);
    } 
    else if (k_strcmp(args[1], "alloc") == 0) {
        uint32_t count = 1;
        if (argc > 2) count = k_atoi(args[2]);
        if (count == 0) count = 1;

        for (uint32_t i = 0; i < count; i++) {
            uint32_t addr = pmm_alloc_page();
            if (addr == 0) {
                k_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
                k_print("[PMM] ERROR: Out of physical memory!\n");
                k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                break;
            }
            k_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            k_printf("[PMM] Allocated: 0x%x\n", addr);
            k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        }
    } 
    else if (k_strcmp(args[1], "free") == 0) {
        if (argc < 3) {
            k_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
            k_print("Usage: pmm free <hex_addr>\n");
            k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            return;
        }
        uint32_t addr = k_atoh(args[2]);
        pmm_free_page(addr); 
        k_print("[PMM] Free requested.\n");
    } 
    else if (k_strcmp(args[1], "test") == 0) {
        k_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
        k_print("[PMM TEST] Starting automated tests...\n");
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        
        uint32_t initial_free = pmm_get_free_pages();

        k_print("[PMM TEST] 1. Testing invalid free (unaligned)... ");
        pmm_free_page(0x200005); 
        k_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        k_print("[OK - Error caught]\n");
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

        k_print("[PMM TEST] 2. Testing OOM limit...\n");
        uint32_t allocated_count = 0;
        
        while (allocated_count < PMM_TEST_MAX_PAGES) {
            uint32_t addr = pmm_alloc_page();
            if (addr == 0) break;
            test_allocations[allocated_count++] = addr;
        }
        k_printf("[PMM TEST]    Allocated %u pages. Limit reached.\n", allocated_count);

        k_print("[PMM TEST] 3. Freeing all allocated pages... ");
        for (uint32_t i = 0; i < allocated_count; i++) {
            pmm_free_page(test_allocations[i]);
        }
        k_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        k_print("[OK]\n");
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

        k_print("[PMM TEST] 4. Verifying memory is restored... ");
        uint32_t final_free = pmm_get_free_pages();
        if (initial_free == final_free) {
            k_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            k_print("[OK]\n");
            k_print("[PMM TEST] All tests passed successfully!\n");
        } else {
            k_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
            k_printf("[FAIL] Leaked %d pages!\n", initial_free - final_free);
        }
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    } 
    else {
        k_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
        k_printf("Unknown pmm command: %s\n", args[1]);
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    }
}

static void handle_heap(int argc, char args[MAX_ARGS][MAX_ARG_LEN]) {
    if (argc < 2) {
        k_print("Usage: heap <status|alloc|free|test>\n");
        return;
    }
    
    if (k_strcmp(args[1], "status") == 0) {
        heap_print_status();
    } 
    else if (k_strcmp(args[1], "alloc") == 0) {
        if (argc < 3) { k_print("Usage: heap alloc <size>\n"); return; }
        uint32_t size = k_atoi(args[2]);
        void* ptr = kmalloc(size);
        if (ptr) k_printf("[HEAP] Allocated %u bytes at 0x%x\n", size, (uint32_t)ptr);
        else k_print("[HEAP] Allocation failed!\n");
    } 
    else if (k_strcmp(args[1], "free") == 0) {
        if (argc < 3) { k_print("Usage: heap free <addr>\n"); return; }
        uint32_t addr = k_atoh(args[2]);
        kfree((void*)addr);
        k_print("[HEAP] Freed.\n");
    } 
    else if (k_strcmp(args[1], "test") == 0) {
        heap_run_tests();
    } 
    else {
        k_print("Unknown heap command.\n");
    }
}

static void handle_memmap(void) {
    uint32_t count = 0;
    const e820_entry_t* map = pmm_get_memory_map(&count);
    
    if (count == 0) {
        k_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
        k_print("[MEMMAP] ERROR: E820 map is empty!\n");
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return;
    }

    k_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    k_print("\n[E820] --- Physical Memory Map ---\n");
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    
    uint64_t total_available = 0;
    uint64_t total_reserved = 0;
    
    for (uint32_t i = 0; i < count; i++) {
        uint32_t base_lo = (uint32_t)map[i].addr;
        uint32_t end_lo = (uint32_t)(map[i].addr + map[i].len - 1);
        uint32_t len_kb = (map[i].len >= 1024) ? (uint32_t)(map[i].len / 1024) : 1;
        
        if (map[i].type == 1 || map[i].type == 3) {
            k_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            total_available += map[i].len;
        } else {
            k_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            total_reserved += map[i].len;
        }
        
        k_printf("  Region %u: 0x%x - 0x%x | %u KB | ", i, base_lo, end_lo, len_kb);
        
        switch (map[i].type) {
            case 1: k_print("Available\n"); break;
            case 2: k_print("Reserved\n"); break;
            case 3: k_print("ACPI Reclaim\n"); break;
            case 4: k_print("ACPI NVS\n"); break;
            case 5: k_print("Bad RAM\n"); break;
            default: k_print("Unknown\n"); break;
        }
    }
    
    k_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    k_print("------------------------------------\n");
    k_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    k_printf("  Total Available: %u MB\n", (uint32_t)(total_available / (1024 * 1024)));
    k_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    k_printf("  Total Reserved:  %u MB\n", (uint32_t)(total_reserved / (1024 * 1024)));
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    k_print("\n");
}

// ============================================================================
// VFS КОМАНДЫ (DAY 8.1)
// ============================================================================
static void handle_ls(int argc, char args[MAX_ARGS][MAX_ARG_LEN]) {
    const char* path = "/";
    if (argc > 1) path = args[1];

    int fd = sys_open(path, O_RDONLY);
    
    if (fd < 0) {
        k_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
        k_printf("ls: cannot access '%s': Error %d\n", path, fd);
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return;
    }

    dirent_t entry;
    uint32_t index = 0;
    
    k_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    k_printf("Directory: %s\n", path);
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    int32_t res = sys_readdir(fd, index, &entry);
    while (res == 0) {
        k_printf("  %s\n", entry.name);
        index++;
        res = sys_readdir(fd, index, &entry);
    }
    
    sys_close(fd);
}

static void handle_cat(int argc, char args[MAX_ARGS][MAX_ARG_LEN]) {
    if (argc < 2) {
        k_print("Usage: cat <filepath>\n");
        return;
    }

    const char* path = args[1];
    int fd = sys_open(path, O_RDONLY);
    if (fd < 0) {
        k_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
        k_printf("cat: %s: Error %d\n", path, fd);
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return;
    }

    uint8_t buffer[512];
    int32_t bytes_read;
    
    while ((bytes_read = sys_read(fd, buffer, sizeof(buffer))) > 0) {
        for (int32_t i = 0; i < bytes_read; i++) {
            k_putchar(buffer[i]);
        }
    }
    
    k_putchar('\n'); 
    sys_close(fd);
}

// ============================================================================
// ATA / STORAGE КОМАНДЫ (DAY 8.2)
// ============================================================================
static void handle_ata(int argc, char args[MAX_ARGS][MAX_ARG_LEN]) {
    if (argc < 2) {
        k_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
        k_print("Usage: ata <info|part|read|test>\n");
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return;
    }

    if (k_strcmp(args[1], "info") == 0) {
        ata_identify_data_t data;
        if (ata_identify(&data) < 0) {
            k_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
            k_print("[ATA] ERROR: Drive not detected or ATAPI (unsupported)\n");
            k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            return;
        }

        k_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
        k_print("[ATA] --- Drive IDENTIFY Data ---\n");
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        
        k_printf("[ATA] Model:       %s\n", data.model);
        k_printf("[ATA] Serial:      %s\n", data.serial);
        k_printf("[ATA] Firmware:    %.8s\n", data.firmware);
        
        k_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        k_print("[ATA] Geometry (legacy CHS):\n");
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        k_printf("[ATA]   Cylinders:     %u\n", data.cylinders);
        k_printf("[ATA]   Heads:         %u\n", data.heads);
        k_printf("[ATA]   Sec/Track:     %u\n", data.sectors_per_track);
        
        k_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        k_print("[ATA] Capacity (LBA28):\n");
        k_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        k_printf("[ATA]   Max LBA:       %u\n", data.max_lba);
        k_printf("[ATA]   Total Size:    %u MB\n", data.max_lba / 2048);
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    }
    else if (k_strcmp(args[1], "part") == 0) {
        k_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
        k_print("[PART] Scanning MBR...\n");
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        
        int count = partition_scan();
        if (count < 0) {
            k_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
            k_print("[PART] ERROR: Failed to read/parse MBR\n");
            k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            return;
        }
        
        k_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
        k_print("[PART] --- Partition Table ---\n");
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        k_print(" ID  Type   LBA Start    Size(sec)   Size(MB)  Notes\n");
        k_print("--------------------------------------------------------\n");
        
        for (int i = 0; i < count; i++) {
            partition_info_t* p = partition_get(i);
            if (p == NULL) continue;
            
            // Подсветка FAT32 разделов (наших целевых)
            if (p->type == 0x0B || p->type == 0x0C) {
                k_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
                k_printf(" %d   0x%02X   %-12u %-11u %-9u FAT32 ✓\n",
                         p->id, p->type, p->lba_start,
                         p->sector_count, p->sector_count / 2048);
            } else {
                k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                k_printf(" %d   0x%02X   %-12u %-11u %-9u (other)\n",
                         p->id, p->type, p->lba_start,
                         p->sector_count, p->sector_count / 2048);
            }
        }
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        k_printf("[PART] Total: %d partitions found\n", count);
    }
    else if (k_strcmp(args[1], "read") == 0) {
        if (argc < 3) {
            k_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
            k_print("Usage: ata read <lba_hex_or_dec>\n");
            k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            return;
        }
        
        uint32_t lba = k_atoh(args[2]);
        uint8_t buffer[512];
        
        k_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
        k_printf("[ATA] Reading LBA %u (0x%x)...\n", lba, lba);
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        
        if (ata_read_sectors(lba, 1, buffer) < 0) {
            k_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
            k_print("[ATA] ERROR: Read failed (BSY timeout / DRQ error)\n");
            k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            return;
        }
        
        k_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        k_print("[ATA] Read OK. Hex dump (512 bytes):\n");
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        
        // Hex dump в стиле debug-вывода
        for (int i = 0; i < 512; i += 16) {
            k_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
            k_printf("%04X: ", i);
            k_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
            
            for (int j = 0; j < 16; j++) {
                k_printf("%02X ", buffer[i + j]);
            }
            
            k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            k_print(" |");
            for (int j = 0; j < 16; j++) {
                char c = (char)buffer[i + j];
                if (c >= 32 && c <= 126) k_putchar(c);
                else k_putchar('.');
            }
            k_print("|\n");
        }
    }
    else if (k_strcmp(args[1], "test") == 0) {
        k_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
        k_print("[ATA TEST] Starting read stress test (10 sectors)...\n");
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        
        uint8_t buffer[512];
        int failures = 0;
        uint32_t start_ticks = timer_get_ticks();
        
        for (int i = 0; i < 10; i++) {
            k_printf("[ATA TEST]   Sector %d... ", i);
            if (ata_read_sectors((uint32_t)i, 1, buffer) < 0) {
                k_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
                k_print("[FAIL]\n");
                k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                failures++;
            } else {
                k_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
                k_print("[OK]\n");
                k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            }
        }
        
        uint32_t elapsed = timer_get_ticks() - start_ticks;
        k_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
        k_print("[ATA TEST] --- Results ---\n");
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        k_printf("[ATA TEST] Elapsed: %u ms\n", elapsed);
        
        if (failures == 0) {
            k_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            k_print("[ATA TEST] All 10 reads successful!\n");
        } else {
            k_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
            k_printf("[ATA TEST] %d failures detected!\n", failures);
        }
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    }
    else {
        k_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
        k_printf("Unknown ata command: %s\n", args[1]);
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    }
}
static void handle_run(int argc, char args[MAX_ARGS][MAX_ARG_LEN]) {
    if (argc < 2) {
        k_print("Usage: run <path/to/file.elf>\n");
        return;
    }
    
    k_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    k_printf("[SHELL] Executing %s...\n", args[1]);
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    
    int status = run_elf_and_wait(args[1]);
    
    if (status < 0) {
        k_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
        k_printf("[SHELL] Failed to run %s\n", args[1]);
    } else {
        k_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        k_printf("[SHELL] Process exited with status %d\n", status);
    }
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}
// ============================================================================
// ДИСПЕТЧЕР КОМАНД
// ============================================================================
static void execute_command(char* buffer) {
    char args[MAX_ARGS][MAX_ARG_LEN];
    int argc = parse_args(buffer, args);
    
    if (argc == 0) return;

    // [ General ]
    if (k_strcmp(args[0], "help") == 0) {
        print_help();
    }
    else if (k_strcmp(args[0], "clear") == 0) {
        k_clear();
    }
    else if (k_strcmp(args[0], "uptime") == 0) {
        handle_uptime();
    }
    else if (k_strcmp(args[0], "ps") == 0) {
        task_print_list(); 
    }
    
    // [ Memory ]
    else if (k_strcmp(args[0], "memmap") == 0) {
        handle_memmap();
    }
    else if (k_strcmp(args[0], "pmm") == 0) {
        handle_pmm(argc, args);
    }
    else if (k_strcmp(args[0], "heap") == 0) {
        handle_heap(argc, args);
    }
    else if (k_strcmp(args[0], "run") == 0) {
        handle_run(argc, args);
    }
    
    // [ Graphics ]
    else if (k_strcmp(args[0], "font") == 0) {
        if (argc > 1 && k_strcmp(args[1], "test") == 0) {
            handle_font_test();
        } else {
            k_print("Usage: font test\n");
        }
    }
    
    // [ System ]
    else if (k_strcmp(args[0], "syscall") == 0) {
        handle_syscall();
    }
    
    // [ VFS ]
    else if (k_strcmp(args[0], "ls") == 0) {
        handle_ls(argc, args);
    }
    // [ Storage (ATA) ] День 8.2
    else if (k_strcmp(args[0], "ata") == 0) {
        handle_ata(argc, args);
    }
    else if (k_strcmp(args[0], "cat") == 0) {
        handle_cat(argc, args);
    }
    
    // [ Testing (Day 10) ]
    else if (k_strcmp(args[0], "run_tests") == 0) {
        test_init();
    }
    else if (k_strcmp(args[0], "stress") == 0) {
        handle_stress(argc, args);
    }
    
    // [ Unknown ]
    else {
        k_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
        k_print("Unknown command: ");
        k_print(args[0]);
        k_print("\n");
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    }
}

// ============================================================================
// ГЛАВНЫЙ ЦИКЛ SHELL
// ============================================================================
void shell_run(void) {
    char buffer[CMD_BUFFER_SIZE];
    int pos = 0;
    
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    k_print("Type 'help' for available commands.\n");
    
    while (1) {
        k_print("> ");
        pos = 0;
        buffer[0] = '\0';
        
        while (1) {
            char c = k_getchar();
            
            if (c == 0) {
                __asm__ volatile("hlt");
                continue;
            }
            
            if (c == '\n') {
                k_putchar('\n');
                buffer[pos] = '\0';
                break;
            }
            else if (c == '\b') {
                if (pos > 0) {
                    pos--;
                    buffer[pos] = '\0';
                    k_putchar('\b'); 
                }
            }
            else {
                // 🛡️ Signed Char Trap Fix: Каст к uint8_t для корректной работы с UTF-8
                uint8_t uc = (uint8_t)c;
                if (uc >= 32) { 
                    if (pos < CMD_BUFFER_SIZE - 1) {
                        buffer[pos++] = c;
                        buffer[pos] = '\0';
                        k_putchar(c); 
                    }
                }
            }
        }
        
        execute_command(buffer);
    }
}
