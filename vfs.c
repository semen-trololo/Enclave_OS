#include "vfs.h"
#include "heap.h"
#include "klib.h"
#include "serial.h"
#include "task.h" // 🆕 КРИТИЧНО: Нужно для current_task, task_t и TASK_MAX_OPEN_FILES

// Глобальный корень файловой системы ("/")
static vfs_node_t* vfs_root = 0;

// ==========================================
// 1. УНИВЕРСАЛЬНЫЕ ФУНКЦИИ ОБХОДА ДЕРЕВА (LCRS)
// (Должны быть объявлены ДО vfs_init и vfs_create_node)
// ==========================================

static int32_t vfs_generic_readdir(vfs_node_t* node, uint32_t index, dirent_t* entry) {
    if (!node || !entry) return -1;
    vfs_node_t* child = node->first_child;
    uint32_t i = 0;
    while (child && i < index) { child = child->next_sibling; i++; }
    if (!child) return -1; // Конец директории
    
    entry->ino = 0; 
    int j = 0; while(child->name[j] && j < VFS_MAX_FILENAME - 1) { entry->name[j] = child->name[j]; j++; }
    entry->name[j] = '\0';
    return 0;
}

static vfs_node_t* vfs_generic_finddir(vfs_node_t* node, const char* name) {
    if (!node || !name) return 0;
    vfs_node_t* child = node->first_child;
    while (child) {
        int i = 0;
        while (child->name[i] && name[i] && child->name[i] == name[i]) i++;
        if (child->name[i] == '\0' && name[i] == '\0') return child;
        child = child->next_sibling;
    }
    return 0;
}

// ==========================================
// 2. ИНИЦИАЛИЗАЦИЯ И СОЗДАНИЕ УЗЛОВ
// ==========================================

void vfs_init(void) {
    serial_print("[VFS] Initializing Virtual File System...\n");
    
    vfs_root = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    if (!vfs_root) {
        serial_print("[VFS] FATAL: OOM allocating vfs_root!\n");
        return;
    }
    
    k_memset(vfs_root, 0, sizeof(vfs_node_t));
    vfs_root->name[0] = '/';
    vfs_root->name[1] = '\0';
    //vfs_root->flags = FS_DIRECTORY | FS_SYSTEM;
    vfs_root->permissions = PERM_READ_ONLY;
    vfs_root->readdir = vfs_generic_readdir;
    vfs_root->finddir = vfs_generic_finddir;
    
    serial_print("[VFS] Root node '/' created.\n");
}

vfs_node_t* vfs_create_node(const char* name, uint32_t flags, vfs_node_t* parent, void* private_data) {
    vfs_node_t* node = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    if (!node) return 0;
    
    k_memset(node, 0, sizeof(vfs_node_t));
    
    int i = 0;
    while (name[i] && i < VFS_MAX_FILENAME - 1) {
        node->name[i] = name[i];
        i++;
    }
    node->name[i] = '\0';
    
    node->flags = flags;
    node->private_data = private_data;

    if (flags & FS_DIRECTORY) {
        node->readdir = vfs_generic_readdir;
        node->finddir = vfs_generic_finddir;
    }
    
    if (parent) {
        vfs_add_child(parent, node);
    }
    
    return node;
}

void vfs_add_child(vfs_node_t* parent, vfs_node_t* child) {
    if (!parent || !child) return;
    
    child->parent = parent;
    child->next_sibling = 0;
    
    if (!parent->first_child) {
        parent->first_child = child;
    } else {
        vfs_node_t* current = parent->first_child;
        while (current->next_sibling) {
            current = current->next_sibling;
        }
        current->next_sibling = child;
    }
}

// ==========================================
// 3. МАРШРУТИЗАЦИЯ И МОНТИРОВАНИЕ
// ==========================================

int vfs_mount(const char* mountpoint_path, vfs_node_t* target_node) {
    vfs_node_t* mountpoint = vfs_findnode(mountpoint_path);
    if (!mountpoint || !target_node) return -1;
    
    if (!(mountpoint->flags & FS_DIRECTORY)) return -1;
    
    mountpoint->flags |= FS_MOUNTPOINT;
    mountpoint->mountpoint_node = target_node;
    
    serial_print("[VFS] Mounted filesystem at: ");
    serial_print(mountpoint_path);
    serial_print("\n");
    
    return 0;
}

vfs_node_t* vfs_findnode(const char* path) {
    if (!path || path[0] != '/') return 0;
    if (path[1] == '\0') return vfs_root;
    
    vfs_node_t* current = vfs_root;
    char token[VFS_MAX_FILENAME];
    int i = 1;
    
    while (path[i] != '\0') {
        while (path[i] == '/') i++;
        if (path[i] == '\0') break;
        
        int t_idx = 0;
        while (path[i] != '/' && path[i] != '\0' && t_idx < VFS_MAX_FILENAME - 1) {
            token[t_idx++] = path[i++];
        }
        token[t_idx] = '\0';
        
        if (current->flags & FS_MOUNTPOINT) {
            current = current->mountpoint_node;
            if (!current) return 0;
        }
        
        if (!current->finddir) return 0;
        current = current->finddir(current, token);
        if (!current) return 0;
    }
    
    return current;
}

// ==========================================
// 4. ВНУТРЕННИЕ API ЯДРА (Обходят FS_SYSTEM)
// ==========================================

int32_t vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (!node || !node->read) return -1;
    return node->read(node, offset, size, buffer);
}

int32_t vfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, const uint8_t* buffer) {
    if (!node || !node->write) return -1;
    return node->write(node, offset, size, buffer);
}

// ==========================================
// 5. POSIX SYSCALLS (Граница Ring 3 -> Ring 0)
// ==========================================

int sys_open(const char* pathname, uint32_t flags) {
    if (!pathname) return VFS_ENOENT;
    
    vfs_node_t* node = vfs_findnode(pathname);
    if (!node) return VFS_ENOENT;
    
    // 🛑 RING-BASED ACCESS CONTROL (ИСПРАВЛЕНИЕ ЛОГИКИ ПРАВ)
    // Запрещаем открытие системных ФАЙЛОВ из Ring 3 (например, /boot/secret.txt).
    // Но РАЗРЕШАЕМ открытие системных ДИРЕКТОРИЙ (например, / и /boot), 
    // чтобы команда `ls` могла получить FD и прочитать структуру дерева через sys_readdir.
    // Это безопасно: sys_read на директории не сработает, так как node->read == NULL.
    if ((node->flags & FS_SYSTEM) && !(node->flags & FS_DIRECTORY)) {
        return VFS_EACCES;
    }
    
    if (node->open && node->open(node, flags) != 0) return VFS_EACCES;
    
    open_file_t* of = (open_file_t*)kmalloc(sizeof(open_file_t));
    if (!of) return VFS_ENOMEM;
    
    of->node = node;
    of->offset = 0;
    of->flags = flags;
    of->ref_count = 1;
    
    for (int i = 0; i < TASK_MAX_OPEN_FILES; i++) {
        if (current_task->fd_table[i] == 0) {
            current_task->fd_table[i] = of;
            return i;
        }
    }
    
    kfree(of);
    return VFS_ENOMEM;
}

int sys_close(int fd) {
    if (fd < 0 || fd >= TASK_MAX_OPEN_FILES) return -1;
    
    open_file_t* of = current_task->fd_table[fd];
    if (!of) return -1;
    
    of->ref_count--;
    if (of->ref_count == 0) {
        if (of->node && of->node->close) of->node->close(of->node);
        kfree(of);
    }
    current_task->fd_table[fd] = 0;
    return 0;
}

int32_t sys_read(int fd, void* buf, uint32_t count) {
    if (fd < 0 || fd >= TASK_MAX_OPEN_FILES || !buf) return -1;
    open_file_t* of = current_task->fd_table[fd];
    if (!of || !of->node || !of->node->read) return -1;
    
    int32_t bytes = of->node->read(of->node, of->offset, count, (uint8_t*)buf);
    if (bytes > 0) of->offset += bytes;
    return bytes;
}

int32_t sys_write(int fd, const void* buf, uint32_t count) {
    if (fd < 0 || fd >= TASK_MAX_OPEN_FILES || !buf) return -1;
    open_file_t* of = current_task->fd_table[fd];
    if (!of || !of->node || !of->node->write) return -1;
    
    int32_t bytes = of->node->write(of->node, of->offset, count, (const uint8_t*)buf);
    if (bytes > 0) of->offset += bytes;
    return bytes;
}

int32_t sys_readdir(int fd, uint32_t index, dirent_t* entry) {
    if (fd < 0 || fd >= TASK_MAX_OPEN_FILES || !entry) return -1;
    
    open_file_t* of = current_task->fd_table[fd];
    
    if (!of) { 
        serial_printf("[SYS_READDIR] FAIL: fd_table[%d] is NULL!\n", fd); 
        return -1; 
    }
    if (!of->node) { 
        serial_print("[SYS_READDIR] FAIL: of->node is NULL!\n"); 
        return -1; 
    }
    if (!(of->node->flags & FS_DIRECTORY)) { 
        serial_printf("[SYS_READDI…s' is not a directory (flags=0x%x)!\n", of->node->name, of->node->flags); 
        return -1; 
    }
    if (!of->node->readdir) { 
        serial_printf("[SYS_READDIR] FAIL: Node '%s' has no readdir function!\n", of->node->name); 
        return -1; 
    }
    
    serial_printf("[SYS_READDIR] OK: Calling readdir for '%s', index %d\n", of->node->name, index);
    int32_t ret = of->node->readdir(of->node, index, entry);
    serial_printf("[SYS_READDIR] Result: %d\n", ret);
    
    return ret;
}

// ==========================================
// 6. СТАНДАРТНЫЕ ПОТОКИ (FD 0, 1, 2)
// ==========================================

static int32_t stdout_write(vfs_node_t* node, uint32_t offset, uint32_t size, const uint8_t* buffer) {
    (void)node; (void)offset;
    for (uint32_t i = 0; i < size; i++) {
        serial_putc(buffer[i]);
    }
    return size;
}

void task_init_fds(task_t* task) {
    if (!task) return;
    
    vfs_node_t* stdout_node = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    if (stdout_node) {
        k_memset(stdout_node, 0, sizeof(vfs_node_t));
        stdout_node->flags = FS_CHARDEVICE;
        stdout_node->write = stdout_write;
        
        open_file_t* of_stdin = (open_file_t*)kmalloc(sizeof(open_file_t));
        open_file_t* of_stdout = (open_file_t*)kmalloc(sizeof(open_file_t));
        
        if (of_stdin && of_stdout) {
            vfs_node_t* stdin_node = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
            if(stdin_node) {
                k_memset(stdin_node, 0, sizeof(vfs_node_t));
                stdin_node->flags = FS_CHARDEVICE;
                of_stdin->node = stdin_node;
            }
            
            of_stdin->ref_count = 1;
            of_stdout->node = stdout_node; 
            of_stdout->ref_count = 1;
            
            task->fd_table[0] = of_stdin;
            task->fd_table[1] = of_stdout;
            task->fd_table[2] = of_stdout;
        }
    }
}