#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ==========================================
// КОНСТАНТЫ И ФЛАГИ (БИТОВЫЕ МАСКИ)
// ==========================================

#define FS_FILE        0x01
#define FS_DIRECTORY   0x02
#define FS_CHARDEVICE  0x04
#define FS_BLOCKDEVICE 0x08
#define FS_PIPE        0x10
#define FS_SYMLINK     0x20
#define FS_MOUNTPOINT  0x40
#define FS_SYSTEM      0x80

#define O_RDONLY   0x0000
#define O_WRONLY   0x0001
#define O_RDWR     0x0002
#define O_CREAT    0x0040
#define O_TRUNC    0x0200
#define O_APPEND   0x0400

#define PERM_READ_ONLY  0444
#define PERM_READ_WRITE 0666
#define PERM_EXECUTABLE 0755

#define VFS_MAX_FILENAME 128
#define VFS_MAX_OPEN_FILES 16

#define VFS_ENOENT -2
#define VFS_EACCES -13
#define VFS_ENOMEM -12

// ==========================================
// СТРУКТУРЫ ДАННЫХ
// ==========================================

struct vfs_node;

typedef struct dirent {
    uint32_t ino;
    char name[VFS_MAX_FILENAME];
} dirent_t;

// ==========================================
// ПОЛИМОРФИЗМ НА C (Указатели на функции драйверов)
// ==========================================

typedef int32_t (*read_type_t)(struct vfs_node*, uint32_t offset, uint32_t size, uint8_t* buffer);
typedef int32_t (*write_type_t)(struct vfs_node*, uint32_t offset, uint32_t size, const uint8_t* buffer);
typedef int32_t (*readdir_type_t)(struct vfs_node*, uint32_t index, dirent_t* entry);
typedef struct vfs_node* (*finddir_type_t)(struct vfs_node*, const char* name);
typedef int (*open_type_t)(struct vfs_node*, uint32_t flags);
typedef void (*close_type_t)(struct vfs_node*);
typedef struct vfs_node* (*create_type_t)(struct vfs_node*, const char*, uint32_t mode);
typedef int (*unlink_type_t)(struct vfs_node*, const char*);

// ==========================================
// СЕРДЦЕ VFS: СТРУКТУРА INODE (vfs_node_t)
// ==========================================
typedef struct vfs_node {
    char name[VFS_MAX_FILENAME];
    uint32_t size;
    uint32_t flags;
    uint32_t permissions;
    uint32_t uid;
    uint32_t gid;

    read_type_t read;
    write_type_t write;
    readdir_type_t readdir;
    finddir_type_t finddir;
    open_type_t open;
    close_type_t close;
    
    create_type_t create;
    unlink_type_t unlink;

    void* private_data;

    struct vfs_node* parent;
    struct vfs_node* first_child;
    struct vfs_node* next_sibling;

    struct vfs_node* mountpoint_node;
} vfs_node_t;

// ✅ ИСПРАВЛЕНО: Перемещено ПОСЛЕ определения vfs_node_t
extern vfs_node_t* vfs_root;

// ==========================================
// OPEN FILE DESCRIPTION (Открытый файловый дескриптор)
// ==========================================
typedef struct open_file {
    vfs_node_t* node;
    uint32_t offset;
    uint32_t flags;
    uint32_t ref_count;
} open_file_t;

// ==========================================
// API ЯДРА (Внутренние функции, Ring 0)
// ==========================================
void vfs_init(void);
vfs_node_t* vfs_findnode(const char* path);
int vfs_mount(const char* mountpoint_path, vfs_node_t* target_node);
vfs_node_t* vfs_create_node(const char* name, uint32_t flags, vfs_node_t* parent, void* private_data);
void vfs_add_child(vfs_node_t* parent, vfs_node_t* child);
vfs_node_t* vfs_mkdir_recursive(const char* path);

int32_t vfs_generic_readdir(vfs_node_t* node, uint32_t index, dirent_t* entry);
vfs_node_t* vfs_generic_finddir(vfs_node_t* node, const char* name);

int32_t vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer);
int32_t vfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, const uint8_t* buffer);

// ==========================================
// API СИСТЕМНЫХ ВЫЗОВОВ (Граница Ring 3 -> Ring 0)
// ==========================================
int sys_open(const char* pathname, uint32_t flags, uint32_t mode);
int sys_close(int fd);
int32_t sys_read(int fd, void* buf, uint32_t count);
int32_t sys_write(int fd, const void* buf, uint32_t count);
int32_t sys_readdir(int fd, uint32_t index, dirent_t* entry);

int sys_unlink(const char* pathname);

#endif // VFS_H