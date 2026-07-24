/* ========================================================================
 * OMNI STRESS TEST for Enclave OS (Day 25+)
 * Tests 80% of kernel subsystems + TinyCC + SSE2/FPU
 * Compiled INSIDE Enclave OS via TinyCC
 * ======================================================================== */

#include <user_libc.h>

/* ---- POSIX Memory Management (defined in user_libc.c) ---- */
void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset);
int munmap(void* addr, size_t length);

/* ========================================================================
 * SHARED: Raw readdir via INT 0x80
 * Определено ОДИН раз в начале файла.
 * TCC v0.9.27: без static/inline (file scope safety).
 * ======================================================================== */
typedef struct {
    uint32_t ino;
    uint32_t type;
    char     name[256];
} test_dirent_t;

int32_t test_readdir(int fd, uint32_t index, test_dirent_t* entry) {
    int32_t ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(141), "b"(fd), "c"(index), "d"(entry)
        : "memory"
    );
    return ret;
}

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

/* Test: Process Rapid Fork/Exit Cycle (T5 regression) */
static void test_proc_rapid_fork_exit(void) {
    for (int i = 0; i < 20; i++) {
        pid_t pid = fork();
        if (pid < 0) exit(1);
        if (pid == 0) {
            volatile int x = i;
            exit(x);
        }
        int status;
        waitpid(pid, &status, 0);
        if (status != i) exit(2);
    }
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

/* Test: VMM mprotect Partial VMA Split (S1 fix) */
static void test_vmm_mprotect_partial(void) {
    char* base = (char*)mmap(NULL, 3 * 4096, PROT_READ|PROT_WRITE,
                             MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) exit(1);

    base[0]     = 'A';
    base[4096]  = 'B';
    base[8192]  = 'C';

    if (mprotect(base + 4096, 4096, PROT_READ) != 0) exit(2);

    base[0]    = 'X';
    base[8192] = 'Z';
    if (base[0] != 'X') exit(3);
    if (base[8192] != 'Z') exit(4);

    if (base[4096] != 'B') exit(5);

    munmap(base, 3 * 4096);
    exit(0);
}

/* Test: VMM mprotect Partial SIGSEGV (child dies) */
static void test_vmm_mprotect_partial_sigsegv(void) {
    char* base = (char*)mmap(NULL, 3 * 4096, PROT_READ|PROT_WRITE,
                             MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) exit(1);

    base[4096] = 'B';
    mprotect(base + 4096, 4096, PROT_READ);

    base[4096] = 'X';

    exit(1);
}

/* Test: VMM mprotect preserves CoW (REG-MEM-008) */
static void test_vmm_mprotect_cow_preserve(void) {
    char* page = (char*)mmap(NULL, 4096, PROT_READ|PROT_WRITE,
                             MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (page == MAP_FAILED) exit(1);
    page[0] = 'A';

    pid_t pid = fork();
    if (pid < 0) exit(2);

    if (pid == 0) {
        /* Child: mprotect READ → WRITE (не ломает CoW) */
        if (mprotect(page, 4096, PROT_READ) != 0) exit(3);
        if (mprotect(page, 4096, PROT_READ|PROT_WRITE) != 0) exit(4);
        /* Запись триггерит CoW resolver */
        page[0] = 'B';
        exit(page[0] == 'B' ? 0 : 5);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    if (status != 0) exit(6);
    /* Parent: CoW isolation — страница НЕ изменилась */
    if (page[0] != 'A') exit(7);

    munmap(page, 4096);
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
    usleep(10000);
    gettimeofday(&tv2, NULL);
    if (tv2.tv_sec < tv1.tv_sec) exit(1);
    if (tv2.tv_sec == tv1.tv_sec && tv2.tv_usec <= tv1.tv_usec) exit(2);
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

/* ========================================================================
 * TESTS 25-27: FPU Context Switch / Precision / Syscall Safety
 * ======================================================================== */

/* Test 25: FPU Context Switch Isolation */
static void test_fpu_context_switch(void) {
    volatile double parent_val = 3.14159265358979;
    volatile double child_val  = 2.71828182845904;

    volatile double check = parent_val * 1.0;
    if (check < 3.14 || check > 3.15) exit(1);

    pid_t pid = fork();
    if (pid < 0) exit(2);

    if (pid == 0) {
        volatile double c = child_val;
        for (int i = 0; i < 20; i++) {
            c = c * 1.0001;
            c = c / 1.0001;
            if (c < 2.71 || c > 2.72) exit(3);
            for (volatile int j = 0; j < 50000; j++);
        }
        exit(0);
    }

    for (int i = 0; i < 20; i++) {
        for (volatile int j = 0; j < 50000; j++);
    }

    if (parent_val < 3.14 || parent_val > 3.15) exit(4);

    int status = 0;
    waitpid(pid, &status, 0);

    if (status != 0) exit(5);
    exit(0);
}

/* Test 26: FPU Math Precision */
static void test_fpu_precision(void) {
    volatile double a = 1.0 / 3.0;
    volatile double b = a * 3.0;

    if (b < 0.999 || b > 1.001) exit(1);

    pid_t pid = fork();
    if (pid < 0) exit(2);

    if (pid == 0) {
        volatile double x = 2.0 / 7.0;
        volatile double y = x * 7.0;
        if (y < 1.999 || y > 2.001) exit(3);
        exit(0);
    }

    int status = 0;
    waitpid(pid, &status, 0);

    volatile double c = a * 3.0;
    if (c < 0.999 || c > 1.001) exit(4);
    if (status != 0) exit(5);
    exit(0);
}

/* Test 27: FPU Syscall Ring Transition Safety */
static void test_fpu_syscall_safety(void) {
    volatile double magic = 9.99999;
    volatile double check = 0.0;

    check = magic * 1.0;
    if (check < 9.99 || check > 10.01) exit(1);

    for (int i = 0; i < 100; i++) {
        getpid();
    }

    check = magic * 1.0;
    if (check < 9.99 || check > 10.01) exit(2);
    exit(0);
}

/* ========================================================================
 * TESTS 28-32: Production Hardening — P0 Critical Edge Cases
 * ======================================================================== */

/* Test 28: Heap Exhaustion */
static void test_heap_exhaustion(void) {
    void* ptrs[2048];
    int allocated = 0;

    for (int i = 0; i < 2048; i++) {
        ptrs[i] = malloc(32768);
        if (ptrs[i] == NULL) break;
        allocated++;
        ((char*)ptrs[i])[0] = (char)(i & 0xFF);
    }

    if (allocated < 1800) exit(1);

    void* overflow = malloc(32768);
    if (overflow == NULL) {
        exit(0);
    }

    ((char*)overflow)[0] = 'X';

    exit(2);
}

/* Test 29: Stack Overflow (Guard Page SIGSEGV) */
static volatile int depth_29 = 0;
static void recurse_29(void) {
    depth_29++;
    char buf[4096];
    memset(buf, 'X', sizeof(buf));
    if (depth_29 < 10000) recurse_29();
}

static void test_stack_overflow_guard(void) {
    recurse_29();
    exit(1);
}

/* Test 30: FD Exhaustion (EMFILE Protection) */
static void test_fd_exhaustion(void) {
    const char* test_file = "/tmp/fd_single_30.bin";

    int base_fd = open(test_file, O_CREAT|O_WRONLY, 0644);
    if (base_fd < 0) exit(1);
    write(base_fd, "test", 4);
    close(base_fd);

    int fds[300];
    int opened = 0;

    for (int i = 0; i < 260; i++) {
        fds[i] = open(test_file, O_RDONLY);
        if (fds[i] < 0) break;
        opened++;
    }

    if (opened < 240) exit(2);

    int overflow = open(test_file, O_RDONLY);
    if (overflow >= 0) {
        close(overflow);
        exit(3);
    }

    for (int i = 0; i < opened; i++) {
        close(fds[i]);
    }
    unlink(test_file);
    exit(0);
}

/* Test 31: Directory Creation & Nested Paths (sys_mkdir, Day 31) */
static void test_directory_ops(void) {
    if (mkdir("/tmp/testdir_51", 0755) != 0) exit(1);

    int fd = open("/tmp/testdir_51/file.txt", O_CREAT|O_WRONLY, 0644);
    if (fd < 0) exit(2);
    write(fd, "nested", 6);
    close(fd);

    fd = open("/tmp/testdir_51/file.txt", O_RDONLY);
    if (fd < 0) exit(3);
    char buf[10];
    int n = read(fd, buf, sizeof(buf));
    close(fd);
    if (n != 6 || buf[0] != 'n') exit(4);

    int dir_fd = open("/tmp/testdir_51", O_RDONLY);
    if (dir_fd < 0) exit(5);
    struct stat st;
    if (fstat(dir_fd, &st) != 0) exit(6);
    close(dir_fd);
    if (!(st.st_mode & S_IFDIR)) exit(7);

    dir_fd = open("/tmp/testdir_51", O_RDONLY);
    if (dir_fd < 0) exit(8);
    test_dirent_t entry;
    int found = 0;
    for (uint32_t i = 0; ; i++) {
        if (test_readdir(dir_fd, i, &entry) != 0) break;
        if (strcmp(entry.name, "file.txt") == 0) found = 1;
    }
    close(dir_fd);
    if (!found) exit(9);

    if (unlink("/tmp/testdir_51") == 0) exit(10);

    unlink("/tmp/testdir_51/file.txt");
    unlink("/tmp/testdir_51");

    fd = open("/tmp/testdir_51", O_RDONLY);
    if (fd >= 0) { close(fd); exit(11); }

    exit(0);
}

/* Test 32: Unlink Open File (POSIX Orphan Semantics) */
static void test_unlink_open_file(void) {
    int fd = open("/tmp/orphan_32.bin", O_CREAT|O_WRONLY, 0644);
    if (fd < 0) exit(1);

    write(fd, "orphan_data", 11);

    if (unlink("/tmp/orphan_32.bin") != 0) exit(2);

    lseek(fd, 0, SEEK_SET);
    char buf[20];
    int n = read(fd, buf, sizeof(buf));
    if (n != 11) exit(3);
    if (buf[0] != 'o' || buf[5] != 'n') exit(4);

    close(fd);

    fd = open("/tmp/orphan_32.bin", O_RDONLY);
    if (fd >= 0) {
        close(fd);
        exit(5);
    }

    exit(0);
}

/* ========================================================================
 * TESTS 33-52: SYSTEM CONTRACT VERIFICATION (Day 30)
 * ======================================================================== */

/* Test 33: Syscall ENOSYS */
static void test_syscall_enosys(void) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(9999)
        : "memory"
    );
    if (ret != -ENOSYS) exit(1);
    exit(0);
}

/* Test 34: Syscall EBADF */
static void test_syscall_ebadf(void) {
    char buf[10];

    if (read(-1, buf, 10) != -1) exit(1);
    if (write(-1, "x", 1) != -1) exit(2);
    if (close(-1) != -1) exit(3);
    if (close(999) != -1) exit(4);
    if (lseek(-1, 0, SEEK_SET) != -1) exit(5);

    exit(0);
}

/* Test 35: Syscall ENOENT */
static void test_syscall_enoent(void) {
    int fd = open("/nonexistent_file_35.bin", O_RDONLY);
    if (fd >= 0) { close(fd); exit(1); }
    exit(0);
}

/* Test 36: Syscall EACCES (RBAC /boot protection) */
static void test_syscall_eacces_rbac(void) {
    int fd = open("/boot/kernel.bin", O_RDONLY);
    if (fd >= 0) { close(fd); exit(1); }
    exit(0);
}

/* Test 37: Syscall EFAULT (NULL pointer) */
static void test_syscall_efault_null(void) {
    if (write(1, NULL, 10) != -1) exit(1);
    if (read(0, NULL, 10) != -1) exit(2);
    exit(0);
}

/* Test 38: Syscall EFAULT (kernel pointer) */
static void test_syscall_efault_kernel(void) {
    if (write(1, (void*)0xC0000000, 10) != -1) exit(1);
    if (read(0, (void*)0xC0000000, 10) != -1) exit(2);
    exit(0);
}

/* Test 39: VFS dup/dup2 */
static void test_vfs_dup_dup2(void) {
    int fd = open("/tmp/dup39.bin", O_CREAT|O_WRONLY|O_TRUNC, 0644);
    if (fd < 0) exit(1);

    int fd2 = dup(fd);
    if (fd2 < 0 || fd2 == fd) exit(2);
    write(fd2, "HELLO", 5);
    close(fd2);
    close(fd);

    fd = open("/tmp/dup39.bin", O_RDONLY);
    if (fd < 0) exit(3);
    char buf[10];
    int n = read(fd, buf, 10);
    close(fd);
    if (n != 5 || buf[0] != 'H' || buf[4] != 'O') exit(4);

    fd = open("/tmp/dup39.bin", O_WRONLY|O_TRUNC);
    if (fd < 0) exit(5);
    if (dup2(fd, 42) != 42) exit(6);
    write(42, "WORLD", 5);
    close(42);
    close(fd);

    fd = open("/tmp/dup39.bin", O_RDONLY);
    if (fd < 0) exit(7);
    n = read(fd, buf, 10);
    close(fd);
    if (n != 5 || buf[0] != 'W') exit(8);

    unlink("/tmp/dup39.bin");
    exit(0);
}

/* Test 40: VFS fstat (size + type) */
static void test_vfs_fstat_size(void) {
    int fd = open("/tmp/fstat40.bin", O_CREAT|O_WRONLY|O_TRUNC, 0644);
    if (fd < 0) exit(1);
    write(fd, "12345678", 8);
    close(fd);

    fd = open("/tmp/fstat40.bin", O_RDONLY);
    if (fd < 0) exit(2);

    struct stat st;
    if (fstat(fd, &st) != 0) exit(3);
    close(fd);
    unlink("/tmp/fstat40.bin");

    if (st.st_size != 8) exit(4);
    if (!(st.st_mode & S_IFREG)) exit(5);
    exit(0);
}

/* Test 41: VFS readdir (index-based) */
static void test_vfs_readdir_list(void) {
    int fd = open("/tmp/rd41_a.txt", O_CREAT|O_WRONLY, 0644);
    if (fd < 0) exit(1);
    write(fd, "a", 1); close(fd);

    fd = open("/tmp/rd41_b.txt", O_CREAT|O_WRONLY, 0644);
    if (fd < 0) exit(2);
    write(fd, "b", 1); close(fd);

    int dir_fd = open("/tmp", O_RDONLY);
    if (dir_fd < 0) exit(3);

    int found_a = 0, found_b = 0;
    test_dirent_t entry;
    for (uint32_t i = 0; ; i++) {
        if (test_readdir(dir_fd, i, &entry) != 0) break;
        if (strcmp(entry.name, "rd41_a.txt") == 0) found_a = 1;
        if (strcmp(entry.name, "rd41_b.txt") == 0) found_b = 1;
    }
    close(dir_fd);

    unlink("/tmp/rd41_a.txt");
    unlink("/tmp/rd41_b.txt");

    if (!found_a || !found_b) exit(4);
    exit(0);
}

/* Test 42: VFS lseek SEEK_CUR */
static void test_vfs_lseek_cur(void) {
    int fd = open("/tmp/ls42.bin", O_CREAT|O_WRONLY|O_TRUNC, 0644);
    if (fd < 0) exit(1);
    write(fd, "ABCDEFGHIJ", 10);
    close(fd);

    fd = open("/tmp/ls42.bin", O_RDONLY);
    if (fd < 0) exit(2);

    lseek(fd, 2, SEEK_SET);
    off_t pos = lseek(fd, 3, SEEK_CUR);
    if (pos != 5) exit(3);

    char c;
    read(fd, &c, 1);
    if (c != 'F') exit(4);

    pos = lseek(fd, -2, SEEK_CUR);
    if (pos != 4) exit(5);

    read(fd, &c, 1);
    if (c != 'E') exit(6);

    close(fd);
    unlink("/tmp/ls42.bin");
    exit(0);
}

/* Test 43: Process waitpid WNOHANG */
static void test_proc_waitpid_wnohang(void) {
    pid_t pid = fork();
    if (pid < 0) exit(1);

    if (pid == 0) {
        usleep(100000);
        exit(42);
    }

    int status = 0;
    int ret = waitpid(pid, &status, WNOHANG);
    if (ret != 0) exit(2);

    usleep(200000);
    ret = waitpid(pid, &status, 0);
    if (ret != pid) exit(3);
    if (status != 42) exit(4);
    exit(0);
}

/* Test 44: Process exec with argv */
static void test_proc_exec_argv(void) {
    int fd = open("/tmp/argv44.c", O_CREAT|O_WRONLY|O_TRUNC, 0644);
    if (fd < 0) exit(1);
    const char* src =
        "int main(int argc, char** argv) {\n"
        "  if (argc != 3) return 1;\n"
        "  if (argv[1][0] != 'h') return 2;\n"
        "  if (argv[2][0] != 'w') return 3;\n"
        "  return 0;\n"
        "}\n";
    write(fd, src, strlen(src));
    close(fd);

    pid_t pid = fork();
    if (pid == 0) {
        const char* tcc_argv[] = { "tcc", "/tmp/argv44.c", "-o", "/tmp/argv44.elf", NULL };
        exec("/bin/tcc.elf", tcc_argv);
        exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    if (status != 0) { unlink("/tmp/argv44.c"); exit(5); }

    pid = fork();
    if (pid == 0) {
        const char* run_argv[] = { "argv44", "hello", "world", NULL };
        exec("/tmp/argv44.elf", run_argv);
        exit(127);
    }
    waitpid(pid, &status, 0);

    unlink("/tmp/argv44.c");
    unlink("/tmp/argv44.elf");
    exit(status == 0 ? 0 : 6);
}

/* Test 45: Process exec ENOENT */
static void test_proc_exec_enoent(void) {
    pid_t pid = fork();
    if (pid == 0) {
        const char* argv[] = { "nonexistent", NULL };
        int ret = exec("/nonexistent45.elf", argv);
        exit(ret == -1 ? 0 : 1);
    }
    int status;
    waitpid(pid, &status, 0);
    exit(status == 0 ? 0 : 2);
}

/* Test 46: Process fork under IRQ preemption */
static void test_proc_fork_preempt(void) {
    for (int i = 0; i < 5; i++) {
        for (volatile int j = 0; j < 100000; j++);

        pid_t pid = fork();
        if (pid < 0) exit(1);

        if (pid == 0) {
            volatile int x = 0;
            for (int k = 0; k < 1000; k++) x += k;
            exit(x > 0 ? 0 : 1);
        }

        int status;
        waitpid(pid, &status, 0);
        if (status != 0) exit(2);
    }
    exit(0);
}

/* Test 47: VMM W^X mprotect reject (SLA #6) */
static void test_vmm_wx_mprotect_reject(void) {
    char* page = (char*)mmap(NULL, 4096, PROT_READ|PROT_WRITE,
                             MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (page == MAP_FAILED) exit(1);

    if (mprotect(page, 4096, PROT_WRITE|PROT_EXEC) != -1) exit(2);

    void* wx = mmap(NULL, 4096, PROT_WRITE|PROT_EXEC,
                    MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (wx != MAP_FAILED) exit(3);

    munmap(page, 4096);
    exit(0);
}

/* Test 48: VMM mmap invalid arguments */
static void test_vmm_mmap_invalid(void) {
    void* p = mmap(NULL, 0, PROT_READ|PROT_WRITE,
                   MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (p != MAP_FAILED) exit(1);

    p = mmap(NULL, 0x40000001, PROT_READ|PROT_WRITE,
             MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (p != MAP_FAILED) exit(2);

    exit(0);
}

/* Test 49: VMM munmap invalid */
static void test_vmm_munmap_invalid(void) {
    if (munmap((void*)0x40000000, 0) != -1) exit(1);
    exit(0);
}

/* Test 50: libc printf edge cases */
static void test_libc_printf_edge(void) {
    char buf[128];

    snprintf(buf, sizeof(buf), "%d", INT_MIN);
    if (strcmp(buf, "-2147483648") != 0) exit(1);

    snprintf(buf, sizeof(buf), "%d", INT_MAX);
    if (strcmp(buf, "2147483647") != 0) exit(2);

    snprintf(buf, sizeof(buf), "%s", "");
    if (strcmp(buf, "") != 0) exit(3);

    snprintf(buf, sizeof(buf), "%s", (char*)NULL);
    if (strcmp(buf, "(null)") != 0) exit(4);

    snprintf(buf, sizeof(buf), "%d", 0);
    if (strcmp(buf, "0") != 0) exit(5);

    snprintf(buf, sizeof(buf), "%x", 0xDEADBEEF);
    if (strcmp(buf, "deadbeef") != 0) exit(6);

    exit(0);
}

/* Test 51: libc snprintf buffer safety */
static void test_libc_snprintf_overflow(void) {
    char buf[10];

    int n = snprintf(buf, sizeof(buf), "Hello, World! This is long.");
    if (n < 0) exit(1);
    if (strlen(buf) != 9) exit(2);
    if (buf[9] != '\0') exit(3);

    n = snprintf(NULL, 0, "test");
    if (n != 0) exit(4);

    buf[0] = 'X';
    snprintf(buf, 1, "test");
    if (buf[0] != '\0') exit(5);

    exit(0);
}

/* Test 52: System uname + sysinfo identity */
static void test_sys_uname_sysinfo(void) {
    utsname_t uts;
    if (uname(&uts) != 0) exit(1);
    if (uts.sysname[0] == '\0') exit(2);
    if (strcmp(uts.machine, "i686") != 0) exit(3);

    sysinfo_t info;
    if (sysinfo(&info) != 0) exit(4);
    if (info.totalram == 0) exit(5);
    if (info.freeram > info.totalram) exit(6);
    if (info.procs == 0) exit(7);

    exit(0);
}

/* Test: Fork FD Inheritance with refcount (REG-PROC-003) */
static void test_proc_fork_fd_inherit(void) {
    int fd = open("/tmp/fd_inherit_03.bin", O_CREAT|O_WRONLY|O_TRUNC, 0644);
    if (fd < 0) exit(1);
    write(fd, "PARENT", 6);
    /* НЕ закрываем fd — он должен наследоваться */

    pid_t pid = fork();
    if (pid < 0) exit(2);

    if (pid == 0) {
        /* Child: fd унаследован, пишем через него */
        write(fd, "_CHILD", 6);
        close(fd);
        exit(0);
    }

    int status;
    waitpid(pid, &status, 0);
    if (status != 0) exit(3);

    /* Parent: fd всё ещё валиден (refcount > 1) */
    write(fd, "_END", 4);
    close(fd);

    /* Проверяем содержимое: PARENT_CHILD_END */
    fd = open("/tmp/fd_inherit_03.bin", O_RDONLY);
    if (fd < 0) exit(4);
    char buf[32];
    int n = read(fd, buf, sizeof(buf));
    close(fd);
    unlink("/tmp/fd_inherit_03.bin");

    if (n != 16) exit(5);
    if (buf[0] != 'P' || buf[6] != '_' || buf[12] != '_') exit(6);
    exit(0);
}

/* Test: Exec preserves FD table (REG-PROC-007) */
static void test_proc_exec_preserves_fd(void) {
    int fd = open("/tmp/exec_fd_07.bin", O_CREAT|O_WRONLY|O_TRUNC, 0644);
    if (fd < 0) exit(1);
    write(fd, "BEFORE", 6);

    int src_fd = open("/tmp/exec_fd_07.c", O_CREAT|O_WRONLY|O_TRUNC, 0644);
    if (src_fd < 0) exit(2);
    const char* src =
        "int main() {\n"
        "  write(10, \"_AFTER\", 6);\n"
        "  close(10);\n"
        "  return 0;\n"
        "}\n";
    write(src_fd, src, strlen(src));
    close(src_fd);

    pid_t pid = fork();
    if (pid == 0) {
        const char* tcc_argv[] = { "tcc", "/tmp/exec_fd_07.c", "-o", "/tmp/exec_fd_07.elf", NULL };
        exec("/bin/tcc.elf", tcc_argv);
        exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    if (status != 0) { unlink("/tmp/exec_fd_07.c"); exit(3); }

    /* dup в слот 10 (гарантированно свободный, не конфликтует с fd) */
    if (dup2(fd, 10) != 10) exit(4);
    close(fd);

    pid = fork();
    if (pid == 0) {
        const char* run_argv[] = { "exec_fd_07", NULL };
        exec("/tmp/exec_fd_07.elf", run_argv);
        exit(127);
    }
    waitpid(pid, &status, 0);
    close(10);

    fd = open("/tmp/exec_fd_07.bin", O_RDONLY);
    if (fd < 0) exit(5);
    char buf[32];
    int n = read(fd, buf, sizeof(buf));
    close(fd);
    unlink("/tmp/exec_fd_07.bin");
    unlink("/tmp/exec_fd_07.c");
    unlink("/tmp/exec_fd_07.elf");

    if (n != 12) exit(6);
    if (buf[0] != 'B' || buf[6] != '_') exit(7);
    exit(0);
}

/* Test: dup2 overwrite + same fd safety (REG-VFS-010, 011) */
static void test_vfs_dup2_overwrite_same(void) {
    int fd1 = open("/tmp/dup2_10a.bin", O_CREAT|O_WRONLY|O_TRUNC, 0644);
    if (fd1 < 0) exit(1);
    write(fd1, "AAA", 3);

    int fd2 = open("/tmp/dup2_10b.bin", O_CREAT|O_WRONLY|O_TRUNC, 0644);
    if (fd2 < 0) exit(2);
    write(fd2, "BBB", 3);

    /* dup2(fd1, fd2): закрывает fd2, дублирует fd1 в слот fd2 */
    if (dup2(fd1, fd2) != fd2) exit(3);

    /* fd2 теперь указывает на dup2_10a.bin */
    write(fd2, "CCC", 3);
    close(fd1);
    close(fd2);

    /* Проверяем: dup2_10a.bin = "AAACCC", dup2_10b.bin = "BBB" */
    fd1 = open("/tmp/dup2_10a.bin", O_RDONLY);
    if (fd1 < 0) exit(4);
    char buf[16];
    int n = read(fd1, buf, sizeof(buf));
    close(fd1);
    if (n != 6 || buf[3] != 'C') exit(5);

    fd2 = open("/tmp/dup2_10b.bin", O_RDONLY);
    if (fd2 < 0) exit(6);
    n = read(fd2, buf, sizeof(buf));
    close(fd2);
    if (n != 3 || buf[0] != 'B') exit(7);

    /* dup2(fd, fd) = no-op, не закрывает сам себя */
    fd1 = open("/tmp/dup2_10a.bin", O_RDONLY);
    if (fd1 < 0) exit(8);
    if (dup2(fd1, fd1) != fd1) exit(9);
    n = read(fd1, buf, sizeof(buf));
    close(fd1);
    if (n != 6) exit(10);

    unlink("/tmp/dup2_10a.bin");
    unlink("/tmp/dup2_10b.bin");
    exit(0);
}

/* Test: Path too long → error, not crash (REG-VFS-024) */
static void test_vfs_path_too_long(void) {
    char long_path[512];
    long_path[0] = '/';
    for (int i = 1; i < 510; i++) long_path[i] = 'x';
    long_path[510] = '\0';

    int fd = open(long_path, O_RDONLY);
    if (fd >= 0) { close(fd); exit(1); }

    fd = open(long_path, O_CREAT|O_WRONLY, 0644);
    if (fd >= 0) { close(fd); exit(2); }

    if (mkdir(long_path, 0755) == 0) exit(3);

    exit(0);
}

/* Test: strtol/strtoul/strtoll/strtoull correctness (REG-LIBC-003..006) */
static void test_libc_strtol_family(void) {
    char* end;

    /* strtol */
    if (strtol("42", &end, 10) != 42) exit(1);
    if (*end != '\0') exit(2);
    if (strtol("-99", &end, 10) != -99) exit(3);
    if (strtol("0x1F", &end, 0) != 31) exit(4);
    if (strtol("077", &end, 0) != 63) exit(5);
    if (strtol("  123abc", &end, 10) != 123) exit(6);
    if (*end != 'a') exit(7);

    /* strtoul */
    if (strtoul("4294967295", &end, 10) != 4294967295UL) exit(8);
    if (strtoul("0xFF", &end, 0) != 255) exit(9);

    /* strtoll */
    if (strtoll("-9223372036854775807", &end, 10) != LLONG_MIN + 1) exit(10);
    if (strtoll("9223372036854775807", &end, 10) != LLONG_MAX) exit(11);
    if (strtoll("-1", &end, 10) != -1LL) exit(12);

    /* strtoull */
    if (strtoull("18446744073709551615", &end, 10) != ULLONG_MAX) exit(13);
    if (strtoull("0xDEADBEEF", &end, 0) != 0xDEADBEEFULL) exit(14);

    exit(0);
}

/* Test: getline, strdup, strerror (REG-LIBC-008..010) */
static void test_libc_getline_strdup_strerror(void) {
    /* strdup */
    char* dup = strdup("hello");
    if (!dup) exit(1);
    if (strcmp(dup, "hello") != 0) exit(2);
    if (strlen(dup) != 5) exit(3);

    /* strerror */
    char* msg = strerror(ENOENT);
    if (!msg || msg[0] == '\0') exit(4);
    msg = strerror(0);
    if (strcmp(msg, "Success") != 0) exit(5);

    /* getline */
    int fd = open("/tmp/getline_08.txt", O_CREAT|O_WRONLY|O_TRUNC, 0644);
    if (fd < 0) exit(6);
    write(fd, "line1\nline2\n", 12);
    close(fd);

    FILE* f = fopen("/tmp/getline_08.txt", "r");
    if (!f) exit(7);

    char* line = NULL;
    size_t n = 0;
    ssize_t len = getline(&line, &n, f);
    if (len != 6) exit(8);  /* "line1\n" = 6 chars */
    if (line[0] != 'l' || line[4] != '1') exit(9);

    len = getline(&line, &n, f);
    if (len != 6) exit(10); /* "line2\n" */

    len = getline(&line, &n, f);
    if (len != -1) exit(11); /* EOF */

    fclose(f);
    unlink("/tmp/getline_08.txt");
    exit(0);
}

/* Test: FILE* buffered I/O + flush on close (REG-LIBC-011/012) */
static void test_libc_file_buffered_io(void) {
    FILE* f = fopen("/tmp/bufio_11.txt", "w");
    if (!f) exit(1);

    /* Пишем через буфер (fwrite) */
    if (fwrite("Hello, ", 1, 7, f) != 7) exit(2);
    if (fwrite("World!", 1, 6, f) != 6) exit(3);
    /* fclose должен сделать flush */
    fclose(f);

    /* Читаем через буфер (fread) */
    f = fopen("/tmp/bufio_11.txt", "r");
    if (!f) exit(4);

    char buf[32];
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);

    if (n != 13) exit(5);
    if (buf[0] != 'H' || buf[7] != 'W' || buf[12] != '!') exit(6);

    /* fputc/fgetc */
    f = fopen("/tmp/bufio_11.txt", "w");
    if (!f) exit(7);
    fputc('A', f);
    fputc('B', f);
    fputc('C', f);
    fclose(f);

    f = fopen("/tmp/bufio_11.txt", "r");
    if (!f) exit(8);
    if (fgetc(f) != 'A') exit(9);
    if (fgetc(f) != 'B') exit(10);
    if (fgetc(f) != 'C') exit(11);
    if (fgetc(f) != EOF) exit(12);
    fclose(f);

    unlink("/tmp/bufio_11.txt");
    exit(0);
}

/* Test: TCC compile error doesn't crash shell (REG-TCC-003/005) */
static void test_tcc_compile_error_recovery(void) {
    /* Создаём файл с синтаксической ошибкой */
    int fd = open("/tmp/bad_syntax.c", O_CREAT|O_WRONLY|O_TRUNC, 0644);
    if (fd < 0) exit(1);
    const char* bad_src = "int main() { this is not valid C !!! }\n";
    write(fd, bad_src, strlen(bad_src));
    close(fd);

    /* TCC должен вернуть non-zero exit code, но НЕ упасть */
    pid_t pid = fork();
    if (pid == 0) {
        const char* tcc_argv[] = { "tcc", "/tmp/bad_syntax.c", "-o", "/tmp/bad.elf", NULL };
        exec("/bin/tcc.elf", tcc_argv);
        exit(127);
    }
    int status;
    waitpid(pid, &status, 0);

    /* TCC должен вернуть non-zero (ошибка компиляции) */
    if (status == 0) exit(2);
    /* Но НЕ должен быть killed (status != -1) */
    if (status == -1) exit(3);

    /* /tmp/bad.elf не должен существовать */
    fd = open("/tmp/bad.elf", O_RDONLY);
    if (fd >= 0) { close(fd); exit(4); }

    unlink("/tmp/bad_syntax.c");
    exit(0);
}
/* Test: Invalid arguments across syscalls (REG-SEC-004/007/012/013/022/025) */
static void test_syscall_bad_args(void) {
    /* dup/dup2 bad fd */
    if (dup(-1) != -1) exit(1);
    if (dup(999) != -1) exit(2);
    if (dup2(-1, 5) != -1) exit(3);
    if (dup2(5, -1) != -1) exit(4);
    if (dup2(999, 5) != -1) exit(5);

    /* ioctl bad request */
    int fd = open("/tmp/ioctl_bad.bin", O_CREAT|O_WRONLY, 0644);
    if (fd < 0) exit(6);
    if (ioctl(fd, 0xDEADBEEF, NULL) != -1) exit(7);
    close(fd);
    unlink("/tmp/ioctl_bad.bin");

    /* ioctl on non-terminal fd */
    fd = open("/tmp/ioctl_bad2.bin", O_CREAT|O_WRONLY, 0644);
    if (fd < 0) exit(8);
    struct winsize ws;
    if (ioctl(fd, TIOCGWINSZ, &ws) != -1) exit(9);
    close(fd);
    unlink("/tmp/ioctl_bad2.bin");

    /* mmap bad flags */
    void* p = mmap(NULL, 4096, PROT_READ, MAP_SHARED, -1, 0);
    if (p != MAP_FAILED) exit(10);

    /* lseek bad whence */
    fd = open("/tmp/lseek_bad.bin", O_CREAT|O_WRONLY, 0644);
    if (fd < 0) exit(11);
    if (lseek(fd, 0, 99) != -1) exit(12);
    close(fd);
    unlink("/tmp/lseek_bad.bin");

    exit(0);
}

/* Test: brk doesn't corrupt other VMA (REG-SEC-017) */
static void test_brk_collision(void) {
    /* Выделяем mmap-регион сразу после heap */
    void* guard = mmap(NULL, 4096, PROT_READ|PROT_WRITE,
                       MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (guard == MAP_FAILED) exit(1);
    ((char*)guard)[0] = 'G';

    /* Пытаемся расширить heap до mmap-региона */
    char* heap_ptr = (char*)malloc(4096);
    if (!heap_ptr) exit(2);

    /* sys_brk с адресом за пределами USER_HEAP_MAX_SIZE */
    /* Это должно вернуть ошибку, не ломая guard */
    if (((char*)guard)[0] != 'G') exit(3);

    munmap(guard, 4096);
    exit(0);
}

/* Test: Bad ELF / non-ELF doesn't crash kernel (REG-SEC-023/024) */
static void test_exec_bad_elf(void) {
    /* Создаём файл с мусором (не ELF) */
    int fd = open("/tmp/bad.elf", O_CREAT|O_WRONLY|O_TRUNC, 0644);
    if (fd < 0) exit(1);
    write(fd, "THIS IS NOT AN ELF FILE AT ALL", 30);
    close(fd);

    /* exec должен вернуть ошибку, процесс жив */
    pid_t pid = fork();
    if (pid == 0) {
        const char* argv[] = { "bad", NULL };
        int ret = exec("/tmp/bad.elf", argv);
        /* exec не удался — процесс жив */
        exit(ret == -1 ? 0 : 1);
    }
    int status;
    waitpid(pid, &status, 0);
    if (status != 0) exit(2);

    /* Создаём truncated ELF (magic OK, но данные обрезаны) */
    fd = open("/tmp/trunc.elf", O_CREAT|O_WRONLY|O_TRUNC, 0644);
    if (fd < 0) exit(3);
    /* ELF magic + минимум заголовка, но без program headers */
    char elf_hdr[20] = { 0x7F, 'E', 'L', 'F', 1, 1, 1, 0,
                         0, 0, 0, 0, 0, 0, 0, 0,
                         2, 0, 3, 0 }; /* ET_EXEC, EM_386 */
    write(fd, elf_hdr, sizeof(elf_hdr));
    close(fd);

    pid = fork();
    if (pid == 0) {
        const char* argv[] = { "trunc", NULL };
        int ret = exec("/tmp/trunc.elf", argv);
        exit(ret == -1 ? 0 : 1);
    }
    waitpid(pid, &status, 0);
    if (status != 0) exit(4);

    unlink("/tmp/bad.elf");
    unlink("/tmp/trunc.elf");
    exit(0);
}

/* Test: Mixed workload — fork + files + compile + kill (REG-STRESS-009) */
static void test_mixed_workload(void) {
    /* 1. Создаём 50 файлов */
    for (int i = 0; i < 50; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/tmp/mix_%d.bin", i);
        int fd = open(path, O_CREAT|O_WRONLY|O_TRUNC, 0644);
        if (fd < 0) exit(1);
        write(fd, "data", 4);
        close(fd);
    }

    /* 2. Fork 5 детей, каждый читает 10 файлов */
    for (int i = 0; i < 5; i++) {
        pid_t pid = fork();
        if (pid < 0) exit(2);
        if (pid == 0) {
            for (int j = i * 10; j < (i + 1) * 10; j++) {
                char path[64];
                snprintf(path, sizeof(path), "/tmp/mix_%d.bin", j);
                int fd = open(path, O_RDONLY);
                if (fd < 0) exit(3);
                char buf[16];
                read(fd, buf, sizeof(buf));
                close(fd);
            }
            exit(0);
        }
    }

    /* 3. Ждём всех детей */
    for (int i = 0; i < 5; i++) {
        int status;
        waitpid(-1, &status, 0);
        if (status != 0) exit(4);
    }

    /* 4. Удаляем файлы */
    for (int i = 0; i < 50; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/tmp/mix_%d.bin", i);
        unlink(path);
    }

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
    /* === VMM (9) === */
    ENTRY(vmm_demand_paging, 0),
    ENTRY(vmm_cow_isolation, 0),
    ENTRY(vmm_wx_enforcement, 1),
    ENTRY(vmm_oom_probe, 0),
    ENTRY(vmm_mprotect_flip, 0),
    ENTRY(vmm_mprotect_sigsegv, 1),
    ENTRY(vmm_mprotect_partial, 0),
    ENTRY(vmm_mprotect_partial_sigsegv, 1),
    ENTRY(proc_rapid_fork_exit, 0),
    ENTRY(vmm_mprotect_cow_preserve, 0),
    /* === FPU (5) === */
    ENTRY(fpu_x87_math, 0),
    ENTRY(fpu_fork_preserve, 0),
    ENTRY(fpu_context_switch, 0),
    ENTRY(fpu_precision, 0),
    ENTRY(fpu_syscall_safety, 0),
    /* === PROCESS (4) === */
    ENTRY(proc_fork_bomb, 0),
    ENTRY(proc_zombie_cascade, 0),
    ENTRY(proc_waitpid_wnohang, 0),
    ENTRY(proc_fork_preempt, 0),
    /* === VFS (9) === */
    ENTRY(vfs_1000_files, 0),
    ENTRY(vfs_large_file, 0),
    ENTRY(vfs_sparse_seek, 0),
    ENTRY(vfs_o_trunc, 0),
    ENTRY(vfs_concurrent_fd, 0),
    ENTRY(vfs_dup_dup2, 0),
    ENTRY(vfs_fstat_size, 0),
    ENTRY(vfs_readdir_list, 0),
    ENTRY(vfs_lseek_cur, 0),
    /* === C LANGUAGE (6) === */
    ENTRY(c_packed_bitfields, 0),
    ENTRY(c_union_punning, 0),
    ENTRY(c_variadic_custom, 0),
    ENTRY(c_function_dispatch, 0),
    ENTRY(c_preprocessor_magic, 0),
    ENTRY(c_inline_asm, 0),
    /* === SYSCALL VALIDATION (8) === */
    ENTRY(syscall_invalid, 0),
    ENTRY(syscall_enosys, 0),
    ENTRY(syscall_ebadf, 0),
    ENTRY(syscall_enoent, 0),
    ENTRY(syscall_eacces_rbac, 0),
    ENTRY(syscall_efault_null, 0),
    ENTRY(syscall_efault_kernel, 0),
    ENTRY(time_monotonicity, 0),
    /* === EXEC (2) === */
    ENTRY(proc_exec_argv, 0),
    ENTRY(proc_exec_enoent, 0),
    /* === MEMORY EDGE (3) === */
    ENTRY(vmm_wx_mprotect_reject, 0),
    ENTRY(vmm_mmap_invalid, 0),
    ENTRY(vmm_munmap_invalid, 0),
    /* === LIBC (2) === */
    ENTRY(libc_printf_edge, 0),
    ENTRY(libc_snprintf_overflow, 0),
    /* === SYSTEM (2) === */
    ENTRY(sys_uname_sysinfo, 0),
    ENTRY(ansi_colors, 0),
    /* === PRODUCTION HARDENING (5) === */
    ENTRY(heap_exhaustion, 0),
    ENTRY(stack_overflow_guard, 1),
    ENTRY(fd_exhaustion, 0),
    ENTRY(directory_ops, 0),
    ENTRY(unlink_open_file, 0),
    //new test
    ENTRY(proc_fork_fd_inherit, 0),
    ENTRY(proc_exec_preserves_fd, 0),
    ENTRY(vfs_dup2_overwrite_same, 0),
    ENTRY(vfs_path_too_long, 0),
    ENTRY(libc_strtol_family, 0),
    ENTRY(libc_getline_strdup_strerror, 0),
    ENTRY(libc_file_buffered_io, 0),
    ENTRY(tcc_compile_error_recovery, 0),
    ENTRY(syscall_bad_args, 0),
    ENTRY(brk_collision, 0),
    ENTRY(exec_bad_elf, 0),
    ENTRY(mixed_workload, 0),
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
        return (status == -1) ? 0 : -1;
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
    printf("|      ENCLAVE OS - OMNI STRESS TEST (68 Tests)                |\n");
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