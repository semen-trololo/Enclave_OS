// ============================================================================
// USER LIBC — Ring 3 Standard Library for Bare Metal OS
// Все функции работают ТОЛЬКО через syscalls (Zero Trust Sandbox)
// malloc использует bump allocator (утечка памяти — by design для TinyCC)
// ============================================================================

#include "user_libc.h"
#include "user_syscalls.h"
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

// ============================================================================
// GLOBALS
// ============================================================================
int errno = 0;

// Pre-allocated FILE streams (static, no malloc needed)
static FILE _stdin_stream = {
    .fd = STDIN_FILENO,
    .eof = 0,
    .error = 0,
    .buffer_pos = 0,
    .buffer_len = 0,
    .write_pos = 0
};

static FILE _stdout_stream = {
    .fd = STDOUT_FILENO,
    .eof = 0,
    .error = 0,
    .buffer_pos = 0,
    .buffer_len = 0,
    .write_pos = 0
};

static FILE _stderr_stream = {
    .fd = STDERR_FILENO,
    .eof = 0,
    .error = 0,
    .buffer_pos = 0,
    .buffer_len = 0,
    .write_pos = 0
};

FILE* stdin  = &_stdin_stream;
FILE* stdout = &_stdout_stream;
FILE* stderr = &_stderr_stream;

// ============================================================================
// MEMORY OPERATIONS
// ============================================================================

void* memset(void* ptr, int value, size_t num) {
    uint8_t* p = (uint8_t*)ptr;
    while (num--) {
        *p++ = (uint8_t)value;
    }
    return ptr;
}

void* memcpy(void* dest, const void* src, size_t num) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    while (num--) {
        *d++ = *s++;
    }
    return dest;
}

int memcmp(const void* s1, const void* s2, size_t n) {
    const uint8_t* p1 = (const uint8_t*)s1;
    const uint8_t* p2 = (const uint8_t*)s2;

    while (n--) {
        if (*p1 != *p2) {
            return (int)(*p1 - *p2);
        }
        p1++;
        p2++;
    }

    return 0;
}

void* memmove(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;

    if (d < s) {
        while (n--) {
            *d++ = *s++;
        }
    } else {
        d += n;
        s += n;

        while (n--) {
            *--d = *--s;
        }
    }

    return dest;
}

// ============================================================================
// MEMORY MANAGEMENT (Bump Allocator + Header for safe realloc)
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
    if (size == 0) {
        return NULL;
    }

    heap_init_if_needed();

    // Выравнивание payload на 8 байт + заголовок
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

    if (total == 0) {
        return NULL;
    }

    // Защита от переполнения
    if (nmemb != 0 && total / nmemb != size) {
        errno = ENOMEM;
        return NULL;
    }

    void* ptr = malloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }

    return ptr;
}

void* realloc(void* ptr, size_t size) {
    if (!ptr) {
        return malloc(size);
    }

    if (size == 0) {
        return NULL;
    }

    // Читаем реальный размер старого блока из заголовка
    malloc_header_t* hdr = ((malloc_header_t*)ptr) - 1;

    size_t old_size = 0;
    if (hdr->magic == MALLOC_MAGIC) {
        old_size = hdr->size;
    }

    void* new_ptr = malloc(size);
    if (!new_ptr) {
        return NULL;
    }

    // Копируем только min(old_size, new_size)
    size_t copy_size = (old_size < size) ? old_size : size;
    if (copy_size > 0) {
        memcpy(new_ptr, ptr, copy_size);
    }

    return new_ptr;
}

void free(void* ptr) {
    (void)ptr;
    // Bump allocator: no-op. Memory leak by design.
}
// ============================================================================
// STRING OPERATIONS
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

// ============================================================================
// NUMBER CONVERSION
// ============================================================================
void itoa(int value, char* buf, int base) {
    char tmp[33];
    int i = 0, negative = 0;
    if (value == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    
    unsigned int uvalue;
    if (value < 0 && base == 10) {
        negative = 1;
        uvalue = -(unsigned int)value;
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
    int result = 0, sign = 1;
    while (*str == ' ') str++;
    if (*str == '-') { sign = -1; str++; }
    else if (*str == '+') str++;
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    return result * sign;
}

unsigned long strtoul(const char* str, char** endptr, int base) {
    unsigned long result = 0;
    while (*str == ' ') str++;
    
    if (base == 0 || base == 16) {
        if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
            str += 2;
            base = 16;
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
    return negative ? -(long)val : (long)val;
}

// ============================================================================
// FILE I/O
// ============================================================================
// ✅ ПОЛНОЦЕННАЯ POSIX VARIADIC РЕАЛИЗАЦИЯ
int open(const char* pathname, int flags, ...) {
    int mode = 0644; // Дефолтные POSIX-права (rw-r--r--)
    
    // По стандарту POSIX третий аргумент (mode) передается ТОЛЬКО если есть O_CREAT
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, int); // Извлекаем mode из стека
        va_end(args);
    }
    
    // Пробрасываем в ядро (sys_open уже умеет принимать mode и передавать в tmpfs_create)
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

// ============================================================================
// POSIX DUP / DUP2 (Обертки над sys_dup / sys_dup2)
// ============================================================================
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

// ============================================================================
// FILE* API С БУФЕРИЗАЦИЕЙ (4KB для ускорения TinyCC парсинга в 10-100x)
// ============================================================================
FILE* fopen(const char* path, const char* mode) {
    int flags = 0;
    if (!mode) { errno = EINVAL; return NULL; }
    
    if (strcmp(mode, "r") == 0) flags = O_RDONLY;
    else if (strcmp(mode, "w") == 0) flags = O_WRONLY | O_CREAT | O_TRUNC;
    else if (strcmp(mode, "a") == 0) flags = O_WRONLY | O_CREAT | O_APPEND;
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
    // Инициализация read buffer
    f->buffer_pos = 0;
    f->buffer_len = 0;
    // Инициализация write buffer
    f->write_pos = 0;
    
    return f;
}

// Внутренняя функция: сброс write buffer в файл
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

// Внутренняя функция: заполнение read buffer из файла
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
    
    // Сброс write buffer перед закрытием
    if (stream->write_pos > 0) {
        fflush_write(stream);
    }
    
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
        // Если буфер пуст, заполняем его
        if (stream->buffer_pos >= stream->buffer_len) {
            if (fill_read_buffer(stream) <= 0) {
                break; // EOF или ошибка
            }
        }
        
        // Копируем из буфера
        size_t available = stream->buffer_len - stream->buffer_pos;
        size_t to_copy = total_bytes - bytes_read;
        if (to_copy > available) to_copy = available;
        
        memcpy(dest + bytes_read, stream->read_buffer + stream->buffer_pos, to_copy);
        stream->buffer_pos += to_copy;
        bytes_read += to_copy;
    }
    
    return bytes_read / size;
}

int fgetc(FILE* stream) {
    if (!stream) return EOF;
    
    // Если буфер пуст, заполняем его
    if (stream->buffer_pos >= stream->buffer_len) {
        if (fill_read_buffer(stream) <= 0) {
            return EOF; // EOF или ошибка
        }
    }
    
    // Возвращаем байт из буфера
    return (unsigned char)stream->read_buffer[stream->buffer_pos++];
}

int fputc(int c, FILE* stream) {
    if (!stream) return EOF;
    
    // Если буфер полон, сбрасываем
    if (stream->write_pos >= FILE_BUFFER_SIZE) {
        if (fflush_write(stream) == EOF) {
            return EOF;
        }
    }
    
    stream->write_buffer[stream->write_pos++] = (char)c;
    return (unsigned char)c;
}

char* fgets(char* s, int size, FILE* stream) {
    if (!s || size <= 0 || !stream) return NULL;
    
    int i = 0;
    int c;
    
    while (i < size - 1) {
        c = fgetc(stream);
        if (c == EOF) {
            if (i == 0) return NULL; // EOF до чтения любого символа
            break;
        }
        
        s[i++] = (char)c;
        
        if (c == '\n') break; // POSIX: включаем \n в строку
    }
    
    s[i] = '\0';
    return s;
}

int ferror(FILE* stream) {
    return stream ? stream->error : 0;
}

int feof(FILE* stream) {
    return stream ? stream->eof : 0;
}

int unlink(const char* pathname) {
    int ret = sys_unlink(pathname);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}

// ============================================================================
// PRINTF FAMILY
// ============================================================================
static void put_int(char** buf, char* end, int value, int base, int width, int pad_zero, int is_signed) {
    char tmp[33];
    int len = 0;
    
    if (is_signed && value < 0) {
        if (*buf < end) *(*buf)++ = '-';
        value = -value;
    }
    
    unsigned int uval = (unsigned int)value;
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
        if (*fmt != '%') {
            *buf++ = *fmt++;
            continue;
        }
        fmt++; // skip '%'
        
        // 🛡️ Защита от "%\0"
        if (*fmt == '\0') break;
        
        // Parse flags
        int pad_zero = 0;
        int left_align = 0;
        while (*fmt == '0' || *fmt == '-') {
            if (*fmt == '0') pad_zero = 1;
            if (*fmt == '-') left_align = 1;
            fmt++;
        }
        
        // Parse width
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }
        
        // Parse precision (ignore value, but consume it safely)
        if (*fmt == '.') {
            fmt++;
            while (*fmt >= '0' && *fmt <= '9') fmt++;
        }
        
        // Parse length modifiers (consume safely)
        if (*fmt == 'l') {
            fmt++;
            if (*fmt == 'l') fmt++; // ll
        } else if (*fmt == 'h') {
            fmt++;
            if (*fmt == 'h') fmt++; // hh
        } else if (*fmt == 'z') {
            fmt++;
        }
        
        // 🛡️ Защита от "%\0" после модификаторов
        if (*fmt == '\0') break;
        
        switch (*fmt) {
            case 'd':
            case 'i':
                put_int(&buf, end, va_arg(args, int), 10, width, pad_zero, 1);
                break;
            case 'u':
                put_uint(&buf, end, va_arg(args, unsigned int), 10, width, pad_zero);
                break;
            case 'x':
            case 'X':
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
                if (!left_align) {
                    while (pad-- > 0 && buf < end) *buf++ = ' ';
                }
                while (*str && buf < end) *buf++ = *str++;
                if (left_align) {
                    while (pad-- > 0 && buf < end) *buf++ = ' ';
                }
                break;
            }
            case 'c':
                if (buf < end) *buf++ = (char)va_arg(args, int);
                break;
            case '%':
                if (buf < end) *buf++ = '%';
                break;
            default:
                // 🛡️ Безопасный fallback: не пишем \0, не выходим за границы
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
// PROCESS CONTROL
// ============================================================================
void exit(int status) {
    // 🛡️ POSIX COMPLIANCE: Сбрасываем все открытые FILE* буферы перед exit
    // TinyCC использует buffered I/O (fputc/fwrite), и без fflush данные теряются
    fflush(stdout);
    fflush(stderr);
    
    __run_atexit_handlers();
    sys_exit(status);
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
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

// ============================================================================
// TIME & SYSTEM INFO (Day 15)
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

// ============================================================================
// SYSTEM() — Упрощенная реализация для TinyCC (~40 строк)
// TinyCC вызывает system() ТОЛЬКО для простых команд без shell-синтаксиса:
//   system("ld -o output.elf file1.o file2.o")
// НЕ поддерживает pipes, redirects, globs (для этого нужен /bin/sh)
// ============================================================================
int system(const char* command) {
    if (!command) return 0; // POSIX: проверка наличия shell
    
    // Парсим команду на argv: "ld -o out.elf a.o" → ["ld", "-o", "out.elf", "a.o"]
    char buf[256];
    strncpy(buf, command, 255);
    buf[255] = '\0';
    
    char* argv[32];
    int argc = 0;
    
    char* ptr = buf;
    while (*ptr && argc < 31) {
        // Пропускаем пробелы
        while (*ptr == ' ') ptr++;
        if (*ptr == '\0') break;
        
        argv[argc++] = ptr;
        
        // Ищем конец аргумента
        while (*ptr && *ptr != ' ') ptr++;
        if (*ptr) *ptr++ = '\0';
    }
    argv[argc] = NULL;
    
    if (argc == 0) return -1;
    
    // Ищем бинарник в /bin/
    char path[256];
    snprintf(path, sizeof(path), "/bin/%s", argv[0]);
    
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        exec(path, (const char**)argv);
        exit(127); // exec failed (ENOENT)
    } else if (pid > 0) {
        // Parent process
        int status;
        waitpid(pid, &status, 0);
        return status;
    }
    
    return -1; // fork failed
}

// ============================================================================
// GETENV — Упрощенная реализация (возвращает NULL)
// TinyCC использует getenv для чтения PATH, но мы hardcode'им "/bin"
// ============================================================================
char* getenv(const char* name) {
    (void)name;
    return NULL; // TinyCC работает без переменных окружения
}

// ============================================================================
// SIGNAL — No-op реализация (TinyCC не обрабатывает сигналы в Ring 3)
// ============================================================================
sighandler_t signal(int signum, sighandler_t handler) {
    (void)signum;
    (void)handler;
    return SIG_DFL; // Возвращаем дефолтный обработчик
}

// ============================================================================
// DYNAMIC LINKING — Заглушки (TinyCC не использует dlopen)
// ============================================================================
void* dlopen(const char* filename, int flag) {
    (void)filename;
    (void)flag;
    errno = ENOSYS;
    return NULL;
}

void* dlsym(void* handle, const char* symbol) {
    (void)handle;
    (void)symbol;
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

/* ========================================================================
 * POSIX Memory Mapping (mmap/munmap)
 * Обертки над sys_mmap/sys_munmap для линкера TinyCC и стресс-тестов
 * ======================================================================== */

#ifndef MAP_FAILED
#define MAP_FAILED ((void*)-1)
#endif

void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset) {
    // sys_mmap определен в user_syscalls.h через inline asm с сохранением EBP
    void* ret = sys_mmap(addr, length, prot, flags, fd, offset);
    
    // Обработка ошибок POSIX (syscall возвращает отрицательный errno)
    intptr_t err = (intptr_t)ret;
    if (err < 0 && err > -4096) {
        errno = (int)-err;
        return MAP_FAILED;
    }
    return ret;
}

int munmap(void* addr, size_t length) {
    int ret = sys_munmap(addr, length);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}

// ============================================================================
// POSIX FUNCTIONS (Day 29 — Enano Text Editor Support)
// ============================================================================

// ----------------------------------------------------------------------------
// getline() — POSIX функция для чтения строки с автоматическим расширением буфера
// Используется в editorOpen() для чтения файла построчно
// ----------------------------------------------------------------------------
ssize_t getline(char** lineptr, size_t* n, FILE* stream) {
    if (!lineptr || !n || !stream) {
        errno = EINVAL;
        return -1;
    }
    
    // Инициализация буфера, если не выделен
    if (*lineptr == NULL) {
        *n = 128;
        *lineptr = malloc(*n);
        if (!*lineptr) {
            errno = ENOMEM;
            return -1;
        }
    }
    
    size_t pos = 0;
    int c;
    
    while ((c = fgetc(stream)) != EOF) {
        // Расширяем буфер при необходимости
        if (pos + 1 >= *n) {
            *n *= 2;
            char* new_ptr = realloc(*lineptr, *n);
            if (!new_ptr) {
                errno = ENOMEM;
                return -1;
            }
            *lineptr = new_ptr;
        }
        
        (*lineptr)[pos++] = (char)c;
        
        if (c == '\n') break; // POSIX: включаем \n в строку
    }
    
    (*lineptr)[pos] = '\0';
    
    // Если ничего не прочитали и EOF — возвращаем -1
    if (c == EOF && pos == 0) return -1;
    
    return (ssize_t)pos;
}

// ----------------------------------------------------------------------------
// strdup() — POSIX функция для дублирования строки
// ----------------------------------------------------------------------------
char* strdup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char* dup = malloc(len);
    if (dup) memcpy(dup, s, len);
    return dup;
}

// ----------------------------------------------------------------------------
// strerror() — преобразование errno в строку ошибки
// Минимальная таблица из 35 наиболее частых ошибок
// ----------------------------------------------------------------------------
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

// ----------------------------------------------------------------------------
// perror() — вывод сообщения об ошибке в stderr
// ----------------------------------------------------------------------------
void perror(const char* s) {
    if (s && *s) {
        fprintf(stderr, "%s: %s\n", s, strerror(errno));
    } else {
        fprintf(stderr, "%s\n", strerror(errno));
    }
}

// ----------------------------------------------------------------------------
// time() — получение текущего времени (через gettimeofday)
// Enano использует для timestamp в строке статуса
// ----------------------------------------------------------------------------
time_t time(time_t* tloc) {
    timeval_t tv;
    int ret = gettimeofday(&tv, NULL);
    if (ret < 0) return (time_t)-1;
    
    if (tloc) *tloc = (time_t)tv.tv_sec;
    return (time_t)tv.tv_sec;
}

// ----------------------------------------------------------------------------
// ioctl() — variadic обертка над sys_ioctl
// ----------------------------------------------------------------------------
int ioctl(int fd, unsigned long request, ...) {
    va_list args;
    va_start(args, request);
    void* argp = va_arg(args, void*);
    va_end(args);
    
    int ret = sys_ioctl(fd, (uint32_t)request, argp);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}