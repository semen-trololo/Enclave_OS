// ============================================================================
// USER LIBC — Ring 3 Standard Library for Enclave OS
// КОНСОЛИДИРОВАННЫЙ ФАЙЛ (Day 32: tcc_lib_os.c merged here)
//
// Все функции работают ТОЛЬКО через syscalls (Zero Trust Sandbox).
// malloc использует bump allocator (утечка памяти — by design для TinyCC).
//
// Порядок секций:
//  1. Includes
//  2. Globals (errno, stdin/stdout/stderr)
//  3. Memory ops (memset, memcpy, memcmp, memmove)
//  4. Heap (malloc, calloc, realloc, free)
//  5. String ops
//  6. Number conversion (atoi, strtol, strtoul, strtoll, strtoull, itoa)
//  7. Float parsing (strtod, strtof, strtold, ldexp)
//  8. FILE* I/O (fopen..remove)
//  9. Low-level I/O (open..unlink, isatty, getcwd, chdir, realpath)
// 10. Printf family
// 11. Process control (fork, exec, exit, system, atexit, abort)
// 12. Sort/Search (heapsort qsort, bsearch)
// 13. Time & System (gettimeofday, uname, sysinfo, sleep, clock, time)
// 14. Signals & Dynamic Linking stubs
// 15. POSIX extensions (getline, strdup, strerror, perror, ioctl, mmap)
// 16. glibc compat (__errno_location, __isoc23_*, __assert_fail, _setjmp)
// 17. Environment (getenv, environ)
// ============================================================================

#include "user_libc.h"
#include "user_syscalls.h"

// ============================================================================
// 2. GLOBALS
// ============================================================================
int errno = 0;

static FILE _stdin_stream = {
    .fd = STDIN_FILENO,
    .eof = 0, .error = 0,
    .buffer_pos = 0, .buffer_len = 0,
    .write_pos = 0
};

static FILE _stdout_stream = {
    .fd = STDOUT_FILENO,
    .eof = 0, .error = 0,
    .buffer_pos = 0, .buffer_len = 0,
    .write_pos = 0
};

static FILE _stderr_stream = {
    .fd = STDERR_FILENO,
    .eof = 0, .error = 0,
    .buffer_pos = 0, .buffer_len = 0,
    .write_pos = 0
};

FILE* stdin  = &_stdin_stream;
FILE* stdout = &_stdout_stream;
FILE* stderr = &_stderr_stream;

// ============================================================================
// 3. MEMORY OPERATIONS
// ============================================================================

void* memset(void* ptr, int value, size_t num) {
    uint8_t* p = (uint8_t*)ptr;
    while (num--) *p++ = (uint8_t)value;
    return ptr;
}

void* memcpy(void* dest, const void* src, size_t num) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    while (num--) *d++ = *s++;
    return dest;
}

int memcmp(const void* s1, const void* s2, size_t n) {
    const uint8_t* p1 = (const uint8_t*)s1;
    const uint8_t* p2 = (const uint8_t*)s2;
    while (n--) {
        if (*p1 != *p2) return (int)(*p1 - *p2);
        p1++; p2++;
    }
    return 0;
}

void* memmove(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}

// ============================================================================
// 4. MEMORY MANAGEMENT (Bump Allocator + Header for safe realloc)
// ============================================================================

typedef struct {
    size_t size;
    uint32_t magic;
} malloc_header_t;

#define MALLOC_MAGIC 0xA110CA7E
#define MALLOC_HDR_SIZE (sizeof(malloc_header_t))

static uint32_t heap_end = 0;

static void heap_init_if_needed(void) {
    if (heap_end == 0) {
        heap_end = (uint32_t)sys_brk(0);
    }
}

void* malloc(size_t size) {
    if (size == 0) return NULL;

    heap_init_if_needed();

    size_t aligned = (size + 7) & ~7;
    size_t total = MALLOC_HDR_SIZE + aligned;
    uint32_t new_end = heap_end + total;

    int result = sys_brk(new_end);
    if (result < 0) {
        errno = ENOMEM;
        return NULL;
    }

    malloc_header_t* hdr = (malloc_header_t*)heap_end;
    hdr->size = size;
    hdr->magic = MALLOC_MAGIC;

    heap_end = new_end;
    return (void*)(hdr + 1);
}

void* calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    if (total == 0) return NULL;
    if (nmemb != 0 && total / nmemb != size) {
        errno = ENOMEM;
        return NULL;
    }
    void* ptr = malloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

void* realloc(void* ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) return NULL;

    malloc_header_t* hdr = ((malloc_header_t*)ptr) - 1;
    size_t old_size = 0;
    if (hdr->magic == MALLOC_MAGIC) old_size = hdr->size;

    void* new_ptr = malloc(size);
    if (!new_ptr) return NULL;

    size_t copy_size = (old_size < size) ? old_size : size;
    if (copy_size > 0) memcpy(new_ptr, ptr, copy_size);
    return new_ptr;
}

void free(void* ptr) {
    (void)ptr;
    // Bump allocator: no-op. Memory leak by design.
}

// ============================================================================
// 5. STRING OPERATIONS
// ============================================================================

size_t strlen(const char* str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

int strncmp(const char* s1, const char* s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) { s1++; s2++; n--; }
    if (n == 0) return 0;
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while ((*d++ = *src++));
    return dest;
}

char* strncpy(char* dest, const char* src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) dest[i] = src[i];
    for (; i < n; i++) dest[i] = '\0';
    return dest;
}

char* strcat(char* dest, const char* src) {
    char* d = dest + strlen(dest);
    while ((*d++ = *src++));
    return dest;
}

char* strncat(char* dest, const char* src, size_t n) {
    char* d = dest + strlen(dest);
    size_t i;
    for (i = 0; i < n && src[i]; i++) d[i] = src[i];
    d[i] = '\0';
    return dest;
}

char* strchr(const char* str, int c) {
    while (*str) {
        if (*str == (char)c) return (char*)str;
        str++;
    }
    return (c == '\0') ? (char*)str : NULL;
}

char* strrchr(const char* str, int c) {
    const char* found = NULL;
    while (*str) {
        if (*str == (char)c) found = str;
        str++;
    }
    if (c == '\0') return (char*)str;
    return (char*)found;
}

char* strstr(const char* haystack, const char* needle) {
    size_t nlen = strlen(needle);
    if (nlen == 0) return (char*)haystack;
    while (*haystack) {
        if (strncmp(haystack, needle, nlen) == 0) return (char*)haystack;
        haystack++;
    }
    return NULL;
}

char* strndup(const char* s, size_t n) {
    if (!s) return NULL;
    size_t len = strlen(s);
    if (n < len) len = n;
    char* new_s = (char*)malloc(len + 1);
    if (new_s) {
        memcpy(new_s, s, len);
        new_s[len] = '\0';
    }
    return new_s;
}

char* strpbrk(const char* s, const char* accept) {
    if (!s || !accept) return NULL;
    while (*s) {
        const char* a = accept;
        while (*a) {
            if (*s == *a) return (char*)s;
            a++;
        }
        s++;
    }
    return NULL;
}

char* strdup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char* dup = malloc(len);
    if (dup) memcpy(dup, s, len);
    return dup;
}

// ============================================================================
// 6. NUMBER CONVERSION
// ============================================================================

void itoa(int value, char* buf, int base) {
    char tmp[33];
    int i = 0, negative = 0;
    if (value == 0) { buf[0] = '0'; buf[1] = '\0'; return; }

    unsigned int uvalue;
    if (value < 0 && base == 10) {
        negative = 1;
        uvalue = 0u - (unsigned int)value;  // UL1/KL1 safe
    } else {
        uvalue = (unsigned int)value;
    }

    while (uvalue > 0) {
        int rem = uvalue % base;
        tmp[i++] = (rem < 10) ? (rem + '0') : (rem - 10 + 'a');
        uvalue /= base;
    }

    int j = 0;
    if (negative) buf[j++] = '-';
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

void uitoa(unsigned int value, char* buf, int base) {
    char tmp[33];
    int i = 0;
    if (value == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    while (value > 0) {
        int rem = value % base;
        tmp[i++] = (rem < 10) ? (rem + '0') : (rem - 10 + 'a');
        value /= base;
    }
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

int atoi(const char* str) {
    if (!str) return 0;
    while (*str == ' ') str++;

    int negative = 0;
    if (*str == '-') { negative = 1; str++; }
    else if (*str == '+') str++;

    unsigned int uresult = 0;
    while (*str >= '0' && *str <= '9') {
        unsigned int digit = (unsigned int)(*str - '0');
        if (uresult > (0xFFFFFFFFu - digit) / 10)
            uresult = 0xFFFFFFFFu;
        else
            uresult = uresult * 10u + digit;
        str++;
    }

    if (negative) {
        if (uresult <= 0x80000000u) return (int)(0u - uresult);
        return (int)0x80000000;
    } else {
        if (uresult <= 0x7FFFFFFFu) return (int)uresult;
        return (int)0x7FFFFFFF;
    }
}

unsigned long strtoul(const char* str, char** endptr, int base) {
    unsigned long result = 0;
    while (*str == ' ') str++;

    if (base == 0 || base == 16) {
        if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
            str += 2; base = 16;
        } else if (base == 0 && str[0] == '0') {
            base = 8;
        }
    }
    if (base == 0) base = 10;

    while (*str) {
        int digit;
        if (*str >= '0' && *str <= '9') digit = *str - '0';
        else if (*str >= 'a' && *str <= 'z') digit = *str - 'a' + 10;
        else if (*str >= 'A' && *str <= 'Z') digit = *str - 'A' + 10;
        else break;
        if (digit >= base) break;
        result = result * base + digit;
        str++;
    }
    if (endptr) *endptr = (char*)str;
    return result;
}

long strtol(const char* str, char** endptr, int base) {
    while (*str == ' ') str++;

    int negative = 0;
    if (*str == '-') { negative = 1; str++; }
    else if (*str == '+') str++;

    unsigned long val = strtoul(str, endptr, base);

    if (negative) {
        if (val <= 0x80000000UL) return (long)(0UL - val);
        return (long)0x80000000;
    } else {
        if (val <= 0x7FFFFFFFUL) return (long)val;
        return (long)0x7FFFFFFF;
    }
}

// ============================================================================
// 6b. 64-BIT NUMBER CONVERSION (strtoll, strtoull)
// ============================================================================

unsigned long long strtoull(const char* str, char** endptr, int base) {
    unsigned long long result = 0;
    while (*str == ' ') str++;

    // Пропускаем '+' (для unsigned '-' тоже парсим как magnitude, C standard)
    if (*str == '+') str++;

    if (base == 0 || base == 16) {
        if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
            str += 2; base = 16;
        } else if (base == 0 && str[0] == '0') {
            base = 8;
        }
    }
    if (base == 0) base = 10;

    while (*str) {
        int digit;
        if (*str >= '0' && *str <= '9') digit = *str - '0';
        else if (*str >= 'a' && *str <= 'z') digit = *str - 'a' + 10;
        else if (*str >= 'A' && *str <= 'Z') digit = *str - 'A' + 10;
        else break;
        if (digit >= base) break;

        // Overflow check: result * base + digit > ULLONG_MAX
        if (result > (ULLONG_MAX - (unsigned long long)digit) / (unsigned long long)base) {
            result = ULLONG_MAX;
            errno = ERANGE;
        } else {
            result = result * (unsigned long long)base + (unsigned long long)digit;
        }
        str++;
    }
    if (endptr) *endptr = (char*)str;
    return result;
}

long long strtoll(const char* str, char** endptr, int base) {
    while (*str == ' ') str++;

    int negative = 0;
    if (*str == '-') { negative = 1; str++; }
    else if (*str == '+') str++;

    unsigned long long val = strtoull(str, endptr, base);

    if (negative) {
        // LLONG_MIN magnitude = 9223372036854775808 = 0x8000000000000000
        if (val <= 0x8000000000000000ULL) return (long long)(0ULL - val);
        errno = ERANGE;
        return LLONG_MIN;
    } else {
        if (val <= (unsigned long long)LLONG_MAX) return (long long)val;
        errno = ERANGE;
        return LLONG_MAX;
    }
}

// ============================================================================
// 7. FLOAT PARSING
// ============================================================================

double strtod(const char* nptr, char** endptr) {
    double result = 0.0, sign = 1.0;
    while (*nptr == ' ') nptr++;
    if (*nptr == '-') { sign = -1.0; nptr++; }
    else if (*nptr == '+') nptr++;

    while (*nptr >= '0' && *nptr <= '9') {
        result = result * 10.0 + (*nptr - '0');
        nptr++;
    }
    if (*nptr == '.') {
        nptr++;
        double frac = 0.1;
        while (*nptr >= '0' && *nptr <= '9') {
            result += (*nptr - '0') * frac;
            frac *= 0.1;
            nptr++;
        }
    }
    if (endptr) *endptr = (char*)nptr;
    return sign * result;
}

float strtof(const char* nptr, char** endptr) {
    return (float)strtod(nptr, endptr);
}

long double strtold(const char* nptr, char** endptr) {
    return (long double)strtod(nptr, endptr);
}

double ldexp(double x, int exp) {
    while (exp > 0) { x *= 2.0; exp--; }
    while (exp < 0) { x /= 2.0; exp++; }
    return x;
}

long double ldexpl(long double x, int exp) {
    while (exp > 0) { x *= 2.0; exp--; }
    while (exp < 0) { x /= 2.0; exp++; }
    return x;
}

// ============================================================================
// 8. FILE* I/O С БУФЕРИЗАЦИЕЙ
// ============================================================================

FILE* fopen(const char* path, const char* mode) {
    int flags = 0;
    if (!mode) { errno = EINVAL; return NULL; }

    if (strcmp(mode, "r") == 0)       flags = O_RDONLY;
    else if (strcmp(mode, "w") == 0)  flags = O_WRONLY | O_CREAT | O_TRUNC;
    else if (strcmp(mode, "a") == 0)  flags = O_WRONLY | O_CREAT | O_APPEND;
    else if (strcmp(mode, "r+") == 0) flags = O_RDWR;
    else if (strcmp(mode, "w+") == 0) flags = O_RDWR | O_CREAT | O_TRUNC;
    else if (strcmp(mode, "a+") == 0) flags = O_RDWR | O_CREAT | O_APPEND;
    else { errno = EINVAL; return NULL; }

    int fd = sys_open(path, flags, 0644);
    if (fd < 0) { errno = -fd; return NULL; }

    FILE* f = (FILE*)malloc(sizeof(FILE));
    if (!f) { sys_close(fd); errno = ENOMEM; return NULL; }

    f->fd = fd;
    f->eof = 0;
    f->error = 0;
    f->buffer_pos = 0;
    f->buffer_len = 0;
    f->write_pos = 0;
    return f;
}

static int fflush_write(FILE* stream) {
    if (stream->write_pos == 0) return 0;
    int ret = sys_write(stream->fd, stream->write_buffer, (uint32_t)stream->write_pos);
    if (ret < 0) {
        stream->error = 1;
        return EOF;
    }
    stream->write_pos = 0;
    return 0;
}

static int fill_read_buffer(FILE* stream) {
    int ret = sys_read(stream->fd, stream->read_buffer, FILE_BUFFER_SIZE);
    if (ret < 0) {
        stream->error = 1;
        stream->buffer_len = 0;
        stream->buffer_pos = 0;
        return -1;
    }
    if (ret == 0) {
        stream->eof = 1;
        stream->buffer_len = 0;
        stream->buffer_pos = 0;
        return 0;
    }
    stream->buffer_len = ret;
    stream->buffer_pos = 0;
    return ret;
}

int fclose(FILE* stream) {
    if (!stream) { errno = EBADF; return EOF; }
    if (stream->write_pos > 0) fflush_write(stream);
    int ret = sys_close(stream->fd);
    free(stream);
    return (ret < 0) ? EOF : 0;
}

int fflush(FILE* stream) {
    if (!stream) return EOF;
    return fflush_write(stream);
}

size_t fread(void* ptr, size_t size, size_t nmemb, FILE* stream) {
    if (!stream || size == 0 || nmemb == 0) return 0;

    size_t total_bytes = size * nmemb;
    uint8_t* dest = (uint8_t*)ptr;
    size_t bytes_read = 0;

    while (bytes_read < total_bytes) {
        if (stream->buffer_pos >= stream->buffer_len) {
            if (fill_read_buffer(stream) <= 0) break;
        }
        size_t available = stream->buffer_len - stream->buffer_pos;
        size_t to_copy = total_bytes - bytes_read;
        if (to_copy > available) to_copy = available;
        memcpy(dest + bytes_read, stream->read_buffer + stream->buffer_pos, to_copy);
        stream->buffer_pos += to_copy;
        bytes_read += to_copy;
    }
    return bytes_read / size;
}

size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream) {
    if (!stream || size == 0 || nmemb == 0) return 0;

    size_t total_bytes = size * nmemb;
    const uint8_t* src = (const uint8_t*)ptr;
    size_t bytes_written = 0;

    while (bytes_written < total_bytes) {
        if (stream->write_pos >= FILE_BUFFER_SIZE) {
            if (fflush_write(stream) == EOF) break;
        }
        size_t available = FILE_BUFFER_SIZE - stream->write_pos;
        size_t to_copy = total_bytes - bytes_written;
        if (to_copy > available) to_copy = available;
        memcpy(stream->write_buffer + stream->write_pos, src + bytes_written, to_copy);
        stream->write_pos += to_copy;
        bytes_written += to_copy;
    }
    return bytes_written / size;
}

int fgetc(FILE* stream) {
    if (!stream) return EOF;
    if (stream->buffer_pos >= stream->buffer_len) {
        if (fill_read_buffer(stream) <= 0) return EOF;
    }
    return (unsigned char)stream->read_buffer[stream->buffer_pos++];
}

int fputc(int c, FILE* stream) {
    if (!stream) return EOF;
    if (stream->write_pos >= FILE_BUFFER_SIZE) {
        if (fflush_write(stream) == EOF) return EOF;
    }
    stream->write_buffer[stream->write_pos++] = (char)c;
    return (unsigned char)c;
}

char* fgets(char* s, int size, FILE* stream) {
    if (!s || size <= 0 || !stream) return NULL;
    int i = 0, c;
    while (i < size - 1) {
        c = fgetc(stream);
        if (c == EOF) {
            if (i == 0) return NULL;
            break;
        }
        s[i++] = (char)c;
        if (c == '\n') break;
    }
    s[i] = '\0';
    return s;
}

int fputs(const char* s, FILE* stream) {
    if (!s || !stream) return EOF;
    size_t len = strlen(s);
    return fwrite(s, 1, len, stream) == len ? 0 : EOF;
}

int fseek(FILE* stream, long offset, int whence) {
    if (!stream) return -1;
    fflush_write(stream);
    stream->buffer_pos = 0;
    stream->buffer_len = 0;
    off_t new_pos = lseek(stream->fd, (off_t)offset, whence);
    if (new_pos < 0) return -1;
    stream->eof = 0;
    return 0;
}

long ftell(FILE* stream) {
    if (!stream) return -1L;
    fflush_write(stream);
    off_t pos = lseek(stream->fd, 0, SEEK_CUR);
    if (stream->buffer_len > 0)
        pos -= (stream->buffer_len - stream->buffer_pos);
    return (long)pos;
}

FILE* fdopen(int fd, const char* mode) {
    (void)mode;
    if (fd < 0) { errno = EINVAL; return NULL; }
    FILE* f = (FILE*)malloc(sizeof(FILE));
    if (!f) { errno = ENOMEM; return NULL; }
    f->fd = fd;
    f->eof = 0;
    f->error = 0;
    f->buffer_pos = 0;
    f->buffer_len = 0;
    f->write_pos = 0;
    return f;
}

FILE* freopen(const char* path, const char* mode, FILE* stream) {
    if (stream) fclose(stream);
    return fopen(path, mode);
}

int remove(const char* pathname) {
    return unlink(pathname);
}

int ferror(FILE* stream) {
    return stream ? stream->error : 0;
}

int feof(FILE* stream) {
    return stream ? stream->eof : 0;
}

// ============================================================================
// 9. LOW-LEVEL I/O
// ============================================================================

int open(const char* pathname, int flags, ...) {
    int mode = 0644;
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, int);
        va_end(args);
    }
    int fd = sys_open(pathname, flags, mode);
    if (fd < 0) errno = -fd;
    return fd;
}

int close(int fd) {
    int ret = sys_close(fd);
    if (ret < 0) errno = -ret;
    return ret;
}

ssize_t read(int fd, void* buf, size_t count) {
    int ret = sys_read(fd, buf, (uint32_t)count);
    if (ret < 0) { errno = -ret; return -1; }
    return (ssize_t)ret;
}

ssize_t write(int fd, const void* buf, size_t count) {
    int ret = sys_write(fd, buf, (uint32_t)count);
    if (ret < 0) { errno = -ret; return -1; }
    return (ssize_t)ret;
}

off_t lseek(int fd, off_t offset, int whence) {
    int ret = sys_lseek(fd, (int)offset, whence);
    if (ret < 0) { errno = -ret; return -1; }
    return (off_t)ret;
}

int mkdir(const char* pathname, mode_t mode) {
    int ret = sys_mkdir(pathname, (uint32_t)mode);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int unlink(const char* pathname) {
    int ret = sys_unlink(pathname);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int dup(int oldfd) {
    int ret = sys_dup(oldfd);
    if (ret < 0) { errno = -ret; return -1; }
    return ret;
}

int dup2(int oldfd, int newfd) {
    int ret = sys_dup2(oldfd, newfd);
    if (ret < 0) { errno = -ret; return -1; }
    return ret;
}

int fstat(int fd, struct stat* buf) {
    int ret = sys_fstat(fd, buf);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

int isatty(int fd) {
    return (fd == STDIN_FILENO || fd == STDOUT_FILENO || fd == STDERR_FILENO) ? 1 : 0;
}

char* getcwd(char* buf, size_t size) {
    if (!buf || size < 2) { errno = EINVAL; return NULL; }
    buf[0] = '/';
    buf[1] = '\0';
    return buf;
}

int chdir(const char* path) {
    (void)path;
    errno = ENOSYS;
    return -1;
}

char* realpath(const char* path, char* resolved_path) {
    if (!path) { errno = EINVAL; return NULL; }
    char* dest = resolved_path;
    if (!dest) dest = (char*)malloc(strlen(path) + 1);
    if (!dest) { errno = ENOMEM; return NULL; }
    strcpy(dest, path);
    return dest;
}

// ============================================================================
// 10. PRINTF FAMILY
// ============================================================================

static void put_int(char** buf, char* end, int value, int base, int width, int pad_zero, int is_signed) {
    char tmp[33];
    int len = 0, negative = 0;
    unsigned int uval;

    if (is_signed && value < 0) {
        negative = 1;
        uval = 0u - (unsigned int)value;  // UL1/KL1 safe
    } else {
        uval = (unsigned int)value;
    }

    if (uval == 0) {
        tmp[len++] = '0';
    } else {
        while (uval > 0) {
            int rem = uval % base;
            tmp[len++] = (rem < 10) ? (rem + '0') : (rem - 10 + 'a');
            uval /= base;
        }
    }

    int pad = width - len;
    char pad_char = pad_zero ? '0' : ' ';

    if (negative && *buf < end) *(*buf)++ = '-';
    while (pad-- > 0 && *buf < end) *(*buf)++ = pad_char;
    while (len > 0 && *buf < end) *(*buf)++ = tmp[--len];
}

static void put_uint(char** buf, char* end, unsigned int value, int base, int width, int pad_zero) {
    char tmp[33];
    int len = 0;

    if (value == 0) {
        tmp[len++] = '0';
    } else {
        while (value > 0) {
            int rem = value % base;
            tmp[len++] = (rem < 10) ? (rem + '0') : (rem - 10 + 'a');
            value /= base;
        }
    }

    int pad = width - len;
    char pad_char = pad_zero ? '0' : ' ';
    while (pad-- > 0 && *buf < end) *(*buf)++ = pad_char;
    while (len > 0 && *buf < end) *(*buf)++ = tmp[--len];
}

int vsnprintf(char* buf, size_t size, const char* fmt, va_list args) {
    if (size == 0) return 0;
    char* start = buf;
    char* end = buf + size - 1;

    while (*fmt && buf < end) {
        if (*fmt != '%') { *buf++ = *fmt++; continue; }
        fmt++;

        if (*fmt == '\0') break;

        int pad_zero = 0, left_align = 0;
        while (*fmt == '0' || *fmt == '-') {
            if (*fmt == '0') pad_zero = 1;
            if (*fmt == '-') left_align = 1;
            fmt++;
        }

        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        if (*fmt == '.') {
            fmt++;
            while (*fmt >= '0' && *fmt <= '9') fmt++;
        }

        if (*fmt == 'l') { fmt++; if (*fmt == 'l') fmt++; }
        else if (*fmt == 'h') { fmt++; if (*fmt == 'h') fmt++; }
        else if (*fmt == 'z') { fmt++; }

        if (*fmt == '\0') break;

        switch (*fmt) {
            case 'd': case 'i':
                put_int(&buf, end, va_arg(args, int), 10, width, pad_zero, 1);
                break;
            case 'u':
                put_uint(&buf, end, va_arg(args, unsigned int), 10, width, pad_zero);
                break;
            case 'x': case 'X':
                put_uint(&buf, end, va_arg(args, unsigned int), 16, width, pad_zero);
                break;
            case 'p': {
                void* p = va_arg(args, void*);
                if (buf < end) *buf++ = '0';
                if (buf < end) *buf++ = 'x';
                put_uint(&buf, end, (unsigned int)(uintptr_t)p, 16, 8, 1);
                break;
            }
            case 's': {
                const char* str = va_arg(args, const char*);
                if (!str) str = "(null)";
                int slen = strlen(str);
                int pad = width - slen;
                if (!left_align)
                    while (pad-- > 0 && buf < end) *buf++ = ' ';
                while (*str && buf < end) *buf++ = *str++;
                if (left_align)
                    while (pad-- > 0 && buf < end) *buf++ = ' ';
                break;
            }
            case 'c':
                if (buf < end) *buf++ = (char)va_arg(args, int);
                break;
            case '%':
                if (buf < end) *buf++ = '%';
                break;
            default:
                if (buf < end) *buf++ = '%';
                if (*fmt && buf < end) *buf++ = *fmt;
                break;
        }
        fmt++;
    }

    *buf = '\0';
    return (int)(buf - start);
}

int vsprintf(char* buf, const char* fmt, va_list args) {
    return vsnprintf(buf, (size_t)-1, fmt, args);
}

int snprintf(char* buf, size_t size, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vsnprintf(buf, size, fmt, args);
    va_end(args);
    return ret;
}

int sprintf(char* buf, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vsprintf(buf, fmt, args);
    va_end(args);
    return ret;
}

int vfprintf(FILE* stream, const char* fmt, va_list args) {
    char buf[1024];
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    if (len <= 0) return len;
    if ((size_t)len >= sizeof(buf)) len = sizeof(buf) - 1;
    return write(stream->fd, buf, (size_t)len);
}

int fprintf(FILE* stream, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vfprintf(stream, fmt, args);
    va_end(args);
    return ret;
}

int vprintf(const char* fmt, va_list args) {
    return vfprintf(stdout, fmt, args);
}

int printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vfprintf(stdout, fmt, args);
    va_end(args);
    return ret;
}

// ============================================================================
// 11. PROCESS CONTROL
// ============================================================================

static void (*atexit_funcs[32])(void);
static int atexit_count = 0;

int atexit(void (*func)(void)) {
    if (atexit_count >= 32) return -1;
    atexit_funcs[atexit_count++] = func;
    return 0;
}

void __run_atexit_handlers(void) {
    while (atexit_count > 0) {
        atexit_count--;
        if (atexit_funcs[atexit_count])
            atexit_funcs[atexit_count]();
    }
}

void exit(int status) {
    fflush(stdout);
    fflush(stderr);
    __run_atexit_handlers();
    sys_exit(status);
    __builtin_unreachable();
}

void abort(void) {
    sys_exit(134);
    __builtin_unreachable();
}

int getpid(void) {
    return sys_getpid();
}

int fork(void) {
    return sys_fork();
}

int waitpid(int pid, int* status, int options) {
    return sys_waitpid(pid, status, options);
}

int exec(const char* path, const char** argv) {
    int ret = sys_exec(path, argv);
    if (ret < 0) { errno = -ret; return -1; }
    return ret;
}

int execvp(const char* file, char* const argv[]) {
    char path[256];
    if (file[0] == '/') {
        strncpy(path, file, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    } else {
        snprintf(path, sizeof(path), "/bin/%s", file);
    }
    return exec(path, (const char**)argv);
}

int system(const char* command) {
    if (!command) return 0;

    char buf[256];
    strncpy(buf, command, 255);
    buf[255] = '\0';

    char* argv[32];
    int argc = 0;
    char* ptr = buf;

    while (*ptr && argc < 31) {
        while (*ptr == ' ') ptr++;
        if (*ptr == '\0') break;
        argv[argc++] = ptr;
        while (*ptr && *ptr != ' ') ptr++;
        if (*ptr) *ptr++ = '\0';
    }
    argv[argc] = NULL;

    if (argc == 0) return -1;

    char path[256];
    snprintf(path, sizeof(path), "/bin/%s", argv[0]);

    pid_t pid = fork();
    if (pid == 0) {
        exec(path, (const char**)argv);
        exit(127);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        return status;
    }
    return -1;
}

// ============================================================================
// 12. SORT / SEARCH (Heapsort — O(1) stack, safe for 64 KB Ring 3 stack)
// ============================================================================

static void swap_elements(uint8_t* a, uint8_t* b, size_t size) {
    for (size_t i = 0; i < size; i++) {
        uint8_t tmp = a[i]; a[i] = b[i]; b[i] = tmp;
    }
}

static void sift_down(uint8_t* arr, size_t size, int (*cmp)(const void*, const void*), int start, int end) {
    int root = start;
    while (root * 2 + 1 <= end) {
        int child = root * 2 + 1;
        int swap = root;
        if (cmp(arr + swap * size, arr + child * size) < 0) swap = child;
        if (child + 1 <= end && cmp(arr + swap * size, arr + (child + 1) * size) < 0) swap = child + 1;
        if (swap == root) return;
        swap_elements(arr + root * size, arr + swap * size, size);
        root = swap;
    }
}

void qsort(void* base, size_t nmemb, size_t size, int (*cmp)(const void*, const void*)) {
    if (nmemb < 2 || size == 0) return;
    uint8_t* arr = (uint8_t*)base;

    for (int start = (int)((nmemb - 2) / 2); start >= 0; start--)
        sift_down(arr, size, cmp, start, (int)(nmemb - 1));

    for (int end = (int)(nmemb - 1); end > 0; end--) {
        swap_elements(arr, arr + end * size, size);
        sift_down(arr, size, cmp, 0, end - 1);
    }
}

void* bsearch(const void* key, const void* base, size_t nmemb, size_t size,
              int (*cmp)(const void*, const void*)) {
    const uint8_t* arr = (const uint8_t*)base;
    size_t low = 0, high = nmemb;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        int res = cmp(key, arr + mid * size);
        if (res == 0) return (void*)(arr + mid * size);
        if (res > 0) low = mid + 1;
        else high = mid;
    }
    return NULL;
}

// ============================================================================
// 13. TIME & SYSTEM
// ============================================================================

int gettimeofday(timeval_t* tv, void* tz) {
    (void)tz;
    int ret = sys_gettimeofday(tv);
    if (ret < 0) errno = -ret;
    return ret;
}

int uname(utsname_t* buf) {
    int ret = sys_uname(buf);
    if (ret < 0) errno = -ret;
    return ret;
}

int sysinfo(sysinfo_t* info) {
    int ret = sys_sysinfo(info);
    if (ret < 0) errno = -ret;
    return ret;
}

unsigned int sleep(unsigned int seconds) {
    sys_sleep(seconds * 1000);
    return 0;
}

int usleep(unsigned int usec) {
    sys_sleep(usec / 1000);
    return 0;
}

clock_t clock(void) {
    timeval_t tv;
    gettimeofday(&tv, NULL);
    return (clock_t)(tv.tv_sec * CLOCKS_PER_SEC + tv.tv_usec);
}

time_t time(time_t* tloc) {
    timeval_t tv;
    int ret = gettimeofday(&tv, NULL);
    if (ret < 0) return (time_t)-1;
    if (tloc) *tloc = (time_t)tv.tv_sec;
    return (time_t)tv.tv_sec;
}

struct tm* localtime(const time_t* timep) {
    static struct tm t;
    (void)timep;
    memset(&t, 0, sizeof(t));
    t.tm_mday = 1;
    return &t;
}

// ============================================================================
// 14. SIGNALS & DYNAMIC LINKING STUBS
// ============================================================================

sighandler_t signal(int signum, sighandler_t handler) {
    (void)signum; (void)handler;
    return SIG_DFL;
}

void* dlopen(const char* filename, int flag) {
    (void)filename; (void)flag;
    errno = ENOSYS;
    return NULL;
}

void* dlsym(void* handle, const char* symbol) {
    (void)handle; (void)symbol;
    errno = ENOSYS;
    return NULL;
}

int dlclose(void* handle) {
    (void)handle;
    errno = ENOSYS;
    return -1;
}

char* dlerror(void) {
    return (char*)"Dynamic linking not supported";
}

// ============================================================================
// 15. POSIX EXTENSIONS
// ============================================================================

ssize_t getline(char** lineptr, size_t* n, FILE* stream) {
    if (!lineptr || !n || !stream) { errno = EINVAL; return -1; }

    if (*lineptr == NULL) {
        *n = 128;
        *lineptr = malloc(*n);
        if (!*lineptr) { errno = ENOMEM; return -1; }
    }

    size_t pos = 0;
    int c;
    while ((c = fgetc(stream)) != EOF) {
        if (pos + 1 >= *n) {
            *n *= 2;
            char* new_ptr = realloc(*lineptr, *n);
            if (!new_ptr) { errno = ENOMEM; return -1; }
            *lineptr = new_ptr;
        }
        (*lineptr)[pos++] = (char)c;
        if (c == '\n') break;
    }

    (*lineptr)[pos] = '\0';
    if (c == EOF && pos == 0) return -1;
    return (ssize_t)pos;
}

char* strerror(int errnum) {
    switch (errnum) {
        case 0:        return "Success";
        case EPERM:    return "Operation not permitted";
        case ENOENT:   return "No such file or directory";
        case ESRCH:    return "No such process";
        case EINTR:    return "Interrupted system call";
        case EIO:      return "I/O error";
        case ENXIO:    return "No such device or address";
        case E2BIG:    return "Argument list too long";
        case ENOEXEC:  return "Exec format error";
        case EBADF:    return "Bad file descriptor";
        case ECHILD:   return "No child processes";
        case EAGAIN:   return "Try again";
        case ENOMEM:   return "Out of memory";
        case EACCES:   return "Permission denied";
        case EFAULT:   return "Bad address";
        case ENOTBLK:  return "Block device required";
        case EBUSY:    return "Device or resource busy";
        case EEXIST:   return "File exists";
        case EXDEV:    return "Cross-device link";
        case ENODEV:   return "No such device";
        case ENOTDIR:  return "Not a directory";
        case EISDIR:   return "Is a directory";
        case EINVAL:   return "Invalid argument";
        case ENFILE:   return "File table overflow";
        case EMFILE:   return "Too many open files";
        case ENOTTY:   return "Not a typewriter";
        case EFBIG:    return "File too large";
        case ENOSPC:   return "No space left on device";
        case ESPIPE:   return "Illegal seek";
        case EROFS:    return "Read-only file system";
        case EMLINK:   return "Too many links";
        case EPIPE:    return "Broken pipe";
        case EDOM:     return "Math argument out of domain";
        case ERANGE:   return "Math result not representable";
        case ENOSYS:   return "Function not implemented";
        default:       return "Unknown error";
    }
}

void perror(const char* s) {
    if (s && *s)
        fprintf(stderr, "%s: %s\n", s, strerror(errno));
    else
        fprintf(stderr, "%s\n", strerror(errno));
}

int ioctl(int fd, unsigned long request, ...) {
    va_list args;
    va_start(args, request);
    void* argp = va_arg(args, void*);
    va_end(args);
    int ret = sys_ioctl(fd, (uint32_t)request, argp);
    if (ret < 0) { errno = -ret; return -1; }
    return ret;
}

long sysconf(int name) {
    if (name == _SC_PAGESIZE) return 4096;
    return -1;
}

int mprotect(void* addr, size_t len, int prot) {
    int ret = sys_mprotect(addr, len, prot);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset) {
    void* ret = sys_mmap(addr, length, prot, flags, fd, offset);
    intptr_t err = (intptr_t)ret;
    if (err < 0 && err > -4096) {
        errno = (int)-err;
        return MAP_FAILED;
    }
    return ret;
}

int munmap(void* addr, size_t length) {
    int ret = sys_munmap(addr, length);
    if (ret < 0) { errno = -ret; return -1; }
    return 0;
}

// ============================================================================
// 16. GLIBC COMPATIBILITY
// ============================================================================

int* __errno_location(void) {
    return &errno;
}

long __isoc23_strtol(const char* nptr, char** endptr, int base) {
    return strtol(nptr, endptr, base);
}

unsigned long __isoc23_strtoul(const char* nptr, char** endptr, int base) {
    return strtoul(nptr, endptr, base);
}

long long __isoc23_strtoll(const char* nptr, char** endptr, int base) {
    return strtoll(nptr, endptr, base);  // FIXED: было strtoul (баг C2)
}

unsigned long long __isoc23_strtoull(const char* nptr, char** endptr, int base) {
    return strtoull(nptr, endptr, base);  // FIXED: было strtoul (баг C3)
}

void __assert_fail(const char* assertion, const char* file,
                   unsigned int line, const char* function) {
    fprintf(stderr, "Assertion failed: %s, file %s, line %u, function %s\n",
            assertion, file, line, function);
    abort();
}

int _setjmp(jmp_buf env) {
    return setjmp(env);
}

// ============================================================================
// 17. ENVIRONMENT
// ============================================================================

char** environ = NULL;

char* getenv(const char* name) {
    (void)name;
    return NULL;
}
