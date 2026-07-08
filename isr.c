#include "isr.h"
#include "klib.h"
#include "vga.h"

// Объявляем 32 ассемблерные заглушки для CPU-исключений (ISR 0-31)
extern void isr0();   extern void isr1();   extern void isr2();
extern void isr3();   extern void isr4();   extern void isr5();
extern void isr6();   extern void isr7();   extern void isr8();
extern void isr9();   extern void isr10();  extern void isr11();
extern void isr12();  extern void isr13();  extern void isr14();
extern void isr15();  extern void isr16();  extern void isr17();
extern void isr18();  extern void isr19();  extern void isr20();
extern void isr21();  extern void isr22();  extern void isr23();
extern void isr24();  extern void isr25();  extern void isr26();
extern void isr27();  extern void isr28();  extern void isr29();
extern void isr30();  extern void isr31();

// Объявляем 16 ассемблерных заглушек для IRQ (IRQ 0-15 → INT 32-47)
extern void irq0();   extern void irq1();   extern void irq2();
extern void irq3();   extern void irq4();   extern void irq5();
extern void irq6();   extern void irq7();   extern void irq8();
extern void irq9();   extern void irq10();  extern void irq11();
extern void irq12();  extern void irq13();  extern void irq14();
extern void irq15();

// Названия исключений для отладки
static const char* exception_messages[] = {
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Into Detected Overflow",
    "Out of Bounds",
    "Invalid Opcode",
    "No Coprocessor",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Bad TSS",
    "Segment Not Present",
    "Stack Fault",
    "General Protection Fault",
    "Page Fault",
    "Unknown Interrupt",
    "Coprocessor Fault",
    "Alignment Check",
    "Machine Check",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved"
};

static void exception_handler(struct regs* r) {
    // 1. Дамп в Serial (на случай, если VGA уже мертв)
    serial_printf("\n[!!!] KERNEL PANIC: %s [!!!]\n", exception_messages[r->int_no]);
    serial_printf(" INT: 0x%x | ERR: 0x%x\n", r->int_no, r->err_code);
    serial_printf(" EIP: 0x%x | CS: 0x%x\n", r->eip, r->cs);
    serial_printf(" ESP: 0x%x | SS: 0x%x\n", r->esp, r->ss);
    serial_printf(" EFLAGS: 0x%x\n", r->eflags);
    
    // 2. Дамп на экран
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_RED);
    k_printf("\n KERNEL PANIC: %s \n", exception_messages[r->int_no]);
    k_printf(" EIP: 0x%x | ERR: 0x%x \n", r->eip, r->err_code);
    
    // Останавливаем процессор
    while(1) __asm__ volatile("cli; hlt");
}

void isr_install(void) {
    // Регистрируем 32 CPU-исключения в IDT
    // 0x08 = Kernel Code Segment
    // 0x8E = Interrupt Gate, Ring 0, Present
    idt_set_gate(0,  (uint32_t)isr0,  0x08, 0x8E);
    idt_set_gate(1,  (uint32_t)isr1,  0x08, 0x8E);
    idt_set_gate(2,  (uint32_t)isr2,  0x08, 0x8E);
    idt_set_gate(3,  (uint32_t)isr3,  0x08, 0x8E);
    idt_set_gate(4,  (uint32_t)isr4,  0x08, 0x8E);
    idt_set_gate(5,  (uint32_t)isr5,  0x08, 0x8E);
    idt_set_gate(6,  (uint32_t)isr6,  0x08, 0x8E);
    idt_set_gate(7,  (uint32_t)isr7,  0x08, 0x8E);
    idt_set_gate(8,  (uint32_t)isr8,  0x08, 0x8E);
    idt_set_gate(9,  (uint32_t)isr9,  0x08, 0x8E);
    idt_set_gate(10, (uint32_t)isr10, 0x08, 0x8E);
    idt_set_gate(11, (uint32_t)isr11, 0x08, 0x8E);
    idt_set_gate(12, (uint32_t)isr12, 0x08, 0x8E);
    idt_set_gate(13, (uint32_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E);
    idt_set_gate(15, (uint32_t)isr15, 0x08, 0x8E);
    idt_set_gate(16, (uint32_t)isr16, 0x08, 0x8E);
    idt_set_gate(17, (uint32_t)isr17, 0x08, 0x8E);
    idt_set_gate(18, (uint32_t)isr18, 0x08, 0x8E);
    idt_set_gate(19, (uint32_t)isr19, 0x08, 0x8E);
    idt_set_gate(20, (uint32_t)isr20, 0x08, 0x8E);
    idt_set_gate(21, (uint32_t)isr21, 0x08, 0x8E);
    idt_set_gate(22, (uint32_t)isr22, 0x08, 0x8E);
    idt_set_gate(23, (uint32_t)isr23, 0x08, 0x8E);
    idt_set_gate(24, (uint32_t)isr24, 0x08, 0x8E);
    idt_set_gate(25, (uint32_t)isr25, 0x08, 0x8E);
    idt_set_gate(26, (uint32_t)isr26, 0x08, 0x8E);
    idt_set_gate(27, (uint32_t)isr27, 0x08, 0x8E);
    idt_set_gate(28, (uint32_t)isr28, 0x08, 0x8E);
    idt_set_gate(29, (uint32_t)isr29, 0x08, 0x8E);
    idt_set_gate(30, (uint32_t)isr30, 0x08, 0x8E);
    idt_set_gate(31, (uint32_t)isr31, 0x08, 0x8E);

    // Регистрируем общий обработчик для всех исключений
    for (int i = 0; i < 32; i++) {
        isr_register_handler(i, exception_handler);
    }
}

void irq_install(void) {
    // Remap PIC (ОБЯЗАТЕЛЬНО ДО регистрации IRQ в IDT!)
    extern void pic_remap(void);
    pic_remap();

    // Регистрируем IRQ 0-15 в IDT (прерывания 32-47)
    idt_set_gate(32, (uint32_t)irq0,  0x08, 0x8E);
    idt_set_gate(33, (uint32_t)irq1,  0x08, 0x8E);
    idt_set_gate(34, (uint32_t)irq2,  0x08, 0x8E);
    idt_set_gate(35, (uint32_t)irq3,  0x08, 0x8E);
    idt_set_gate(36, (uint32_t)irq4,  0x08, 0x8E);
    idt_set_gate(37, (uint32_t)irq5,  0x08, 0x8E);
    idt_set_gate(38, (uint32_t)irq6,  0x08, 0x8E);
    idt_set_gate(39, (uint32_t)irq7,  0x08, 0x8E);
    idt_set_gate(40, (uint32_t)irq8,  0x08, 0x8E);
    idt_set_gate(41, (uint32_t)irq9,  0x08, 0x8E);
    idt_set_gate(42, (uint32_t)irq10, 0x08, 0x8E);
    idt_set_gate(43, (uint32_t)irq11, 0x08, 0x8E);
    idt_set_gate(44, (uint32_t)irq12, 0x08, 0x8E);
    idt_set_gate(45, (uint32_t)irq13, 0x08, 0x8E);
    idt_set_gate(46, (uint32_t)irq14, 0x08, 0x8E);
    idt_set_gate(47, (uint32_t)irq15, 0x08, 0x8E);
    
}
