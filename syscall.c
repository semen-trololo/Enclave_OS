#include "syscall.h"
#include "idt.h"
#include "isr.h"
#include "klib.h"
#include "vfs.h"   // Маршрутизация в VFS
#include "task.h"  // Для task_exit()
#include "serial.h"
#include <stdint.h>
#include <stdbool.h>
#include "config.h"
#include "vma.h"
#include "pmm.h"
#include "elf.h"
#include "config.h"
#include "paging.h" 

typedef int (*syscall_func_t)(struct regs* r);

#define MAX_SYSCALLS 256
static syscall_func_t syscall_table[MAX_SYSCALLS];

static inline bool is_user_pointer(const void* ptr, size_t size) {
    uint32_t addr = (uint32_t)ptr;
    // 1. Указатель не должен смотреть в Kernel Space
    if (addr >= KERNEL_SPACE_START) return false;
    // 2. Размер не должен быть абсурдным
    if (size > USER_SPACE_END) return false;
    // 3. Защита от переполнения (wrap-around) при сложении
    if (addr + size > KERNEL_SPACE_START) return false; 
    return true;
}

// ========================================================================
// Реализации системных вызовов
// ========================================================================

static int sys_exit_handler(struct regs* r) {
    uint32_t exit_code = r->ebx;
    serial_printf("[SYSCALL] PID %d exiting with code %u\n", current_task->pid, exit_code);

    // 🛡️ ИСПРАВЛЕНО: Убираем Context Hijacking.
    // task_exit() корректно закроет FD, пометит задачу как DEAD, 
    // и передаст управление планировщику (Reaper освободит память).
    task_exit(); 
    
    // Недостижимо, так как task_exit() делает schedule() и не возвращается
    return 0;
}

static int sys_write_handler(struct regs* r) {
    int fd = (int)r->ebx;
    const void* buf = (const void*)r->ecx;
    uint32_t count = r->edx;

    if (!is_user_pointer(buf, count)) {
        serial_printf("[SYSCALL] FATAL: Ring 3 attempted kernel write! Addr: 0x%x\n", (uint32_t)buf);
        return -1; // EFAULT
    }

    // Делегируем работу в VFS (используется fd_table процесса)
    return sys_write(fd, buf, count);
}

static int sys_read_handler(struct regs* r) {
    int fd = (int)r->ebx;
    void* buf = (void*)r->ecx;
    uint32_t count = r->edx;

    if (!is_user_pointer(buf, count)) {
        serial_printf("[SYSCALL] FATAL: Ring 3 attempted kernel read! Addr: 0x%x\n", (uint32_t)buf);
        return -1; // EFAULT
    }

    return sys_read(fd, buf, count);
}

static int sys_yield_handler(struct regs* r) {
    (void)r;
    extern void schedule(void);
    schedule(); 
    return 0;
}

// ========================================================================
// Диспетчер системных вызовов
// ========================================================================
static void syscall_dispatcher(struct regs* r) {
    uint32_t syscall_num = r->eax;

    if (syscall_num >= MAX_SYSCALLS || syscall_table[syscall_num] == 0) {
        serial_printf("[SYSCALL] Invalid syscall number: %u\n", syscall_num);
        r->eax = (uint32_t)-1; 
        return;
    }

    r->eax = (uint32_t)syscall_table[syscall_num](r);
}

// ========================================================================
// sys_brk: Динамическое управление кучей процесса
// ========================================================================
static int sys_brk_handler(struct regs* r) {
    uint32_t new_brk = r->ebx;
    
    // Если new_brk == 0, возвращаем текущий конец кучи
    if (new_brk == 0) {
        // Ищем VMA кучи (последняя VMA перед стеком)
        vma_node_t* current = current_task->vma_head;
        vma_node_t* heap_vma = NULL;
        
        while (current) {
            if (current->start >= USER_HEAP_START && current->start < USER_HEAP_START + USER_HEAP_MAX_SIZE) {
                heap_vma = current;
            }
            current = current->next;
        }
        
        if (heap_vma) {
            return heap_vma->end;
        } else {
            return USER_HEAP_START; // Куча еще не создана
        }
    }
    
    // Проверяем валидность нового адреса
    if (new_brk < USER_HEAP_START || new_brk >= USER_STACK_VIRT_TOP - USER_STACK_SIZE) {
        serial_printf("[SYSCALL] sys_brk: Invalid address 0x%x\n", new_brk);
        return -12; // -ENOMEM
    }
    
    // Ищем существующую VMA кучи
    vma_node_t* current = current_task->vma_head;
    vma_node_t* heap_vma = NULL;
    
    while (current) {
        if (current->start >= USER_HEAP_START && current->start < USER_HEAP_START + USER_HEAP_MAX_SIZE) {
            heap_vma = current;
            break;
        }
        current = current->next;
    }
    
    if (!heap_vma) {
        // Создаем новую VMA для кучи
        vma_add(current_task, USER_HEAP_START, new_brk, VMA_READ | VMA_WRITE);
        serial_printf("[SYSCALL] sys_brk: Created heap VMA 0x%x - 0x%x\n", 
                      USER_HEAP_START, new_brk);
    } else {
        // Расширяем существующую VMA
        if (new_brk > heap_vma->end) {
            // Проверяем, не пересекается ли со стеком
            if (new_brk >= USER_STACK_VIRT_TOP - USER_STACK_SIZE) {
                serial_printf("[SYSCALL] sys_brk: Heap would overlap stack\n");
                return -12; // -ENOMEM
            }
            
            // Проактивная проверка OOM
            uint32_t pages_needed = (new_brk - heap_vma->end + 0xFFF) / 0x1000;
            uint32_t free_pages = pmm_get_free_pages();
            
            if (pages_needed > free_pages / 2) { // Оставляем половину для ядра
                serial_printf("[SYSCALL] sys_brk: OOM protection triggered\n");
                return -12; // -ENOMEM
            }
            
            heap_vma->end = new_brk;
            serial_printf("[SYSCALL] sys_brk: Extended heap to 0x%x\n", new_brk);
        }
    }
    
    return 0; // Успех
}

// ========================================================================
// sys_exec: Загрузка и запуск ELF-бинарника в User Mode
// ========================================================================
static int sys_exec_handler(struct regs* r) {
    const char* filename = (const char*)r->ebx;
    
    // Проверка указателя из User Space
    if (!is_user_pointer(filename, 1)) {
        serial_printf("[SYSCALL] sys_exec: Invalid filename pointer\n");
        return -14; // EFAULT
    }
    
    serial_printf("[SYSCALL] sys_exec: Loading '%s'\n", filename);
    
    // 1. Поиск файла в VFS
    vfs_node_t* file_node = vfs_findnode(filename);
    if (!file_node) {
        serial_printf("[SYSCALL] sys_exec: File not found: %s\n", filename);
        return -2; // ENOENT
    }
    
    if (!(file_node->flags & FS_FILE)) {
        serial_printf("[SYSCALL] sys_exec: Not a file: %s\n", filename);
        return -2; // ENOENT
    }
    
    // 2. Создание адресного пространства для нового процесса
    uint32_t* pdir_virt = vmm_create_address_space();
    if (!pdir_virt) {
        serial_print("[SYSCALL] sys_exec: OOM creating address space\n");
        return -12; // ENOMEM
    }
    
    // 3. Загрузка ELF
    uint32_t entry_point = elf_load(file_node, pdir_virt);
    if (entry_point == 0) {
        serial_print("[SYSCALL] sys_exec: Failed to load ELF\n");
        vmm_destroy_address_space(pdir_virt);
        return -8; // ENOEXEC
    }
    
    // 4. Создание VMA для стека (с Guard Page)
    uint32_t stack_top = USER_STACK_VIRT_TOP;
    uint32_t stack_bottom = stack_top - USER_STACK_SIZE;
    
    // Guard Page (не мапим, не добавляем в VMA)
    // Stack VMA
    vma_add(current_task, stack_bottom, stack_top, VMA_READ | VMA_WRITE);
    
    // 5. Создание VMA для кучи (начальный размер 0)
    vma_add(current_task, USER_HEAP_START, USER_HEAP_START, VMA_READ | VMA_WRITE);
    
    // 6. Создание user-mode задачи
    task_t* new_task = task_create(filename, (void (*)(void))entry_point, true, stack_top);
    if (!new_task) {
        serial_print("[SYSCALL] sys_exec: Failed to create task\n");
        vmm_destroy_address_space(pdir_virt);
        return -12; // ENOMEM
    }
    
    // 7. Копируем Page Directory в новую задачу
    // (task_create уже создал свой pdir_virt, но нам нужно использовать тот, в который загрузили ELF)
    // TODO: Нужно модифицировать task_create, чтобы она принимала готовый pdir_virt
    
    serial_printf("[SYSCALL] sys_exec: Started PID %d at 0x%x\n", 
                  new_task->pid, entry_point);
    
    return new_task->pid;
}

// ========================================================================
// Инициализация
// ========================================================================
void syscall_init(void) {
    
    for (int i = 0; i < MAX_SYSCALLS; i++) syscall_table[i] = 0;

    syscall_table[SYS_EXIT]  = sys_exit_handler;
    syscall_table[SYS_READ]  = sys_read_handler;
    syscall_table[SYS_WRITE] = sys_write_handler;
    syscall_table[SYS_YIELD] = sys_yield_handler;
    syscall_table[SYS_BRK] = sys_brk_handler;
    syscall_table[SYS_EXEC] = sys_exec_handler;
    extern void isr128(); 
    idt_set_gate(128, (uint32_t)isr128, 0x08, 0xEE); // DPL=3 для Ring 3

    isr_register_handler(128, syscall_dispatcher);
    serial_print("[SYSCALL] INT 0x80 dispatcher initialized.\n");
}
