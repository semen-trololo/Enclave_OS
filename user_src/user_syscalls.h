#ifndef USER_SYSCALLS_H
#define USER_SYSCALLS_H

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

// ========================================================================
// Номера системных вызовов (Синхронизировано с include/syscall.h)
// Стандарт Linux x86 (i386) ABI — SSOT
// ========================================================================
#define SYS_EXIT        1
#define SYS_FORK        2
#define SYS_READ        3
#define SYS_WRITE       4
#define SYS_OPEN        5
#define SYS_CLOSE       6
#define SYS_WAITPID     7
#define SYS_UNLINK     10
#define SYS_EXEC       11
#define SYS_LSEEK      19   // ✅ [ДЕНЬ 13] SSOT
#define SYS_FSTAT      28   // ✅ [ДЕНЬ 13] SSOT
#define SYS_BRK        45
#define SYS_IOCTL      54   // ✅ [ДЕНЬ 13] SSOT
#define SYS_UNAME      63   // ✅ [ДЕНЬ 15] SSOT
#define SYS_GETTIMEOFDAY 78 // ✅ [ДЕНЬ 15] SSOT
#define SYS_MMAP       90
#define SYS_MUNMAP     91
#define SYS_SYSINFO   116   // ✅ [ДЕНЬ 15] SSOT
#define SYS_GETPID    122   // 🛡️ FIX: Было 20 (x86_64), стало 122 (i386)
#define SYS_MPROTECT  125
#define SYS_YIELD     158
#define SYS_SLEEP     230   // ✅ [ДЕНЬ 15] SSOT

// ============================================================================
// [ДЕНЬ 13] SEEK CONSTANTS (SSOT sync)
// ============================================================================
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

// ============================================================================
// [ДЕНЬ 15] WNOHANG for waitpid
// ============================================================================
#define WNOHANG 1

// ============================================================================
// Универсальный макрос для syscall (до 3 аргументов)
// ============================================================================
static inline int syscall3(int num, int a1, int a2, int a3) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a" (ret)
        : "a" (num), "b" (a1), "c" (a2), "d" (a3)
        : "memory"
    );
    return ret;
}

static inline int syscall2(int num, int a1, int a2) {
    return syscall3(num, a1, a2, 0);
}

static inline int syscall1(int num, int a1) {
    return syscall3(num, a1, 0, 0);
}

static inline int syscall0(int num) {
    return syscall3(num, 0, 0, 0);
}

// ============================================================================
// Обертки для POSIX-like API
// ============================================================================
static inline void sys_exit(int code) {
    syscall1(SYS_EXIT, code);
    while(1) {} // Защита от возврата в никуда
}

static inline int sys_fork(void) {
    return syscall0(SYS_FORK);
}

static inline int sys_read(int fd, void* buf, uint32_t count) {
    return syscall3(SYS_READ, fd, (int)buf, count);
}

static inline int sys_write(int fd, const void* buf, uint32_t count) {
    return syscall3(SYS_WRITE, fd, (int)buf, count);
}

static inline int sys_open(const char* path, int flags, int mode) {
    return syscall3(SYS_OPEN, (int)path, flags, mode);
}


static inline int sys_close(int fd) {
    return syscall1(SYS_CLOSE, fd);
}

static inline int sys_waitpid(int pid, int* status, int options) {
    return syscall3(SYS_WAITPID, pid, (int)status, options);
}

static inline int sys_unlink(const char* path) {
    return syscall1(SYS_UNLINK, (int)path);
}

static inline int sys_exec(const char* path, const char** argv) {
    return syscall2(SYS_EXEC, (int)path, (int)argv);
}

static inline int sys_getpid(void) {
    return syscall0(SYS_GETPID);
}

static inline int sys_brk(uint32_t new_brk) {
    return syscall1(SYS_BRK, new_brk);
}

static inline void sys_yield(void) {
    syscall0(SYS_YIELD);
}

// ============================================================================
// [ДЕНЬ 13] sys_lseek
// ============================================================================
static inline int sys_lseek(int fd, int offset, int whence) {
    int result;
    __asm__ volatile(
        "mov $19, %%eax\n"
        "mov %1, %%ebx\n"
        "mov %2, %%ecx\n"
        "mov %3, %%edx\n"
        "int $0x80\n"
        "mov %%eax, %0"
        : "=r"(result)
        : "r"(fd), "r"(offset), "r"(whence)
        : "eax", "ebx", "ecx", "edx", "memory"
    );
    return result;
}

// ============================================================================
// [ДЕНЬ 13] sys_fstat
// ============================================================================
static inline int sys_fstat(int fd, void* stat_buf) {
    int result;
    __asm__ volatile(
        "mov $28, %%eax\n"
        "mov %1, %%ebx\n"
        "mov %2, %%ecx\n"
        "int $0x80\n"
        "mov %%eax, %0"
        : "=r"(result)
        : "r"(fd), "r"(stat_buf)
        : "eax", "ebx", "ecx", "memory"
    );
    return result;
}

// ============================================================================
// [ДЕНЬ 13] sys_ioctl
// ============================================================================
static inline int sys_ioctl(int fd, unsigned int request, void* argp) {
    int result;
    __asm__ volatile(
        "mov $54, %%eax\n"
        "mov %1, %%ebx\n"
        "mov %2, %%ecx\n"
        "mov %3, %%edx\n"
        "int $0x80\n"
        "mov %%eax, %0"
        : "=r"(result)
        : "r"(fd), "r"(request), "r"(argp)
        : "eax", "ebx", "ecx", "edx", "memory"
    );
    return result;
}

// ============================================================================
// Day 12: Memory Management Syscalls
// ============================================================================
static inline void* sys_mmap(void* addr, uint32_t len, uint32_t prot, uint32_t flags, int fd, uint32_t offset) {
    void* ret;
    __asm__ volatile (
        "push %%ebp\n\t"
        "mov %7, %%ebp\n\t"
        "int $0x80\n\t"
        "pop %%ebp"
        : "=a" (ret)
        : "a" (SYS_MMAP), "b" (addr), "c" (len), "d" (prot), "S" (flags), "D" (fd), "rm" (offset)
        : "memory"
    );
    return ret;
}

static inline int sys_munmap(void* addr, uint32_t len) {
    return syscall2(SYS_MUNMAP, (int)addr, (int)len);
}

static inline int sys_mprotect(void* addr, uint32_t len, uint32_t prot) {
    return syscall3(SYS_MPROTECT, (int)addr, (int)len, (int)prot);
}

// ============================================================================
// [ДЕНЬ 15] Time & System Info Syscalls
// ============================================================================
static inline int sys_gettimeofday(void* tv) {
    return syscall1(SYS_GETTIMEOFDAY, (int)tv);
}

static inline int sys_uname(void* buf) {
    return syscall1(SYS_UNAME, (int)buf);
}

static inline int sys_sysinfo(void* info) {
    return syscall1(SYS_SYSINFO, (int)info);
}

static inline int sys_sleep(uint32_t ms) {
    return syscall1(SYS_SLEEP, (int)ms);
}

#endif // USER_SYSCALLS_H
