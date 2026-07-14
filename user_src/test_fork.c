#include "user_libc.h"

// Глобальная переменная для проверки Copy-on-Write
int shared_var = 100;

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    
    printf("[TEST FORK] Starting fork test...\n");

    int pid = fork();

    if (pid < 0) {
        printf("[TEST FORK] FAIL: fork failed!\n");
        return 1;
    }

    if (pid == 0) {
        // === CHILD PROCESS ===
        printf("[CHILD] I am the child! Modifying shared_var...\n");
        shared_var = 999; // 🛡️ Это триггерит Copy-on-Write Page Fault!

        printf("[CHILD] shared_var is now: %d\n", shared_var);
        printf("[CHILD] Exiting with code 42.\n");
        return 42;
    } else {
        // === PARENT PROCESS ===
        printf("[PARENT] I am the parent! Child PID: %d\n", pid);
        printf("[PARENT] Waiting for child to exit...\n");
        
        int status = 0;
        int waited_pid = waitpid(pid, &status, 0);

        printf("[PARENT] Child reaped! PID: %d, Status: %d\n", waited_pid, status);

        // Проверяем CoW: переменная родителя НЕ должна измениться!
        printf("[PARENT] Checking Copy-on-Write... shared_var = %d\n", shared_var);

        if (shared_var == 100 && status == 42) {
            printf("[TEST FORK] PASS: CoW and waitpid work perfectly!\n");
            return 0;
        } else {
            printf("[TEST FORK] FAIL: Memory corruption or waitpid error!\n");
            return 1;
        }
    }
}