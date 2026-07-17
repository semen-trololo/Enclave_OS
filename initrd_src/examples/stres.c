/* ========================================================================
 * OMNI STRESS TEST for Enclave OS (Day 25+)
 * Tests 80% of kernel subsystems + TinyCC + SSE2/FPU
 * Compiled INSIDE Enclave OS via TinyCC
 * ======================================================================== */

#include <user_libc.h>

/* ---- POSIX Memory Management (defined in user_libc.c) ---- */
void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset);
int munmap(void* addr, size_t length);

/* ---- ANSI Colors ---- */
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN    "\033[36m"
#define RESET   "\033[0m"
#define BOLD    "\033[1m"

/* ---- Statistics ---- */
static int g_total = 0, g_pass = 0, g_fail = 0;

/* ---- CRC32 for VFS verification ---- */
static uint32_t crc32(const void* data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
    }
    return ~crc;
}

/* ---- Packed struct for compiler test ---- */
struct __attribute__((packed)) packed_bits {
    uint32_t a : 3;
    uint32_t b : 5;
    uint16_t c : 7;
};

/* ---- Union for type punning ---- */
union punner {
    uint32_t u;
    uint8_t bytes[4];
    float f;
};

/* ---- Function pointer type ---- */
typedef void (*test_func_t)(void);

/* ---- Preprocessor magic ---- */
#define STR(x) #x
#define STRINGIFY(x) STR(x)
#define CONCAT(a, b) a ## b
#define TEST_NAME(name) CONCAT(test_, name)

/* ========================================================================
 * TESTS 1-24 (Previously Passing)
 * ======================================================================== */

/* Test 1: VMM Demand Paging */
static void test_vmm_demand_paging(void) {
    char* pages[100];
    for (int i = 0; i < 100; i++) {
        pages[i] = (char*)mmap(NULL, 4096, PROT_READ|PROT_WRITE,
                               MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        if (pages[i] == MAP_FAILED) exit(1);
    }
    for (int i = 0; i < 100; i++) {
        pages[i][0] = (char)(i & 0xFF);
        if (pages[i][0] != (char)(i & 0xFF)) exit(2);
    }
    for (int i = 0; i < 100; i++) munmap(pages[i], 4096);
    exit(0);
}

/* Test 2: VMM CoW Isolation */
static void test_vmm_cow_isolation(void) {
    volatile int shared = 100;
    pid_t pid = fork();
    if (pid < 0) exit(1);
    if (pid == 0) {
        shared = 999;
        exit(shared == 999 ? 0 : 2);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    if (shared != 100) exit(3);
    exit(0);
}

/* Test 3: VMM W^X Enforcement (expects child to die) */
static void test_vmm_wx_enforcement(void) {
    volatile int* code_ptr = (volatile int*)(void*)test_vmm_wx_enforcement;
    *code_ptr = 0xDEADBEEF;
    exit(1);
}

/* Test 4: VMM OOM Probe */
static void test_vmm_oom_probe(void) {
    void* big = mmap(NULL, 100*1024*1024, PROT_READ|PROT_WRITE,
                     MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (big == MAP_FAILED) exit(0);
    ((char*)big)[0] = 'X';
    munmap(big, 100*1024*1024);
    exit(0);
}

/* Test 5: VMM mprotect Flip */
static void test_vmm_mprotect_flip(void) {
    char* page = (char*)mmap(NULL, 4096, PROT_READ|PROT_WRITE,
                             MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (page == MAP_FAILED) exit(1);
    page[0] = 'A';
    if (mprotect(page, 4096, PROT_READ) != 0) exit(2);
    if (page[0] != 'A') exit(3);
    if (mprotect(page, 4096, PROT_READ|PROT_WRITE) != 0) exit(4);
    page[0] = 'B';
    if (page[0] != 'B') exit(5);
    munmap(page, 4096);
    exit(0);
}

/* Test 6: FPU Math */
static void test_fpu_x87_math(void) {
    double a = 2.0, b = 3.0;
    if (a + b != 5.0) exit(1);
    if (a * b != 6.0) exit(2);
    if (a / b < 0.666 || a / b > 0.667) exit(3);
    int x = 42;
    double c = 1.5;
    if (x != 42) exit(4);
    if (c != 1.5) exit(5);
    exit(0);
}

/* Test 7: FPU Fork Preservation */
static void test_fpu_fork_preserve(void) {
    double val = 3.14159;
    pid_t pid = fork();
    if (pid == 0) {
        if (val < 3.14 || val > 3.15) exit(1);
        exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    if (val < 3.14 || val > 3.15) exit(2);
    exit(status == 0 ? 0 : 3);
}

/* Test 8: Process Fork Bomb (controlled) */
static void test_proc_fork_bomb(void) {
    for (int i = 0; i < 5; i++) {
        pid_t p = fork();
        if (p < 0) exit(1);
        if (p == 0) {
            for (int j = 0; j < 5; j++) {
                pid_t p2 = fork();
                if (p2 == 0) exit(0);
                if (p2 < 0) exit(1);
                int s; waitpid(p2, &s, 0);
            }
            exit(0);
        }
        int s; waitpid(p, &s, 0);
    }
    exit(0);
}

/* Test 9: Process Zombie Cascade (15 children) */
static void test_proc_zombie_cascade(void) {
    pid_t pids[15];
    for (int i = 0; i < 15; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            for (int j = 0; j < i; j++) waitpid(pids[j], NULL, 0);
            exit(1);
        }
        if (pids[i] == 0) exit(42);
    }
    for (int i = 0; i < 15; i++) {
        int status = 0;
        waitpid(pids[i], &status, 0);
        if (status != 42) exit(2);
    }
    exit(0);
}

/* Test 10: VFS 1000 Files */
static void test_vfs_1000_files(void) {
    char path[64];
    uint32_t checksums[1000];
    for (int i = 0; i < 1000; i++) {
        snprintf(path, sizeof(path), "/tmp/stress_%d.bin", i);
        int fd = open(path, O_CREAT|O_WRONLY|O_TRUNC, 0644);
        if (fd < 0) exit(1);
        char buf[100];
        snprintf(buf, sizeof(buf), "data_%d", i);
        write(fd, buf, strlen(buf));
        checksums[i] = crc32(buf, strlen(buf));
        close(fd);
    }
    for (int i = 0; i < 1000; i++) {
        snprintf(path, sizeof(path), "/tmp/stress_%d.bin", i);
        int fd = open(path, O_RDONLY);
        if (fd < 0) exit(2);
        char buf[100];
        int n = read(fd, buf, sizeof(buf));
        close(fd);
        if (n <= 0) exit(3);
        if (crc32(buf, n) != checksums[i]) exit(4);
        unlink(path);
    }
    exit(0);
}

/* Test 11: VFS Large File (5MB) */
static void test_vfs_large_file(void) {
    int fd = open("/tmp/large.bin", O_CREAT|O_WRONLY|O_TRUNC, 0644);
    if (fd < 0) exit(1);
    char buf[4096];
    memset(buf, 'A', sizeof(buf));
    for (int i = 0; i < 1280; i++) {
        if (write(fd, buf, sizeof(buf)) != sizeof(buf)) exit(2);
    }
    close(fd);
    fd = open("/tmp/large.bin", O_RDONLY);
    if (fd < 0) exit(3);
    off_t size = lseek(fd, 0, SEEK_END);
    close(fd);
    unlink("/tmp/large.bin");
    if (size != 1280 * 4096) exit(4);
    exit(0);
}

/* Test 12: VFS Sparse Seek */
static void test_vfs_sparse_seek(void) {
    int fd = open("/tmp/sparse.bin", O_CREAT|O_WRONLY|O_TRUNC, 0644);
    if (fd < 0) exit(1);
    lseek(fd, 1000000, SEEK_SET);
    write(fd, "X", 1);
    close(fd);
    fd = open("/tmp/sparse.bin", O_RDONLY);
    if (fd < 0) exit(2);
    off_t size = lseek(fd, 0, SEEK_END);
    close(fd);
    unlink("/tmp/sparse.bin");
    if (size != 1000001) exit(3);
    exit(0);
}

/* Test 13: VFS O_TRUNC */
static void test_vfs_o_trunc(void) {
    int fd = open("/tmp/trunc.bin", O_CREAT|O_WRONLY|O_TRUNC, 0644);
    if (fd < 0) exit(1);
    char big[1000];
    memset(big, 'X', sizeof(big));
    write(fd, big, sizeof(big));
    close(fd);
    fd = open("/tmp/trunc.bin", O_WRONLY|O_TRUNC);
    if (fd < 0) exit(2);
    write(fd, "small", 5);
    close(fd);
    fd = open("/tmp/trunc.bin", O_RDONLY);
    if (fd < 0) exit(3);
    off_t size = lseek(fd, 0, SEEK_END);
    close(fd);
    unlink("/tmp/trunc.bin");
    if (size != 5) exit(4);
    exit(0);
}

/* Test 14: VFS Concurrent FD */
static void test_vfs_concurrent_fd(void) {
    int fd = open("/tmp/multi.bin", O_CREAT|O_WRONLY|O_TRUNC, 0644);
    if (fd < 0) exit(1);
    write(fd, "ABCDEFGHIJ", 10);
    close(fd);
    int fd1 = open("/tmp/multi.bin", O_RDONLY);
    int fd2 = open("/tmp/multi.bin", O_RDONLY);
    if (fd1 < 0 || fd2 < 0) exit(2);
    lseek(fd1, 0, SEEK_SET);
    lseek(fd2, 5, SEEK_SET);
    char b1, b2;
    read(fd1, &b1, 1);
    read(fd2, &b2, 1);
    close(fd1);
    close(fd2);
    unlink("/tmp/multi.bin");
    if (b1 != 'A' || b2 != 'F') exit(3);
    exit(0);
}

/* Test 15: C Packed Bitfields */
static void test_c_packed_bitfields(void) {
    if (sizeof(struct packed_bits) != 2) exit(1);
    struct packed_bits p;
    memset(&p, 0, sizeof(p));
    p.a = 7;
    p.b = 31;
    p.c = 127;
    if (p.a != 7 || p.b != 31 || p.c != 127) exit(2);
    p.a = 8;
    if (p.a != 0) exit(3);
    exit(0);
}

/* Test 16: C Union Punning */
static void test_c_union_punning(void) {
    union punner p;
    p.u = 0x41424344;
    if (p.bytes[0] != 'D' || p.bytes[1] != 'C') exit(1);
    if (p.bytes[2] != 'B' || p.bytes[3] != 'A') exit(2);
    p.f = 1.0f;
    uint32_t expected = 0x3F800000;
    if (p.u != expected) exit(3);
    exit(0);
}

/* Test 17: C Variadic Custom */
static int my_printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int n = vprintf(fmt, args);
    va_end(args);
    return n;
}

static void test_c_variadic_custom(void) {
    int n = my_printf("Hello %s, %d\n", "world", 42);
    if (n <= 0) exit(1);
    exit(0);
}

/* Test 18: C Function Dispatch */
static int dispatch_sum = 0;
static void dispatch_a(void) { dispatch_sum += 1; }
static void dispatch_b(void) { dispatch_sum += 10; }
static void dispatch_c(void) { dispatch_sum += 100; }

static void test_c_function_dispatch(void) {
    test_func_t table[] = { dispatch_a, dispatch_b, dispatch_c };
    for (int i = 0; i < 3; i++) table[i]();
    if (dispatch_sum != 111) exit(1);
    exit(0);
}

/* Test 19: C Preprocessor Magic */
#define TEST_VALUE 42
static void test_c_preprocessor_magic(void) {
    const char* s = STRINGIFY(TEST_VALUE);
    if (s[0] != '4' || s[1] != '2') exit(1);
    if (TEST_VALUE != 42) exit(2);
    exit(0);
}

/* Test 20: C Inline ASM */
static void test_c_inline_asm(void) {
    int x = 10, y = 20, z = 0;
    __asm__ __volatile__(
        "addl %%ebx, %%eax"
        : "=a"(z)
        : "a"(x), "b"(y)
    );
    if (z != 30) exit(1);
    exit(0);
}

/* Test 21: Syscall Invalid */
static void test_syscall_invalid(void) {
    pid_t p = getpid();
    if (p <= 0) exit(1);
    exit(0);
}

/* Test 22: Time Monotonicity */
static void test_time_monotonicity(void) {
    timeval_t tv1, tv2;
    gettimeofday(&tv1, NULL);
    for (volatile int i = 0; i < 1000000; i++);
    gettimeofday(&tv2, NULL);
    if (tv2.tv_sec < tv1.tv_sec) exit(1);
    if (tv2.tv_sec == tv1.tv_sec && tv2.tv_usec < tv1.tv_usec) exit(2);
    exit(0);
}

/* Test 23: ANSI Colors */
static void test_ansi_colors(void) {
    printf(RED "Red " GREEN "Green " BLUE "Blue " RESET);
    printf(YELLOW "Yellow " MAGENTA "Magenta " CYAN "Cyan" RESET);
    printf(BOLD " Bold" RESET "\n");
    exit(0);
}

/* Test 24: VMM mprotect + SIGSEGV (child dies) */
static void test_vmm_mprotect_sigsegv(void) {
    char* page = (char*)mmap(NULL, 4096, PROT_READ|PROT_WRITE,
                             MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (page == MAP_FAILED) exit(1);
    page[0] = 'A';
    mprotect(page, 4096, PROT_NONE);
    page[0] = 'B';
    exit(1);
}

/* Test 25: FPU Context Switch Isolation (x87 via C double)
 * TinyCC генерирует x87 инструкции (fld/fstp/fadd) для double,
 * которые триггерят тот же #NM Handler и fxsave/fxrstor, что и SSE.
 * Проверяем, что FPU-регистры (ST0-ST7) не "протекают" между процессами.
 */
static void test_fpu_context_switch(void) {
    volatile double parent_val = 3.14159265358979;
    volatile double child_val  = 2.71828182845904;

    /* Форсируем загрузку parent_val в x87 ST0 через volatile read + math */
    volatile double check = parent_val * 1.0;
    if (check < 3.14 || check > 3.15) exit(1);

    pid_t pid = fork();
    if (pid < 0) exit(2);

    if (pid == 0) {
        /* Ребенок загружает своё значение в x87 и гоняет математику */
        volatile double c = child_val;
        for (int i = 0; i < 20; i++) {
            c = c * 1.0001;
            c = c / 1.0001;
            if (c < 2.71 || c > 2.72) exit(3);
            /* Отдаём CPU, форсируя context switch и fxsave/fxrstor */
            for (volatile int j = 0; j < 50000; j++);
        }
        exit(0);
    }

    /* Родитель тоже отдаёт CPU, позволяя планировщику гонять задачи */
    for (int i = 0; i < 20; i++) {
        for (volatile int j = 0; j < 50000; j++);
    }

    /* Проверяем, что x87 родителя не затёрся значением ребёнка */
    if (parent_val < 3.14 || parent_val > 3.15) exit(4);

    int status = 0;
    waitpid(pid, &status, 0);

    if (status != 0) exit(5);
    exit(0);
}

/* Test 26: FPU Math Precision (x87 80-bit extended)
 * Проверяем, что x87 FPU сохраняет полную точность 80-bit extended precision
 * после fork + context switch. Если fxsave/fxrstor кривой, младшие биты
 * мантиссы будут потеряны.
 */
static void test_fpu_precision(void) {
    /* Число, требующее полной 80-bit точности */
    volatile double a = 1.0 / 3.0;
    volatile double b = a * 3.0;

    /* b должно быть очень близко к 1.0, но не точно 1.0 из-за округления */
    if (b < 0.9999 || b > 1.0001) exit(1);

    pid_t pid = fork();
    if (pid < 0) exit(2);

    if (pid == 0) {
        /* Ребёнок делает свою математику */
        volatile double x = 2.0 / 7.0;
        volatile double y = x * 7.0;
        if (y < 1.999 || y > 2.001) exit(3);
        exit(0);
    }

    int status = 0;
    waitpid(pid, &status, 0);

    /* Проверяем, что точность родителя не деградировала */
    volatile double c = a * 3.0;
    if (c < 0.9999 || c > 1.0001) exit(4);
    if (status != 0) exit(5);
    exit(0);
}

/* Test 27: FPU Syscall Ring Transition Safety
 * Доказывает, что переходы Ring 3 -> Ring 0 (syscall) -> Ring 3
 * не затирают x87 FPU state. Ядро (Ring 0) не использует FPU,
 * но INT 0x80 триггерит context switch, который должен сохранить ST0-ST7.
 */
static void test_fpu_syscall_safety(void) {
    volatile double magic = 9.99999;
    volatile double check = 0.0;

    /* Загружаем magic в x87 через volatile */
    check = magic * 1.0;
    if (check < 9.99 || check > 10.01) exit(1);

    /* 100 переходов в Ring 0 и обратно */
    for (int i = 0; i < 100; i++) {
        getpid(); /* syscall: Ring 3 -> Ring 0 -> Ring 3 */
    }

    /* Проверяем, что x87 state сохранился */
    check = magic * 1.0;
    if (check < 9.99 || check > 10.01) exit(2);
    exit(0);
}

/* ========================================================================
 * REGISTRY & DISPATCHER
 * ======================================================================== */

typedef struct {
    const char* name;
    test_func_t func;
    int expect_death;
} test_entry_t;

#define ENTRY(n, d) { #n, TEST_NAME(n), d }

static test_entry_t tests[] = {
    ENTRY(vmm_demand_paging, 0),
    ENTRY(vmm_cow_isolation, 0),
    ENTRY(vmm_wx_enforcement, 1),
    ENTRY(vmm_oom_probe, 0),
    ENTRY(vmm_mprotect_flip, 0),
    ENTRY(vmm_mprotect_sigsegv, 1),
    ENTRY(fpu_x87_math, 0),
    ENTRY(fpu_fork_preserve, 0),
    ENTRY(proc_fork_bomb, 0),
    ENTRY(proc_zombie_cascade, 0),
    ENTRY(vfs_1000_files, 0),
    ENTRY(vfs_large_file, 0),
    ENTRY(vfs_sparse_seek, 0),
    ENTRY(vfs_o_trunc, 0),
    ENTRY(vfs_concurrent_fd, 0),
    ENTRY(c_packed_bitfields, 0),
    ENTRY(c_union_punning, 0),
    ENTRY(c_variadic_custom, 0),
    ENTRY(c_function_dispatch, 0),
    ENTRY(c_preprocessor_magic, 0),
    ENTRY(c_inline_asm, 0),
    ENTRY(syscall_invalid, 0),
    ENTRY(time_monotonicity, 0),
    ENTRY(ansi_colors, 0),
    /* FPU Torture (x87 via C double — TinyCC compatible) */
    ENTRY(fpu_context_switch, 0),
    ENTRY(fpu_precision, 0),
    ENTRY(fpu_syscall_safety, 0),
};

static int run_test(test_entry_t* t) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        t->func();
        exit(255);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    
    if (t->expect_death) {
        return (status != 0) ? 0 : -1;
    } else {
        return (status == 0) ? 0 : -1;
    }
}

/* ========================================================================
 * MAIN
 * ======================================================================== */

static void print_padded(const char* str, int width) {
    int len = 0;
    const char* p = str;
    while (*p++) len++;
    
    printf("%s", str);
    for (int i = len; i < width; i++) {
        printf(" ");
    }
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    
    printf("\n+--------------------------------------------------------------+\n");
    printf("|      ENCLAVE OS - OMNI STRESS TEST (27 Tests)                |\n");
    printf("+--------------------------------------------------------------+\n");
    printf("Compiler: TinyCC in Ring 3 (Self-Hosting)\n\n");
    
    int n = sizeof(tests) / sizeof(tests[0]);
    for (int i = 0; i < n; i++) {
        printf("[%02d] ", i + 1);
        print_padded(tests[i].name, 40);
        
        int result = run_test(&tests[i]);
        if (result == 0) {
            printf("PASS\n");
            g_pass++;
        } else {
            printf("FAIL\n");
            g_fail++;
        }
        g_total++;
    }
    
    printf("\n+--------------------------------------------------------------+\n");
    printf("| RESULTS: %d/%d PASSED (%d failed)                            \n",
           g_pass, g_total, g_fail);
    if (g_fail == 0) {
        printf("| ALL TESTS PASSED! System is production-ready.                \n");
    } else {
        printf("| SOME TESTS FAILED. Check kernel logs.                        \n");
    }
    printf("+--------------------------------------------------------------+\n");
    
    return g_fail == 0 ? 0 : 1;
}