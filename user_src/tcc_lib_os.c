// ============================================================================
// TCC LIB OS — Day 19 Adaptation Layer for Enclave Operating System
// Реализует недостающие POSIX-функции, объявленные в user_libc.h,
// которые критически важны для работы TinyCC и стандартных C-программ.
// ============================================================================

#include "user_libc.h"
#include "user_syscalls.h"


// ============================================================================
// FILE I/O (fwrite)
// ============================================================================
size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream) {
    if (!stream || size == 0 || nmemb == 0) return 0;
    
    size_t total_bytes = size * nmemb;
    const uint8_t* src = (const uint8_t*)ptr;
    size_t bytes_written = 0;
    
    while (bytes_written < total_bytes) {
        if (stream->write_pos >= FILE_BUFFER_SIZE) {
            if (fflush(stream) == EOF) break;
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

// ============================================================================
// TERMINAL DETECTION (isatty)
// ============================================================================
int isatty(int fd) {
    if (fd == STDIN_FILENO || fd == STDOUT_FILENO || fd == STDERR_FILENO) {
        return 1; // Assume terminal for standard streams
    }
    return 0;
}

// ============================================================================
// PATH OPERATIONS (Stubs for TinyCC)
// ============================================================================
char* getcwd(char* buf, size_t size) {
    if (!buf || size < 2) {
        errno = EINVAL;
        return NULL;
    }
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
    if (!path) {
        errno = EINVAL;
        return NULL;
    }
    char* dest = resolved_path;
    if (!dest) dest = (char*)malloc(strlen(path) + 1);
    if (!dest) {
        errno = ENOMEM;
        return NULL;
    }
    strcpy(dest, path);
    return dest;
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

// ============================================================================
// PROCESS TERMINATION & ATEXIT
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
        if (atexit_funcs[atexit_count]) {
            atexit_funcs[atexit_count]();
        }
    }
}

void abort(void) {
    sys_exit(134); // Standard abort exit code (128 + SIGABRT=6)
    __builtin_unreachable();
}

// ============================================================================
// ASSERTIONS
// ============================================================================
void __assert_fail(const char* assertion, const char* file, 
                   unsigned int line, const char* function) {
    fprintf(stderr, "Assertion failed: %s, file %s, line %u, function %s\n",
            assertion, file, line, function);
    abort();
}

// ============================================================================
// TIME (clock, time)
// ============================================================================
clock_t clock(void) {
    timeval_t tv;
    gettimeofday(&tv, NULL);
    return (clock_t)(tv.tv_sec * CLOCKS_PER_SEC + tv.tv_usec);
}

// ============================================================================
// SORTING & SEARCHING (Heapsort & Bsearch)
// Heapsort выбран вместо Quicksort, чтобы избежать глубокой рекурсии
// и потенциального Stack Overflow в изолированной среде Ring 3.
// ============================================================================
static void swap_elements(uint8_t* a, uint8_t* b, size_t size) {
    for (size_t i = 0; i < size; i++) {
        uint8_t tmp = a[i];
        a[i] = b[i];
        b[i] = tmp;
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
    
    // Build max heap
    for (int start = (nmemb - 2) / 2; start >= 0; start--) {
        sift_down(arr, size, cmp, start, nmemb - 1);
    }
    
    // Extract elements
    for (int end = nmemb - 1; end > 0; end--) {
        swap_elements(arr, arr + end * size, size);
        sift_down(arr, size, cmp, 0, end - 1);
    }
}

void* bsearch(const void* key, const void* base, size_t nmemb, size_t size,
              int (*cmp)(const void*, const void*)) {
    const uint8_t* arr = (const uint8_t*)base;
    size_t low = 0;
    size_t high = nmemb;
    
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
// DAY 20: TCC ADAPTATION LAYER (Part 2 - Missing POSIX & glibc functions)
// Современный GCC (14+) компилирует с C23 и glibc-специфичными вызовами.
// Этот блок закрывает все undefined reference при линковке tcc.elf.
// ============================================================================

// glibc использует __errno_location() для thread-safe errno.
// В нашей однопоточной (per-process) среде просто возвращаем адрес глобальной errno.
int* __errno_location(void) {
    return &errno;
}

// C23 compliance: GCC 14+ генерирует вызовы __isoc23_* вместо стандартных strtol.
// Мы просто перенаправляем их на наши существующие реализации.
long __isoc23_strtol(const char* nptr, char** endptr, int base) {
    return strtol(nptr, endptr, base);
}
unsigned long __isoc23_strtoul(const char* nptr, char** endptr, int base) {
    return strtoul(nptr, endptr, base);
}
long long __isoc23_strtoll(const char* nptr, char** endptr, int base) {
    return (long long)strtoul(nptr, endptr, base);
}
unsigned long long __isoc23_strtoull(const char* nptr, char** endptr, int base) {
    return (unsigned long long)strtoul(nptr, endptr, base);
}

// FILE* advanced operations
int fputs(const char* s, FILE* stream) {
    if (!s || !stream) return EOF;
    size_t len = strlen(s);
    return fwrite(s, 1, len, stream) == len ? 0 : EOF;
}

int fseek(FILE* stream, long offset, int whence) {
    if (!stream) return -1;
    fflush(stream); // Сбрасываем write buffer
    stream->buffer_pos = 0; // Инвалидируем read buffer
    stream->buffer_len = 0;
    
    off_t new_pos = lseek(stream->fd, (off_t)offset, whence);
    if (new_pos < 0) return -1;
    stream->eof = 0;
    return 0;
}

long ftell(FILE* stream) {
    if (!stream) return -1L;
    fflush(stream);
    off_t pos = lseek(stream->fd, 0, SEEK_CUR);
    // Корректируем на непрочитанные байты в read buffer
    if (stream->buffer_len > 0) {
        pos -= (stream->buffer_len - stream->buffer_pos);
    }
    return (long)pos;
}

FILE* fdopen(int fd, const char* mode) {
    (void)mode; // Режим игнорируем, fd уже открыт с нужными правами
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

// String operations
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

// Process execution (TinyCC использует для запуска линкера)
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

// System configuration
long sysconf(int name) {
    if (name == _SC_PAGESIZE) return 4096; // Размер страницы в Enclave OS
    return -1;
}

// Memory protection (обертка над sys_mprotect)
int mprotect(void* addr, size_t len, int prot) {
    int ret = sys_mprotect(addr, len, prot);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}

// Time
struct tm* localtime(const time_t* timep) {
    static struct tm t;
    (void)timep;
    memset(&t, 0, sizeof(t));
    t.tm_mday = 1; // Валидная заглушка
    return &t;
}

// Math / Float parsing (Базовая реализация для парсинга float literals)
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
float strtof(const char* nptr, char** endptr) { return (float)strtod(nptr, endptr); }
long double strtold(const char* nptr, char** endptr) { return (long double)strtod(nptr, endptr); }
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

// Environment
char** environ = NULL;

// setjmp alias
int _setjmp(jmp_buf env) {
    return setjmp(env);
}