#ifndef USER_LIBC_H
#define USER_LIBC_H

/* ==========================================================================
 * ENCLAVE OS - MONOLITHIC USER LIBC HEADER (SSOT)
 * Bypasses VFS shadowing and standard headers to ensure TinyCC compatibility.
 * ========================================================================== */

/* --------------------------------------------------------------------------
 * 1. Compiler Builtins & Variadic Arguments
 * -------------------------------------------------------------------------- */
#ifndef va_list
typedef __builtin_va_list va_list;
#define va_start(v,l) __builtin_va_start(v,l)
#define va_end(v)     __builtin_va_end(v)
#define va_arg(v,l)   __builtin_va_arg(v,l)
#define va_copy(d,s)  __builtin_va_copy(d,s)
#endif

/* --------------------------------------------------------------------------
 * 2. Standard Integer Types (Bypass <stdint.h>)
 * -------------------------------------------------------------------------- */
typedef signed char        int8_t;
typedef short              int16_t;
typedef int                int32_t;
typedef long long          int64_t;

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

typedef int                intptr_t;
typedef unsigned int       uintptr_t;
typedef int                ptrdiff_t;

/* --------------------------------------------------------------------------
 * 3. POSIX & ISO C Core Types
 * -------------------------------------------------------------------------- */
typedef unsigned int       size_t;
typedef int                ssize_t;
typedef int                off_t;
typedef int                pid_t;
typedef unsigned int       mode_t;
typedef unsigned int       uid_t;
typedef unsigned int       gid_t;
typedef unsigned int       dev_t;
typedef unsigned int       ino_t;
typedef unsigned int       nlink_t;
typedef unsigned int       blksize_t;
typedef unsigned int       blkcnt_t;
typedef unsigned int       clock_t;
typedef unsigned int       time_t;

#ifndef NULL
#define NULL ((void*)0)
#endif

#ifndef EOF
#define EOF (-1)
#endif

#define CLOCKS_PER_SEC 1000000

/* --------------------------------------------------------------------------
 * 4. Standard Macros & Limits
 * -------------------------------------------------------------------------- */
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define PATH_MAX 256
#define NAME_MAX 255

#define INT_MAX 2147483647
#define INT_MIN (-2147483647 - 1)
#define UINT_MAX 4294967295U
#define LONG_MAX 2147483647L
#define LONG_MIN (-2147483647L - 1L)
#define CHAR_BIT 8

/* waitpid options */
#define WNOHANG 1

/* dirent file types (sync с user_syscalls.h) */
#define DT_UNKNOWN  0
#define DT_DIR      4
#define DT_REG      8

/* --------------------------------------------------------------------------
 * 5. Error Codes (POSIX errno)
 * -------------------------------------------------------------------------- */
extern int errno;
int* __errno_location(void);

#define EPERM            1
#define ENOENT           2
#define ESRCH            3
#define EINTR            4
#define EIO              5
#define ENXIO            6
#define E2BIG            7
#define ENOEXEC          8
#define EBADF            9
#define ECHILD          10
#define EAGAIN          11
#define ENOMEM          12
#define EACCES          13
#define EFAULT          14
#define ENOTBLK         15
#define EBUSY           16
#define EEXIST          17
#define EXDEV           18
#define ENODEV          19
#define ENOTDIR         20
#define EISDIR          21
#define EINVAL          22
#define ENFILE          23
#define EMFILE          24
#define ENOTTY          25
#define ETXTBSY         26
#define EFBIG           27
#define ENOSPC          28
#define ESPIPE          29
#define EROFS           30
#define EMLINK          31
#define EPIPE           32
#define EDOM            33
#define ERANGE          34
#define EDEADLK         35
#define ENAMETOOLONG    36
#define ENOLCK          37
#define ENOSYS          38
#define ENOTEMPTY       39
#define ELOOP           40
#define EWOULDBLOCK     EAGAIN
#define ENOMSG          42
#define EIDRM           43
#define ENOSTR          60
#define ENODATA         61
#define ETIME           62
#define ENOSR           63
#define ENONET          64
#define ENOLINK         67
#define EPROTO          71
#define EMULTIHOP       72
#define EBADMSG         74
#define EOVERFLOW       75
#define ENOTUNIQ        76
#define EILSEQ          84
#define ENOTSOCK        88
#define EDESTADDRREQ    89
#define EMSGSIZE        90
#define EPROTOTYPE      91
#define ENOPROTOOPT     92
#define EPROTONOSUPPORT 93
#define EOPNOTSUPP      95
#define EAFNOSUPPORT    97
#define EADDRINUSE      98
#define EADDRNOTAVAIL   99
#define ENETDOWN       100
#define ENETUNREACH    101
#define ENETRESET      102
#define ECONNABORTED   103
#define ECONNRESET     104
#define ENOBUFS        105
#define EISCONN        106
#define ENOTCONN       107
#define ETIMEDOUT      110
#define ECONNREFUSED   111
#define EHOSTDOWN      112
#define EHOSTUNREACH   113
#define EALREADY       114
#define EINPROGRESS    115

/* --------------------------------------------------------------------------
 * 6. File I/O Flags & Modes
 * -------------------------------------------------------------------------- */
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_CREAT     0x0040
#define O_TRUNC     0x0200
#define O_APPEND    0x0400
#define O_BINARY    0x0000

#define S_IFMT  0170000
#define S_IFSOCK 0140000
#define S_IFLNK 0120000
#define S_IFREG 0100000
#define S_IFBLK 0060000
#define S_IFDIR 0040000
#define S_IFCHR 0020000
#define S_IFIFO 0010000
#define S_ISUID 0004000
#define S_ISGID 0002000
#define S_ISVTX 0001000

#define S_IRWXU 00700
#define S_IRUSR 00400
#define S_IWUSR 00200
#define S_IXUSR 00100
#define S_IRWXG 00070
#define S_IRGRP 00040
#define S_IWGRP 00020
#define S_IXGRP 00010
#define S_IRWXO 00007
#define S_IROTH 00004
#define S_IWOTH 00002
#define S_IXOTH 00001

/* --------------------------------------------------------------------------
 * 7. Memory Mapping & Protection
 * -------------------------------------------------------------------------- */
#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define PROT_EXEC   0x4
#define PROT_NONE   0x0

#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20
#define MAP_FAILED    ((void *) -1)

/* --------------------------------------------------------------------------
 * 8. Terminal & IOCTL
 * -------------------------------------------------------------------------- */
#define TIOCGWINSZ 0x5413

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

/* --------------------------------------------------------------------------
 * 9. Standard I/O (FILE structure)
 * -------------------------------------------------------------------------- */
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define FILE_BUFFER_SIZE 4096

typedef struct {
    int fd;
    int eof;
    int error;
    char read_buffer[FILE_BUFFER_SIZE];
    int buffer_pos;
    int buffer_len;
    char write_buffer[FILE_BUFFER_SIZE];
    int write_pos;
} FILE;

extern FILE* stdin;
extern FILE* stdout;
extern FILE* stderr;

/* --------------------------------------------------------------------------
 * 10. Directory & Stat Structures
 * -------------------------------------------------------------------------- */
struct dirent {
    unsigned int d_ino;
    off_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[256];
};

typedef unsigned long dev_t_stat;
typedef unsigned long ino_t_stat;

struct stat {
    dev_t_stat st_dev;
    ino_t_stat st_ino;
    mode_t st_mode;
    nlink_t st_nlink;
    uid_t st_uid;
    gid_t st_gid;
    dev_t_stat st_rdev;
    off_t st_size;
    blksize_t st_blksize;
    blkcnt_t st_blocks;
    time_t st_atime;
    time_t st_mtime;
    time_t st_ctime;
};

/* --------------------------------------------------------------------------
 * 11. Time Structures
 * -------------------------------------------------------------------------- */
typedef struct {
    unsigned int tv_sec;
    unsigned int tv_usec;
} timeval_t;

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

/* --------------------------------------------------------------------------
 * 12. System Info Structures
 * -------------------------------------------------------------------------- */
#define UTSNAME_LENGTH 65
typedef struct {
    char sysname[UTSNAME_LENGTH];
    char nodename[UTSNAME_LENGTH];
    char release[UTSNAME_LENGTH];
    char version[UTSNAME_LENGTH];
    char machine[UTSNAME_LENGTH];
} utsname_t;

typedef struct {
    unsigned int uptime;
    unsigned int totalram;
    unsigned int freeram;
    unsigned int sharedram;
    unsigned int bufferram;
    unsigned int totalswap;
    unsigned int freeswap;
    unsigned short procs;
    unsigned short pad;
    unsigned int totalhigh;
    unsigned int freehigh;
    unsigned int mem_unit;
} sysinfo_t;

/* --------------------------------------------------------------------------
 * 13. Function Prototypes: Memory & Strings
 * -------------------------------------------------------------------------- */
void* malloc(size_t size);
void* calloc(size_t nmemb, size_t size);
void* realloc(void* ptr, size_t size);
void free(void* ptr);

void* memset(void* ptr, int value, size_t num);
void* memcpy(void* dest, const void* src, size_t num);
int memcmp(const void* s1, const void* s2, size_t n);
void* memmove(void* dest, const void* src, size_t n);

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
char* strndup(const char* s, size_t n);
char* strpbrk(const char* s, const char* accept);

int atoi(const char* str);
long strtol(const char* str, char** endptr, int base);
unsigned long strtoul(const char* str, char** endptr, int base);
double strtod(const char* nptr, char** endptr);
float strtof(const char* nptr, char** endptr);
long double strtold(const char* nptr, char** endptr);

void itoa(int value, char* buf, int base);
void uitoa(unsigned int value, char* buf, int base);

/* --------------------------------------------------------------------------
 * 14. Function Prototypes: File I/O
 * -------------------------------------------------------------------------- */
int open(const char* pathname, int flags, ...);
int close(int fd);
ssize_t read(int fd, void* buf, size_t count);
ssize_t write(int fd, const void* buf, size_t count);
off_t lseek(int fd, off_t offset, int whence);
int unlink(const char* pathname);

FILE* fopen(const char* path, const char* mode);
int fclose(FILE* stream);
int fflush(FILE* stream);
size_t fread(void* ptr, size_t size, size_t nmemb, FILE* stream);
size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream);
int fgetc(FILE* stream);
int fputc(int c, FILE* stream);
char* fgets(char* s, int size, FILE* stream);
int fputs(const char* s, FILE* stream);
int fseek(FILE* stream, long offset, int whence);
long ftell(FILE* stream);
FILE* fdopen(int fd, const char* mode);
FILE* freopen(const char* path, const char* mode, FILE* stream);
int remove(const char* pathname);

int ferror(FILE* stream);
int feof(FILE* stream);

int isatty(int fd);
char* getcwd(char* buf, size_t size);
int chdir(const char* path);
char* realpath(const char* path, char* resolved_path);
int dup(int oldfd);
int dup2(int oldfd, int newfd);
// POSIX FUNCTIONS (Day 29 — Enano Text Editor Support)
ssize_t getline(char** lineptr, size_t* n, FILE* stream);
char* strdup(const char* s);
char* strerror(int errnum);
void perror(const char* s);
time_t time(time_t* tloc);
int ioctl(int fd, unsigned long request, ...);

/* --------------------------------------------------------------------------
 * 15. Function Prototypes: Output (printf family)
 * -------------------------------------------------------------------------- */
int printf(const char* fmt, ...);
int fprintf(FILE* stream, const char* fmt, ...);
int sprintf(char* buf, const char* fmt, ...);
int snprintf(char* buf, size_t size, const char* fmt, ...);
int vprintf(const char* fmt, va_list args);
int vfprintf(FILE* stream, const char* fmt, va_list args);
int vsprintf(char* buf, const char* fmt, va_list args);
int vsnprintf(char* buf, size_t size, const char* fmt, va_list args);

/* --------------------------------------------------------------------------
 * 16. Function Prototypes: Process Control
 * -------------------------------------------------------------------------- */
void exit(int status) __attribute__((noreturn));
int getpid(void);
int fork(void);
int waitpid(int pid, int* status, int options);
int exec(const char* path, const char** argv);
int execvp(const char* file, char* const argv[]);
int system(const char* command);
void abort(void) __attribute__((noreturn));
int atexit(void (*func)(void));
void __run_atexit_handlers(void);

/* --------------------------------------------------------------------------
 * 17. Function Prototypes: Time & System
 * -------------------------------------------------------------------------- */
int gettimeofday(timeval_t* tv, void* tz);
int uname(utsname_t* buf);
int sysinfo(sysinfo_t* info);
unsigned int sleep(unsigned int seconds);
int usleep(unsigned int usec);

clock_t clock(void);
time_t time(time_t* tloc);
struct tm* localtime(const time_t* timep);

/* --------------------------------------------------------------------------
 * 18. Function Prototypes: Signals, Dynamic Linking, Non-local Jumps
 * -------------------------------------------------------------------------- */
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

#define RTLD_LAZY   0x00001
#define RTLD_NOW    0x00002
#define RTLD_GLOBAL 0x00100

void* dlopen(const char* filename, int flag);
void* dlsym(void* handle, const char* symbol);
int dlclose(void* handle);
char* dlerror(void);

typedef int jmp_buf[6];
int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val) __attribute__((noreturn));
int _setjmp(jmp_buf env);

/* --------------------------------------------------------------------------
 * 19. Function Prototypes: Sorting, Searching, Error Strings
 * -------------------------------------------------------------------------- */
void qsort(void* base, size_t nmemb, size_t size, int (*cmp)(const void*, const void*));
void* bsearch(const void* key, const void* base, size_t nmemb, size_t size, int (*cmp)(const void*, const void*));
char* strerror(int errnum);

/* --------------------------------------------------------------------------
 * 20. Environment & Assertions
 * -------------------------------------------------------------------------- */
extern char** environ;
char* getenv(const char* name);

void __assert_fail(const char* assertion, const char* file, unsigned int line, const char* function) __attribute__((noreturn));

#ifdef NDEBUG
#define assert(expr) ((void)0)
#else
#define assert(expr) \
    ((expr) ? (void)0 : __assert_fail(#expr, __FILE__, __LINE__, __func__))
#endif

/* --------------------------------------------------------------------------
 * 21. Math & Floating Point
 * -------------------------------------------------------------------------- */
double ldexp(double x, int exp);
long double ldexpl(long double x, int exp);

/* --------------------------------------------------------------------------
 * 22. System Configuration
 * -------------------------------------------------------------------------- */
#define _SC_PAGESIZE 30
long sysconf(int name);
int mprotect(void* addr, size_t len, int prot);

/* --------------------------------------------------------------------------
 * 23. GCC 14+ / C23 Compliance Stubs (Prototypes)
 * -------------------------------------------------------------------------- */
long __isoc23_strtol(const char* nptr, char** endptr, int base);
unsigned long __isoc23_strtoul(const char* nptr, char** endptr, int base);
long long __isoc23_strtoll(const char* nptr, char** endptr, int base);
unsigned long long __isoc23_strtoull(const char* nptr, char** endptr, int base);

#endif /* USER_LIBC_H */
