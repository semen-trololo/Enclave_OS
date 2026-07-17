// ============================================================================
// INIT — PID 1 Launcher для Enclave Operating System
// Корень дерева процессов. Запускает Shell через fork+exec.
// При падении Shell — мгновенный respawn (Let it crash philosophy).
// ============================================================================

#include "user_libc.h"
#include "user_syscalls.h"

int main(void) {

    // 🛡️ [ДЕНЬ 24] CRITICAL FIX: Занимаем fd 0, 1, 2 фиктивными файлами.
    // Это предотвращает баг, при котором sys_open возвращает 0 для обычного файла,
    // и sys_read(0) начинает читать с клавиатуры вместо файла.
    // Так как DevFS еще нет, мы открываем сам init.elf три раза.
    open("/sbin/init.elf", O_RDONLY); // Займет fd 0
    open("/sbin/init.elf", O_RDONLY); // Займет fd 1
    open("/sbin/init.elf", O_RDONLY); // Займет fd 2
    printf("[INIT] Enclave OS Init Task (PID 1) started\n");

    // Главный цикл: запускаем Shell и ждем его смерти
    while (1) {
        pid_t shell_pid = fork();

        if (shell_pid < 0) {
            // Fork failed — критическая ошибка
            fprintf(stderr, "[INIT] CRITICAL: fork() failed, retrying in 1s\n");
            sleep(1);
            continue;
        }

        if (shell_pid == 0) {
            // Ребенок → запускаем Shell
            const char* shell_path = "/bin/shell.elf";
            const char* argv[] = {"shell", NULL};

            exec(shell_path, argv);

            // Если exec вернул управление — ошибка
            fprintf(stderr, "[INIT] Failed to exec %s\n", shell_path);
            exit(127); // Standard exit code for exec failure
        }

        // Родитель (Init) → ждем смерти Shell
        int status;
        waitpid(shell_pid, &status, 0);

        // Shell умер → логируем и перезапускаем
        if (status == 0) {
            printf("[INIT] Shell exited normally, respawning...\n");
        } else {
            fprintf(stderr, "[INIT] Shell crashed (exit code %d), respawning...\n", status);
        }

        // Небольшая задержка перед respawn (защита от fork bomb)
        sleep(1);
    }

    return 0; // Never reached
}
