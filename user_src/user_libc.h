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
// POSIX errno codes (Linux i386 ABI — Full Set for TinyCC & POSIX Compliance)
// ============================================================================
#define EPERM            1   /* Operation not permitted */
#define ENOENT           2   /* No such file or directory */
#define ESRCH            3   /* No such process */
#define EINTR            4   /* Interrupted system call */
#define EIO              5   /* I/O error */
#define ENXIO            6   /* No such device or address */
#define E2BIG            7   /* Argument list too long */
#define ENOEXEC          8   /* Exec format error */
#define EBADF            9   /* Bad file number */
#define ECHILD          10   /* No child processes */
#define EAGAIN          11   /* Try again (would block) */
#define ENOMEM          12   /* Out of memory */
#define EACCES          13   /* Permission denied */
#define EFAULT          14   /* Bad address */
#define ENOTBLK         15   /* Block device required */
#define EBUSY           16   /* Device or resource busy */
#define EEXIST          17   /* File exists */
#define EXDEV           18   /* Cross-device link */
#define ENODEV          19   /* No such device */
#define ENOTDIR         20   /* Not a directory */
#define EISDIR          21   /* Is a directory */
#define EINVAL          22   /* Invalid argument */
#define ENFILE          23   /* File table overflow */
#define EMFILE          24   /* Too many open files */
#define ENOTTY          25   /* Not a typewriter / Inappropriate ioctl */
#define ETXTBSY         26   /* Text file busy */
#define EFBIG           27   /* File too large */
#define ENOSPC          28   /* No space left on device */
#define ESPIPE          29   /* Illegal seek */
#define EROFS           30   /* Read-only file system */
#define EMLINK          31   /* Too many links */
#define EPIPE           32   /* Broken pipe */
#define EDOM            33   /* Math argument out of domain of func */
#define ERANGE          34   /* Math result not representable */
#define EDEADLK         35   /* Resource deadlock would occur */
#define ENAMETOOLONG    36   /* File name too long */
#define ENOLCK          37   /* No record locks available */
#define ENOSYS          38   /* Function not implemented */
#define ENOTEMPTY       39   /* Directory not empty */
#define ELOOP           40   /* Too many symbolic links encountered */
#define EWOULDBLOCK     EAGAIN /* Operation would block */
#define ENOMSG          42   /* No message of desired type */
#define EIDRM           43   /* Identifier removed */
#define ENOSTR          60   /* Device not a stream */
#define ENODATA         61   /* No data available */
#define ETIME           62   /* Timer expired */
#define ENOSR           63   /* Out of streams resources */
#define ENONET          64   /* Machine is not on the network */
#define ENOLINK         67   /* Link has been severed */
#define EPROTO          71   /* Protocol error */
#define EMULTIHOP       72   /* Multihop attempted */
#define EBADMSG         74   /* Not a data message */
#define EOVERFLOW       75   /* Value too large for defined data type */
#define ENOTUNIQ        76   /* Name not unique on network */
#define EILSEQ          84   /* Illegal byte sequence */
#define ENOTSOCK        88   /* Socket operation on non-socket */
#define EDESTADDRREQ    89   /* Destination address required */
#define EMSGSIZE        90   /* Message too long */
#define EPROTOTYPE      91   /* Protocol wrong type for socket */
#define ENOPROTOOPT     92   /* Protocol not available */
#define EPROTONOSUPPORT 93   /* Protocol not supported */
#define EOPNOTSUPP      95   /* Operation not supported on transport endpoint */
#define EAFNOSUPPORT    97   /* Address family not supported by protocol */
#define EADDRINUSE      98   /* Address already in use */
#define EADDRNOTAVAIL   99   /* Cannot assign requested address */
#define ENETDOWN       100   /* Network is down */
#define ENETUNREACH    101   /* Network is unreachable */
#define ENETRESET      102   /* Network dropped connection because of reset */
#define ECONNABORTED   103   /* Software caused connection abort */
#define ECONNRESET     104   /* Connection reset by peer */
#define ENOBUFS        105   /* No buffer space available */
#define EISCONN        106   /* Transport endpoint is already connected */
#define ENOTCONN       107   /* Transport endpoint is not connected */
#define ETIMEDOUT      110   /* Connection timed out */
#define ECONNREFUSED   111   /* Connection refused */
#define EHOSTDOWN      112   /* Host is down */
#define EHOSTUNREACH   113   /* No route to host */
#define EALREADY       114   /* Operation already in progress */
#define EINPROGRESS    115   /* Operation now in progress */

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
#define O_BINARY    0x0000

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

// ============================================================================
// Non-local Jumps (для TinyCC error recovery)
// ============================================================================
typedef int jmp_buf[6];  // 6 регистров: EBX, ESI, EDI, EBP, ESP, EIP

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val) __attribute__((noreturn));

// ============================================================================
// Sorting & Searching (для TinyCC symbol tables)
// ============================================================================
void qsort(void* base, size_t nmemb, size_t size, 
           int (*cmp)(const void*, const void*));
void* bsearch(const void* key, const void* base, size_t nmemb, size_t size,
              int (*cmp)(const void*, const void*));

// ============================================================================
// Error Strings
// ============================================================================
char* strerror(int errnum);

// ============================================================================
// Terminal Detection
// ============================================================================
int isatty(int fd);

// ============================================================================
// Path Operations (упрощенные заглушки)
// ============================================================================
char* getcwd(char* buf, size_t size);
int chdir(const char* path);
char* realpath(const char* path, char* resolved_path);

// ============================================================================
// String Duplication
// ============================================================================
char* strdup(const char* s);
char* strndup(const char* s, size_t n);

// ============================================================================
// Process Termination
// ============================================================================
void abort(void) __attribute__((noreturn));
int atexit(void (*func)(void));

// Внутренняя функция: вызывается из exit() для atexit handlers
void __run_atexit_handlers(void);

// ============================================================================
// Time (обертки над gettimeofday)
// ============================================================================
typedef uint32_t clock_t;
typedef uint32_t time_t;

#define CLOCKS_PER_SEC 1000000

clock_t clock(void);
time_t time(time_t* tloc);

// ============================================================================
// Assertions
// ============================================================================
void __assert_fail(const char* assertion, const char* file, 
                   unsigned int line, const char* function) 
                   __attribute__((noreturn));

// Стандартный assert макрос
#ifdef NDEBUG
#define assert(expr) ((void)0)
#else
#define assert(expr) \
    ((expr) ? (void)0 : __assert_fail(#expr, __FILE__, __LINE__, __func__))
#endif

// ============================================================================
// DAY 20: EXTENDED POSIX & GLIBC COMPATIBILITY (Для компиляции TinyCC)
// ============================================================================

// glibc thread-safe errno accessor (в нашей однопоточной среде просто возвращает адрес)
int* __errno_location(void);

// C23 compliance (GCC 14+ заменяет strtol на эти функции)
long __isoc23_strtol(const char* nptr, char** endptr, int base);
unsigned long __isoc23_strtoul(const char* nptr, char** endptr, int base);
long long __isoc23_strtoll(const char* nptr, char** endptr, int base);
unsigned long long __isoc23_strtoull(const char* nptr, char** endptr, int base);

// Extended FILE* API
int fputs(const char* s, FILE* stream);
int fseek(FILE* stream, long offset, int whence);
long ftell(FILE* stream);
FILE* fdopen(int fd, const char* mode);
FILE* freopen(const char* path, const char* mode, FILE* stream);
int remove(const char* pathname);

// String operations
char* strpbrk(const char* s, const char* accept);

// Process execution (TinyCC использует для запуска внешних утилит)
int execvp(const char* file, char* const argv[]);

// System configuration
#define _SC_PAGESIZE 30
long sysconf(int name);

// Memory protection (обертка над sys_mprotect)
int mprotect(void* addr, size_t len, int prot);

// Time structures
struct tm {
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year;
    int tm_wday, tm_yday, tm_isdst;
};
struct tm* localtime(const time_t* timep);

// Math / Float parsing (TinyCC использует для парсинга float literals в C коде)
double strtod(const char* nptr, char** endptr);
float strtof(const char* nptr, char** endptr);
long double strtold(const char* nptr, char** endptr);
double ldexp(double x, int exp);
long double ldexpl(long double x, int exp);

// Environment
extern char** environ;

// setjmp alias (glibc lightweight setjmp)
int _setjmp(jmp_buf env);

#endif // USER_LIBC_H
