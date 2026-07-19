#include "user_libc.h"
#include "user_syscalls.h"

int main(void) {
    // 🛡️ [ДЕНЬ 28] DevFS: Открываем /dev/console для fd 0, 1, 2
    int console_fd = open("/dev/console", O_RDWR);
    
    if (console_fd < 0) {
        // Fallback: если DevFS не работает (не должно случиться)
        fprintf(stderr, "[INIT] FATAL: Failed to open /dev/console\n");
        // Бесконечный цикл, чтобы не упасть и не вызвать reboot loop
        while(1) { sleep(10); }
    }
    
    // console_fd должен быть 0 (первый свободный FD)
    if (console_fd != 0) {
        // Если вдруг fd 0 был занят, принудительно делаем dup2
        dup2(console_fd, 0);
        close(console_fd);
    }
    
    // Дублируем fd 0 в fd 1 (stdout) и fd 2 (stderr)
    dup2(0, 1);
    dup2(0, 2);
    
    printf("[INIT] Enclave OS Init Task (PID 1) started\n");
    printf("[INIT] /dev/console bound to FD 0, 1, 2\n");

    // Главный цикл: запускаем Shell и ждем его смерти
    while (1) {
        pid_t shell_pid = fork();

        if (shell_pid < 0) {
            fprintf(stderr, "[INIT] CRITICAL: fork() failed, retrying in 1s\n");
            sleep(1);
            continue;
        }

        if (shell_pid == 0) {
            // Ребенок → запускаем Shell
            // FD 0, 1, 2 уже унаследованы от Init через fork()
            const char* shell_path = "/bin/shell.elf";
            const char* argv[] = {"shell", NULL};

            exec(shell_path, argv);

            fprintf(stderr, "[INIT] Failed to exec %s\n", shell_path);
            exit(127);
        }

        // Родитель (Init) → ждем смерти Shell
        int status;
        waitpid(shell_pid, &status, 0);

        if (status == 0) {
            printf("[INIT] Shell exited normally, respawning...\n");
        } else {
            fprintf(stderr, "[INIT] Shell crashed (exit code %d), respawning...\n", status);
        }

        sleep(1); // Защита от fork bomb
    }

    return 0;
}