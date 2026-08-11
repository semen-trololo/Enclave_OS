#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// ARCHITECTURE SELECTION (Compile-Time HAL)
// ============================================================================
// Определяется через Makefile: -DCONFIG_ARCH_X86 или -DCONFIG_ARCH_ARM
// По умолчанию — x86 (обратная совместимость).
#if !defined(CONFIG_ARCH_X86) && !defined(CONFIG_ARCH_ARM)
#define CONFIG_ARCH_X86 1
#endif

// ============================================================================
// USER SPACE BOUNDARIES (Common: x86 + ARM)
// ============================================================================
#define USER_SPACE_START    0x00000000
#define USER_SPACE_END      0xBFFFFFFF

// ============================================================================
// KERNEL SPACE BOUNDARIES (Common: x86 + ARM)
// ============================================================================
#define KERNEL_SPACE_START  0xC0000000
#define KERNEL_DIRECT_MAP   0xC0000000

// ============================================================================
// SSOT ADDRESS TRANSLATION MACROS (Перенесено из paging.h и pmm.c)
// ============================================================================
// VIRT_TO_PHYS: учитывает, что секции .boot имеют адреса < 0xC0000000
#define VIRT_TO_PHYS(addr) (((uint32_t)(addr) >= KERNEL_SPACE_START) ? ((uint32_t)(addr) - KERNEL_SPACE_START) : (uint32_t)(addr))
// PHYS_TO_VIRT: нижняя память (0-16MB) замаплена в Higher Half (Direct Map)
#define PHYS_TO_VIRT(addr) ((uint32_t)(addr) + KERNEL_SPACE_START)

// ============================================================================
// LOWER MEMORY (Reserved by PMM)
// ============================================================================
#define LOWER_MEM_START     0x00000000
#define LOWER_MEM_END       0x00100000

// ============================================================================
// PCI MMIO HOLE (Reserved by PMM) — x86 only
// ============================================================================
#define PCI_MMIO_HOLE_START 0xE0000000
#define PCI_MMIO_HOLE_END   0xFFFFFFFF

// ============================================================================
// PROCESS MEMORY LAYOUT (User Space) — Common
// ============================================================================
#define USER_STACK_VIRT_TOP 0xBFFFF000
#define USER_STACK_SIZE     (64 * 1024)
#define USER_STACK_GUARD_SIZE (4 * 1024)

#define USER_HEAP_START 0x10000000
#define USER_HEAP_MAX_SIZE  (64 * 1024 * 1024)

// ============================================================================
// KERNEL MEMORY LAYOUT — Common
// ============================================================================
#define KERNEL_HEAP_VIRT    0xD0000000
#define KERNEL_HEAP_SIZE (128 * 1024 * 1024)
#define KERNEL_HEAP_END     (KERNEL_HEAP_VIRT + KERNEL_HEAP_SIZE)

// ============================================================================
// LAZY ALLOCATION (On-Demand Paging для ядра) — Common
// ============================================================================
#define LAZY_ALLOC_START    KERNEL_HEAP_VIRT
#define LAZY_ALLOC_END      KERNEL_HEAP_END

// ============================================================================
// FRAMEBUFFER — x86 only (VBE/VESA)
// ============================================================================
#define FB_VIRT_BASE        0xFD000000
#define FB_PHYS_BASE        0xFD000000
#define FB_SIZE_MB          16

// ============================================================================
// MMAP REGION (Day 12) — Common
// ============================================================================
#define USER_MMAP_START      0x40000000
#define USER_MMAP_MAX_SIZE   0x40000000

// ============================================================================
// KERNEL STACK POOL (Day 16 Security Hardening) — Common
// ============================================================================
#define KERNEL_STACK_POOL_START 0xC8000000
#define KERNEL_STACK_POOL_SIZE  (16 * 1024 * 1024)
#define KERNEL_STACK_SLOT_PAGES 5
#define KERNEL_STACK_USABLE_SIZE (4 * 4096)

// ============================================================================
// ============================================================================
//
//                    ARCHITECTURE-SPECIFIC CONSTANTS
//
// ============================================================================
// ============================================================================

#ifdef CONFIG_ARCH_X86
// ============================================================================
// x86-SPECIFIC: Multiboot, GDT selectors, PIC, PIT
// ============================================================================
#define X86_MULTIBOOT_MAGIC     0x1BADB002
#define X86_MULTIBOOT_EAX_MAGIC 0x2BADB002

#define X86_GDT_KERNEL_CODE     0x08
#define X86_GDT_KERNEL_DATA     0x10
#define X86_GDT_USER_CODE       0x18
#define X86_GDT_USER_DATA       0x20
#define X86_GDT_TSS             0x28

#define X86_PIC_MASTER_CMD      0x20
#define X86_PIC_MASTER_DATA     0x21
#define X86_PIC_SLAVE_CMD       0xA0
#define X86_PIC_SLAVE_DATA      0xA1

#define X86_PIT_CHANNEL0        0x40
#define X86_PIT_CMD             0x43
#define X86_PIT_FREQUENCY       1193182

#define X86_COM1_PORT           0x3F8

#define X86_SYSCALL_INT         0x80

#endif // CONFIG_ARCH_X86

#ifdef CONFIG_ARCH_ARM
// ============================================================================
// ARM-SPECIFIC: BCM2835 (Raspberry Pi 1, ARM1176JZF-S, ARMv6)
// ============================================================================

// --- CPU Mode (CPSR.M[4:0]) ---
#define ARM_MODE_USR            0x10    // User (Ring 3)
#define ARM_MODE_FIQ            0x11
#define ARM_MODE_IRQ            0x12
#define ARM_MODE_SVC            0x13    // Supervisor (Ring 0)
#define ARM_MODE_ABT            0x17
#define ARM_MODE_UND            0x1B
#define ARM_MODE_SYS            0x1F

// --- CPSR bits ---
#define ARM_CPSR_FIQ_DISABLE    (1 << 6)
#define ARM_CPSR_IRQ_DISABLE    (1 << 7)
#define ARM_CPSR_THUMB          (1 << 5)

// --- Exception Vectors (High Vectors: 0xFFFF0000) ---
#define ARM_VECTORS_BASE        0xFFFF0000
#define ARM_VECTOR_RESET        0x00
#define ARM_VECTOR_UNDEF        0x04
#define ARM_VECTOR_SVC          0x08    // Syscall entry
#define ARM_VECTOR_PABT         0x0C    // Prefetch Abort
#define ARM_VECTOR_DABT         0x10    // Data Abort
#define ARM_VECTOR_RESERVED     0x14
#define ARM_VECTOR_IRQ          0x18
#define ARM_VECTOR_FIQ          0x1C

// --- BCM2835 Peripheral Base (Physical) ---
#define BCM2835_PERIPH_BASE     0x20000000

// ============================================================================
// [P3] Higher-Half Virtual Base for BCM2835 Peripherals
// ============================================================================
// После перехода на HH-only mapping (без identity) все MMIO-доступы
// идут через 0xE0000000+. Макрос BCM2835_VIRT транслирует физический
// адрес периферии в kernel virtual.
//
// Сейчас (spike): identity mapping активен, можно использовать физ. адреса.
// Позже (arm_mmu.c Фаза 2): identity убирается, используем BCM2835_VIRT.
// ============================================================================
#define BCM2835_PERIPH_VIRT_BASE  0xE0000000

// Translate physical peripheral register address to kernel virtual address.
// Example: BCM2835_VIRT(BCM2835_ST_CS) → 0xE0003000
#define BCM2835_VIRT(reg) \
    ((reg) - BCM2835_PERIPH_BASE + BCM2835_PERIPH_VIRT_BASE)

// --- System Timer (MMIO) ---
#define BCM2835_ST_BASE         (BCM2835_PERIPH_BASE + 0x3000)
#define BCM2835_ST_CS           (BCM2835_ST_BASE + 0x00)  // Control/Status
#define BCM2835_ST_CLO          (BCM2835_ST_BASE + 0x04)  // Counter Low
#define BCM2835_ST_CHI          (BCM2835_ST_BASE + 0x08)  // Counter High
#define BCM2835_ST_C0           (BCM2835_ST_BASE + 0x0C)  // Compare 0
#define BCM2835_ST_C1           (BCM2835_ST_BASE + 0x10)  // Compare 1
#define BCM2835_ST_C2           (BCM2835_ST_BASE + 0x14)  // Compare 2
#define BCM2835_ST_C3           (BCM2835_ST_BASE + 0x18)  // Compare 3
#define BCM2835_ST_FREQUENCY    1000000  // 1 MHz

// --- ARM Timer (SP804-like, MMIO) ---
#define BCM2835_ARM_TIMER_BASE  (BCM2835_PERIPH_BASE + 0xB000)
#define BCM2835_ARM_TIMER_LOAD  (BCM2835_ARM_TIMER_BASE + 0x400)
#define BCM2835_ARM_TIMER_VALUE (BCM2835_ARM_TIMER_BASE + 0x404)
#define BCM2835_ARM_TIMER_CTRL  (BCM2835_ARM_TIMER_BASE + 0x408)
#define BCM2835_ARM_TIMER_IRQCLR (BCM2835_ARM_TIMER_BASE + 0x40C)
#define BCM2835_ARM_TIMER_RELOAD (BCM2835_ARM_TIMER_BASE + 0x418)
#define BCM2835_ARM_TIMER_PREDIV (BCM2835_ARM_TIMER_BASE + 0x41C)
#define BCM2835_ARM_TIMER_CNTR  (BCM2835_ARM_TIMER_BASE + 0x420)

// --- Interrupt Controller (MMIO) ---
#define BCM2835_IRQ_BASE        (BCM2835_PERIPH_BASE + 0xB200)
#define BCM2835_IRQ_BASIC       (BCM2835_IRQ_BASE + 0x00)
#define BCM2835_IRQ_PEND1       (BCM2835_IRQ_BASE + 0x04)
#define BCM2835_IRQ_PEND2       (BCM2835_IRQ_BASE + 0x08)
#define BCM2835_IRQ_FIQ_CTRL    (BCM2835_IRQ_BASE + 0x0C)
#define BCM2835_IRQ_ENABLE1     (BCM2835_IRQ_BASE + 0x10)
#define BCM2835_IRQ_ENABLE2     (BCM2835_IRQ_BASE + 0x14)
#define BCM2835_IRQ_ENABLE_BASIC (BCM2835_IRQ_BASE + 0x18)
#define BCM2835_IRQ_DISABLE1    (BCM2835_IRQ_BASE + 0x1C)
#define BCM2835_IRQ_DISABLE2    (BCM2835_IRQ_BASE + 0x20)
#define BCM2835_IRQ_DISABLE_BASIC (BCM2835_IRQ_BASE + 0x24)

// IRQ line numbers (GPU IRQs in PEND1)
#define BCM2835_IRQ_TIMER0      0   // System Timer 0
#define BCM2835_IRQ_TIMER1      1   // System Timer 1
#define BCM2835_IRQ_TIMER2      2   // System Timer 2
#define BCM2835_IRQ_TIMER3      3   // System Timer 3
#define BCM2835_IRQ_AUX         29  // AUX (UART1, SPI1, SPI2)

// Basic IRQs (in BASIC register)
#define BCM2835_IRQ_BASIC_ARM_TIMER 0
#define BCM2835_IRQ_BASIC_ARM_MAILBOX 1
#define BCM2835_IRQ_BASIC_GPU0_HALTED 7

// --- GPIO (MMIO) ---
#define BCM2835_GPIO_BASE       (BCM2835_PERIPH_BASE + 0x200000)
#define BCM2835_GPIO_GPFSEL0    (BCM2835_GPIO_BASE + 0x00)
#define BCM2835_GPIO_GPFSEL1    (BCM2835_GPIO_BASE + 0x04)
#define BCM2835_GPIO_GPSET0     (BCM2835_GPIO_BASE + 0x1C)
#define BCM2835_GPIO_GPCLR0     (BCM2835_GPIO_BASE + 0x28)
#define BCM2835_GPIO_GPLEV0     (BCM2835_GPIO_BASE + 0x34)
#define BCM2835_GPIO_GPPUD      (BCM2835_GPIO_BASE + 0x94)
#define BCM2835_GPIO_GPPUDCLK0  (BCM2835_GPIO_BASE + 0x98)

// --- UART0: PL011 (MMIO) ---
#define BCM2835_UART0_BASE      (BCM2835_PERIPH_BASE + 0x201000)
#define PL011_DR                (BCM2835_UART0_BASE + 0x00)  // Data Register
#define PL011_FR                (BCM2835_UART0_BASE + 0x18)  // Flag Register
#define PL011_IBRD              (BCM2835_UART0_BASE + 0x24)  // Integer Baud Rate
#define PL011_FBRD              (BCM2835_UART0_BASE + 0x28)  // Fractional Baud Rate
#define PL011_LCRH              (BCM2835_UART0_BASE + 0x2C)  // Line Control
#define PL011_CR                (BCM2835_UART0_BASE + 0x30)  // Control Register
#define PL011_IMSC              (BCM2835_UART0_BASE + 0x38)  // Interrupt Mask
#define PL011_ICR               (BCM2835_UART0_BASE + 0x44)  // Interrupt Clear

// PL011 Flag Register bits
#define PL011_FR_TXFF           (1 << 5)  // TX FIFO Full
#define PL011_FR_RXFE           (1 << 4)  // RX FIFO Empty
#define PL011_FR_BUSY           (1 << 3)  // UART Busy

// PL011 Control Register bits
#define PL011_CR_UARTEN         (1 << 0)  // UART Enable
#define PL011_CR_TXE            (1 << 8)  // TX Enable
#define PL011_CR_RXE            (1 << 9)  // RX Enable

// PL011 Line Control bits
#define PL011_LCRH_WLEN_8       (3 << 5)  // 8-bit word
#define PL011_LCRH_FEN          (1 << 4)  // FIFO Enable

// --- MMU: ARMv6 Translation Table ---
// TTBR0: User space (0x00000000 - 0xBFFFFFFF), 4096 entries, 16KB
// TTBR1: Kernel space (0xC0000000 - 0xFFFFFFFF), 1024 entries, 4KB, N=2
#define ARM_TTBR0_ENTRIES       4096
#define ARM_TTBR0_SIZE          (ARM_TTBR0_ENTRIES * 4)  // 16384 bytes (16 KB)
#define ARM_TTBR0_ALIGN         16384                     // Must be 16KB aligned

#define ARM_TTBR1_ENTRIES       1024
#define ARM_TTBR1_SIZE          (ARM_TTBR1_ENTRIES * 4)  // 4096 bytes (4 KB)
#define ARM_TTBR1_ALIGN         4096                      // Must be 4KB aligned
#define ARM_TTBR1_N             2                         // N field: 2^(32-2) = 1GB

// First-Level Descriptor types
#define ARM_L1_TYPE_FAULT       0x00  // Unmapped
#define ARM_L1_TYPE_COARSE      0x01  // Points to Second-Level Table
#define ARM_L1_TYPE_SECTION     0x02  // 1MB Section
#define ARM_L1_TYPE_SUPERSECTION 0x03 // 16MB (not used)

// Section descriptor bits (for 1MB mappings)
#define ARM_L1_SECTION_B        (1 << 2)   // Bufferable
#define ARM_L1_SECTION_C        (1 << 3)   // Cacheable
#define ARM_L1_SECTION_XN       (1 << 4)   // Execute-Never
#define ARM_L1_SECTION_DOMAIN(d) (((d) & 0xF) << 5)
#define ARM_L1_SECTION_AP(ap)   (((ap) & 0x3) << 10)
#define ARM_L1_SECTION_TEX(t)   (((t) & 0x7) << 12)
#define ARM_L1_SECTION_APX      (1 << 15)  // AP[2] (extended)
#define ARM_L1_SECTION_S        (1 << 16)  // Shareable
#define ARM_L1_SECTION_NG       (1 << 17)  // Not-Global (ASID)

// Second-Level Descriptor (Small Page, 4KB)
#define ARM_L2_TYPE_FAULT       0x00
#define ARM_L2_TYPE_SMALL_PAGE  0x02
#define ARM_L2_SMALL_B          (1 << 2)
#define ARM_L2_SMALL_C          (1 << 3)
#define ARM_L2_SMALL_AP(ap)     (((ap) & 0x3) << 4)
#define ARM_L2_SMALL_TEX(t)     (((t) & 0x7) << 6)
#define ARM_L2_SMALL_APX        (1 << 9)
#define ARM_L2_SMALL_S          (1 << 10)
#define ARM_L2_SMALL_NG         (1 << 11)
#define ARM_L2_SMALL_XN         (1 << 0)

// Access Permissions (AP[2:0] with APX)
#define ARM_AP_NONE             0x0  // No access
#define ARM_AP_KERNEL_RW        0x1  // Kernel: RW, User: None
#define ARM_AP_KERNEL_RW_USER_RO 0x2 // Kernel: RW, User: RO
#define ARM_AP_KERNEL_RW_USER_RW 0x3 // Kernel: RW, User: RW

// Domains
#define ARM_DOMAIN_KERNEL       0    // Kernel domain (Manager)
#define ARM_DOMAIN_USER         1    // User domain (Client)

// DACR values
#define ARM_DACR_NO_ACCESS(d)   (0x0 << ((d) * 2))
#define ARM_DACR_CLIENT(d)      (0x1 << ((d) * 2))
#define ARM_DACR_MANAGER(d)     (0x3 << ((d) * 2))

// Memory types (TEX/C/B encoding for Normal Memory)
#define ARM_MEM_NORMAL_WB_WA    0x1FB  // TEX=1, C=1, B=1 (Write-Back, Write-Allocate)
#define ARM_MEM_NORMAL_WT       0x1FA  // TEX=1, C=1, B=0 (Write-Through)
#define ARM_MEM_NORMAL_NC       0x1F8  // TEX=1, C=0, B=0 (Non-Cacheable)
#define ARM_MEM_DEVICE          0x004  // TEX=0, C=0, B=1 (Device/Shared)
#define ARM_MEM_STRONGLY_ORDERED 0x000 // TEX=0, C=0, B=0

// --- ATAGS (Boot parameters from GPU) ---
#define ARM_ATAGS_BASE          0x00000100  // Default ATAGS address (r2)
#define ATAG_NONE               0x00000000
#define ATAG_CORE               0x54410001
#define ATAG_MEM                0x54410002
#define ATAG_CMDLINE            0x54410009

// --- ARM Syscall ---
#define ARM_SYSCALL_SVC         0  // svc #0

// --- Kernel physical load address (from start.elf) ---
#define ARM_KERNEL_PHYS_BASE    0x00010000  /* QEMU raspi1ap */

// ============================================================================
// ARM USER MODE 4KB PAGE SPIKE (Day 52+ Per-Process Isolation)
// ============================================================================
// Каждая задача получает собственный L1 address space и собственные физические
// 4KB страницы. Коллизии виртуальных адресов невозможны.
//
// Code: 1 страница (4 KB)
// Data: 1 страница (4 KB), используется для данных и стека.
// ============================================================================

#define ARM_USER_CODE_VA_4K     0x00001000  // 4 KB (избегаем NULL page)
#define ARM_USER_DATA_VA_4K     0x00002000  // 8 KB
#define ARM_USER_STACK_VA_4K    0x00003000  // 12 KB (вершина стека внутри data page)

// Размеры для совместимости (если где-то нужны)
#define ARM_USER_CODE_SIZE_4K   0x1000
#define ARM_USER_DATA_SIZE_4K   0x1000

// User stacks live inside the user data section.
#define ARM_USER_STACK_A_TOP        (ARM_USER_DATA_VADDR + 0x80000)
#define ARM_USER_STACK_B_TOP        (ARM_USER_DATA_VADDR + 0xC0000)

// ----------------------------------------------------------------------------
// ARMv6 short descriptor section flags
// ----------------------------------------------------------------------------
//
// Base normal memory section used by boot RAM:
//   0x140E = Section | TEX=001 | C=1 | B=1 | AP=01
//
// User code section:
//   Section | TEX=001 | C=1 | B=1 | AP=10 | XN=0
//   AP=10: privileged RW, user RO
//   Value: 0x180E
//
// User data section:
//   Section | TEX=001 | C=1 | B=1 | AP=11 | XN=1
//   AP=11: privileged RW, user RW
//   Value: 0x1C1E
//
// W^X:
//   code = R/X, no W for user
//   data = R/W, XN
// ----------------------------------------------------------------------------

#define ARM_SECTION_USER_RX         0x180E
#define ARM_SECTION_USER_RWXN       0x1C1E

// ----------------------------------------------------------------------------
// CPSR helpers
// ----------------------------------------------------------------------------
//
// ARM_CPSR_USER:
//   USR mode | FIQ disabled | IRQ enabled
//   Reserved for future true preemptive user mode (Day 45+).
//
// ARM_CPSR_USER_COOP:
//   USR mode | FIQ disabled | IRQ disabled
//   Used for current user-mode spike without IRQ-from-user trap support.
//
// ARM_CPSR_SVC_IRQ_DISABLED:
//   SVC mode | IRQ disabled
//   Used as forged CPSR for first-time user task trampoline.
// ----------------------------------------------------------------------------

#define ARM_CPSR_USER               (ARM_MODE_USR | ARM_CPSR_FIQ_DISABLE)
#define ARM_CPSR_USER_COOP          (ARM_MODE_USR | ARM_CPSR_FIQ_DISABLE | ARM_CPSR_IRQ_DISABLE)
#define ARM_CPSR_SVC_IRQ_DISABLED   (ARM_MODE_SVC | ARM_CPSR_IRQ_DISABLE)

// ============================================================================
// [M4] RPi1 RAM size (default, overridden by ATAGS)
// ============================================================================
// QEMU raspi1ap запускается с -m 512M.
// Реальный RPi1 Model B: 256 MB или 512 MB.
// ATAG_MEM от start.elf перекрывает этот дефолт при boot.
// ============================================================================
#define BCM2835_RAM_SIZE_DEFAULT (512 * 1024 * 1024)  // 512 MB (QEMU raspi1ap)

// ============================================================================
// ARM Mailbox / Framebuffer (Day 55 video spike)
// ============================================================================
// BCM2835 mailbox используется для запроса framebuffer у GPU firmware.
// ARM не управляет HDMI напрямую. GPU выделяет framebuffer и scanout.
// ============================================================================

#define BCM2835_MBOX_BASE             (BCM2835_PERIPH_BASE + 0xB880)

#define BCM2835_MBOX0_READ            (BCM2835_MBOX_BASE + 0x00)
#define BCM2835_MBOX0_STATUS          (BCM2835_MBOX_BASE + 0x18)
#define BCM2835_MBOX1_WRITE           (BCM2835_MBOX_BASE + 0x20)
#define BCM2835_MBOX1_STATUS          (BCM2835_MBOX_BASE + 0x38)

#define BCM2835_MBOX_STATUS_EMPTY     0x40000000u
#define BCM2835_MBOX_STATUS_FULL      0x80000000u

// Property channel: framebuffer / property tags.
#define BCM2835_MBOX_CHANNEL_PROP     8u

// GPU bus address offset.
// Часто firmware ожидает bus address = physical | 0x40000000.
#define BCM2835_MBOX_BUS_OFFSET       0x40000000u

// ============================================================================
// ARM Framebuffer Kernel Virtual Window
// ============================================================================
// 16 MB kernel virtual window для framebuffer.
// Маппится 1MB sections как kernel-only, XN, non-cacheable/device.
// ============================================================================

#define ARM_FB_VIRT_BASE              0xFD000000u
#define ARM_FB_MAX_MAP_SIZE           (16 * 1024 * 1024)



#endif // CONFIG_ARCH_ARM

#endif // CONFIG_H
