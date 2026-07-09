#include "vfs.h"
#include "heap.h"
#include "klib.h"
#include "serial.h"
#include "task.h" 

static vfs_node_t* vfs_root = 0;

// ==========================================
// ГЛОБАЛЬНЫЕ СИНГЛТОНЫ СТАНДАРТНЫХ ПОТОКОВ
// ==========================================
static vfs_node_t stdin_node_singleton;
static vfs_node_t stdout_node_singleton;
static bool std_nodes_initialized = false;

static int32_t stdout_write(vfs_node_t* node, uint32_t offset, uint32_t size, const uint8_t* buffer) {
    (void)node; (void)offset;
    for (uint32_t i = 0; i < size; i++) {
        k_putchar(buffer[i]);   // Вывод на экран (VGA/FB через Strategy Pattern)
        serial_putc(buffer[i]); // Зеркалирование в COM1 для headless debug
    }
    return size;
}

static int32_t stdin_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    (void)node; (void)offset; (void)size; (void)buffer;
    // TODO: День 10 - интеграция с Ring Buffer клавиатуры
    return 0; // EOF
}

// ==========================================
// 1. УНИВЕРСАЛЬНЫЕ ФУНКЦИИ ОБХОДА ДЕРЕВА (LCRS)
// ==========================================

static int32_t vfs_generic_readdir(vfs_node_t* node, uint32_t index, dirent_t* entry) {
    if (!node || !entry) return -1;
    vfs_node_t* child = node->first_child;
    uint32_t i = 0;
    while (child && i < index) { child = child->next_sibling; i++; }
    if (!child) return -1; 
    
    entry->ino = 0; 
    int j = 0; while(child->name[j] && j < VFS_MAX_FILENAME - 1) { entry->name[j] = child->name[j]; j++; }
    entry->name[j] = '\0';
    return 0;
}

static vfs_node_t* vfs_generic_finddir(vfs_node_t* node, const char* name) {
    if (!node || !name) return 0;
    vfs_node_t* child = node->first_child;
    while (child) {
        if (k_strcmp(child->name, name) == 0) return child;
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
    vfs_root->flags = FS_DIRECTORY;
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
    while (name[i] && i < VFS_MAX_FILENAME - 1) { node->name[i] = name[i]; i++; }
    node->name[i] = '\0';
    
    node->flags = flags;
    node->private_data = private_data;

    if (flags & FS_DIRECTORY) {
        node->readdir = vfs_generic_readdir;
        node->finddir = vfs_generic_finddir;
    }
    
    if (parent) vfs_add_child(parent, node);
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
        while (current->next_sibling) current = current->next_sibling;
        current->next_sibling = child;
    }
}

// Добавь в vfs.c после vfs_add_child
vfs_node_t* vfs_mkdir_recursive(const char* path) {
    if (!path || path[0] != '/') return NULL;
    
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
        
        // Ищем существующую директорию
        vfs_node_t* child = current->finddir ? current->finddir(current, token) : NULL;
        
        if (!child) {
            // Создаем новую директорию
            child = vfs_create_node(token, FS_DIRECTORY, current, NULL);
            if (!child) return NULL;
        }
        
        current = child;
    }
    return current;
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
    serial_printf("[VFS] Mounted filesystem at: %s\n", mountpoint_path);
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
// 4. ВНУТРЕННИЕ API ЯДРА
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
    
    if ((node->flags & FS_SYSTEM) && !(node->flags & FS_DIRECTORY)) return VFS_EACCES;
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
    if (!of || !of->node) return -1;
    if (!(of->node->flags & FS_DIRECTORY)) return -1;
    if (!of->node->readdir) return -1;
    
    return of->node->readdir(of->node, index, entry);
}

// ==========================================
// 6. СТАНДАРТНЫЕ ПОТОКИ (FD 0, 1, 2)
// ==========================================
void task_init_fds(task_t* task) {
    if (!task) return;

    if (!std_nodes_initialized) {
        k_memset(&stdin_node_singleton, 0, sizeof(vfs_node_t));
        stdin_node_singleton.flags = FS_CHARDEVICE;
        stdin_node_singleton.read = stdin_read;

        k_memset(&stdout_node_singleton, 0, sizeof(vfs_node_t));
        stdout_node_singleton.flags = FS_CHARDEVICE;
        stdout_node_singleton.write = stdout_write;
        
        std_nodes_initialized = true;
    }

    open_file_t* of_stdin = (open_file_t*)kmalloc(sizeof(open_file_t));
    open_file_t* of_stdout = (open_file_t*)kmalloc(sizeof(open_file_t));

    if (of_stdin && of_stdout) {
        of_stdin->node = &stdin_node_singleton;
        of_stdin->offset = 0;
        of_stdin->flags = O_RDONLY;
        of_stdin->ref_count = 1;

        of_stdout->node = &stdout_node_singleton;
        of_stdout->offset = 0;
        of_stdout->flags = O_WRONLY;
        of_stdout->ref_count = 2; // stdout (1) и stderr (2) делят один open_file

        task->fd_table[0] = of_stdin;
        task->fd_table[1] = of_stdout;
        task->fd_table[2] = of_stdout; 
    }
}
