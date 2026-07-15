// ============================================================================
// tcc_lib_os.c — Adaptation Layer для TinyCC в Bare Metal OS
// Закрывает пробелы между POSIX-ожиданиями TinyCC и нашей минималистичной libc.
// Линкуется вместе с user_libc.o и crt0.o.
// ============================================================================

#include "user_libc.h"

// ============================================================================
// STRERROR — Таблица POSIX errno → человекочитаемые строки
// TinyCC вызывает strerror(errno) для вывода сообщений об ошибках.
// ============================================================================
static const char* error_strings[] = {
    [0]           = "Success",
    [EPERM]       = "Operation not permitted",
    [ENOENT]      = "No such file or directory",
    [ESRCH]       = "No such process",
    [EINTR]       = "Interrupted system call",
    [EIO]         = "I/O error",
    [EBADF]       = "Bad file descriptor",
    [ENOMEM]      = "Out of memory",
    [EACCES]      = "Permission denied",
    [EFAULT]      = "Bad address",
    [EBUSY]       = "Device or resource busy",
    [EEXIST]      = "File exists",
    [ENODEV]      = "No such device",
    [ENOTDIR]     = "Not a directory",
    [EISDIR]      = "Is a directory",
    [EINVAL]      = "Invalid argument",
    [ENFILE]      = "Too many open files in system",
    [EMFILE]      = "Too many open files",
    [ENOSPC]      = "No space left on device",
    [ERANGE]      = "Numerical result out of range",
    [ENAMETOOLONG]= "File name too long",
    [ENOSYS]      = "Function not implemented",
    [ENOTTY]      = "Inappropriate ioctl for device"
};

#define MAX_ERRNO (sizeof(error_strings) / sizeof(error_strings[0]))

char* strerror(int errnum) {
    if (errnum < 0) errnum = -errnum;
    if (errnum >= 0 && (size_t)errnum < MAX_ERRNO && error_strings[errnum]) {
        return (char*)error_strings[errnum];
    }
    return (char*)"Unknown error";
}

// ============================================================================
// QSORT — Heapsort (итеративный, без рекурсии, безопасный для стека)
// TinyCC сортирует таблицы символов, массивы секций ELF и т.д.
// Используем heapsort вместо quicksort: гарантированный O(n log n),
// без рекурсии (не переполнит стек Ring 3 на 64KB).
// ============================================================================

// Вспомогательная функция: просеивание элемента вниз в куче
static void sift_down(char* base, size_t size, 
                      int (*cmp)(const void*, const void*),
                      int start, int end, char* tmp) {
    int root = start;
    
    while (root * 2 + 1 <= end) {
        int child = root * 2 + 1;   // левый потомок
        int swap = root;
        
        // Если левый потомок больше корня
        if (cmp(base + swap * size, base + child * size) < 0) {
            swap = child;
        }
        // Если правый потомок существует и больше текущего max
        if (child + 1 <= end && 
            cmp(base + swap * size, base + (child + 1) * size) < 0) {
            swap = child + 1;
        }
        
        // Если корень уже максимум — куча сбалансирована
        if (swap == root) return;
        
        // Swap root ↔ swap
        memcpy(tmp, base + root * size, size);
        memcpy(base + root * size, base + swap * size, size);
        memcpy(base + swap * size, tmp, size);
        
        root = swap;
    }
}

void qsort(void* base, size_t nmemb, size_t size, 
           int (*cmp)(const void*, const void*)) {
    if (!base || nmemb <= 1 || size == 0 || !cmp) return;
    
    // Выделяем временный буфер для swap операций
    char* tmp = (char*)malloc(size);
    if (!tmp) return;  // OOM: молча пропускаем сортировку
    
    char* arr = (char*)base;
    
    // Шаг 1: Построение max-кучи (heapify)
    for (int start = (int)((nmemb - 2) / 2); start >= 0; start--) {
        sift_down(arr, size, cmp, start, (int)nmemb - 1, tmp);
    }
    
    // Шаг 2: Извлечение элементов из кучи (сортировка)
    for (int end = (int)nmemb - 1; end > 0; end--) {
        // Swap: корень (max) ↔ последний элемент
        memcpy(tmp, arr, size);
        memcpy(arr, arr + end * size, size);
        memcpy(arr + end * size, tmp, size);
        
        // Восстанавливаем кучу для оставшейся части
        sift_down(arr, size, cmp, 0, end - 1, tmp);
    }
    
    // Bump allocator: память не освободится, но это OK для TinyCC
    free(tmp);
}

// ============================================================================
// BSEARCH — Классический бинарный поиск в отсортированном массиве
// TinyCC ищет keywords, типы, символы в отсортированных таблицах.
// ============================================================================
void* bsearch(const void* key, const void* base, size_t nmemb, size_t size,
              int (*cmp)(const void*, const void*)) {
    if (!key || !base || nmemb == 0 || size == 0 || !cmp) return NULL;
    
    const char* arr = (const char*)base;
    size_t low = 0;
    size_t high = nmemb;
    
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        int result = cmp(key, arr + mid * size);
        
        if (result < 0) {
            high = mid;
        } else if (result > 0) {
            low = mid + 1;
        } else {
            return (void*)(arr + mid * size);
        }
    }
    
    return NULL;  // Не найдено
}

// ============================================================================
// ISATTY — Определение, является ли FD терминалом
// TinyCC проверяет isatty(STDERR_FILENO) для цветного вывода ошибок.
// В QEMU мы всегда в терминале → возвращаем 1.
// ============================================================================
int isatty(int fd) {
    if (fd == STDIN_FILENO || fd == STDOUT_FILENO || fd == STDERR_FILENO) {
        return 1;  // Да, это терминал
    }
    errno = ENOTTY;
    return 0;
}

// ============================================================================
// GETCWD / CHDIR — Работа с текущей директорией
// Наш VFS пока не поддерживает cwd per-process. Делаем заглушки.
// TinyCC использует их для нормализации путей — "/" достаточно.
// ============================================================================
char* getcwd(char* buf, size_t size) {
    if (!buf || size == 0) {
        errno = EINVAL;
        return NULL;
    }
    if (size < 2) {
        errno = ERANGE;
        return NULL;
    }
    buf[0] = '/';
    buf[1] = '\0';
    return buf;
}

int chdir(const char* path) {
    (void)path;
    errno = ENOSYS;  // Пока не реализовано
    return -1;
}

// ============================================================================
// ABORT — Аварийное завершение процесса
// Вызывается при assertion failure или критических ошибках.
// ============================================================================
void abort(void) {
    fprintf(stderr, "\nAborted (core dumped)\n");
    exit(134);  // 128 + SIGABRT(6)
}

// ============================================================================
// ATEXIT — Регистрация функций для вызова при exit()
// TinyCC может вызывать atexit для cleanup. Делаем no-op.
// ============================================================================

// Максимум 32 обработчика (статический массив, без malloc)
#define MAX_ATEXIT_HANDLERS 32
static void (*atexit_handlers[MAX_ATEXIT_HANDLERS])(void);
static int atexit_count = 0;

int atexit(void (*func)(void)) {
    if (!func) return -1;
    if (atexit_count >= MAX_ATEXIT_HANDLERS) return -1;
    
    atexit_handlers[atexit_count++] = func;
    return 0;
}

// Внутренняя функция: вызывается из exit() для запуска atexit handlers
// (должна быть вызвана до sys_exit)
void __run_atexit_handlers(void) {
    // Вызываем в обратном порядке (LIFO — стандарт POSIX)
    for (int i = atexit_count - 1; i >= 0; i--) {
        if (atexit_handlers[i]) {
            atexit_handlers[i]();
        }
    }
    atexit_count = 0;
}

// ============================================================================
// STRDUP — Дублирование строки с выделением памяти
// Очень часто используется в парсерах (включая TinyCC).
// ============================================================================
char* strdup(const char* s) {
    if (!s) return NULL;
    
    size_t len = strlen(s) + 1;
    char* dup = (char*)malloc(len);
    if (!dup) return NULL;
    
    memcpy(dup, s, len);
    return dup;
}

// ============================================================================
// STRNDUP — Дублирование не более n символов
// ============================================================================
char* strndup(const char* s, size_t n) {
    if (!s) return NULL;
    
    size_t len = strlen(s);
    if (len > n) len = n;
    
    char* dup = (char*)malloc(len + 1);
    if (!dup) return NULL;
    
    memcpy(dup, s, len);
    dup[len] = '\0';
    return dup;
}

// ============================================================================
// CLOCK — Процессорное время (заглушка)
// TinyCC может использовать для profiling. Возвращаем 0.
// ============================================================================
typedef uint32_t clock_t;
#define CLOCKS_PER_SEC 1000000

clock_t clock(void) {
    return 0;  // Заглушка
}

// ============================================================================
// TIME — Календарное время
// Обертка над gettimeofday (который у нас уже есть).
// ============================================================================
typedef uint32_t time_t;

time_t time(time_t* tloc) {
    timeval_t tv;
    if (gettimeofday(&tv, NULL) < 0) {
        return (time_t)-1;
    }
    
    if (tloc) *tloc = tv.tv_sec;
    return tv.tv_sec;
}

// ============================================================================
// REALPATH — Канонический путь (упрощенная заглушка)
// TinyCC использует для нормализации include paths.
// Возвращаем копию входного пути (без разрешения symlink).
// ============================================================================
char* realpath(const char* path, char* resolved_path) {
    if (!path) {
        errno = EINVAL;
        return NULL;
    }
    
    // Если resolved_path == NULL, выделяем память (POSIX)
    int need_free = 0;
    if (!resolved_path) {
        resolved_path = (char*)malloc(4096);
        if (!resolved_path) return NULL;
        need_free = 1;
    }
    
    // Упрощенно: просто копируем путь
    // TODO: разрешение symlink, удаление "/./", "/../"
    size_t len = strlen(path);
    if (len >= 4096) {
        if (need_free) free(resolved_path);
        errno = ENAMETOOLONG;
        return NULL;
    }
    
    memcpy(resolved_path, path, len + 1);
    return resolved_path;
}

// ============================================================================
// ASSERT — Макрос для отладочных проверок (реализация __assert_fail)
// ============================================================================
void __assert_fail(const char* assertion, const char* file, 
                   unsigned int line, const char* function) {
    fprintf(stderr, "%s:%u: %s: Assertion `%s' failed.\n", 
            file, line, function ? function : "(unknown)", assertion);
    abort();
}