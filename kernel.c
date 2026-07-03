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

// Внешние переменные из boot.asm
extern framebuffer_info_t fb_params;
extern uint32_t multiboot_info_ptr;
extern uint32_t multiboot_magic_val;
extern uint8_t stack_top;

// ASM-функция для перехода в Ring 3 (реализована в usermode.asm)
extern void enter_usermode(uint32_t user_esp);

void kernel_main(void) {
    serial_print("\n[DEBUG] === KERNEL MAIN ENTRY ===\n");
    
    if (multiboot_magic_val != 0x2BADB002) {
        serial_print("[DEBUG] WARNING: Invalid Multiboot Magic!\n");
    }

    // 1. Инициализация видео (используем глобальную fb_params напрямую)
    fb_init(&fb_params);
    if (fb_is_available()) {
        fb_clear(COLOR_BLACK);
        asm volatile("wbinvd");
    } else {
        vga_init();
    }
    serial_print("[DEBUG] Video initialized\n");

    k_set_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    k_print("\n Bare Metal OS v1.0 (Safe Mode Debug)\n");
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    gdt_install();   serial_print("[DEBUG] GDT OK\n");
    idt_install();   serial_print("[DEBUG] IDT OK\n");
    tss_install();   serial_print("[DEBUG] TSS OK\n");
    syscall_init();  serial_print("[DEBUG] Syscall OK\n");
    
    // Передаем сохраненный физический указатель в PMM
    pmm_init((multiboot_info_t*)multiboot_info_ptr);
    serial_print("[DEBUG] PMM OK\n");

    paging_init();
    serial_print("[DEBUG] VMM OK\n");

    // Воскрешение фреймбуфера
    fb_init(&fb_params); 
    if (fb_is_available()) {
        fb_clear(COLOR_BLACK); 
        asm volatile("wbinvd"); 
        fb_set_color(0x0000FF00, 0x00000000);
        fb_print("[VMM] Higher Half Kernel Active.\n");
    } else {
        k_print("[ERR] FB DEAD\n");
    }
    serial_print("[DEBUG] FB Resurrected\n");

    heap_init();
    serial_print("[DEBUG] Heap OK\n");
    
    keyboard_install();
    timer_init(1000);
    serial_print("[DEBUG] IRQs OK\n");

    k_print("\n System ready.\n");
    serial_print("[DEBUG] Entering Shell...\n");
    
    // ==========================================
    // ТЕСТ ПЕРЕХОДА В RING 3 (USER MODE) - ДЕНЬ 4
    // ==========================================
    k_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    k_print("\n[RING 3 TEST] Preparing to enter User Mode...\n");
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    
    // 1. Выделяем физическую страницу под user stack
    uint32_t user_stack_phys = pmm_alloc_page();
    if (user_stack_phys == 0) {
        k_print("[ERR] OOM: Cannot allocate user stack!\n");
    } else {
        // 2. Выбираем виртуальный адрес в User Space.
        // Сдвигаем на страницу ниже (0xBFFFE000), чтобы создать Guard Page 
        // перед границей ядра (0xC0000000). Это защитит ядро от stack overflow в Ring 3.
        uint32_t user_stack_virt = 0xBFFFE000; 
        
        // Мапим с флагами USER, WRITE, PRESENT
        vmm_map_page(user_stack_virt, user_stack_phys, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
        
        // 3. Стек растёт вниз. Вершина стека (ESP) = конец выделенной страницы.
        uint32_t user_esp = user_stack_virt + 4096; // 0xBFFFF000
        
        k_printf("[RING 3 TEST] User stack mapped: Virt 0x%x -> Phys 0x%x\n", user_stack_virt, user_stack_phys);
        k_print("[RING 3 TEST] Executing IRET to Ring 3...\n");
        serial_print("[DEBUG] Jumping to Ring 3!\n");
        
        // 4. Прыжок в Ring 3! (Функция не возвращает управление, если user_task не вызовет sys_exit)
        enter_usermode(user_esp);
        
        // Если мы оказались здесь, значит user_task завершился через sys_exit
        k_print("\n[RING 3 TEST] Returned to Ring 0 successfully!\n");
    }
    // ==========================================

    shell_run();
}