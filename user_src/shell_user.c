// ============================================================================
// SHELL USER — Ring 3 Shell для Enclave Operating System
// Полноценный user-space shell, работающий ТОЛЬКО через syscalls.
// Версия: Day 28 (Readline + History + Arrow Keys)
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
            printf("  %c " ANSI_BLUE ANSI_BOLD "%s" ANSI_RESET "\n", type_char, entry.name);
        } else if (len > 4 && strcmp(entry.name + len - 4, ".elf") == 0) {
            printf("  %c " ANSI_GREEN ANSI_BOLD "%s" ANSI_RESET "\n", type_char, entry.name);
        } else if (len > 2 && strcmp(entry.name + len - 2, ".c") == 0) {
            printf("  %c " ANSI_YELLOW "%s" ANSI_RESET "\n", type_char, entry.name);
        } else {
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
// [DAY 28] READLINE: History + Arrow Keys + Line Editing
// ============================================================================

#define HISTORY_SIZE 32
#define SCRATCH_SIZE CMD_BUFFER_SIZE

static char history[HISTORY_SIZE][CMD_BUFFER_SIZE];
static int history_count = 0;
static int history_view = 0;
static char scratch[SCRATCH_SIZE];
static int scratch_len = 0;
static int last_drawn_len = 0;

static void history_add(const char* line) {
    if (strlen(line) == 0) return;
    if (history_count > 0 && strcmp(history[history_count - 1], line) == 0) return;
    
    if (history_count < HISTORY_SIZE) {
        strcpy(history[history_count], line);
        history_count++;
    } else {
        for (int i = 0; i < HISTORY_SIZE - 1; i++) {
            strcpy(history[i], history[i + 1]);
        }
        strcpy(history[HISTORY_SIZE - 1], line);
    }
    history_view = history_count;
    scratch_len = 0;
}

static int ansi_visible_len(const char* s) {
    int len = 0;
    int in_escape = 0;
    
    while (*s) {
        unsigned char c = (unsigned char)*s;
        if (in_escape) {
            if (c >= 0x40 && c <= 0x7E) in_escape = 0;
        } else {
            if (c == 0x1B) in_escape = 1;
            else len++;
        }
        s++;
    }
    return len;
}

static void redraw_line(const char* prompt, const char* buffer, int cursor_pos) {
    // 1. Вернуться в начало текущей строки
    write(STDOUT_FILENO, "\r", 1);
    
    // 2. Перепечатать промпт (содержит ANSI-коды для цветов)
    int prompt_bytes = strlen(prompt);
    int prompt_visible = ansi_visible_len(prompt);
    write(STDOUT_FILENO, prompt, prompt_bytes);
    
    // 3. Перепечатать buffer полностью
    int buf_len = strlen(buffer);
    write(STDOUT_FILENO, buffer, buf_len);
    
    int current_visible = prompt_visible + buf_len;
    
    // 4. Затереть "хвосты" от предыдущей более длинной строки
    if (current_visible < last_drawn_len) {
        int spaces_needed = last_drawn_len - current_visible;
        if (current_visible + spaces_needed > 200) {
            spaces_needed = 200 - current_visible;
            if (spaces_needed < 0) spaces_needed = 0;
        }
        for (int i = 0; i < spaces_needed; i++) {
            write(STDOUT_FILENO, " ", 1);
        }
    }
    last_drawn_len = current_visible;
    
    // 5. Если курсор НЕ в конце строки — делаем reprint до позиции курсора
    //    Это гарантирует корректное позиционирование без зависимости от
    //    ANSI cursor movement (который может работать некорректно в VGA/FB)
    if (cursor_pos < buf_len) {
        write(STDOUT_FILENO, "\r", 1);
        write(STDOUT_FILENO, prompt, prompt_bytes);
        write(STDOUT_FILENO, buffer, cursor_pos);
    }
    
    fflush(stdout);
}

static int readline(const char* prompt, char* buffer, int max_size) {
    typedef enum { STATE_NORMAL, STATE_ESC, STATE_CSI } read_state_t;
    read_state_t state = STATE_NORMAL;
    
    char csi_buf[32];
    int csi_pos = 0;
    
    int pos = 0;
    int buf_len = 0;
    buffer[0] = '\0';
    
    last_drawn_len = ansi_visible_len(prompt);
    
    write(STDOUT_FILENO, prompt, strlen(prompt));
    fflush(stdout);
    
    while (1) {
        char c;
        ssize_t ret = read(STDIN_FILENO, &c, 1);
        
        if (ret <= 0) { sleep(0); continue; }
        
        if (state == STATE_ESC) {
            if (c == '[') { state = STATE_CSI; csi_pos = 0; continue; }
            else { state = STATE_NORMAL; }
        }
        else if (state == STATE_CSI) {
            if ((c >= '0' && c <= '9') || c == ';' || c == '?') {
                if (csi_pos < 31) csi_buf[csi_pos++] = c;
                continue;
            }
            if ((c >= 0x40 && c <= 0x7E)) {
                csi_buf[csi_pos] = '\0';
                state = STATE_NORMAL;
                
                if (c == 'A' && csi_pos == 0) {
                    if (history_view > 0) {
                        if (history_view == history_count) { strcpy(scratch, buffer); scratch_len = buf_len; }
                        history_view--;
                        strcpy(buffer, history[history_view]);
                        buf_len = strlen(buffer); pos = buf_len;
                        redraw_line(prompt, buffer, pos);
                    }
                    continue;
                }
                else if (c == 'B' && csi_pos == 0) {
                    if (history_view < history_count) {
                        history_view++;
                        if (history_view == history_count) { strcpy(buffer, scratch); buf_len = scratch_len; }
                        else { strcpy(buffer, history[history_view]); buf_len = strlen(buffer); }
                        pos = buf_len; redraw_line(prompt, buffer, pos);
                    }
                    continue;
                }
                else if (c == 'C' && csi_pos == 0) { if (pos < buf_len) { pos++; redraw_line(prompt, buffer, pos); } continue; }
                else if (c == 'D' && csi_pos == 0) { if (pos > 0) { pos--; redraw_line(prompt, buffer, pos); } continue; }
                else if (c == 'H' && csi_pos == 0) { if (pos != 0) { pos = 0; redraw_line(prompt, buffer, pos); } continue; }
                else if (c == 'F' && csi_pos == 0) { if (pos != buf_len) { pos = buf_len; redraw_line(prompt, buffer, pos); } continue; }
                else if (c == '~' && csi_pos == 1 && csi_buf[0] == '3') {
                    if (pos < buf_len) {
                        memmove(&buffer[pos], &buffer[pos + 1], buf_len - pos);
                        buf_len--; buffer[buf_len] = '\0'; redraw_line(prompt, buffer, pos);
                    }
                    continue;
                }
                continue;
            }
            state = STATE_NORMAL;
            continue;
        }
        
        if ((unsigned char)c == 0x1B) { state = STATE_ESC; continue; }
        
        if (c == '\n' || c == '\r') {
            write(STDOUT_FILENO, "\n", 1);
            buffer[buf_len] = '\0';
            return 0;
        }
        
        if (c == '\b' || c == 127) {
            if (pos > 0) {
                memmove(&buffer[pos - 1], &buffer[pos], buf_len - pos + 1);
                pos--; buf_len--; redraw_line(prompt, buffer, pos);
            }
            continue;
        }
        
        if ((unsigned char)c < 32) {
            switch (c) {
                case 1: if (pos != 0) { pos = 0; redraw_line(prompt, buffer, pos); } break;
                case 3:
                    write(STDOUT_FILENO, "^C\n", 3);
                    buffer[0] = '\0'; buf_len = 0; pos = 0;
                    history_view = history_count; last_drawn_len = 0;
                    return 0;
                case 4:
                    if (buf_len == 0) { write(STDOUT_FILENO, "\n", 1); return -1; }
                    if (pos < buf_len) {
                        memmove(&buffer[pos], &buffer[pos + 1], buf_len - pos);
                        buf_len--; buffer[buf_len] = '\0'; redraw_line(prompt, buffer, pos);
                    }
                    break;
                case 5: if (pos != buf_len) { pos = buf_len; redraw_line(prompt, buffer, pos); } break;
                case 11: if (pos < buf_len) { buffer[pos] = '\0'; buf_len = pos; redraw_line(prompt, buffer, pos); } break;
                case 12:
                    write(STDOUT_FILENO, "\033[2J\033[H", 7);
                    last_drawn_len = 0; redraw_line(prompt, buffer, pos);
                    break;
                case 21:
                    if (pos > 0) {
                        memmove(buffer, &buffer[pos], buf_len - pos + 1);
                        buf_len -= pos; pos = 0; redraw_line(prompt, buffer, pos);
                    }
                    break;
                default: break;
            }
            continue;
        }
        
        if ((unsigned char)c >= 32 && (unsigned char)c < 127) {
            if (buf_len < max_size - 1) {
                memmove(&buffer[pos + 1], &buffer[pos], buf_len - pos + 1);
                buffer[pos] = c;
                pos++; buf_len++;
                redraw_line(prompt, buffer, pos);
            }
            continue;
        }
    }
}

// ============================================================================
// ГЛАВНЫЙ ЦИКЛ SHELL
// ============================================================================
int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    
    handle_clear();
    
    printf(ANSI_CYAN ANSI_BOLD "Welcome to Enclave OS Shell (Ring 3)" ANSI_RESET "\n");
    printf("Type '" ANSI_YELLOW "help" ANSI_RESET "' for available commands.\n\n");
    printf(ANSI_YELLOW "Arrow keys, Home/End, Delete, Ctrl+A/E/K/U/L/C/D supported." ANSI_RESET "\n\n");

    char buffer[CMD_BUFFER_SIZE];
    
    while (1) {
        const char* prompt = ANSI_GREEN ANSI_BOLD "enclave" ANSI_RESET "@" 
                             ANSI_MAGENTA "os" ANSI_RESET ":" 
                             ANSI_CYAN ANSI_BOLD "/" ANSI_RESET "# ";
        
        int result = readline(prompt, buffer, CMD_BUFFER_SIZE);
        
        if (result == -1) {
            printf(ANSI_YELLOW "exit" ANSI_RESET "\n");
            exit(0);
        }
        
        if (strlen(buffer) == 0) continue;
        
        history_add(buffer);
        
        execute_command(buffer);
    }

    return 0;
}