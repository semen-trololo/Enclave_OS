//keyboard.c

#include "keyboard.h"
#include "port_io.h"
#include "idt.h"
#include "pic.h" 

#define KEYBOARD_DATA_PORT   0x60
#define KEYBOARD_STATUS_PORT 0x64

#define KEY_ESC        0x01
#define KEY_BACKSPACE  0x0E
#define KEY_TAB        0x0F
#define KEY_ENTER      0x1C
#define KEY_LCTRL      0x1D
#define KEY_LSHIFT     0x2A
#define KEY_RSHIFT     0x36
#define KEY_CAPSLOCK   0x3A

// --- КОЛЬЦЕВОЙ БУФЕР (RING BUFFER) ---
#define KBD_BUFFER_SIZE 256

// КРИТИЧЕСКИ ВАЖНО: volatile запрещает компилятору кэшировать эти переменные.
// Они меняются из прерывания (IRQ), а читаются из основного кода (Shell).
static volatile char kbd_buffer[KBD_BUFFER_SIZE];
static volatile uint8_t kbd_head = 0; // Куда пишем (Producer)
static volatile uint8_t kbd_tail = 0; // Откуда читаем (Consumer)

static uint8_t shift_pressed = 0;
static uint8_t ctrl_pressed  = 0;
static uint8_t capslock_on   = 0;
static uint8_t extended_key  = 0; 

// --- ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ БУФЕРА ---

static void kbd_buffer_push(char c) {
    uint8_t next_head = (kbd_head + 1) % KBD_BUFFER_SIZE;
    if (next_head == kbd_tail) {
        // Буфер переполнен! В реальной ОС тут был бы дроп или лог.
        // Мы просто игнорируем новый символ, чтобы не затереть непрочитанные.
        return; 
    }
    kbd_buffer[kbd_head] = c;
    kbd_head = next_head;
}

char k_getchar(void) {
    if (kbd_head == kbd_tail) {
        return 0; // Буфер пуст
    }
    char c = kbd_buffer[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUFFER_SIZE;
    return c;
}

// ============================================================================
// [DAY 28] Helper: pushes a multi-byte ANSI escape sequence into the ring buffer
// Used for arrow keys, Home, End, Delete to feed Shell's readline() state machine.
// ============================================================================
static void kbd_push_ansi_seq(const char* seq) {
    while (*seq) {
        kbd_buffer_push(*seq++);
    }
}

// --- ТАБЛИЦЫ И ТРАНСЛЯЦИЯ ---

static const char scancode_lower[] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' '
};

static const char scancode_upper[] = {
    0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' '
};

static char translate_scancode(uint8_t scancode) {
    if (scancode >= sizeof(scancode_lower)) return 0;

    uint8_t use_upper = shift_pressed;
    char base_char = scancode_lower[scancode];
    
    uint8_t is_letter = (base_char >= 'a' && base_char <= 'z');
    if (capslock_on && is_letter) {
        use_upper = !use_upper;
    }

    return use_upper ? scancode_upper[scancode] : base_char;
}

// --- ОБРАБОТЧИК ПРЕРЫВАНИЯ (PRODUCER) ---

static void keyboard_handler(struct regs* r) {
    (void)r;
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);

    if (scancode == 0xE0) {
        extended_key = 1;
        return;
    }
    
    // ========================================================================
    // [DAY 28] Extended Key Handler: Arrow keys, Home, End, Delete
    // Generates ANSI escape sequences for Shell's readline() state machine.
    // ========================================================================
    if (extended_key) {
        extended_key = 0;
        // Ignore break codes (key release) for extended keys
        if (scancode & 0x80) return;
        
        switch (scancode) {
            case 0x48: kbd_push_ansi_seq("\033[A");    return; // Up Arrow
            case 0x50: kbd_push_ansi_seq("\033[B");    return; // Down Arrow
            case 0x4D: kbd_push_ansi_seq("\033[C");    return; // Right Arrow
            case 0x4B: kbd_push_ansi_seq("\033[D");    return; // Left Arrow
            case 0x47: kbd_push_ansi_seq("\033[H");    return; // Home
            case 0x4F: kbd_push_ansi_seq("\033[F");    return; // End
            case 0x53: kbd_push_ansi_seq("\033[3~");   return; // Delete (4-byte seq)
            // Ignore other extended keys (Insert=0x52, PgUp=0x49, PgDn=0x51, etc.)
            default: return;
        }
    }

    if (scancode & 0x80) {
        scancode &= 0x7F; 
        if (scancode == KEY_LSHIFT || scancode == KEY_RSHIFT) {
            shift_pressed = 0;
        } else if (scancode == KEY_LCTRL) {
            ctrl_pressed = 0;
        }
        return;
    }

    switch (scancode) {
        case KEY_LSHIFT:
        case KEY_RSHIFT:
            shift_pressed = 1;
            return;
        case KEY_LCTRL:
            ctrl_pressed = 1;
            return;
        case KEY_CAPSLOCK:
            capslock_on = !capslock_on; 
            return;
        case KEY_ESC:
            kbd_buffer_push(0x1B); // ASCII Escape
            return;
        case KEY_BACKSPACE:
            kbd_buffer_push('\b'); // Отдаем Backspace в буфер
            return;
        case KEY_ENTER:
            kbd_buffer_push('\n'); // Отдаем Enter в буфер
            return;
        case KEY_TAB:
            kbd_buffer_push('\t'); // Отдаем Tab в буфер
            return;
    }

    char c = translate_scancode(scancode);
    if (c != 0) {
        if (ctrl_pressed) {
            // [DAY 28] Ctrl+letter -> ASCII control characters (1-26)
            // Ctrl+A=1 (SOH), Ctrl+C=3 (ETX), Ctrl+D=4 (EOT), Ctrl+E=5 (ENQ),
            // Ctrl+K=11 (VT), Ctrl+L=12 (FF), Ctrl+U=21 (NAK)
            if (c >= 'a' && c <= 'z') {
                kbd_buffer_push(c - 'a' + 1);
            } else if (c >= 'A' && c <= 'Z') {
                kbd_buffer_push(c - 'A' + 1);
            }
            // Ignore Ctrl with non-letter keys
        } else {
            kbd_buffer_push(c);
        }
    }
}

void keyboard_install(void) {
    irq_clear_mask(1); 
    irq_register_handler(1, keyboard_handler);
    
    // Временный вывод через k_print, так как shell еще не инициализирован
    extern void k_print(const char*); // forward declaration, чтобы не тащить klib.h
    k_print(" [OK] Keyboard driver (PS/2) installed on IRQ1.\n");
}