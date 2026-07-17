// ============================================================================
// SHELL USER — Ring 3 Shell для Enclave Operating System
// Полноценный user-space shell, работающий ТОЛЬКО через syscalls.
// Версия: Day 25 Polish (ANSI Colors, Smart LS, Interactive UX)
// ============================================================================

#include "user_libc.h"
#include "user_syscalls.h"

#define CMD_BUFFER_SIZE 256
#define MAX_ARGS 16
#define MAX_ARG_LEN 64

// ============================================================================
// ANSI ESCAPE CODES (Для раскрашивания вывода в Serial/Terminal)
// ============================================================================
#define ANSI_RESET   "\033[0m"
#define ANSI_BOLD    "\033[1m"
#define ANSI_RED     "\033[31m"
#define ANSI_GREEN   "\033[32m"
#define ANSI_YELLOW  "\033[33m"
#define ANSI_BLUE    "\033[34m"
#define ANSI_MAGENTA "\033[35m"
#define ANSI_CYAN    "\033[36m"
#define ANSI_WHITE   "\033[37m"

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
    printf(ANSI_CYAN ANSI_BOLD "=== Enclave OS - Available Commands ===" ANSI_RESET "\n");
    printf(ANSI_YELLOW "  [ General ]" ANSI_RESET "\n");
    printf("  " ANSI_BOLD "help" ANSI_RESET "             - Show this help message\n");
    printf("  " ANSI_BOLD "clear" ANSI_RESET "            - Clear the screen\n");
    printf("  " ANSI_BOLD "uptime" ANSI_RESET "           - Show system uptime\n");
    printf("  " ANSI_BOLD "sysinfo" ANSI_RESET "          - Show system statistics\n");
    printf("  " ANSI_BOLD "exit" ANSI_RESET "             - Exit shell (Init will respawn it)\n");
    printf("  " ANSI_BOLD "run <elf> [args]" ANSI_RESET " - Execute ELF binary with arguments\n");
    printf("  " ANSI_BOLD "compile <c> [out]" ANSI_RESET "- Compile C file using TinyCC\n");
    printf(ANSI_YELLOW "  [ File System (VFS) ]" ANSI_RESET "\n");
    printf("  " ANSI_BOLD "ls [path]" ANSI_RESET "        - List directory contents\n");
    printf("  " ANSI_BOLD "cat <path>" ANSI_RESET "       - Print file contents\n");
    printf("  " ANSI_BOLD "mkdir <path>" ANSI_RESET "     - Create directory\n");
    printf("  " ANSI_BOLD "rm <path>" ANSI_RESET "        - Remove file\n");
    printf("\n");
}

// ============================================================================
// ОБРАБОТЧИКИ КОМАНД
// ============================================================================

static void handle_clear(void) {
    // ANSI escape code для очистки экрана и сброса курсора в 0,0
    printf("\033[2J\033[H");
    fflush(stdout);
}

static void handle_uptime(void) {
    timeval_t tv;
    gettimeofday(&tv, NULL);

    uint32_t total_seconds = tv.tv_sec;
    uint32_t hours   = total_seconds / 3600;
    uint32_t minutes = (total_seconds % 3600) / 60;
    uint32_t seconds = total_seconds % 60;

    printf(ANSI_CYAN ANSI_BOLD "System Uptime: " ANSI_RESET "%u hours, %u minutes, %u seconds\n",
           hours, minutes, seconds);
}

static void handle_sysinfo(void) {
    sysinfo_t info;
    sysinfo(&info);

    printf(ANSI_CYAN ANSI_BOLD "=== System Information ===" ANSI_RESET "\n");
    printf(ANSI_BOLD "Uptime:       " ANSI_RESET "%u seconds\n", info.uptime);
    printf(ANSI_BOLD "Total RAM:    " ANSI_RESET "%u MB\n", info.totalram / (1024 * 1024));
    printf(ANSI_BOLD "Free RAM:     " ANSI_RESET "%u MB\n", info.freeram / (1024 * 1024));
    printf(ANSI_BOLD "Processes:    " ANSI_RESET "%u\n", info.procs);
    printf("\n");
}

static void handle_ls(int argc, char args[MAX_ARGS][MAX_ARG_LEN]) {
    const char* path = "/";
    if (argc > 1) path = args[1];

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, ANSI_RED "ls: cannot access '%s': %s" ANSI_RESET "\n", path, strerror(errno));
        return;
    }

    dirent_t entry;
    uint32_t index = 0;

    int32_t res = sys_readdir(fd, index, &entry);
    while (res == 0) {
        char type_char = (entry.type == 4) ? 'd' : '-';
        int len = strlen(entry.name);
        
        if (entry.type == 4) {
            // Директория: Синий + Жирный
            printf("  %c " ANSI_BLUE ANSI_BOLD "%s" ANSI_RESET "\n", type_char, entry.name);
        } else if (len > 4 && strcmp(entry.name + len - 4, ".elf") == 0) {
            // Исполняемый файл: Зеленый + Жирный
            printf("  %c " ANSI_GREEN ANSI_BOLD "%s" ANSI_RESET "\n", type_char, entry.name);
        } else if (len > 2 && strcmp(entry.name + len - 2, ".c") == 0) {
            // Исходник C: Желтый
            printf("  %c " ANSI_YELLOW "%s" ANSI_RESET "\n", type_char, entry.name);
        } else {
            // Обычный файл
            printf("  %c %s\n", type_char, entry.name);
        }
        
        index++;
        res = sys_readdir(fd, index, &entry);
    }

    close(fd);
}

static void handle_cat(int argc, char args[MAX_ARGS][MAX_ARG_LEN]) {
    if (argc < 2) {
        fprintf(stderr, ANSI_RED "Usage: cat <filepath>" ANSI_RESET "\n");
        return;
    }

    const char* path = args[1];
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, ANSI_RED "cat: %s: %s" ANSI_RESET "\n", path, strerror(errno));
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
        fprintf(stderr, ANSI_RED "Usage: mkdir <path>" ANSI_RESET "\n");
        return;
    }

    int fd = open(args[1], O_CREAT | O_RDONLY, 0755);
    if (fd < 0) {
        fprintf(stderr, ANSI_RED "mkdir: %s: %s" ANSI_RESET "\n", args[1], strerror(errno));
        return;
    }
    close(fd);
    printf(ANSI_GREEN "Created directory: %s" ANSI_RESET "\n", args[1]);
}

static void handle_rm(int argc, char args[MAX_ARGS][MAX_ARG_LEN]) {
    if (argc < 2) {
        fprintf(stderr, ANSI_RED "Usage: rm <path>" ANSI_RESET "\n");
        return;
    }

    int ret = unlink(args[1]);
    if (ret < 0) {
        fprintf(stderr, ANSI_RED "rm: %s: %s" ANSI_RESET "\n", args[1], strerror(errno));
        return;
    }
    printf(ANSI_GREEN "Removed: %s" ANSI_RESET "\n", args[1]);
}

static void handle_run(int argc, char args[MAX_ARGS][MAX_ARG_LEN]) {
    if (argc < 2) {
        fprintf(stderr, ANSI_RED "Usage: run <path/to/file.elf> [args...]" ANSI_RESET "\n");
        return;
    }

    printf(ANSI_CYAN "Executing %s..." ANSI_RESET "\n", args[1]);

    char* argv[MAX_ARGS];
    for (int i = 0; i < argc - 1; i++) {
        argv[i] = args[i + 1];
    }
    argv[argc - 1] = NULL;

    pid_t pid = fork();

    if (pid < 0) {
        fprintf(stderr, ANSI_RED "fork() failed: %s" ANSI_RESET "\n", strerror(errno));
        return;
    }

    if (pid == 0) {
        exec(args[1], (const char**)argv);
        fprintf(stderr, ANSI_RED "exec(%s) failed: %s" ANSI_RESET "\n", args[1], strerror(errno));
        exit(127);
    }

    int status;
    waitpid(pid, &status, 0);

    if (status == 0) {
        printf(ANSI_GREEN "Process exited successfully (code 0)" ANSI_RESET "\n");
    } else {
        printf(ANSI_YELLOW "Process exited with status %d" ANSI_RESET "\n", status);
    }
}

static void handle_compile(int argc, char args[MAX_ARGS][MAX_ARG_LEN]) {
    if (argc < 2) {
        fprintf(stderr, ANSI_RED "Usage: compile <file.c> [output.elf]" ANSI_RESET "\n");
        return;
    }

    const char* input_file = args[1];
    const char* output_file = (argc >= 3) ? args[2] : "/tmp/a.out";

    printf(ANSI_CYAN "Compiling %s -> %s using TinyCC..." ANSI_RESET "\n", input_file, output_file);

    pid_t pid = fork();

    if (pid < 0) {
        fprintf(stderr, ANSI_RED "fork() failed: %s" ANSI_RESET "\n", strerror(errno));
        return;
    }

    if (pid == 0) {
        const char* tcc_argv[] = {
            "tcc",
            input_file,
            "-o",
            output_file,
            NULL
        };

        exec("/bin/tcc.elf", tcc_argv);
        fprintf(stderr, ANSI_RED "exec(/bin/tcc.elf) failed: %s" ANSI_RESET "\n", strerror(errno));
        exit(127);
    }

    int status;
    waitpid(pid, &status, 0);

    if (status == 0) {
        printf(ANSI_GREEN ANSI_BOLD "✓ Compiled successfully: %s" ANSI_RESET "\n", output_file);
    } else {
        fprintf(stderr, ANSI_RED ANSI_BOLD "✗ Compilation failed (exit code %d)" ANSI_RESET "\n", status);
    }
}

// ============================================================================
// ДИСПЕТЧЕР КОМАНД
// ============================================================================
static void execute_command(char* buffer) {
    char args[MAX_ARGS][MAX_ARG_LEN];
    int argc = parse_args(buffer, args);

    if (argc == 0) return;

    if (strcmp(args[0], "help") == 0) print_help();
    else if (strcmp(args[0], "clear") == 0) handle_clear();
    else if (strcmp(args[0], "uptime") == 0) handle_uptime();
    else if (strcmp(args[0], "sysinfo") == 0) handle_sysinfo();
    else if (strcmp(args[0], "ls") == 0) handle_ls(argc, args);
    else if (strcmp(args[0], "cat") == 0) handle_cat(argc, args);
    else if (strcmp(args[0], "mkdir") == 0) handle_mkdir(argc, args);
    else if (strcmp(args[0], "rm") == 0) handle_rm(argc, args);
    else if (strcmp(args[0], "run") == 0) handle_run(argc, args);
    else if (strcmp(args[0], "compile") == 0) handle_compile(argc, args);
    else if (strcmp(args[0], "exit") == 0) {
        printf(ANSI_YELLOW "Exiting... (Init will respawn)" ANSI_RESET "\n");
        exit(0);
    }
    else {
        fprintf(stderr, ANSI_RED "Unknown command: %s" ANSI_RESET "\n", args[0]);
        fprintf(stderr, "Type '" ANSI_YELLOW "help" ANSI_RESET "' for available commands.\n");
    }
}

// ============================================================================
// ГЛАВНЫЙ ЦИКЛ SHELL
// ============================================================================
int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    
    // Очищаем экран при старте, чтобы скрыть boot-логи ядра
    handle_clear();
    
    printf(ANSI_CYAN ANSI_BOLD "Welcome to Enclave OS Shell (Ring 3)" ANSI_RESET "\n");
    printf("Type '" ANSI_YELLOW "help" ANSI_RESET "' for available commands.\n\n");

    char buffer[CMD_BUFFER_SIZE];
    int pos = 0;

    while (1) {
        // Цветной Prompt: user@host:/# 
        printf(ANSI_GREEN ANSI_BOLD "enclave" ANSI_RESET "@" ANSI_MAGENTA "os" ANSI_RESET ":" ANSI_CYAN ANSI_BOLD "/" ANSI_RESET "# ");
        fflush(stdout);

        pos = 0;
        buffer[0] = '\0';

        // Чтение команды посимвольно
        while (1) {
            char c;
            ssize_t ret = read(STDIN_FILENO, &c, 1);
            
            if (ret <= 0) {
                // Буфер клавиатуры пуст. Отдаем CPU планировщику (sys_yield).
                sleep(0); 
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