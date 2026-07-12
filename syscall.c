#include "syscall.h"
#include "idt.h"
#include "isr.h"
#include "klib.h"
#include "vfs.h"
#include "task.h"
#include "serial.h"
#include <stdint.h>
#include <stdbool.h>
#include "config.h"
#include "vma.h"
#include "pmm.h"
#include "elf.h"
#include "paging.h"

// ========================================================================
// Тип для функций-обработчиков системных вызовов
// ========================================================================
typedef int (*syscall_func_t)(struct regs* r);

// ========================================================================
// Максимальное количество системных вызовов (256 для совместимости с Linux)
// ========================================================================
#define MAX_SYSCALLS 256

// ========================================================================
// ✅ Designated Initializers: таблица инициализируется на этапе КОМПИЛЯЦИИ
// Все пропущенные индексы автоматически заполняются NULL
// Это стандарт индустрии (Linux, FreeBSD) — безопасно и читаемо
// ========================================================================
static syscall_func_t syscall_table[MAX_SYSCALLS] = {
    [SYS_EXIT]   = NULL,
    [SYS_READ]   = NULL,
    [SYS_WRITE]  = NULL,
    [SYS_OPEN]   = NULL,
    [SYS_CLOSE]  = NULL,
    [SYS_UNLINK] = NULL,
    [SYS_YIELD]  = NULL,
    [SYS_BRK]    = NULL,
    [SYS_EXEC]   = NULL,
    // Все остальные 247 элементов = NULL автоматически
};

// ========================================================================
// ✅ Zero Trust: безопасная проверка указателя из Ring 3
// Проверяет, что весь диапазон [ptr, ptr+size) находится в User Space
// ========================================================================
static inline bool is_user_pointer(const void* ptr, size_t size) {
    if (!ptr) return false;
    uint32_t addr = (uint32_t)ptr;
    
    // Адрес должен быть строго в User Space
    if (addr >= KERNEL_SPACE_START) return false;
    
    // Проверка переполнения: addr + size не должен выйти за USER_SPACE_END
    if (size == 0) return true;
    if (addr > USER_SPACE_END - size + 1) return false;
    
    return true;
}

// ========================================================================
// ✅ Zero Trust: безопасное копирование строки из Ring 3 → Ring 0
// Проверяет КАЖДЫЙ байт перед чтением. Возвращает 0 (успех) или -errno
// ========================================================================
static int copy_string_from_user(char* dest, const char* user_src, size_t max_len) {
    if (!dest || !user_src) return -EFAULT;
    if ((uint32_t)user_src >= KERNEL_SPACE_START) return -EFAULT;
    
    size_t copied = 0;
    const char* src = user_src;
    
    while (copied < max_len - 1) {
        // Проверяем каждый байт ПЕРЕД чтением (Zero Trust)
        if ((uint32_t)src >= KERNEL_SPACE_START) return -EFAULT;
        
        char byte = *src;
        *dest = byte;
        dest++;
        src++;
        copied++;
        
        if (byte == '\0') return 0;  // Успешно скопировали null-terminated строку
    }
    
    // Строка слишком длинная или не имеет завершающего нуля
    dest[0] = '\0';  // Гарантируем null-терминацию в буфере ядра
    return -ENAMETOOLONG;
}

// ========================================================================
// sys_exit: завершение текущего процесса
// Регистр EBX содержит код выхода
// ========================================================================
static int sys_exit_handler(struct regs* r) {
    (void)r;
    uint32_t exit_code = r->ebx;  // Берем код выхода из регистра
    serial_printf("[SYSCALL] PID %d exiting with code %u\n", current_task->pid, exit_code);
    task_exit(); 
    return 0;  // task_exit() не возвращает, но компилятор спокоен
}

// ========================================================================
// sys_write: запись в файловый дескриптор
// EBX = fd, ECX = buffer, EDX = count
// ========================================================================
static int sys_write_handler(struct regs* r) {
    int fd = (int)r->ebx;
    const void* buf = (const void*)r->ecx;
    uint32_t count = r->edx;

    if (!is_user_pointer(buf, count)) {
        serial_printf("[SYSCALL] FATAL: Ring 3 attempted kernel write! Addr: 0x%x\n", (uint32_t)buf);
        return -EFAULT;
    }

    return sys_write(fd, buf, count);
}

// ========================================================================
// sys_read: чтение из файлового дескриптора
// EBX = fd, ECX = buffer, EDX = count
// ========================================================================
static int sys_read_handler(struct regs* r) {
    int fd = (int)r->ebx;
    void* buf = (void*)r->ecx;
    uint32_t count = r->edx;

    if (!is_user_pointer(buf, count)) {
        serial_printf("[SYSCALL] FATAL: Ring 3 attempted kernel read! Addr: 0x%x\n", (uint32_t)buf);
        return -EFAULT;
    }

    return sys_read(fd, buf, count);
}

// ========================================================================
// sys_yield: добровольная передача управления планировщику
// ========================================================================
static int sys_yield_handler(struct regs* r) {
    (void)r;
    extern void schedule(void);
    schedule(); 
    return 0;
}

// ========================================================================
// sys_brk: управление кучей процесса (Day 12: VMA Collision Detection)
// EBX = новый конец кучи (или 0 для запроса текущего значения)
// ========================================================================
static int sys_brk_handler(struct regs* r) {
    uint32_t new_brk = r->ebx;
    
    // Запрос текущего конца кучи
    if (new_brk == 0) {
        vma_node_t* current = current_task->vma_head;
        while (current) {
            if (current->start == USER_HEAP_START) {
                return current->end;
            }
            current = current->next;
        }
        return USER_HEAP_START;
    }
    
    // Валидация нового адреса
    if (new_brk < USER_HEAP_START || new_brk >= USER_STACK_VIRT_TOP - USER_STACK_SIZE) {
        serial_printf("[SYSCALL] sys_brk: Invalid address 0x%x\n", new_brk);
        return -EINVAL;
    }
    
    // Поиск VMA для кучи
    vma_node_t* heap_vma = NULL;
    vma_node_t* current = current_task->vma_head;
    
    while (current) {
        if (current->start == USER_HEAP_START) {
            heap_vma = current;
            break;
        }
        current = current->next;
    }
    
    if (!heap_vma) {
        int ret = vma_add(current_task, USER_HEAP_START, new_brk, VMA_READ | VMA_WRITE);
        if (ret < 0) {
            serial_printf("[SYSCALL] sys_brk: OOM creating heap VMA\n");
            return -ENOMEM;
        }
        serial_printf("[SYSCALL] sys_brk: Created heap VMA 0x%x - 0x%x\n", 
                      USER_HEAP_START, new_brk);
    } else {
        if (new_brk > heap_vma->end) {
            // ✅ Day 12: VMA Collision Detection
            // Проверяем, не уперлась ли куча в mmap-регионы или .text
            if (vma_intersects(current_task, heap_vma->end, new_brk, heap_vma)) {
                serial_printf("[SYSCALL] sys_brk: Heap would overlap other VMA (mmap or .text)\n");
                return -ENOMEM;
            }
            
            // OOM protection: не выделяем больше половины свободной памяти
            uint32_t pages_needed = (new_brk - heap_vma->end + 0xFFF) / 0x1000;
            uint32_t free_pages = pmm_get_free_pages();
            
            if (pages_needed > free_pages / 2) {
                serial_printf("[SYSCALL] sys_brk: OOM protection triggered\n");
                return -ENOMEM;
            }
            
            heap_vma->end = new_brk;
            serial_printf("[SYSCALL] sys_brk: Extended heap to 0x%x\n", new_brk);
        } else if (new_brk < heap_vma->end) {
            // Уменьшение кучи
            heap_vma->end = new_brk;
        }
    }
    
    return 0;
}

// ========================================================================
// ✅ VFS Syscalls с Zero Trust: безопасное копирование строк
// ========================================================================

// sys_unlink: удаление файла
// EBX = pathname
static int sys_unlink_handler(struct regs* r) {
    const char* user_path = (const char*)r->ebx;
    char path_buf[256];
    
    int ret = copy_string_from_user(path_buf, user_path, sizeof(path_buf));
    if (ret < 0) {
        serial_printf("[SYSCALL] sys_unlink: Invalid path pointer or too long\n");
        return ret;  // Возвращаем -EFAULT или -ENAMETOOLONG
    }
    
    return sys_unlink(path_buf);
}

// sys_open: открытие файла
// EBX = pathname, ECX = flags
static int sys_open_handler(struct regs* r) {
    const char* user_path = (const char*)r->ebx;
    uint32_t flags = (uint32_t)r->ecx;
    char path_buf[256];
    
    int ret = copy_string_from_user(path_buf, user_path, sizeof(path_buf));
    if (ret < 0) {
        serial_printf("[SYSCALL] sys_open: Invalid path pointer or too long\n");
        return ret;
    }
    
    return sys_open(path_buf, flags);
}

// sys_close: закрытие файлового дескриптора
// EBX = fd
static int sys_close_handler(struct regs* r) {
    int fd = (int)r->ebx;
    return sys_close(fd);
}

// ========================================================================
// ✅ True POSIX sys_exec: ЗАМЕНА текущего процесса (НЕ spawn!)
// Это критически важно для fork/exec паттерна Unix
// EBX = filename
// ========================================================================
static int sys_exec_handler(struct regs* r) {
    const char* user_filename = (const char*)r->ebx;
    
    // 1. Безопасное копирование имени файла из Ring 3
    char filename_buf[256];
    int ret = copy_string_from_user(filename_buf, user_filename, sizeof(filename_buf));
    if (ret < 0) {
        serial_printf("[SYSCALL] sys_exec: Invalid filename pointer\n");
        return ret;
    }
    
    serial_printf("[SYSCALL] sys_exec: PID %d loading '%s'\n", current_task->pid, filename_buf);
    
    // 2. Поиск файла через VFS
    vfs_node_t* file_node = vfs_findnode(filename_buf);
    if (!file_node) {
        serial_printf("[SYSCALL] sys_exec: File not found: %s\n", filename_buf);
        return -ENOENT;
    }
    
    if (!(file_node->flags & FS_FILE)) {
        serial_printf("[SYSCALL] sys_exec: Not a file: %s\n", filename_buf);
        return -EACCES;
    }
    
    // 3. Создаем НОВОЕ адресное пространство для загрузки ELF
    uint32_t* new_pdir_virt = vmm_create_address_space();
    if (!new_pdir_virt) {
        serial_print("[SYSCALL] sys_exec: OOM creating address space\n");
        return -ENOMEM;
    }
    
    // Временная задача для elf_load (нужна для VMA tracking)
    task_t temp_task;
    k_memset(&temp_task, 0, sizeof(task_t));
    temp_task.pdir_virt = new_pdir_virt;
    temp_task.vma_head = NULL;
    
    // 4. Загружаем ELF в НОВОЕ адресное пространство
    uint32_t entry_point = elf_load(file_node, &temp_task);
    if (entry_point == 0) {
        serial_print("[SYSCALL] sys_exec: Failed to load ELF\n");
        vma_destroy_all(&temp_task);
        vmm_destroy_address_space(new_pdir_virt);
        return -ENOEXEC;
    }
    
    // 5. ✅ НАЧИНАЕМ ЗАМЕНУ: сохраняем идентичность процесса
    uint32_t saved_pid = current_task->pid;
    char saved_name[32];
    k_strncpy(saved_name, current_task->name, sizeof(saved_name));
    
    // Сохраняем FD table (файловые дескрипторы ДОЛЖНЫ пережить exec!)
    struct open_file* saved_fds[TASK_MAX_OPEN_FILES];
    for (int i = 0; i < TASK_MAX_OPEN_FILES; i++) {
        saved_fds[i] = current_task->fd_table[i];
    }
    
    // 6. Уничтожаем СТАРОЕ user-space адресное пространство текущей задачи
    if (current_task->vma_head) {
        vma_destroy_all(current_task);
    }
    
    // 7. ✅ ЗАМЕНЯЕМ адресное пространство: модифицируем текущую task_t
    if (current_task->pdir_virt && current_task->pdir_virt != new_pdir_virt) {
        vmm_destroy_address_space(current_task->pdir_virt);
    }
    
    current_task->pdir_virt = new_pdir_virt;
    current_task->cr3 = VIRT_TO_PHYS(new_pdir_virt);  // Обновляем физический адрес PD
    
    // Переносим VMA из temp_task в current_task
    current_task->vma_head = temp_task.vma_head;
    
    // Восстанавливаем FD table (файловые дескрипторы сохраняются!)
    for (int i = 0; i < TASK_MAX_OPEN_FILES; i++) {
        current_task->fd_table[i] = saved_fds[i];
    }
    
    // Восстанавливаем метаданные
    current_task->pid = saved_pid;
    k_strncpy(current_task->name, saved_name, sizeof(current_task->name));
    
    // 8. Добавляем VMA для стека и кучи в НОВОМ адресном пространстве
    uint32_t stack_top = USER_STACK_VIRT_TOP;
    uint32_t stack_bottom = stack_top - USER_STACK_SIZE;
    
    // Проверяем результат создания VMA (критично для стабильности)
    if (vma_add(current_task, stack_bottom, stack_top, VMA_READ | VMA_WRITE) < 0 ||
        vma_add(current_task, USER_HEAP_START, USER_HEAP_START, VMA_READ | VMA_WRITE) < 0) {
        serial_print("[SYSCALL] sys_exec: OOM creating stack/heap VMA\n");
        // В идеале здесь нужно откатить изменения (kill task), 
        // но пока просто вернем ошибку. IRET вернет управление в старую программу.
        return -ENOMEM; 
    }
    
    serial_printf("[SYSCALL] sys_exec: PID %d replaced with '%s' at 0x%x\n", 
                  current_task->pid, current_task->name, entry_point);
    
    // 9. ✅ КРИТИЧЕСКИЙ МОМЕНТ: модифицируем регистры для возврата в Ring 3
    //    Мы НЕ возвращаемся в старую программу — прыгаем в новую!
    r->eip = entry_point;   // Новая точка входа
    r->esp = stack_top;     // Новый стек
    r->eax = 0;             // exec возвращает 0 при успехе (в новую программу)
    
    // CR3 обновится автоматически при IRET, так как task->cr3 уже изменен
    
    // 10. Возвращаемся из syscall — IRET загрузит новые EIP/ESP/CR3
    return 0;
}
// ========================================================================
// sys_mmap: выделение виртуальной памяти (Day 12)
// EBX = addr, ECX = len, EDX = prot, ESI = flags, EDI = fd, EBP = offset
// ========================================================================
static int sys_mmap_handler(struct regs* r) {
    uint32_t addr = r->ebx;
    uint32_t len = r->ecx;
    uint32_t prot = r->edx;
    uint32_t flags = r->esi;
    // int fd = (int)r->edi;      // Игнорируем в Day 12 (только MAP_ANONYMOUS)
    // uint32_t offset = r->ebp;  // Игнорируем в Day 12

    if (len == 0 || len > USER_MMAP_MAX_SIZE) return -EINVAL;

    // Zero Trust: W^X Enforcement
    if ((prot & PROT_WRITE) && (prot & PROT_EXEC)) {
        serial_printf("[SYSCALL] sys_mmap: W^X violation (Write + Exec)\n");
        return -EPERM;
    }

    // Day 12 MVP: Поддерживаем только анонимную приватную память
    if (!(flags & MAP_ANONYMOUS) || !(flags & MAP_PRIVATE)) {
        return -ENOSYS;
    }
    
    uint32_t aligned_len = (len + 0xFFF) & ~0xFFF;
    uint32_t map_addr = 0;

    if (addr == 0) {
        map_addr = vma_find_free_area(current_task, aligned_len);
        if (map_addr == 0) return -ENOMEM;
    } else {
        map_addr = addr & ~0xFFF; // Page align
        if (vma_intersects(current_task, map_addr, map_addr + aligned_len, NULL)) {
            return -EINVAL; // Коллизия, если не MAP_FIXED
        }
    }

    uint32_t vma_flags = 0;
    if (prot & PROT_READ)  vma_flags |= VMA_READ;
    if (prot & PROT_WRITE) vma_flags |= VMA_WRITE;
    if (prot & PROT_EXEC)  vma_flags |= VMA_EXEC;

    int ret = vma_add(current_task, map_addr, map_addr + aligned_len, vma_flags);
    if (ret < 0) return -ENOMEM;

    serial_printf("[SYSCALL] sys_mmap: Allocated VMA 0x%x - 0x%x\n", map_addr, map_addr + aligned_len);
    return map_addr; // mmap возвращает адрес
}

// ========================================================================
// sys_munmap: освобождение памяти (Day 12)
// EBX = addr, ECX = len
// ========================================================================
static int sys_munmap_handler(struct regs* r) {
    uint32_t addr = r->ebx;
    uint32_t len = r->ecx;

    if (len == 0) return -EINVAL;
    
    uint32_t aligned_addr = addr & ~0xFFF;
    uint32_t aligned_len = (len + 0xFFF) & ~0xFFF;
    uint32_t end = aligned_addr + aligned_len;

    // ✅ Strict Error Propagation: Сначала модифицируем VMA.
    // Если kmalloc внутри vma_unmap_range упадет с OOM, мы НЕ тронем Page Tables.
    int ret = vma_unmap_range(current_task, aligned_addr, end);
    if (ret < 0) return ret;

    // Теперь безопасно освобождаем физические страницы
    for (uint32_t p = aligned_addr; p < end; p += 0x1000) {
        vmm_unmap_and_free_page_in_pd(current_task->pdir_virt, p);
    }

    return 0;
}

// ========================================================================
// sys_mprotect: изменение прав доступа (Day 12)
// EBX = addr, ECX = len, EDX = prot
// ========================================================================
static int sys_mprotect_handler(struct regs* r) {
    uint32_t addr = r->ebx;
    uint32_t len = r->ecx;
    uint32_t prot = r->edx;

    if (len == 0) return -EINVAL;
    if ((prot & PROT_WRITE) && (prot & PROT_EXEC)) return -EPERM;

    uint32_t aligned_addr = addr & ~0xFFF;
    uint32_t aligned_len = (len + 0xFFF) & ~0xFFF;
    uint32_t end = aligned_addr + aligned_len;

    vma_node_t* curr = current_task->vma_head;
    while (curr) {
        if (curr->end <= aligned_addr || curr->start >= end) {
            curr = curr->next;
            continue;
        }
        
        // Обновляем флаги в VMA
        uint32_t vma_flags = 0;
        if (prot & PROT_READ)  vma_flags |= VMA_READ;
        if (prot & PROT_WRITE) vma_flags |= VMA_WRITE;
        if (prot & PROT_EXEC)  vma_flags |= VMA_EXEC;
        curr->flags = vma_flags;
        
        // Обновляем PTE только для пересечения VMA и запрошенного диапазона
        uint32_t p_start = (curr->start > aligned_addr) ? curr->start : aligned_addr;
        uint32_t p_end = (curr->end < end) ? curr->end : end;
        
        uint32_t pte_flags = PAGE_PRESENT | PAGE_USER;
        if (vma_flags & VMA_WRITE) pte_flags |= PAGE_WRITE;
        
        for (uint32_t p = p_start; p < p_end; p += 0x1000) {
            vmm_protect_page_in_pd(current_task->pdir_virt, p, pte_flags);
        }
        
        curr = curr->next;
    }

    return 0;
}

// ============================================================================
// [ДЕНЬ 14] sys_fork: создание копии процесса (Copy-on-Write)
// ============================================================================

static int sys_fork_handler(struct regs* r) {
    serial_printf("[SYSCALL] sys_fork: PID %d\n", current_task->pid);
    return task_fork(r); // Передаем контекст прерывания
}

// ============================================================================
// [ДЕНЬ 14] sys_waitpid: ожидание завершения ребенка
// EBX = pid, ECX = status pointer, EDX = options
// ============================================================================
static int sys_waitpid_handler(struct regs* r) {
    int pid = (int)r->ebx;
    int* status = (int*)r->ecx;
    int options = (int)r->edx;
    
    // Zero Trust: проверяем указатель status
    if (status && !is_user_pointer(status, sizeof(int))) {
        return -EFAULT;
    }
    
    serial_printf("[SYSCALL] sys_waitpid: PID %d waiting for %d\n", current_task->pid, pid);
    return task_waitpid(pid, status, options);
}

// ============================================================================
// [ДЕНЬ 14] sys_getpid: получить PID текущего процесса
// ============================================================================
static int sys_getpid_handler(struct regs* r) {
    (void)r;
    return current_task->pid;
}

// ========================================================================
// ✅ Диспатчер системных вызовов (вызывается из isr_asm.asm)
// ========================================================================
static void syscall_dispatcher(struct regs* r) {
    uint32_t syscall_num = r->eax;

    // Валидация номера syscall
    if (syscall_num >= MAX_SYSCALLS) {
        serial_printf("[SYSCALL] Invalid syscall number: %u (>= %d)\n", syscall_num, MAX_SYSCALLS);
        r->eax = (uint32_t)-ENOSYS; 
        return;
    }
    
    // Проверка наличия обработчика
    if (syscall_table[syscall_num] == NULL) {
        serial_printf("[SYSCALL] Unimplemented syscall: %u\n", syscall_num);
        r->eax = (uint32_t)-ENOSYS;
        return;
    }

    // Вызов обработчика и возврат результата
    int result = syscall_table[syscall_num](r);
    r->eax = (uint32_t)result;
}

// ========================================================================
// ✅ Инициализация: регистрация обработчиков в таблице
// ========================================================================
void syscall_init(void) {
    syscall_table[SYS_EXIT]   = sys_exit_handler;
    syscall_table[SYS_FORK]   = sys_fork_handler;      // [ДЕНЬ 14]
    syscall_table[SYS_READ]   = sys_read_handler;
    syscall_table[SYS_WRITE]  = sys_write_handler;
    syscall_table[SYS_OPEN]   = sys_open_handler;
    syscall_table[SYS_CLOSE]  = sys_close_handler;
    syscall_table[SYS_UNLINK] = sys_unlink_handler;
    syscall_table[SYS_WAITPID] = sys_waitpid_handler;  // [ДЕНЬ 14]
    syscall_table[SYS_YIELD]  = sys_yield_handler;
    syscall_table[SYS_BRK]    = sys_brk_handler;
    syscall_table[SYS_EXEC]   = sys_exec_handler;
    syscall_table[SYS_GETPID] = sys_getpid_handler;    // [ДЕНЬ 14]
    syscall_table[SYS_MMAP]     = sys_mmap_handler;
    syscall_table[SYS_MUNMAP]   = sys_munmap_handler;
    syscall_table[SYS_MPROTECT] = sys_mprotect_handler;
    
    // Регистрация INT 0x80 в IDT
    extern void isr128(); 
    idt_set_gate(128, (uint32_t)isr128, 0x08, 0xEE);  // 0xEE = Interrupt Gate, DPL=3, Present
    
    // Регистрация C-диспатчера
    isr_register_handler(128, syscall_dispatcher);
    
    serial_print("[SYSCALL] INT 0x80 dispatcher initialized with True POSIX exec.\n");
}