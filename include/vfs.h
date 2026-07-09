#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ==========================================
// КОНСТАНТЫ И ФЛАГИ (БИТОВЫЕ МАСКИ)
// ==========================================

// Типы узлов (можно комбинировать, например: FS_DIRECTORY | FS_MOUNTPOINT)
#define FS_FILE        0x01
#define FS_DIRECTORY   0x02
#define FS_CHARDEVICE  0x04  // Например, /dev/tty
#define FS_BLOCKDEVICE 0x08  // Например, /dev/hda
#define FS_PIPE        0x10
#define FS_SYMLINK     0x20
#define FS_MOUNTPOINT  0x40  // Точка монтирования (телепортация в другую ФС)

// 🛑 ФЛАГ БЕЗОПАСНОСТИ (RING-BASED ACCESS CONTROL)
// Запрещает доступ из User Space (Ring 3) через sys_open.
// Внутренние вызовы ядра (vfs_findnode) игнорируют этот флаг.
#define FS_SYSTEM      0x80

// Флаги открытия файла (POSIX O_*)
#define O_RDONLY   0x0000
#define O_WRONLY   0x0001
#define O_RDWR     0x0002
#define O_CREAT    0x0100
#define O_TRUNC    0x0200
#define O_APPEND   0x0400

// Dummy Permissions (для совместимости с POSIX stat)
#define PERM_READ_ONLY  0444
#define PERM_READ_WRITE 0666
#define PERM_EXECUTABLE 0755

// Ограничения ядра
#define VFS_MAX_FILENAME 128
#define VFS_MAX_OPEN_FILES 16 // Максимум открытых файлов на один процесс (FD)

// Коды ошибок (упрощенные, для возврата из syscall)
#define VFS_ENOENT -2  // No such file or directory
#define VFS_EACCES -13 // Permission denied (FS_SYSTEM or Read-Only)
#define VFS_ENOMEM -12 // Out of memory (при создании open_file_t)

// ==========================================
// СТРУКТУРЫ ДАННЫХ
// ==========================================

struct vfs_node; // Forward declaration

// Структура для возврата записи директории (аналог struct dirent в Linux)
typedef struct dirent {
    uint32_t ino;                      // Inode number (или 0, если не используется)
    char name[VFS_MAX_FILENAME];       // Имя файла
} dirent_t;

// ==========================================
// ПОЛИМОРФИЗМ НА C (Указатели на функции драйверов)
// ==========================================
// Возвращают количество прочитанных/записанных байт, или -1 при ошибке.
typedef int32_t (*read_type_t)(struct vfs_node*, uint32_t offset, uint32_t size, uint8_t* buffer);
typedef int32_t (*write_type_t)(struct vfs_node*, uint32_t offset, uint32_t size, const uint8_t* buffer);

// Возвращает 0 при успехе, -1 если индекс вышел за пределы (конец директории)
typedef int32_t (*readdir_type_t)(struct vfs_node*, uint32_t index, dirent_t* entry);

// Возвращает указатель на найденный дочерний узел или 0
typedef struct vfs_node* (*finddir_type_t)(struct vfs_node*, const char* name);

// Хуки открытия/закрытия (нужны для устройств или выделения private_data)
typedef int (*open_type_t)(struct vfs_node*, uint32_t flags);
typedef void (*close_type_t)(struct vfs_node*);

// ==========================================
// СЕРДЦЕ VFS: СТРУКТУРА INODE (vfs_node_t)
// ==========================================
typedef struct vfs_node {
    char name[VFS_MAX_FILENAME];       // Имя узла ("hello.txt", "boot")
    uint32_t size;                     // Размер в байтах (0 для директорий)
    uint32_t flags;                    // Тип (FS_FILE | FS_SYSTEM и т.д.)
    uint32_t permissions;              // Права доступа (Dummy: 0777 или 0444)
    uint32_t uid;                      // Dummy (всегда 0)
    uint32_t gid;                      // Dummy (всегда 0)

    // Указатели на функции конкретного бэкенда (RAM Disk, FAT32, TTY)
    read_type_t read;
    write_type_t write;
    readdir_type_t readdir;
    finddir_type_t finddir;
    open_type_t open;
    close_type_t close;

    // КОНТЕКСТ БЭКЕНДА (Магия полиморфизма)
    void* private_data;

    // СВЯЗИ В ДЕРЕВЕ КАТАЛОГОВ (Left-Child Right-Sibling)
    struct vfs_node* parent;           // Родительская директория
    struct vfs_node* first_child;      // Первый дочерний элемент
    struct vfs_node* next_sibling;     // Следующий элемент в той же директории

    // ТОЧКА МОНТИРОВАНИЯ
    struct vfs_node* mountpoint_node;
} vfs_node_t;

// ==========================================
// OPEN FILE DESCRIPTION (Открытый файловый дескриптор)
// ==========================================
// Создается при каждом sys_open(). Хранит курсор (offset).
typedef struct open_file {
    vfs_node_t* node;                  // Указатель на сам файл в дереве VFS
    uint32_t offset;                   // Текущий курсор чтения/записи
    uint32_t flags;                    // Флаги открытия (O_RDONLY, O_APPEND)
    uint32_t ref_count;                // Счетчик ссылок (для fork() и dup())
} open_file_t;

// ==========================================
// API ЯДРА (Внутренние функции, Ring 0)
// ==========================================
// Инициализация корневого узла "/"
void vfs_init(void);

// Поиск узла по абсолютному пути (ИГНОРИРУЕТ FS_SYSTEM)
vfs_node_t* vfs_findnode(const char* path);

// Монтирование одной ФС в другую
int vfs_mount(const char* mountpoint_path, vfs_node_t* target_node);

// Создание нового узла в памяти (для tmpfs/initrd)
vfs_node_t* vfs_create_node(const char* name, uint32_t flags, vfs_node_t* parent, void* private_data);

// Добавление ребенка к родителю (O(1) операция)
void vfs_add_child(vfs_node_t* parent, vfs_node_t* child);

vfs_node_t* vfs_mkdir_recursive(const char* path);

// Внутреннее чтение/запись (для ELF Loader и ядра, обходят проверку FS_SYSTEM)
int32_t vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer);
int32_t vfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, const uint8_t* buffer);

// ==========================================
// API СИСТЕМНЫХ ВЫЗЫВОВ (Граница Ring 3 -> Ring 0)
// ==========================================
// ПРОВЕРЯЮТ ФЛАГ FS_SYSTEM И ПРАВА ДОСТУПА
int sys_open(const char* pathname, uint32_t flags);
int sys_close(int fd);
int32_t sys_read(int fd, void* buf, uint32_t count);
int32_t sys_write(int fd, const void* buf, uint32_t count);
int32_t sys_readdir(int fd, uint32_t index, dirent_t* entry);

#endif // VFS_H
