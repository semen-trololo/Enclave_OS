#include "klib.h"
#include "vga.h"
#include "framebuffer.h"
#include "gdt.h"
#include "idt.h"
#include "keyboard.h"
#include "shell.h"
#include "timer.h"
#include "pmm.h"
#include "paging.h"
#include "heap.h"
#include "tss.h"
#include "syscall.h"
#include "multiboot.h"
#include "serial.h"
#include "task.h"
#include "univga_font.h"
#include "vfs.h"
#include "initrd.h"

// ==========================================
// КОНСТАНТЫ И КОНФИГУРАЦИЯ
// ==========================================
#define MULTIBOOT_MAGIC_EXPECTED 0x2BADB002
#define USER_STACK_VIRT_ADDR     0xBFFFE000 // Guard page перед 0xC0000000
#define LAZY_ALLOC_TEST_ADDR_1   0xD0000000
#define LAZY_ALLOC_TEST_ADDR_2   0xD0001004

// Раскомментируй, чтобы включить стресс-тесты при загрузке
#define RUN_KERNEL_TESTS 1 

// ==========================================
// ВНЕШНИЕ СИМВОЛЫ (из boot.asm)
// ==========================================
extern framebuffer_info_t fb_params;
extern uint32_t multiboot_info_ptr;
extern uint32_t multiboot_magic_val;
extern uint8_t stack_top;
extern void enter_usermode(uint32_t user_esp);

// ==========================================
// ТЕСТОВЫЕ ПОТОКИ (Day 7)
// ==========================================
static void thread_a(void) {
    while(1) {
        // Busy wait для нагрузки планировщика
        for(volatile int i = 0; i < 10000000; i++);
    }
}

static void thread_b(void) {
    while(1) {
        for(volatile int i = 0; i < 10000000; i++);
    }
}

static void thread_math(void) {
    while(1) {
        double a = 3.14159;
        double b = 2.71828;
        double result;
        
        // Принудительное использование x87 FPU. 
        // Триггерит #NM (INT 7) для проверки Lazy FPU Switching.
        __asm__ volatile (
            "fldl %1\n\t"   
            "fldl %2\n\t"   
            "faddp\n\t"     
            "fstpl %0\n\t"  
            : "=m"(result)
            : "m"(a), "m"(b)
        );
        
        serial_printf("[TASK: MATH] FPU computed: %d (approx)\n", (int)result);
        for(volatile int i = 0; i < 10000000; i++);
        task_yield();
    }
}

// ==========================================
// ФАЗЫ ИНИЦИАЛИЗАЦИИ
// ==========================================

static void init_early_hardware(void) {
    serial_print("[BOOT] Phase 1: Early Hardware Init...\n");

    // Первичная инициализация видео (физические адреса)
    fb_init(&fb_params);
    if (fb_is_available()) {
        fb_clear(COLOR_BLACK);
        asm volatile("wbinvd"); // Сброс кэшей
    } else {
        vga_init();
        serial_print("[WARN] Framebuffer not available, falling back to VGA.\n");
    }

    gdt_install();   serial_print("  [+] GDT installed\n");
    idt_install();   serial_print("  [+] IDT installed\n");
    tss_install();   serial_print("  [+] TSS installed\n");
    syscall_init();  serial_print("  [+] Syscalls (INT 0x80) ready\n");
}

static void init_memory_management(void) {
    serial_print("[BOOT] Phase 2: Memory Management...\n");

    pmm_init((multiboot_info_t*)multiboot_info_ptr);
    serial_print("  [+] PMM (Physical Memory) initialized\n");

    paging_init();
    serial_print("  [+] VMM (Paging/Higher Half) enabled\n");

    // Воскрешение фреймбуфера (виртуальные адреса)
    fb_init(&fb_params); 
    if (fb_is_available()) {
        fb_clear(COLOR_BLACK); 
        asm volatile("wbinvd"); 
        fb_set_color(0x0000FF00, 0x00000000); // Green on Black
    }
    serial_print("  [+] Framebuffer resurrected in Higher Half\n");

    heap_init();
    serial_print("  [+] Kernel Heap (Buddy System) online\n");

    // Включаем Double Buffering и шрифты (требуют Heap)
    fb_enable_double_buffering(); 
    fb_init_font(Uni2_VGA16_psf, Uni2_VGA16_psf_len);
    serial_print("  [+] Double Buffering & PSF1 Font loaded\n");
}

static void init_subsystems(void) {
    serial_print("[BOOT] Phase 3: Core Subsystems...\n");

    vfs_init();
    initrd_init();
    serial_print("  [+] VFS & Initrd (tmpfs) mounted\n");

    tasking_init();
    serial_print("  [+] Task Scheduler (Round-Robin) ready\n");

    keyboard_install();
    timer_init(1000); // 1000 Hz
    serial_print("  [+] IRQs (Keyboard/PIT) enabled\n");
}

// ==========================================
// ТОЧКА ВХОДА
// ==========================================
void kernel_main(void) {
    serial_init(); // Инициализация COM1 для headless debug
    serial_print("\n================================================\n");
    serial_print("       BARE METAL OS - KERNEL BOOT SEQUENCE       \n");
    serial_print("================================================\n\n");

    // 1. Проверка загрузчика
    if (multiboot_magic_val != MULTIBOOT_MAGIC_EXPECTED) {
        serial_printf("[FATAL] Invalid Multiboot Magic: 0x%x (Expected 0x%x)\n", 
                      multiboot_magic_val, MULTIBOOT_MAGIC_EXPECTED);
        // В реальной ОС здесь был бы k_panic(), пока просто halt
        while(1) { asm volatile("cli; hlt"); }
    }
    serial_print("[OK] Multiboot magic verified.\n");

    // 2. Инициализация по фазам
    init_early_hardware();
    init_memory_management();
    init_subsystems();

    // 3. Вывод на экран (UX) - только самое важное
    if (fb_is_available()) {
        fb_set_color(COLOR_GREEN, COLOR_BLACK);
        fb_print("  ____                  __  __      _   _       _       ___  ____  \n");
        fb_print(" |  _ \\                |  \\/  |    | | | |     | |     / _ \\/ ___| \n");
        fb_print(" | |_) | __ _ _ __ ___| \\  / | ___| |_| | ___ | |    | | | \\___ \\ \n");
        fb_print(" |  _ < / _` | '__/ _ \\ |\\/| |/ _ \\ __| |/ _ \\| |    | | | |___) |\n");
        fb_print(" | |_) | (_| | | |  __/ |  | |  __/ |_| | (_) | |____| |_| |____/ \n");
        fb_print(" |____/ \\__,_|_|  \\___|_|  |_|\\___|\\__|_|\\___/|______|\\___/|____/ \n\n");
        
        fb_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
        fb_print(" [ OK ] Core systems initialized. Higher Half active.\n");
        fb_print(" [ OK ] VFS & Initrd mounted. Multitasking enabled.\n");
        fb_print(" [ OK ] Привет, мир! (UTF-8 / Cyrillic test)\n\n");
        fb_flush();
    }
    
    // 5. Запуск пользовательских задач
    serial_print("[TASK] Spawning background tasks...\n");
    task_create("Task_A", thread_a);
    task_create("Task_B", thread_b);
    task_create("Math_Task", thread_math);
    serial_print("[TASK] Background tasks queued.\n");

    // 6. Передача управления CLI
    serial_print("[BOOT] Handover to Shell. Have fun! ;)\n\n");
    shell_run();
}
