//syscall.c

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
#include "timer.h"
#include "framebuffer.h" // ✅ Для fb_is_available()
#include "heap.h"

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
    [SYS_LSEEK]  = NULL,
    [SYS_FSTAT]  = NULL,
    [SYS_IOCTL]  = NULL,
    [SYS_GETTIMEOFDAY] = NULL,
    [SYS_UNAME]  = NULL,
    [SYS_SYSINFO] = NULL,
    [SYS_SLEEP]  = NULL,
    [SYS_READDIR] = NULL, // Заполним в syscall_init
};

// ========================================================================
// ✅ Zero Trust: безопасная проверка указателя из Ring 3
// ========================================================================
static inline bool is_user_pointer(const void* ptr, size_t size) {
    if (!ptr) return false;
    uint32_t addr = (uint32_t)ptr;
    
    if (addr >= KERNEL_SPACE_START) return false;
    
    if (size == 0) return true;
    if (addr > USER_SPACE_END - size + 1) return false;
    
    return true;
}

// ========================================================================
// ✅ Zero Trust: безопасное копирование строки из Ring 3 → Ring 0
// ========================================================================
static int copy_string_from_user(char* dest, const char* user_src, size_t max_len) {
    if (!dest || !user_src) return -EFAULT;
    if ((uint32_t)user_src >= KERNEL_SPACE_START) return -EFAULT;
    
    size_t copied = 0;
    const char* src = user_src;
    
    while (copied < max_len - 1) {
        if ((uint32_t)src >= KERNEL_SPACE_START) return -EFAULT;
        
        char byte = *src;
        *dest = byte;
        dest++;
        src++;
        copied++;
        
        if (byte == '\0') return 0;
    }
    
    dest[0] = '\0';
    return -ENAMETOOLONG;
}


static int sys_exec_handler(struct regs* r) {
    const char* user_filename = (const char*)r->ebx;
    const char** user_argv = (const char**)r->ecx;
    
    char filename_buf[256];
    int ret = copy_string_from_user(filename_buf, user_filename, sizeof(filename_buf));
    if (ret < 0) return ret;
    
    #define EXEC_MAX_ARGS 64
    #define EXEC_MAX_ARG_LEN 256
    
    // 🛡️ [ДЕНЬ 16] FIX: Выделяем argv буфер в Kernel Heap, а не на стеке ядра!
    char (*k_argv_buf)[EXEC_MAX_ARG_LEN] = kmalloc(EXEC_MAX_ARGS * EXEC_MAX_ARG_LEN);
    if (!k_argv_buf) return -ENOMEM;
    
    int argc = 0;
    if (user_argv && is_user_pointer(user_argv, sizeof(char*))) {
        for (int i = 0; i < EXEC_MAX_ARGS; i++) {
            if (!is_user_pointer(user_argv + i, sizeof(char*))) break;
            const char* u_arg = user_argv[i];
            if (u_arg == NULL) break;
            
            if (copy_string_from_user(k_argv_buf[argc], u_arg, EXEC_MAX_ARG_LEN) < 0) {
                k_argv_buf[argc][0] = '\0';
            }
            argc++;
        }
    }
    
    // 🛡️ Bounds Check: User Stack Underflow Protection
    uint32_t total_args_size = 0;
    for (int i = 0; i < argc; i++) {
        total_args_size += k_strlen(k_argv_buf[i]) + 1;
    }
    total_args_size += (argc + 1) * sizeof(char*);
    total_args_size += sizeof(int);
    total_args_size += 64; // envp + padding
    
    if (total_args_size > USER_STACK_SIZE - USER_STACK_GUARD_SIZE) {
        kfree(k_argv_buf);
        serial_printf("[SYSCALL] sys_exec: E2BIG - argv too large (%u bytes)\n", total_args_size);
        return -E2BIG;
    }
    
    serial_printf("[SYSCALL] sys_exec: PID %d loading '%s' (argc=%d)\n", 
                  current_task->pid, filename_buf, argc);
    
    vfs_node_t* file_node = vfs_findnode(filename_buf);
    if (!file_node) {
        kfree(k_argv_buf);
        return -ENOENT;
    }
    
    if (!(file_node->flags & FS_FILE)) {
        kfree(k_argv_buf);
        return -EACCES;
    }
    
    uint32_t* new_pdir_virt = vmm_create_address_space();
    if (!new_pdir_virt) {
        kfree(k_argv_buf);
        return -ENOMEM;
    }
    
    task_t temp_task;
    k_memset(&temp_task, 0, sizeof(task_t));
    temp_task.pdir_virt = new_pdir_virt;
    temp_task.vma_head = NULL;
    
    uint32_t entry_point = elf_load(file_node, &temp_task);
    if (entry_point == 0) {
        kfree(k_argv_buf);
        vma_destroy_all(&temp_task);
        vmm_destroy_address_space(new_pdir_virt);
        return -ENOEXEC;
    }
    
    uint32_t saved_pid = current_task->pid;
    
    struct open_file* saved_fds[TASK_MAX_OPEN_FILES];
    for (int i = 0; i < TASK_MAX_OPEN_FILES; i++) {
        saved_fds[i] = current_task->fd_table[i];
    }
    
    if (current_task->vma_head) vma_destroy_all(current_task);
    if (current_task->pdir_virt && current_task->pdir_virt != new_pdir_virt) {
        vmm_destroy_address_space(current_task->pdir_virt);
    }
    
    current_task->pdir_virt = new_pdir_virt;
    current_task->cr3 = VIRT_TO_PHYS(new_pdir_virt);
    vmm_switch_pdir(current_task->cr3);
    current_task->vma_head = temp_task.vma_head;
    
    for (int i = 0; i < TASK_MAX_OPEN_FILES; i++) current_task->fd_table[i] = saved_fds[i];
    current_task->pid = saved_pid;
    
    // ✅ FIX: POSIX exec заменяет образ процесса, включая имя. 
    // Мы используем новое имя файла, чтобы в логах (и в Reaper) отображался актуальный процесс.
    k_strncpy(current_task->name, filename_buf, sizeof(current_task->name));
    
    fpu_release_ownership(current_task);
    current_task->fpu_initialized = 0;
    current_task->sleep_until = 0;
    
    uint32_t stack_top = USER_STACK_VIRT_TOP;
    uint32_t stack_bottom = stack_top - USER_STACK_SIZE;
    
    // 🛡️ CRITICAL: Защищаем создание VMA от прерываний PIT
    uint32_t eflags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(eflags));
    
    if (vma_add(current_task, stack_bottom, stack_top, VMA_READ | VMA_WRITE) < 0 ||
        vma_add(current_task, USER_HEAP_START, USER_HEAP_START, VMA_READ | VMA_WRITE) < 0) {
        kfree(k_argv_buf);
        __asm__ volatile("push %0; popf" : : "r"(eflags));
        return -ENOMEM; 
    }
    
    __asm__ volatile("push %0; popf" : : "r"(eflags));
    
    uint32_t* stack_ptr = (uint32_t*)stack_top;
    char* argv_ptrs[EXEC_MAX_ARGS + 1];
    
    for (int i = argc - 1; i >= 0; i--) {
        size_t len = k_strlen(k_argv_buf[i]) + 1;
        stack_ptr = (uint32_t*)((uint8_t*)stack_ptr - len);
        k_memcpy(stack_ptr, k_argv_buf[i], len);
        argv_ptrs[i] = (char*)stack_ptr;
    }
    argv_ptrs[argc] = NULL;
    
    if ((uint32_t)stack_ptr >= KERNEL_SPACE_START) {
        kfree(k_argv_buf);
        stack_ptr = (uint32_t*)(KERNEL_SPACE_START - 16);
    }
    
    stack_ptr = (uint32_t*)((uint32_t)stack_ptr & ~0x3);
    *(--stack_ptr) = 0;
    stack_ptr -= (argc + 1);
    for (int i = 0; i <= argc; i++) stack_ptr[i] = (uint32_t)argv_ptrs[i];
    *(--stack_ptr) = (uint32_t)argc;
    
    if ((uint32_t)stack_ptr >= KERNEL_SPACE_START) {
        kfree(k_argv_buf);
        stack_ptr = (uint32_t*)(KERNEL_SPACE_START - 16);
    }
// ========================================================================
    // 🛡️ CRITICAL FIX: Stack Switch для IRET
    // ========================================================================
    r->esp = (uint32_t)stack_ptr;      // Фиктивный ESP (для pusha/popa, игнорируется)
    
    // КРИТИЧНО: Обновляем аппаратный User ESP, который IRET использует 
    // для переключения стека при возврате в Ring 3!
    // (Проверь точное имя поля в idt.h: useresp или user_esp)
    r->useresp = (uint32_t)stack_ptr;  
    
    r->eip = entry_point;
    r->eax = 0;
    
    kfree(k_argv_buf);
    
    serial_printf("[SYSCALL] sys_exec: PID %d replaced with '%s' at 0x%x, ESP=0x%x\n", 
                  current_task->pid, current_task->name, entry_point, r->useresp);
    return 0;
}

// ========================================================================
// sys_exit: завершение текущего процесса
// ========================================================================
static int sys_exit_handler(struct regs* r) {
    (void)r;
    uint32_t exit_code = r->ebx;
    serial_printf("[SYSCALL] PID %d exiting with code %u\n", current_task->pid, exit_code);
    task_exit(exit_code); // ✅ FIX: Передаем код выхода для waitpid
    return 0;
}

static int sys_write_handler(struct regs* r) {
    int fd = (int)r->ebx;
    const void* buf = (const void*)r->ecx;
    uint32_t count = r->edx;

    if (!is_user_pointer(buf, count)) {
        serial_printf("[SYSCALL] FATAL: Ring 3 attempted kernel write! Addr: 0x%x\n", (uint32_t)buf);
        return -EFAULT;
    }

    // Делегируем запись полиморфному VFS callback'у (DevFS, tmpfs, FAT32)
    return sys_write(fd, buf, count);
}
static int sys_read_handler(struct regs* r) {
    int fd = (int)r->ebx;
    void* buf = (void*)r->ecx;
    uint32_t count = r->edx;

    if (!is_user_pointer(buf, count)) {
        serial_printf("[SYSCALL] FATAL: Ring 3 attempted kernel read! Addr: 0x%x\n", (uint32_t)buf);
        return -EFAULT;
    }

    // Делегируем чтение полиморфному VFS callback'у (DevFS, tmpfs, FAT32)
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
// sys_brk: управление кучей процесса
// ========================================================================
static int sys_brk_handler(struct regs* r) {
    uint32_t new_brk = r->ebx;
    
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
    
    // 🛡️ FIX: Enforce USER_HEAP_MAX_SIZE (64MB limit per process)
    if (new_brk > USER_HEAP_START + USER_HEAP_MAX_SIZE) {
        serial_printf("[SYSCALL] sys_brk: Exceeded USER_HEAP_MAX_SIZE (64MB limit)\n");
        return -ENOMEM;
    }
    
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
            if (vma_intersects(current_task, heap_vma->end, new_brk, heap_vma)) {
                serial_printf("[SYSCALL] sys_brk: Heap would overlap other VMA\n");
                return -ENOMEM;
            }
            
            uint32_t pages_needed = (new_brk - heap_vma->end + 0xFFF) / 0x1000;
            uint32_t free_pages = pmm_get_free_pages();
            
            if (pages_needed > free_pages / 2) {
                serial_printf("[SYSCALL] sys_brk: OOM protection triggered\n");
                return -ENOMEM;
            }
            
            heap_vma->end = new_brk;
            //serial_printf("[SYSCALL] sys_brk: Extended heap to 0x%x\n", new_brk);
        } else if (new_brk < heap_vma->end) {
            heap_vma->end = new_brk;
        }
    }
    
    return 0;
}

// ========================================================================
// sys_unlink: удаление файла
// ========================================================================
static int sys_unlink_handler(struct regs* r) {
    const char* user_path = (const char*)r->ebx;
    char path_buf[256];
    
    int ret = copy_string_from_user(path_buf, user_path, sizeof(path_buf));
    if (ret < 0) {
        serial_printf("[SYSCALL] sys_unlink: Invalid path pointer or too long\n");
        return ret;
    }
    
    return sys_unlink(path_buf);
}

static int sys_open_handler(struct regs* r) {
    const char* user_path = (const char*)r->ebx;
    uint32_t flags = (uint32_t)r->ecx;
    uint32_t mode = (uint32_t)r->edx; // 🛡️ Читаем mode из 3-го аргумента (EDX)
    char path_buf[256];
    
    int ret = copy_string_from_user(path_buf, user_path, sizeof(path_buf));
    if (ret < 0) {
        serial_printf("[SYSCALL] sys_open: Invalid path pointer or too long\n");
        return ret;
    }
    
    // 🛡️ POSIX COMPLIANCE & SECURITY HARDENING:
    // User-space передает только права (0644). Ядро обязано добавить тип файла (S_IFREG),
    // иначе tmpfs_create отвергнет запрос. Маска 07777 защищает от инъекции S_IFCHR/S_IFDIR.
    if (flags & O_CREAT) {
        mode = (mode & 07777) | S_IFREG; 
    }
    
    return sys_open(path_buf, flags, mode);
}

// ========================================================================
// sys_close: закрытие файлового дескриптора
// ========================================================================
static int sys_close_handler(struct regs* r) {
    int fd = (int)r->ebx;
    return vfs_close_fd(current_task, fd);
}

// ========================================================================
// sys_lseek: перемещение позиции чтения/записи в файле
// ========================================================================
static int sys_lseek_handler(struct regs* r) {
    int fd = (int)r->ebx;
    int32_t offset = (int32_t)r->ecx;
    int whence = (int)r->edx;
    
    if (fd < 0 || fd >= TASK_MAX_OPEN_FILES) {
        serial_printf("[SYSCALL] sys_lseek: Invalid fd %d\n", fd);
        return -EBADF;
    }
    
    struct open_file* file = current_task->fd_table[fd];
    if (!file) {
        serial_printf("[SYSCALL] sys_lseek: fd %d not open in PID %d\n", fd, current_task->pid);
        return -EBADF;
    }
    
    if (!file->node) {
        serial_printf("[SYSCALL] sys_lseek: fd %d has NULL vfs_node\n", fd);
        return -EBADF;
    }
    
    int32_t new_offset;
    uint32_t file_size = file->node->size;
    
    switch (whence) {
        case SEEK_SET:
            new_offset = offset;
            break;
        case SEEK_CUR:
            new_offset = (int32_t)file->offset + offset;
            break;
        case SEEK_END:
            new_offset = (int32_t)file_size + offset;
            break;
        default:
            serial_printf("[SYSCALL] sys_lseek: Invalid whence %d\n", whence);
            return -EINVAL;
    }
    
    if (new_offset < 0) {
        serial_printf("[SYSCALL] sys_lseek: Negative offset %d (fd %d)\n", new_offset, fd);
        return -EINVAL;
    }
    
    file->offset = (uint32_t)new_offset;
    
    serial_printf("[SYSCALL] sys_lseek: fd %d -> offset %u (PID %d)\n", 
                  fd, file->offset, current_task->pid);
    
    return new_offset;
}

// ========================================================================
// sys_fstat: получение метаданных файла
// ========================================================================
static int sys_fstat_handler(struct regs* r) {
    int fd = (int)r->ebx;
    stat_t* user_stat = (stat_t*)r->ecx;
    
    if (fd < 0 || fd >= TASK_MAX_OPEN_FILES) {
        serial_printf("[SYSCALL] sys_fstat: Invalid fd %d\n", fd);
        return -EBADF;
    }
    
    struct open_file* file = current_task->fd_table[fd];
    if (!file) {
        serial_printf("[SYSCALL] sys_fstat: fd %d not open in PID %d\n", fd, current_task->pid);
        return -EBADF;
    }
    
    if (!file->node) {
        serial_printf("[SYSCALL] sys_fstat: fd %d has NULL vfs_node\n", fd);
        return -EBADF;
    }
    
    if (!is_user_pointer(user_stat, sizeof(stat_t))) {
        serial_printf("[SYSCALL] sys_fstat: Invalid stat pointer 0x%x (PID %d)\n", 
                      (uint32_t)user_stat, current_task->pid);
        return -EFAULT;
    }
    
    stat_t kernel_stat;
    k_memset(&kernel_stat, 0, sizeof(stat_t));
    
    kernel_stat.st_dev = 0;
    kernel_stat.st_ino = 0;
    kernel_stat.st_nlink = 1;
    kernel_stat.st_uid = 0;
    kernel_stat.st_gid = 0;
    kernel_stat.st_rdev = 0;
    kernel_stat.st_size = file->node->size;
    kernel_stat.st_blksize = 4096;
    kernel_stat.st_blocks = (file->node->size + 511) / 512;
    kernel_stat.st_atime = 0;
    kernel_stat.st_mtime = 0;
    kernel_stat.st_ctime = 0;
    
    if (file->node->flags & FS_DIRECTORY) {
        kernel_stat.st_mode = S_IFDIR | 0755;
    } else if (file->node->flags & FS_FILE) {
        kernel_stat.st_mode = S_IFREG | 0644;
    } else if (file->node->flags & FS_MOUNTPOINT) {
        kernel_stat.st_mode = S_IFDIR | 0755;
    } else {
        kernel_stat.st_mode = S_IFREG | 0644;
    }
    
    k_memcpy(user_stat, &kernel_stat, sizeof(stat_t));
    
    serial_printf("[SYSCALL] sys_fstat: fd %d, size=%u, mode=0%o (PID %d)\n", 
                  fd, kernel_stat.st_size, kernel_stat.st_mode, current_task->pid);
    
    return 0;
}

// ========================================================================
// sys_ioctl: управление устройствами
// ========================================================================
static int sys_ioctl_handler(struct regs* r) {
    int fd = (int)r->ebx;
    uint32_t request = r->ecx;
    void* argp = (void*)r->edx;
    
    if (fd < 0 || fd >= TASK_MAX_OPEN_FILES) {
        serial_printf("[SYSCALL] sys_ioctl: Invalid fd %d\n", fd);
        return -EBADF;
    }
    
    struct open_file* file = current_task->fd_table[fd];
    if (!file) {
        serial_printf("[SYSCALL] sys_ioctl: fd %d not open in PID %d\n", fd, current_task->pid);
        return -EBADF;
    }
    
    switch (request) {
        case TIOCGWINSZ: {
            if (fd != 1 && fd != 2) {
                return -ENOTTY;
            }
            
            if (!is_user_pointer(argp, sizeof(winsize_t))) {
                serial_printf("[SYSCALL] sys_ioctl: Invalid winsize pointer 0x%x\n", 
                              (uint32_t)argp);
                return -EFAULT;
            }
            
            winsize_t ws;
            if (fb_is_available()) {
                ws.ws_row = 48;
                ws.ws_col = 128;
                ws.ws_xpixel = 1024;
                ws.ws_ypixel = 768;
            } else {
                ws.ws_row = 50;
                ws.ws_col = 80;
                ws.ws_xpixel = 640;
                ws.ws_ypixel = 400;
            }
            
            k_memcpy(argp, &ws, sizeof(winsize_t));
            
            serial_printf("[SYSCALL] sys_ioctl: TIOCGWINSZ -> %ux%u (PID %d)\n", 
                          ws.ws_col, ws.ws_row, current_task->pid);
            return 0;
        }
        
        default:
            serial_printf("[SYSCALL] sys_ioctl: Unsupported request 0x%x on fd %d (PID %d)\n", 
                          request, fd, current_task->pid);
            return -ENOTTY;
    }
}
// ========================================================================
// sys_readdir: чтение записи директории
// ========================================================================
static int sys_readdir_handler(struct regs* r) {
    int fd = (int)r->ebx;
    uint32_t index = r->ecx;
    dirent_t* user_entry = (dirent_t*)r->edx;
    
    if (!is_user_pointer(user_entry, sizeof(dirent_t))) {
        serial_printf("[SYSCALL] sys_readdir: Invalid entry pointer 0x%x\n", (uint32_t)user_entry);
        return -EFAULT;
    }
    
    if (fd < 0 || fd >= TASK_MAX_OPEN_FILES) return -EBADF;
    
    struct open_file* of = current_task->fd_table[fd];
    if (!of || !of->node) return -EBADF;
    if (!(of->node->flags & FS_DIRECTORY)) return -EINVAL; // Not a directory
    if (!of->node->readdir) return -ENOSYS;
    
    // 🛡️ Kernel-Buffer Pattern: заполняем в kernel space, затем копируем
    dirent_t kernel_entry;
    k_memset(&kernel_entry, 0, sizeof(dirent_t));
    
    int32_t res = of->node->readdir(of->node, index, &kernel_entry);
    if (res != 0) return res; // -1 означает конец директории
    
    k_memcpy(user_entry, &kernel_entry, sizeof(dirent_t));
    return 0;
}
// ========================================================================
// sys_mmap: выделение виртуальной памяти
// ========================================================================
static int sys_mmap_handler(struct regs* r) {
    uint32_t addr = r->ebx;
    uint32_t len = r->ecx;
    uint32_t prot = r->edx;
    uint32_t flags = r->esi;

    if (len == 0 || len > USER_MMAP_MAX_SIZE) return -EINVAL;

    if ((prot & PROT_WRITE) && (prot & PROT_EXEC)) {
        serial_printf("[SYSCALL] sys_mmap: W^X violation (Write + Exec)\n");
        return -EPERM;
    }

    if (!(flags & MAP_ANONYMOUS) || !(flags & MAP_PRIVATE)) {
        return -ENOSYS;
    }
    
    uint32_t aligned_len = (len + 0xFFF) & ~0xFFF;
    uint32_t map_addr = 0;

    if (addr == 0) {
        map_addr = vma_find_free_area(current_task, aligned_len);
        if (map_addr == 0) return -ENOMEM;
    } else {
        map_addr = addr & ~0xFFF;
        if (vma_intersects(current_task, map_addr, map_addr + aligned_len, NULL)) {
            return -EINVAL;
        }
    }

    uint32_t vma_flags = 0;
    if (prot & PROT_READ)  vma_flags |= VMA_READ;
    if (prot & PROT_WRITE) vma_flags |= VMA_WRITE;
    if (prot & PROT_EXEC)  vma_flags |= VMA_EXEC;

    int ret = vma_add(current_task, map_addr, map_addr + aligned_len, vma_flags);
    if (ret < 0) return -ENOMEM;

    serial_printf("[SYSCALL] sys_mmap: Allocated VMA 0x%x - 0x%x\n", map_addr, map_addr + aligned_len);
    return map_addr;
}

// ========================================================================
// sys_munmap: освобождение памяти
// ========================================================================
static int sys_munmap_handler(struct regs* r) {
    uint32_t addr = r->ebx;
    uint32_t len = r->ecx;

    if (len == 0) return -EINVAL;
    
    uint32_t aligned_addr = addr & ~0xFFF;
    uint32_t aligned_len = (len + 0xFFF) & ~0xFFF;
    uint32_t end = aligned_addr + aligned_len;

    int ret = vma_unmap_range(current_task, aligned_addr, end);
    if (ret < 0) return ret;

    for (uint32_t p = aligned_addr; p < end; p += 0x1000) {
        vmm_unmap_and_free_page_in_pd(current_task->pdir_virt, p);
    }

    return 0;
}

// ========================================================================
// sys_mprotect: изменение прав доступа
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
        
        uint32_t vma_flags = 0;
        if (prot & PROT_READ)  vma_flags |= VMA_READ;
        if (prot & PROT_WRITE) vma_flags |= VMA_WRITE;
        if (prot & PROT_EXEC)  vma_flags |= VMA_EXEC;
        curr->flags = vma_flags;
        
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
//  sys_fork: создание копии процесса (Copy-on-Write)
// ============================================================================
static int sys_fork_handler(struct regs* r) {
    serial_printf("[SYSCALL] sys_fork: PID %d\n", current_task->pid);
    return task_fork(r);
}

// ============================================================================
// sys_waitpid: ожидание завершения ребенка
// ============================================================================
static int sys_waitpid_handler(struct regs* r) {
    int pid = (int)r->ebx;
    int* status = (int*)r->ecx;
    int options = (int)r->edx;
    
    if (status && !is_user_pointer(status, sizeof(int))) {
        return -EFAULT;
    }
    
    serial_printf("[SYSCALL] sys_waitpid: PID %d waiting for %d\n", current_task->pid, pid);
    return task_waitpid(pid, status, options);
}

// ============================================================================
//  sys_getpid: получить PID текущего процесса
// ============================================================================
static int sys_getpid_handler(struct regs* r) {
    (void)r;
    return current_task->pid;
}

// ============================================================================
//  sys_gettimeofday: получение времени с момента загрузки
// ============================================================================
static int sys_gettimeofday_handler(struct regs* r) {
    timeval_t* tv = (timeval_t*)r->ebx;
    if (!is_user_pointer(tv, sizeof(timeval_t))) return -EFAULT;
    
    uint32_t ticks = timer_get_ticks();
    uint32_t freq = timer_get_frequency();
    if (freq == 0) freq = 1000;
    
    timeval_t kernel_tv;
    kernel_tv.tv_sec = ticks / freq;
    kernel_tv.tv_usec = ((ticks % freq) * 1000000) / freq;
    
    k_memcpy(tv, &kernel_tv, sizeof(timeval_t));
    return 0;
}

// ============================================================================
//  sys_sleep: усыпление процесса (в миллисекундах)
// ============================================================================
static int sys_sleep_handler(struct regs* r) {
    uint32_t ms = r->ebx; 
    if (ms == 0) {
        schedule();
        return 0;
    }
    
    current_task->sleep_until = timer_get_ticks() + ms;
    current_task->state = TASK_SLEEPING;
    
    schedule();
    return 0;
}

// ============================================================================
//  sys_uname: информация об ОС
// ============================================================================
static int sys_uname_handler(struct regs* r) {
    utsname_t* user_buf = (utsname_t*)r->ebx;
    if (!is_user_pointer(user_buf, sizeof(utsname_t))) return -EFAULT;
    
    utsname_t kernel_buf;
    k_memset(&kernel_buf, 0, sizeof(utsname_t));
    
    k_strncpy(kernel_buf.sysname, "Enclave OS", UTSNAME_LENGTH - 1);
    k_strncpy(kernel_buf.nodename, "localhost", UTSNAME_LENGTH - 1);
    k_strncpy(kernel_buf.release, "0.3-alpha", UTSNAME_LENGTH - 1);
    k_strncpy(kernel_buf.version, "Day 30 Build", UTSNAME_LENGTH - 1);
    k_strncpy(kernel_buf.machine, "i686", UTSNAME_LENGTH - 1);
    
    k_memcpy(user_buf, &kernel_buf, sizeof(utsname_t));
    return 0;
}

// ============================================================================
//  sys_sysinfo: статистика системы
// ============================================================================
static int sys_sysinfo_handler(struct regs* r) {
    sysinfo_t* user_buf = (sysinfo_t*)r->ebx;
    if (!is_user_pointer(user_buf, sizeof(sysinfo_t))) return -EFAULT;
    
    uint32_t ticks = timer_get_ticks();
    uint32_t freq = timer_get_frequency();
    if (freq == 0) freq = 1000;
    
    sysinfo_t kernel_buf;
    k_memset(&kernel_buf, 0, sizeof(sysinfo_t));
    
    kernel_buf.uptime = ticks / freq;
    kernel_buf.totalram = pmm_get_total_pages() * PMM_PAGE_SIZE;
    kernel_buf.freeram = pmm_get_free_pages() * PMM_PAGE_SIZE;
    kernel_buf.procs = (uint16_t)task_get_count();
    kernel_buf.mem_unit = 1;
    
    k_memcpy(user_buf, &kernel_buf, sizeof(sysinfo_t));
    return 0;
}

// ============================================================================
//  sys_dup: дублирование файлового дескриптора
// ============================================================================
static int sys_dup_handler(struct regs* r) {
    int old_fd = (int)r->ebx;
    
    if (old_fd < 0 || old_fd >= TASK_MAX_OPEN_FILES) return -EBADF;
    
    open_file_t* of = current_task->fd_table[old_fd];
    if (!of) return -EBADF;
    
    for (int i = 0; i < TASK_MAX_OPEN_FILES; i++) {
        if (current_task->fd_table[i] == 0) {
            irq_flags_t flags = irq_save();
            of->ref_count++;
            if (of->node) of->node->ref_count++;
            current_task->fd_table[i] = of;
            irq_restore(flags);
            return i;
        }
    }
    
    return -EMFILE;
}

// ============================================================================
//  sys_dup2: атомарное дублирование в конкретный FD
// ============================================================================
static int sys_dup2_handler(struct regs* r) {
    int old_fd = (int)r->ebx;
    int new_fd = (int)r->ecx;
    
    if (old_fd < 0 || old_fd >= TASK_MAX_OPEN_FILES) return -EBADF;
    if (new_fd < 0 || new_fd >= TASK_MAX_OPEN_FILES) return -EBADF;
    
    open_file_t* of = current_task->fd_table[old_fd];
    if (!of) return -EBADF;
    
    if (old_fd == new_fd) return new_fd;
    
    // Закрываем new_fd, если он уже открыт
    if (current_task->fd_table[new_fd] != 0) {
    vfs_close_fd(current_task, new_fd);
    }
    
    irq_flags_t flags = irq_save();
    of->ref_count++;
    if (of->node) of->node->ref_count++;
    current_task->fd_table[new_fd] = of;
    irq_restore(flags);
    
    return new_fd;
}

/* ============================================================================
 * sys_mkdir: создание директории (Day 31)
 * ebx = user_path, ecx = mode
 * ========================================================================== */
static int sys_mkdir_handler(struct regs* r) {
    const char* user_path = (const char*)r->ebx;
    uint32_t mode         = (uint32_t)r->ecx;
    char path_buf[256];

    int ret = copy_string_from_user(path_buf, user_path, sizeof(path_buf));
    if (ret < 0) return ret;

    int len = k_strlen(path_buf);
    if (len == 0 || len >= 256) return -EINVAL;

    /* Находим последний '/' → разделяем parent / name */
    int last_slash = -1;
    for (int i = len - 1; i >= 0; i--) {
        if (path_buf[i] == '/') { last_slash = i; break; }
    }
    if (last_slash < 0) return -EINVAL;

    const char* name = path_buf + last_slash + 1;
    if (name[0] == '\0') return -EINVAL;

    /* Защита: name не должен содержать '/' */
    for (int i = 0; name[i]; i++) {
        if (name[i] == '/') return -EINVAL;
    }

    char parent_buf[256];
    if (last_slash == 0) {
        parent_buf[0] = '/';
        parent_buf[1] = '\0';
    } else {
        k_memcpy(parent_buf, path_buf, last_slash);
        parent_buf[last_slash] = '\0';
    }

    vfs_node_t* parent = vfs_findnode(parent_buf);
    if (!parent) return -ENOENT;

    if (!(parent->flags & FS_DIRECTORY)) return -ENOTDIR;

    /* 🛡️ RBAC: Ring 3 не может создавать в FS_SYSTEM (/boot) */
    if (parent->flags & FS_SYSTEM) return -EACCES;

    if (!parent->create) return -EPERM;

    /* EEXIST: имя не должно быть занято */
    if (parent->finddir && parent->finddir(parent, name)) return -EEXIST;

    vfs_node_t* new_dir = parent->create(parent, name, (mode & 07777) | S_IFDIR);
    if (!new_dir) return -ENOMEM;

    serial_printf("[SYSCALL] sys_mkdir: '%s/%s' (PID %d)\n",
                  parent_buf, name, current_task->pid);
    return 0;
}
// ========================================================================
// ✅ Диспатчер системных вызовов
// ========================================================================
static void syscall_dispatcher(struct regs* r) {
    uint32_t syscall_num = r->eax;

    if (syscall_num >= MAX_SYSCALLS) {
        serial_printf("[SYSCALL] Invalid syscall number: %u (>= %d)\n", syscall_num, MAX_SYSCALLS);
        r->eax = (uint32_t)-ENOSYS; 
        return;
    }
    
    if (syscall_table[syscall_num] == NULL) {
        serial_printf("[SYSCALL] Unimplemented syscall: %u\n", syscall_num);
        r->eax = (uint32_t)-ENOSYS;
        return;
    }

    int result = syscall_table[syscall_num](r);
    r->eax = (uint32_t)result;
}

// ========================================================================
// ✅ Инициализация: регистрация обработчиков в таблице
// ========================================================================
void syscall_init(void) {
    syscall_table[SYS_EXIT]   = sys_exit_handler;
    syscall_table[SYS_FORK]   = sys_fork_handler;
    syscall_table[SYS_READ]   = sys_read_handler;
    syscall_table[SYS_WRITE]  = sys_write_handler;
    syscall_table[SYS_OPEN]   = sys_open_handler;
    syscall_table[SYS_CLOSE]  = sys_close_handler;
    syscall_table[SYS_UNLINK] = sys_unlink_handler;
    syscall_table[SYS_WAITPID] = sys_waitpid_handler;
    syscall_table[SYS_YIELD]  = sys_yield_handler;
    syscall_table[SYS_BRK]    = sys_brk_handler;
    syscall_table[SYS_EXEC]   = sys_exec_handler;
    syscall_table[SYS_GETPID] = sys_getpid_handler;
    syscall_table[SYS_MMAP]     = sys_mmap_handler;
    syscall_table[SYS_MUNMAP]   = sys_munmap_handler;
    syscall_table[SYS_MPROTECT] = sys_mprotect_handler;
    syscall_table[SYS_LSEEK]  = sys_lseek_handler;
    syscall_table[SYS_FSTAT]  = sys_fstat_handler;
    syscall_table[SYS_IOCTL]  = sys_ioctl_handler;
    syscall_table[SYS_GETTIMEOFDAY] = sys_gettimeofday_handler;
    syscall_table[SYS_UNAME]  = sys_uname_handler;
    syscall_table[SYS_SYSINFO] = sys_sysinfo_handler;
    syscall_table[SYS_SLEEP]  = sys_sleep_handler;
    syscall_table[SYS_READDIR] = sys_readdir_handler;
    syscall_table[SYS_DUP]    = sys_dup_handler;
    syscall_table[SYS_DUP2]   = sys_dup2_handler;
    syscall_table[SYS_MKDIR]  = sys_mkdir_handler;
    
    extern void isr128(); 
    idt_set_gate(128, (uint32_t)isr128, 0x08, 0xEE);
    
    isr_register_handler(128, syscall_dispatcher);
    
    serial_print("[SYSCALL] INT 0x80 dispatcher initialized with True POSIX exec.\n");
}
