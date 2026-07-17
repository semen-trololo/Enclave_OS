//kernel.c

#include "klib.h"
#include "vga.h"
#include "framebuffer.h"
#include "gdt.h"
#include "idt.h"
#include "keyboard.h"
// #include "shell.h"  <-- ❌ УБРАНО: Ядро больше не запускает Shell напрямую
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
#include "ata.h"
#include "fat32.h"
#include "tmpfs.h"
#include "elf.h"     // ✅ НОВОЕ: Для загрузки /sbin/init.elf
#include "vma.h"     // ✅ НОВОЕ: Для создания VMA стека/кучи
#include "config.h"  // ✅ НОВОЕ: Для USER_STACK_VIRT_TOP и USER_HEAP_START

// ==========================================
// КОНСТАНТЫ И КОНФИГУРАЦИЯ
// ==========================================
#define MULTIBOOT_MAGIC_EXPECTED 0x2BADB002

// ==========================================
// ВНЕШНИЕ СИМВОЛЫ (из boot.asm)
// ==========================================
extern framebuffer_info_t fb_params;
extern uint32_t multiboot_info_ptr;
extern uint32_t multiboot_magic_val;
extern uint8_t stack_top;
extern void enter_usermode(uint32_t entry_point, uint32_t user_esp);

// ==========================================
// ФАЗЫ ИНИЦИАЛИЗАЦИИ
// ==========================================

static void init_early_hardware(void) {
    serial_print("[BOOT] Phase 1: Early Hardware Init...\n");

    fb_init(&fb_params);
    if (fb_is_available()) {
        fb_clear(COLOR_BLACK);
        asm volatile("wbinvd");
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

    fb_init(&fb_params); 
    if (fb_is_available()) {
        fb_clear(COLOR_BLACK); 
        asm volatile("wbinvd"); 
        fb_set_color(0x0000FF00, 0x00000000);
    }
    serial_print("  [+] Framebuffer resurrected in Higher Half\n");

    heap_init();
    serial_print("  [+] Kernel Heap (Buddy System) online\n");

    fb_enable_double_buffering(); 
    fb_init_font(Uni2_VGA16_psf, Uni2_VGA16_psf_len);
    serial_print("  [+] Double Buffering & PSF1 Font loaded\n");
}

static void init_subsystems(void) {
    serial_print("[BOOT] Phase 3: Core Subsystems...\n");

    vfs_init();
    initrd_init();
    serial_print("  [+] VFS & Initrd (tmpfs) mounted\n");

    fat32_init();
    serial_print("  [+] FAT32 Driver (Read-Only) mounted\n");
    
    tmpfs_init();
    serial_print("  [+] TMPFS mounted\n");

    tasking_init();
    serial_print("  [+] Task Scheduler (Round-Robin) ready\n");

    keyboard_install();
    timer_init(1000);
    serial_print("  [+] IRQs (Keyboard/PIT) enabled\n");
}

static void init_storage(void) {
    serial_print("[BOOT] Phase 2.5: Storage Subsystem (ATA PIO)...\n");
    
    ata_init();
    serial_print("  [+] ATA Primary Bus initialized (Polling Mode)\n");
    
    ata_identify_data_t identify_data;
    int identify_result = ata_identify(&identify_data);
    
    if (identify_result == 0) {
        serial_printf("  [+] Disk: %s\n", identify_data.model);
        serial_printf("  [+] Serial: %s\n", identify_data.serial);
        serial_printf("  [+] Firmware: %s\n", identify_data.firmware);
        serial_printf("  [+] LBA Capacity: %u sectors (%u MB)\n", 
                      identify_data.lba_capacity,
                      identify_data.lba_capacity / 2048);
        
        if (fb_is_available()) {
            fb_set_color(COLOR_CYAN, COLOR_BLACK);
            fb_print(" [ OK ] ATA: ");
            fb_print(identify_data.model);
            fb_print(" detected.\n");
            fb_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
        }
    } else if (identify_result == -2) {
        serial_print("  [INFO] ATAPI device detected (CD-ROM), skipping.\n");
        if (fb_is_available()) {
            fb_set_color(COLOR_YELLOW, COLOR_BLACK);
            fb_print(" [INFO] ATAPI device (CD-ROM) detected.\n");
            fb_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
        }
    } else {
        serial_printf("  [WARN] ATA IDENTIFY failed (code %d).\n", identify_result);
        if (fb_is_available()) {
            fb_set_color(COLOR_YELLOW, COLOR_BLACK);
            fb_print(" [WARN] No ATA disk detected.\n");
            fb_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
        }
    }
    
    int part_count = partition_scan();
    if (part_count > 0) {
        serial_printf("  [+] MBR: Found %d partitions\n", part_count);
        for (int i = 0; i < MAX_PARTITIONS; i++) {
            partition_info_t* p = partition_get(i);
            if (p && p->active) {
                serial_printf("      [Part %d] Type: 0x%x, LBA: %u, Size: %u MB\n",
                              i, p->type, p->lba_start, p->sector_count / 2048);
            }
        }
    } else {
        serial_print("  [INFO] No valid MBR partitions found.\n");
    }
}

// ==========================================
// ТОЧКА ВХОДА
// ==========================================
void kernel_main(void) {
    serial_init();
    serial_print("\n================================================\n");
    serial_print("       ENCLAVE OS - KERNEL BOOT SEQUENCE          \n");
    serial_print("================================================\n\n");

    if (multiboot_magic_val != MULTIBOOT_MAGIC_EXPECTED) {
        serial_printf("[FATAL] Invalid Multiboot Magic: 0x%x (Expected 0x%x)\n", 
                      multiboot_magic_val, MULTIBOOT_MAGIC_EXPECTED);
        while(1) { asm volatile("cli; hlt"); }
    }
    serial_print("[OK] Multiboot magic verified.\n");

    init_early_hardware();
    init_memory_management();
    init_storage();
    init_subsystems();

    if (fb_is_available()) {        
        fb_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
        fb_print(" [ OK ] Core systems initialized. Higher Half active.\n");
        fb_print(" [ OK ] VFS & Initrd mounted. Multitasking enabled.\n");
        fb_print(" [ OK ] Привет, мир! (UTF-8 / Cyrillic test)\n\n");
        fb_flush();
    }
    
    // ========================================================================
    // DAY 24: LAUNCH PID 1 (Ring 3 Init Process)
    // Ядро больше не запускает shell_run(). Оно создает Ring 3 процесс
    // /sbin/init.elf и уходит в бесконечный Idle Loop (sti; hlt).
    // Планировщик (PIT) сам подхватит PID 1 и передаст ему CPU.
    // ========================================================================
        serial_print("[BOOT] Launching PID 1 (/sbin/init.elf)...\n");
    
    vfs_node_t* init_node = vfs_findnode("/sbin/init.elf");
    if (!init_node) {
        serial_print("[FATAL] /sbin/init.elf not found in VFS!\n");
        // ... (error handling)
    }

    uint32_t* init_pdir = vmm_create_address_space();
    if (!init_pdir) {
        serial_print("[FATAL] Failed to create address space for Init!\n");
        while(1) { asm volatile("cli; hlt"); }
    }

    task_t temp_task;
    temp_task.pdir_virt = init_pdir;
    temp_task.vma_head = NULL;

    uint32_t init_entry = elf_load(init_node, &temp_task);
    if (init_entry == 0) {
        serial_print("[FATAL] Failed to load /sbin/init.elf!\n");
        vma_destroy_all(&temp_task);
        vmm_destroy_address_space(init_pdir);
        while(1) { asm volatile("cli; hlt"); }
    }

    uint32_t stack_top_init = USER_STACK_VIRT_TOP - 16;
    
    // 🛡️ CRITICAL: Защищаем создание задачи от прерываний PIT
    uint32_t eflags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(eflags));
    
    task_t* init_task = task_create("/sbin/init.elf", (void (*)(void))init_entry, true, stack_top_init, init_pdir);
    if (!init_task) {
        serial_print("[FATAL] Failed to create Init task!\n");
        vma_destroy_all(&temp_task);
        vmm_destroy_address_space(init_pdir);
        __asm__ volatile("push %0; popf" : : "r"(eflags));
        while(1) { asm volatile("cli; hlt"); }
    }

    init_task->vma_head = temp_task.vma_head;
    init_task->pid = 1;

    vma_add(init_task, stack_top_init - USER_STACK_SIZE, stack_top_init, VMA_READ | VMA_WRITE);
    vma_add(init_task, USER_HEAP_START, USER_HEAP_START, VMA_READ | VMA_WRITE);
    
    __asm__ volatile("push %0; popf" : : "r"(eflags));

    serial_printf("[BOOT] ✓ PID 1 (/sbin/init.elf) ready. Entry: 0x%x\n", init_entry);
    serial_print("[BOOT] Entering Kernel Idle Loop (PID 0)...\n\n");

    // ========================================================================
    // KERNEL IDLE LOOP (PID 0)
    // Ядро НИКОГДА не должно выходить из этого цикла.
    // Если init.elf упадет, task.c (Reaper) заметит это и перезапустит его.
    // ========================================================================
    while (1) {
        asm volatile("sti; hlt; cli");
    }
}