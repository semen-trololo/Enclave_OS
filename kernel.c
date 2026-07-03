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

// Объявляем внешние переменные из boot.asm
extern framebuffer_info_t fb_params;
extern uint32_t multiboot_info_ptr;
extern uint32_t multiboot_magic_val;
extern uint8_t stack_top;

// СИГНАТУРА ИЗМЕНЕНА: аргументов нет!
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
    
    shell_run();
}