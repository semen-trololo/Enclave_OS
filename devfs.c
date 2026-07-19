#include "devfs.h"
#include "vfs.h"
#include "heap.h"
#include "klib.h"
#include "serial.h"
#include "task.h"
#include "framebuffer.h"
#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// DEVFS GLOBALS
// ============================================================================
static vfs_node_t* devfs_root = NULL;
static vfs_node_t* console_node = NULL;
static uint32_t console_open_count = 0;

// ============================================================================
// ANSI STATE MACHINE (Перенесена из syscall.c)
// ============================================================================
typedef enum {
    ANSI_IDLE,
    ANSI_ESCAPE,
    ANSI_COLLECTING
} ansi_state_t;

static ansi_state_t ansi_state = ANSI_IDLE;
static char ansi_buffer[32];
static int ansi_pos = 0;

static const uint8_t ansi_fg_map[8] = {
    K_COLOR_BLACK, K_COLOR_RED, K_COLOR_GREEN, K_COLOR_BROWN,
    K_COLOR_BLUE, K_COLOR_MAGENTA, K_COLOR_CYAN, K_COLOR_LIGHT_GREY
};

static const uint8_t ansi_fg_bright_map[8] = {
    K_COLOR_DARK_GREY, K_COLOR_LIGHT_RED, K_COLOR_LIGHT_GREEN, K_COLOR_YELLOW,
    K_COLOR_LIGHT_BLUE, K_COLOR_LIGHT_MAGENTA, K_COLOR_LIGHT_CYAN, K_COLOR_WHITE
};

static uint8_t current_fg = K_COLOR_LIGHT_GREY;
static uint8_t current_bg = K_COLOR_BLACK;

static void process_ansi_csi(void) {
    if (ansi_pos == 0) return;
    
    // 🛡️ ENCLAVE OS PATCH: Игнорируем Private Mode Sequences (\033[?25h / \033[?25l)
    // Это предотвращает вывод мусора "25h" на экран
    if (ansi_buffer[0] == '?') {
        ansi_state = ANSI_IDLE;
        ansi_pos = 0;
        return;
    }

    char final_char = ansi_buffer[ansi_pos - 1];
    bool bold = false;
    
    // ========================================================================
    // ED (Erase in Display) — Очистка экрана (\033[2J)
    // ========================================================================
    if (final_char == 'J') {
        int mode = 0;
        if (ansi_pos > 1 && ansi_buffer[0] >= '0' && ansi_buffer[0] <= '9') {
            mode = ansi_buffer[0] - '0';
        }
        if (mode == 2 || mode == 3 || ansi_pos == 1) {
            // Используем нашу идеальную реализацию из klib.c
            k_clear();
            k_set_cursor(0, 0);
        }
    }
    // ========================================================================
    // CUP (Cursor Position) — \033[<row>;<col>H
    // ========================================================================
    else if (final_char == 'H' || final_char == 'f') {
        int row = 1, col = 1;
        int i = 0;
        int limit = ansi_pos - 1;
        
        // Парсим row
        while (i < limit && ansi_buffer[i] >= '0' && ansi_buffer[i] <= '9') {
            row = row * 10 + (ansi_buffer[i] - '0');
            i++;
        }
        // Пропускаем ';'
        if (i < limit && ansi_buffer[i] == ';') i++;
        
        // Парсим col
        col = 0; 
        while (i < limit && ansi_buffer[i] >= '0' && ansi_buffer[i] <= '9') {
            col = col * 10 + (ansi_buffer[i] - '0');
            i++;
        }
        
        if (col == 0) col = 1;
        if (row < 1) row = 1;
        serial_printf("[CUP] row=%d col=%d -> k_set_cursor(%d,%d)\n", row, col, row-1, col-1);
        k_set_cursor(row - 1, col - 1);
    }
    // ========================================================================
    // SGR (Select Graphic Rendition) — Colors (\033[...m)
    // ========================================================================
    else if (final_char == 'm') {
        if (ansi_pos == 1) {
            current_fg = K_COLOR_LIGHT_GREY;
            current_bg = K_COLOR_BLACK;
            bold = false;
        } else {
            int i = 0;
            int limit = ansi_pos - 1;
            while (i < limit) {
                while (i < limit && ansi_buffer[i] == ' ') i++;
                int num = 0;
                bool has_num = false;
                while (i < limit && ansi_buffer[i] >= '0' && ansi_buffer[i] <= '9') {
                    num = num * 10 + (ansi_buffer[i] - '0');
                    has_num = true;
                    i++;
                }
                if (has_num) {
                    if (num == 0) {
                        current_fg = K_COLOR_LIGHT_GREY;
                        current_bg = K_COLOR_BLACK;
                        bold = false;
                    } else if (num == 1) {
                        bold = true;
                    } else if (num >= 30 && num <= 37) {
                        current_fg = ansi_fg_map[num - 30];
                        if (bold) {
                            if (current_fg == K_COLOR_BROWN) current_fg = K_COLOR_YELLOW;
                            else if (current_fg < 8) current_fg += 8;
                        }
                    } else if (num >= 40 && num <= 47) {
                        current_bg = ansi_fg_map[num - 40];
                    } else if (num >= 90 && num <= 97) {
                        current_fg = ansi_fg_bright_map[num - 90];
                    }
                }
                if (i < limit && ansi_buffer[i] == ';') i++;
            }
        }
        k_set_color(current_fg, current_bg);
    }
    // ========================================================================
    // Остальные команды (игнорируем безопасно)
    // ========================================================================
    else {
        // CUF, CUB, CUU, CUD, DECTCEM и прочие игнорируются
        (void)0;
    }
    
    // Сброс состояния автомата после обработки
    ansi_state = ANSI_IDLE;
    ansi_pos = 0;
}
// ============================================================================
// /dev/console CALLBACKS
// ============================================================================

static int devfs_console_open(vfs_node_t* node, uint32_t flags) {
    (void)node; (void)flags;

    __asm__ volatile("cli");
    if (console_open_count > 0) {
        __asm__ volatile("sti");
        serial_print("[DEVFS] /dev/console already open (exclusive resource)\n");
        return -1; // EBUSY
    }
    console_open_count++;
    __asm__ volatile("sti");

    return 0;
}

static void devfs_console_close(vfs_node_t* node) {
    (void)node;
    __asm__ volatile("cli");
    if (console_open_count > 0) console_open_count--;
    __asm__ volatile("sti");
}

static int32_t devfs_console_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    (void)node; (void)offset;

    extern char k_getchar(void);
    uint32_t read_count = 0;

    while (read_count < size) {
        char c = k_getchar();

        if (c != 0) {
            buffer[read_count++] = (uint8_t)c;
            // 🛡️ ENCLAVE OS: RAW MODE — возвращаем символы сразу, не ждём '\n'.
            // Line Discipline (эхо, накопление до Enter) — задача Ring 3 (Shell/Editor).
        } else {
            if (read_count > 0) break;
            task_yield();
        }
    }

    return read_count;
}

// ============================================================================
// /dev/console CALLBACKS
// ============================================================================

static int32_t devfs_console_write(vfs_node_t* node, uint32_t offset, uint32_t size, const uint8_t* buffer) {
    (void)node; 
    (void)offset;

    for (uint32_t i = 0; i < size; i++) {
        unsigned char c = buffer[i];

        switch (ansi_state) {
            case ANSI_IDLE:
                if (c == 0x1B) {
                    ansi_state = ANSI_ESCAPE;
                } else {
                    // Печатаем только печатные символы и стандартные управляющие (\n, \r, \t)
                    if (c == '\n' || c == '\r' || c == '\t' || (c >= 32 && c < 127)) {
                        serial_putc(c);
                    }
                    k_putchar(c);
                }
                break;

            case ANSI_ESCAPE:
                if (c == '[') {
                    ansi_state = ANSI_COLLECTING;
                    ansi_pos = 0;
                    // Опционально: можно раскомментировать для отладки ANSI в Serial
                    // serial_putc(0x1B);
                    // serial_putc('[');
                } else {
                    // Двухбайтовые последовательности (например, \033O) или одиночный ESC
                    serial_putc(0x1B);
                    if (c == '\n' || c == '\r' || c == '\t' || (c >= 32 && c < 127)) {
                        serial_putc(c);
                    }
                    k_putchar(0x1B);
                    k_putchar(c);
                    ansi_state = ANSI_IDLE;
                }
                break;

            case ANSI_COLLECTING:
                // 🛡️ ENCLAVE OS PATCH: Разрешаем символ '?' для Private Mode Sequences (\033[?25h / \033[?25l)
                if ((c >= '0' && c <= '9') || c == ';' || c == ' ' || c == '?') {
                    if (ansi_pos < 31) {
                        ansi_buffer[ansi_pos++] = c;
                    }
                } else if (c >= 0x40 && c <= 0x7E) {
                    // Финальный символ CSI последовательности (m, H, J, l, h, и т.д.)
                    if (ansi_pos < 31) {
                        ansi_buffer[ansi_pos++] = c;
                    }
                    
                    // Обработка последовательности (включая безопасный игнор ?25h/l)
                    process_ansi_csi();
                    
                    // Явный сброс состояния для надежности (process_ansi_csi тоже это делает)
                    ansi_state = ANSI_IDLE;
                    ansi_pos = 0;
                } else {
                    // Неизвестный символ прерывает ANSI-последовательность, сбрасываем автомат
                    ansi_state = ANSI_IDLE;
                    ansi_pos = 0;
                }
                break;
        }
    }

    // Синхронизация Framebuffer, если он активен (Double Buffering Dirty Rectangles)
    if (fb_is_available()) {
        fb_flush();
    }
    
    return size;
}

// ============================================================================
// DEVFS INITIALIZATION
// ============================================================================
void devfs_init(void) {
    serial_print("[DEVFS] Initializing Device File System...\n");

    // Создаем /dev директорию
    devfs_root = vfs_mkdir_recursive("/dev");
    if (!devfs_root) {
        serial_print("[DEVFS] FATAL: Failed to create /dev\n");
        return;
    }

    // Создаем /dev/console устройство
    console_node = vfs_create_node("console", FS_CHARDEVICE, devfs_root, NULL);
    if (!console_node) {
        serial_print("[DEVFS] FATAL: Failed to create /dev/console\n");
        return;
    }

    console_node->read = devfs_console_read;
    console_node->write = devfs_console_write;
    console_node->open = devfs_console_open;
    console_node->close = devfs_console_close;
    console_node->permissions = PERM_READ_WRITE;

    serial_print("[DEVFS] /dev/console registered (exclusive access).\n");
}
