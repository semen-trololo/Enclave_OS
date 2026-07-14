#ifndef USER_LIBC_H
#define USER_LIBC_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

// ============================================================================
// POSIX TYPES (sys/types.h equivalent for Freestanding Environment)
// ============================================================================
typedef int32_t ssize_t;  // Signed size_t (для read/write, возвращает -1 при ошибке)
typedef int32_t off_t;    // File offset (для lseek)
typedef int32_t pid_t;    // Process ID (для fork/waitpid/system)
// ============================================================================
// POSIX-совместимый API для Ring 3 программ
// Все функции работают ТОЛЬКО через syscalls (Zero Trust Sandbox)
// ============================================================================

// ============================================================================
// POSIX errno codes (subset)
// ============================================================================
#define EPERM        1
#define ENOENT       2
#define ESRCH        3
#define EINTR        4
#define EIO          5
#define ENOMEM      12
#define EACCES      13
#define EFAULT      14
#define EBUSY       16
#define EEXIST      17
#define ENODEV      19
#define ENOTDIR     20
#define EISDIR      21
#define EINVAL      22
#define ENFILE      23
#define EMFILE      24
#define ENOSPC      28
#define ERANGE      34
#define ENAMETOOLONG 36
#define ENOSYS      38
#define ENOTTY      25
#define EBADF        9

// Глобальная errno (TLS в будущем, пока просто global)
extern int errno;

// ============================================================================
// Standard I/O (File Descriptors)
// ============================================================================
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define FILE_BUFFER_SIZE 4096

typedef struct {
    int fd;
    int eof;
    int error;
    // Read buffer (для ускорения fread/fgetc в 10-100x)
    char read_buffer[FILE_BUFFER_SIZE];
    int buffer_pos;      // Текущая позиция в буфере
    int buffer_len;      // Количество валидных байт в буфере
    // Write buffer (для ускорения fwrite/fputc)
    char write_buffer[FILE_BUFFER_SIZE];
    int write_pos;       // Текущая позиция записи
} FILE;

extern FILE* stdin;
extern FILE* stdout;
extern FILE* stderr;

#define EOF (-1)

// ============================================================================
// File I/O Flags (fcntl.h)
// ============================================================================
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_CREAT     0x0040
#define O_TRUNC     0x0200
#define O_APPEND    0x0400

// ============================================================================
// Memory Management (Bump Allocator через sys_brk)
// ============================================================================
void* malloc(size_t size);
void* calloc(size_t nmemb, size_t size);
void* realloc(void* ptr, size_t size);
void free(void* ptr);  // No-op в bump allocator (memory leak by design)

// ============================================================================
// Memory Operations
// ============================================================================
void* memset(void* ptr, int value, size_t num);
void* memcpy(void* dest, const void* src, size_t num);
int memcmp(const void* s1, const void* s2, size_t n);
void* memmove(void* dest, const void* src, size_t n);

// ============================================================================
// String Operations
// ============================================================================
size_t strlen(const char* str);
int strcmp(const char* s1, const char* s2);
int strncmp(const char* s1, const char* s2, size_t n);
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t n);
char* strcat(char* dest, const char* src);
char* strncat(char* dest, const char* src, size_t n);
char* strchr(const char* str, int c);
char* strrchr(const char* str, int c);
char* strstr(const char* haystack, const char* needle);

// ============================================================================
// File I/O (через syscalls)
// ============================================================================
int open(const char* pathname, int flags, ...);
int close(int fd);
ssize_t read(int fd, void* buf, size_t count);
ssize_t write(int fd, const void* buf, size_t count);
off_t lseek(int fd, off_t offset, int whence);

FILE* fopen(const char* path, const char* mode);
int fclose(FILE* stream);
int fflush(FILE* stream);
size_t fread(void* ptr, size_t size, size_t nmemb, FILE* stream);
size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream);
int fgetc(FILE* stream);      // ✅ ДОБАВИТЬ
int fputc(int c, FILE* stream);  // ✅ ДОБАВИТЬ
char* fgets(char* s, int size, FILE* stream);  // ✅ ДОБАВИТЬ
int ferror(FILE* stream);
int feof(FILE* stream);
int unlink(const char* pathname);

// ============================================================================
// Output (printf family)
// ============================================================================
int printf(const char* fmt, ...);
int fprintf(FILE* stream, const char* fmt, ...);
int sprintf(char* buf, const char* fmt, ...);
int snprintf(char* buf, size_t size, const char* fmt, ...);
int vprintf(const char* fmt, va_list args);
int vfprintf(FILE* stream, const char* fmt, va_list args);
int vsprintf(char* buf, const char* fmt, va_list args);
int vsnprintf(char* buf, size_t size, const char* fmt, va_list args);

// ============================================================================
// Process Control
// ============================================================================
void exit(int status) __attribute__((noreturn));
int getpid(void);
int fork(void);
int waitpid(int pid, int* status, int options);
int exec(const char* path, const char** argv); 

// ============================================================================
// Time & System Info (Day 15)
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

int gettimeofday(timeval_t* tv, void* tz);
int uname(utsname_t* buf);
int sysinfo(sysinfo_t* info);
unsigned int sleep(unsigned int seconds);
int usleep(unsigned int usec);

// ============================================================================
// Number Conversion Utilities
// ============================================================================
int atoi(const char* str);
long strtol(const char* str, char** endptr, int base);
unsigned long strtoul(const char* str, char** endptr, int base);

// Internal helpers (exposed for completeness)
void itoa(int value, char* buf, int base);
void uitoa(unsigned int value, char* buf, int base);

// ============================================================================
// Process Execution (system() для TinyCC)
// ============================================================================
int system(const char* command);

// ============================================================================
// Environment Variables (упрощенные для TinyCC)
// ============================================================================
char* getenv(const char* name);

// ============================================================================
// Signal Handling (no-op для TinyCC)
// ============================================================================
#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)
#define SIG_ERR ((void (*)(int))-1)

#define SIGHUP    1
#define SIGINT    2
#define SIGQUIT   3
#define SIGILL    4
#define SIGTRAP   5
#define SIGABRT   6
#define SIGBUS    7
#define SIGFPE    8
#define SIGKILL   9
#define SIGUSR1  10
#define SIGSEGV  11
#define SIGUSR2  12
#define SIGPIPE  13
#define SIGALRM  14
#define SIGTERM  15

typedef void (*sighandler_t)(int);
sighandler_t signal(int signum, sighandler_t handler);

// ============================================================================
// Dynamic Linking (заглушки для TinyCC)
// ============================================================================
#define RTLD_LAZY   0x00001
#define RTLD_NOW    0x00002
#define RTLD_GLOBAL 0x00100

void* dlopen(const char* filename, int flag);
void* dlsym(void* handle, const char* symbol);
int dlclose(void* handle);
char* dlerror(void);

#endif // USER_LIBC_H
