#include "shell.h"
#include "keyboard.h"
#include "klib.h"
#include "timer.h"
#include "pmm.h"
#include "heap.h"
#include "vga.h"
#include "syscall.h"

#define CMD_BUFFER_SIZE 256
#define MAX_ARGS 4
#define MAX_ARG_LEN 64

// Статический массив для стресс-теста PMM (занимает 128 КБ в секции .bss)
static uint32_t test_allocations[PMM_PAGES_COUNT];

// Токенайзер: разбивает строку на аргументы
static int parse_args(char* buffer, char args[MAX_ARGS][MAX_ARG_LEN]) {
    int argc = 0;
    char* ptr = buffer;
    
    for (int i = 0; i < MAX_ARGS; i++) args[i][0] = '\0';

    while (*ptr && argc < MAX_ARGS) {
        while (*ptr == ' ') ptr++; // Пропускаем пробелы
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

static void print_help(void) {
    k_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    k_print("Available commands:\n");
    
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    k_print("  help             - Show this help message\n");
    k_print("  clear            - Clear the screen\n");
    k_print("  uptime           - Show system uptime\n");
    k_print("  memmap           - Show E820 physical memory map\n");
    
    k_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    k_print("  --- Memory Management (PMM) ---\n");
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    k_print("  pmm status       - Show physical memory usage\n");
    k_print("  pmm alloc [num]  - Allocate physical pages\n");
    k_print("  pmm free <addr>  - Free a physical page (hex)\n");
    k_print("  pmm test         - Run PMM stress tests\n");
    k_print("  heap <status|alloc|free|test> - Test heap\n");
}

// Обработчик команд PMM
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
        uint32_t total = used + free;
        
        k_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
        k_print("[PMM] --- Physical Memory Status ---\n");
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        k_printf("[PMM] Total Tracked: %d MB (%d pages)\n", (total * 4) / 1024, total);
        k_printf("[PMM] Used:          %d MB (%d pages)\n", (used * 4) / 1024, used);
        k_printf("[PMM] Free:          %d MB (%d pages)\n", (free * 4) / 1024, free);
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
            k_printf("[PMM] Allocated: %x\n", addr);
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
        pmm_free_page(addr); // Ошибки (unaligned/out of range) напечатает сама pmm_free_page
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
        while (allocated_count < PMM_PAGES_COUNT) {
            uint32_t addr = pmm_alloc_page();
            if (addr == 0) break;
            test_allocations[allocated_count++] = addr;
        }
        k_printf("[PMM TEST]    Allocated %d pages. Limit reached.\n", allocated_count);

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
}static const char* get_e820_type_string(uint32_t type) {
    switch (type) {
        case 1: return "Available";
        case 2: return "Reserved";
        case 3: return "ACPI Reclaim";
        case 4: return "ACPI NVS";
        case 5: return "Bad RAM";
        default: return "Unknown";
    }
}

static void handle_memmap(void) {
    uint32_t count = 0;
    const e820_entry_t* map = pmm_get_memory_map(&count);
    
    if (count == 0) {
        k_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
        k_print("[MEMMAP] ERROR: E820 map is empty!\n");
        k_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        k_print("[MEMMAP] Check if MBOOT_MMAP is added to boot.asm flags.\n");
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return;
    }

    k_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    k_print("\n[E820] --- Physical Memory Map ---\n");
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    
    uint64_t total_available = 0;
    uint64_t total_reserved = 0;
    
    for (uint32_t i = 0; i < count; i++) {
        // Приводим к 32-битному виду
        uint32_t base_lo = (uint32_t)map[i].addr;
        uint32_t end_lo = (uint32_t)(map[i].addr + map[i].len - 1);
        uint32_t len_kb = (map[i].len >= 1024) ? (uint32_t)(map[i].len / 1024) : 1;
        
        // Цветовая дифференциация
        if (map[i].type == 1) {
            k_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            total_available += map[i].len;
        } else if (map[i].type == 3) {
            k_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
            total_available += map[i].len;
        } else {
            k_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            total_reserved += map[i].len;
        }
        
        // БЕЗОПАСНЫЙ ВЫВОД: Только %x, %u, %s без модификаторов ширины!
        k_printf("  Region %u: 0x%x - 0x%x | %u KB | ", i, base_lo, end_lo, len_kb);
        
        // Тип выводим отдельным вызовом k_print, чтобы не зависеть от %s в k_printf
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


static void execute_command(char* buffer) {
    char args[MAX_ARGS][MAX_ARG_LEN];
    int argc = parse_args(buffer, args);
    
    if (argc == 0) return;

    if (k_strcmp(args[0], "help") == 0) {
        print_help();
    }
    else if (k_strcmp(args[0], "clear") == 0) {
        k_clear();
    }
    else if (k_strcmp(args[0], "uptime") == 0) {
        uint32_t ticks = timer_get_ticks();
        uint32_t freq = timer_get_frequency(); 
        
        uint32_t total_seconds = ticks / freq;
        uint32_t hours   = total_seconds / 3600;
        uint32_t minutes = (total_seconds % 3600) / 60;
        uint32_t seconds = total_seconds % 60;
        
        k_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
        k_print("System Uptime: ");
        
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        k_printf("%d hours, %d minutes, %d seconds\n", hours, minutes, seconds);
        
        k_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK); 
        k_printf("  (Raw ticks: %d @ %d Hz)\n", ticks, freq);
        
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    }
    else if (k_strcmp(args[0], "syscall") == 0) {
            const char* msg = "Hello from Syscall!\n";
            uint32_t len = k_strlen(msg);
            
            uint32_t result;
            // Вызываем INT 0x80 прямо из Ring 0!
            // EAX = SYS_WRITE (4)
            // EBX = fd (1 = stdout)
            // ECX = buf (msg)
            // EDX = count (len)
            __asm__ volatile (
                "int $0x80"
                : "=a" (result)
                : "a" (SYS_WRITE), "b" (1), "c" (msg), "d" (len)
            );
            
            k_printf("\n[Shell] Syscall returned: %d\n", result);
        }
    else if (k_strcmp(args[0], "heap") == 0) {
        if (argc < 2) {
            k_print("Usage: heap <status|alloc|free|test>\n");
        } else if (k_strcmp(args[1], "status") == 0) {
            heap_print_status();
        } else if (k_strcmp(args[1], "alloc") == 0) {
            if (argc < 3) { k_print("Usage: heap alloc <size>\n"); return; }
            uint32_t size = k_atoi(args[2]);
            void* ptr = kmalloc(size);
            if (ptr) k_printf("[HEAP] Allocated %u bytes at %p\n", size, ptr);
            else k_print("[HEAP] Allocation failed!\n");
        } else if (k_strcmp(args[1], "free") == 0) {
            if (argc < 3) { k_print("Usage: heap free <addr>\n"); return; }
            uint32_t addr = k_atoh(args[2]);
            kfree((void*)addr);
            k_print("[HEAP] Freed.\n");
        } else if (k_strcmp(args[1], "test") == 0) {
            heap_run_tests();
        } else {
            k_print("Unknown heap command.\n");
        }
    }
    else if (k_strcmp(args[0], "memmap") == 0) {
        handle_memmap();
    }
    else if (k_strcmp(args[0], "pmm") == 0) {
        handle_pmm(argc, args);
    }
    else if (k_strlen(args[0]) > 0) {
        k_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
        k_print("Unknown command: ");
        k_print(args[0]);
        k_print("\n");
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    }
}

void shell_run(void) {
    char buffer[CMD_BUFFER_SIZE];
    int pos = 0;
    
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    k_print("  Help             - Show this help message\n");
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
                    k_putchar('\b'); // Отрисовка пробела и сдвиг курсора назад
                }
            }
            else {
                uint8_t uc = (uint8_t)c;
                // Пропускаем управляющие символы (< 32), но пропускаем UTF-8 байты (>= 128)
                if (uc >= 32) { 
                    if (pos < CMD_BUFFER_SIZE - 1) {
                        buffer[pos++] = c;
                        buffer[pos] = '\0';
                        k_putchar(c); // Для UTF-8 символ появится на экране только после 2-го байта
                    }
                }
            }
        }
        
        execute_command(buffer);
    }
}