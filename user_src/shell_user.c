// ============================================================================
// SHELL USER — Ring 3 Shell для Enclave Operating System
// Полноценный user-space shell, работающий ТОЛЬКО через syscalls.
// Заменяет kernel shell (shell.c) и обеспечивает Zero Trust Sandbox.
// ============================================================================

#include "user_libc.h"
#include "user_syscalls.h"

#define CMD_BUFFER_SIZE 256
#define MAX_ARGS 16
#define MAX_ARG_LEN 64

// ============================================================================
// ПАРСИНГ АРГУМЕНТОВ
// ============================================================================
static int parse_args(char* buffer, char args[MAX_ARGS][MAX_ARG_LEN]) {
    int argc = 0;
    char* ptr = buffer;

    for (int i = 0; i < MAX_ARGS; i++) args[i][0] = '\0';

    while (*ptr && argc < MAX_ARGS) {
        while (*ptr == ' ') ptr++;
        if (*ptr == '\0') break;

        int i = 0;
        while (*ptr && *ptr != ' ' && i < MAX_ARG_LEN - 1) {
            args[argc][i++] = *ptr++;
        }
        args[argc][i] = '\0';
        argc++;
    }
    return argc;
}

// ============================================================================
// СПРАВКА (HELP)
// ============================================================================
static void print_help(void) {
    printf("=== Enclave OS - Available Commands ===\n");
    printf("  [ General ]\n");
    printf("  help             - Show this help message\n");
    printf("  clear            - Clear the screen\n");
    printf("  uptime           - Show system uptime\n");
    printf("  ps               - List running processes\n");
    printf("  exit             - Exit shell (Init will respawn it)\n");
    printf("  run <elf> [args] - Execute ELF binary with arguments\n");
    printf("  compile <c> [out]- Compile C file using TinyCC\n");
    printf("  [ File System (VFS) ]\n");
    printf("  ls [path]        - List directory contents\n");
    printf("  cat <path>       - Print file contents\n");
    printf("  mkdir <path>     - Create directory\n");
    printf("  rm <path>        - Remove file\n");
    printf("  [ Memory Management ]\n");
    printf("  memmap           - Show E820 physical memory map\n");
    printf("  sysinfo          - Show system statistics\n");
    printf("\n");
}

// ============================================================================
// ОБРАБОТЧИКИ КОМАНД
// ============================================================================

static void handle_clear(void) {
    // ANSI escape code для очистки экрана
    printf("\033[2J\033[H");
}

static void handle_uptime(void) {
    timeval_t tv;
    gettimeofday(&tv, NULL);

    uint32_t total_seconds = tv.tv_sec;
    uint32_t hours   = total_seconds / 3600;
    uint32_t minutes = (total_seconds % 3600) / 60;
    uint32_t seconds = total_seconds % 60;

    printf("System Uptime: %u hours, %u minutes, %u seconds\n",
           hours, minutes, seconds);
}

static void handle_sysinfo(void) {
    sysinfo_t info;
    sysinfo(&info);

    printf("=== System Information ===\n");
    printf("Uptime:       %u seconds\n", info.uptime);
    printf("Total RAM:    %u MB\n", info.totalram / (1024 * 1024));
    printf("Free RAM:     %u MB\n", info.freeram / (1024 * 1024));
    printf("Processes:    %u\n", info.procs);
    printf("\n");
}

static void handle_ls(int argc, char args[MAX_ARGS][MAX_ARG_LEN]) {
    const char* path = "/";
    if (argc > 1) path = args[1];

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "ls: cannot access '%s': %s\n", path, strerror(errno));
        return;
    }

    dirent_t entry;
    uint32_t index = 0;

    printf("  %s\n", entry.d_name);

    int32_t res = sys_readdir(fd, index, &entry);
    while (res == 0) {
        printf("  %s\n", entry.d_name);
        index++;
        res = sys_readdir(fd, index, &entry);
    }

    close(fd);
}

static void handle_cat(int argc, char args[MAX_ARGS][MAX_ARG_LEN]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: cat <filepath>\n");
        return;
    }

    const char* path = args[1];
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "cat: %s: %s\n", path, strerror(errno));
        return;
    }

    char buffer[512];
    ssize_t bytes_read;

    while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
        write(STDOUT_FILENO, buffer, bytes_read);
    }

    printf("\n");
    close(fd);
}

static void handle_mkdir(int argc, char args[MAX_ARGS][MAX_ARG_LEN]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: mkdir <path>\n");
        return;
    }

    // В tmpfs директории создаются через open с O_CREAT
    int fd = open(args[1], O_CREAT | O_RDONLY, 0755);
    if (fd < 0) {
        fprintf(stderr, "mkdir: %s: %s\n", args[1], strerror(errno));
        return;
    }
    close(fd);
    printf("Created directory: %s\n", args[1]);
}

static void handle_rm(int argc, char args[MAX_ARGS][MAX_ARG_LEN]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: rm <path>\n");
        return;
    }

    int ret = unlink(args[1]);
    if (ret < 0) {
        fprintf(stderr, "rm: %s: %s\n", args[1], strerror(errno));
        return;
    }
    printf("Removed: %s\n", args[1]);
}

static void handle_run(int argc, char args[MAX_ARGS][MAX_ARG_LEN]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: run <path/to/file.elf> [args...]\n");
        return;
    }

    printf("[SHELL] Executing %s...\n", args[1]);

    // Создаем массив указателей для argv
    char* argv[MAX_ARGS];
    for (int i = 0; i < argc - 1; i++) {
        argv[i] = args[i + 1];
    }
    argv[argc - 1] = NULL;

    pid_t pid = fork();

    if (pid < 0) {
        fprintf(stderr, "[SHELL] fork() failed: %s\n", strerror(errno));
        return;
    }

    if (pid == 0) {
        // Ребенок → запускаем программу через exec
        exec(args[1], (const char**)argv);

        // Если exec вернул управление — ошибка
        fprintf(stderr, "[SHELL] exec(%s) failed: %s\n", args[1], strerror(errno));
        exit(127);
    }

    // Родитель → ждем завершения ребенка
    int status;
    waitpid(pid, &status, 0);

    if (status == 0) {
        printf("[SHELL] Process exited successfully\n");
    } else {
        printf("[SHELL] Process exited with status %d\n", status);
    }
}

static void handle_compile(int argc, char args[MAX_ARGS][MAX_ARG_LEN]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: compile <file.c> [output.elf]\n");
        return;
    }

    const char* input_file = args[1];
    const char* output_file = (argc >= 3) ? args[2] : "/tmp/a.out";

    printf("[SHELL] Compiling %s -> %s using TinyCC\n", input_file, output_file);

    // Запускаем TinyCC через run
    pid_t pid = fork();

    if (pid < 0) {
        fprintf(stderr, "[SHELL] fork() failed: %s\n", strerror(errno));
        return;
    }

    if (pid == 0) {
        // Ребенок → запускаем tcc.elf
        const char* tcc_argv[] = {
            "tcc",
            input_file,
            "-o",
            output_file,
            NULL
        };

        exec("/bin/tcc.elf", tcc_argv);
        fprintf(stderr, "[SHELL] exec(/bin/tcc.elf) failed: %s\n", strerror(errno));
        exit(127);
    }

    // Родитель → ждем завершения компиляции
    int status;
    waitpid(pid, &status, 0);

    if (status == 0) {
        printf("[SHELL] ✓ Compiled successfully: %s\n", output_file);
    } else {
        fprintf(stderr, "[SHELL] ✗ Compilation failed (exit code %d)\n", status);
    }
}

// ============================================================================
// ДИСПЕТЧЕР КОМАНД
// ============================================================================
static void execute_command(char* buffer) {
    char args[MAX_ARGS][MAX_ARG_LEN];
    int argc = parse_args(buffer, args);

    if (argc == 0) return;

    if (strcmp(args[0], "help") == 0) {
        print_help();
    }
    else if (strcmp(args[0], "clear") == 0) {
        handle_clear();
    }
    else if (strcmp(args[0], "uptime") == 0) {
        handle_uptime();
    }
    else if (strcmp(args[0], "sysinfo") == 0) {
        handle_sysinfo();
    }
    else if (strcmp(args[0], "ls") == 0) {
        handle_ls(argc, args);
    }
    else if (strcmp(args[0], "cat") == 0) {
        handle_cat(argc, args);
    }
    else if (strcmp(args[0], "mkdir") == 0) {
        handle_mkdir(argc, args);
    }
    else if (strcmp(args[0], "rm") == 0) {
        handle_rm(argc, args);
    }
    else if (strcmp(args[0], "run") == 0) {
        handle_run(argc, args);
    }
    else if (strcmp(args[0], "compile") == 0) {
        handle_compile(argc, args);
    }
    else if (strcmp(args[0], "exit") == 0) {
        printf("[SHELL] Exiting... (Init will respawn)\n");
        exit(0);
    }
    else {
        fprintf(stderr, "Unknown command: %s\n", args[0]);
        fprintf(stderr, "Type 'help' for available commands.\n");
    }
}

// ============================================================================
// ГЛАВНЫЙ ЦИКЛ SHELL
// ============================================================================
int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    printf("Welcome to Enclave OS Shell (Ring 3)\n");
    printf("Type 'help' for available commands.\n\n");

    char buffer[CMD_BUFFER_SIZE];
    int pos = 0;

    while (1) {
        printf("> ");
        fflush(stdout);

        pos = 0;
        buffer[0] = '\0';

        // Чтение команды посимвольно
        while (1) {
            char c;
            ssize_t ret = read(STDIN_FILENO, &c, 1);
            
            if (ret <= 0) {
                // 🛡️ FIX: Буфер клавиатуры пуст (non-blocking read) или fd не открыт (EBADF).
                // В Ring 3 мы НЕ должны интерпретировать это как EOF и завершаться.
                // Просто отдаем CPU планировщику, ждем и пробуем снова.
                usleep(10); 
                continue;
            }
            
            if (c == '\n') {
                printf("\n");
                buffer[pos] = '\0';
                break;
            }
            else if (c == '\b' || c == 127) { // Backspace или DEL
                if (pos > 0) {
                    pos--;
                    buffer[pos] = '\0';
                    printf("\b \b");
                    fflush(stdout);
                }
            }
            else if (c >= 32 && c < 127) { // Printable ASCII
                if (pos < CMD_BUFFER_SIZE - 1) {
                    buffer[pos++] = c;
                    buffer[pos] = '\0';
                    printf("%c", c);
                    fflush(stdout);
                }
            }
        }

        // Выполнение команды
        execute_command(buffer);
    }

    return 0;
}
