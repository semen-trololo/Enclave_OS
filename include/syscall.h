#ifndef SYSCALL_H
#define SYSCALL_H

#include "idt.h"
#include "kerrno.h"  // ✅ Подключаем POSIX errno
#include <stdint.h>

// ============================================================================
// POSIX mman.h CONSTANTS (Day 12)
// ============================================================================
#define PROT_READ       0x1
#define PROT_WRITE      0x2
#define PROT_EXEC       0x4
#define PROT_NONE       0x0

#define MAP_SHARED      0x01
#define MAP_PRIVATE     0x02
#define MAP_FIXED       0x10
#define MAP_ANONYMOUS   0x20

// ============================================================================
// [ДЕНЬ 13] POSIX fcntl.h CONSTANTS (lseek)
// ============================================================================
#define SEEK_SET        0   // Установить offset = offset
#define SEEK_CUR        1   // Установить offset = current + offset
#define SEEK_END        2   // Установить offset = file_size + offset

// ============================================================================
// [ДЕНЬ 13] POSIX ioctl CONSTANTS
// ============================================================================
#define TIOCGWINSZ      0x5413  // Get Window Size (размер терминала)

// ============================================================================
// [ДЕНЬ 13] POSIX sys/stat.h STRUCTURES
// ============================================================================
// Структура для sys_fstat (упрощенная версия POSIX stat)
typedef struct {
    uint32_t st_dev;      // Device ID (пока 0)
    uint32_t st_ino;      // Inode number (пока 0)
    uint32_t st_mode;     // File mode (тип + права доступа)
    uint32_t st_nlink;    // Number of hard links
    uint32_t st_uid;      // User ID (пока 0)
    uint32_t st_gid;      // Group ID (пока 0)
    uint32_t st_rdev;     // Device ID (для special files)
    uint32_t st_size;     // Total size in bytes
    uint32_t st_blksize;  // Block size (4096)
    uint32_t st_blocks;   // Number of 512B blocks allocated
    uint32_t st_atime;    // Time of last access
    uint32_t st_mtime;    // Time of last modification
    uint32_t st_ctime;    // Time of last status change
} stat_t;

// POSIX file mode bits
#define S_IFMT      0170000  // File type mask
#define S_IFREG     0100000  // Regular file
#define S_IFDIR     0040000  // Directory
#define S_IFCHR     0020000  // Character device
#define S_IFBLK     0060000  // Block device

// ============================================================================
// [ДЕНЬ 13] POSIX sys/ioctl.h STRUCTURES
// ============================================================================
// Структура для TIOCGWINSZ (размер окна терминала)
typedef struct {
    uint16_t ws_row;    // Rows (in characters)
    uint16_t ws_col;    // Columns (in characters)
    uint16_t ws_xpixel; // Horizontal size (pixels) - unused
    uint16_t ws_ypixel; // Vertical size (pixels) - unused
} winsize_t;

// ============================================================================
// [ДЕНЬ 15] POSIX TIME & SYSTEM INFO STRUCTURES
// ============================================================================
typedef struct {
    uint32_t tv_sec;
    uint32_t tv_usec;
} timeval_t;

#define UTSNAME_LENGTH 65
typedef struct {
    char sysname[UTSNAME_LENGTH];
    char nodename[UTSNAME_LENGTH];
    char release[UTSNAME_LENGTH];
    char version[UTSNAME_LENGTH];
    char machine[UTSNAME_LENGTH];
} utsname_t;

typedef struct {
    uint32_t uptime;
    uint32_t totalram;
    uint32_t freeram;
    uint32_t sharedram;
    uint32_t bufferram;
    uint32_t totalswap;
    uint32_t freeswap;
    uint16_t procs;
    uint16_t pad;
    uint32_t totalhigh;
    uint32_t freehigh;
    uint32_t mem_unit;
} sysinfo_t;

// ============================================================================
// [ДЕНЬ 14] POSIX sys/wait.h CONSTANTS
// ============================================================================
#define WNOHANG   1   // Не блокироваться, если нет завершившихся детей (non-blocking)
#define WUNTRACED 2   // Возвращать статус для остановленных детей (пока не используется)
// ========================================================================
// Номера системных вызовов (Linux x86 ABI compatible)
// ========================================================================
#define SYS_EXIT      1
#define SYS_FORK      2
#define SYS_READ      3
#define SYS_WRITE     4
#define SYS_OPEN      5
#define SYS_CLOSE     6
#define SYS_WAITPID   7    // ✅ День 14
#define SYS_CREAT     8    // ✅ Зарезервировано
#define SYS_LINK      9
#define SYS_UNLINK    10
#define SYS_EXEC      11
#define SYS_CHDIR     12
#define SYS_TIME      13
#define SYS_MKNOD     14
#define SYS_CHMOD     15
#define SYS_LSEEK     19   // ✅ [ДЕНЬ 13]
#define SYS_FSTAT     28   // ✅ [ДЕНЬ 13]
#define SYS_BRK       45
#define SYS_IOCTL     54   // ✅ [ДЕНЬ 13]
#define SYS_UNAME     63   // ✅ [ДЕНЬ 15]
#define SYS_GETTIMEOFDAY 78  // ✅ [ДЕНЬ 15]
#define SYS_MMAP      90
#define SYS_MUNMAP    91
#define SYS_SYSINFO   116  // ✅ [ДЕНЬ 15]
#define SYS_GETPID    122  // ✅ День 14
#define SYS_MPROTECT  125
#define SYS_YIELD     158  // Custom: добровольный yield
#define SYS_SLEEP     230  // ✅ [ДЕНЬ 15] Custom: sleep в миллисекундах

// ========================================================================
// Инициализация таблицы системных вызовов
// ========================================================================
void syscall_init(void);

#endif // SYSCALL_H
