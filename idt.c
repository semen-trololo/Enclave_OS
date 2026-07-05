#include "idt.h"
#include "isr.h"
#include <stddef.h>
#include "port_io.h"

#define IDT_ENTRIES 256

// Таблица IDT (256 дескрипторов)
static struct idt_entry idt[IDT_ENTRIES];

// Указатель на IDT (передаётся процессору через lidt)
static struct idt_ptr idtp;

// Внешняя ASM-функция для загрузки IDT
extern void idt_flush(uint32_t);

// Массивы C-обработчиков
static isr_handler_t isr_handlers[IDT_ENTRIES];
static isr_handler_t irq_handlers[16];

// Установка одного дескриптора в IDT
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low  = base & 0xFFFF;          // Младшие 16 бит адреса
    idt[num].base_high = (base >> 16) & 0xFFFF;  // Старшие 16 бит адреса
    idt[num].sel       = sel;                     // Селектор сегмента кода
    idt[num].always0   = 0;                       // Reserved
    idt[num].flags     = flags;                   // Тип gate + DPL + Present
}

void idt_install(void) {
    // Настраиваем указатель на IDT
    idtp.limit = (sizeof(struct idt_entry) * IDT_ENTRIES) - 1;
    idtp.base  = (uint32_t)&idt;

    // Обнуляем массивы обработчиков
    for (int i = 0; i < IDT_ENTRIES; i++) {
        isr_handlers[i] = NULL;
    }
    for (int i = 0; i < 16; i++) {
        irq_handlers[i] = NULL;
    }

    // Регистрируем CPU-исключения (ISR 0-31)
    isr_install();
    
    // Регистрируем IRQ (32-47)
    irq_install();

    // Загружаем IDT в процессор
    idt_flush((uint32_t)&idtp);

     sti(); 
}

void isr_register_handler(uint8_t n, isr_handler_t handler) {
    isr_handlers[n] = handler;
}

void irq_register_handler(uint8_t n, isr_handler_t handler) {
    if (n < 16) {
        irq_handlers[n] = handler;
    }
}

// Вызывается из ASM после сохранения регистров (для ISR)
void isr_handler(struct regs* r) {
    if (isr_handlers[r->int_no]) {
        // Если зарегистрирован обработчик - вызываем его
        isr_handlers[r->int_no](r);
    } else {
        // Нет обработчика - паника ядра
        extern void k_print(const char*);
        k_print("\n KERNEL PANIC: Unhandled exception! INT: ");
        // TODO: напечатаем номер, когда сделаем k_printf
        while(1) __asm__ volatile("hlt");
    }
}

// Вызывается из ASM для IRQ (с EOI контроллеру)
// Вызывается из ASM для IRQ (с EOI контроллеру)
void irq_handler(struct regs* r) {
    int irq_num = r->int_no - 32;
    
    // ✅ СРАЗУ шлём EOI в контроллер! Чтобы PIC мог принимать новые прерывания
    extern void outb(uint16_t, uint8_t);
    if (irq_num >= 8) { outb(0xA0, 0x20); }
    outb(0x20, 0x20);

    if (irq_handlers[irq_num]) {
        irq_handlers[irq_num](r);
    }


    // Отправляем End-Of-Interrupt (EOI) контроллеру PIC
    // Это сигнал PIC'у, что мы закончили обработку и можно принимать новые IRQ
    extern void outb(uint16_t, uint8_t);
    
    // Если IRQ пришёл от Slave PIC (IRQ 8-15), шлём EOI и ему, и Master'у
    if (irq_num >= 8) {
        outb(0xA0, 0x20);  // Slave PIC EOI
    }
    outb(0x20, 0x20);      // Master PIC EOI
}
