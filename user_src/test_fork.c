#include "user_syscalls.h"

// Глобальная переменная для проверки Copy-on-Write
int shared_var = 100;

void print_str(const char* str) {
    int len = 0;
    while (str[len]) len++;
    sys_write(1, str, len);
}

void print_int(int n) {
    char buf[16];
    int i = 0;
    if (n == 0) { buf[i++] = '0'; buf[i] = '\0'; sys_write(1, buf, 1); return; }
    if (n < 0) { sys_write(1, "-", 1); n = -n; }
    char tmp[16];
    int j = 0;
    while (n > 0) { tmp[j++] = '0' + (n % 10); n /= 10; }
    while (j > 0) buf[i++] = tmp[--j];
    buf[i] = '\0';
    sys_write(1, buf, i);
}

void _start() {
    print_str("[TEST FORK] Starting fork test...\n");

    int pid = sys_fork();

    if (pid < 0) {
        print_str("[TEST FORK] FAIL: sys_fork failed!\n");
        sys_exit(1);
    }

    if (pid == 0) {
        // === CHILD PROCESS ===
        print_str("[CHILD] I am the child! Modifying shared_var...\n");
        shared_var = 999; // 🛡️ Это триггерит Copy-on-Write Page Fault!

        print_str("[CHILD] shared_var is now: ");
        print_int(shared_var);
        print_str("\n");

        print_str("[CHILD] Exiting with code 42.\n");
        sys_exit(42);
    } else {
        // === PARENT PROCESS ===
        print_str("[PARENT] I am the parent! Child PID: ");
        print_int(pid);
        print_str("\n");

        print_str("[PARENT] Waiting for child to exit...\n");
        int status = 0;
        int waited_pid = sys_waitpid(pid, &status, 0);

        print_str("[PARENT] Child reaped! PID: ");
        print_int(waited_pid);
        print_str(", Status: ");
        print_int(status);
        print_str("\n");

        // Проверяем CoW: переменная родителя НЕ должна измениться!
        print_str("[PARENT] Checking Copy-on-Write... shared_var = ");
        print_int(shared_var);
        print_str("\n");

        if (shared_var == 100 && status == 42) {
            print_str("[TEST FORK] PASS: CoW and waitpid work perfectly!\n");
            sys_exit(0);
        } else {
            print_str("[TEST FORK] FAIL: Memory corruption or waitpid error!\n");
            sys_exit(1);
        }
    }
}
