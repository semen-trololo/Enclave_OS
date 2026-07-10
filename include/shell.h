#ifndef SHELL_H
#define SHELL_H

// ✅ ИСПРАВЛЕНО: Single Source of Truth для парсинга shell-команд
#define MAX_ARGS 4
#define MAX_ARG_LEN 64

// Запуск бесконечного цикла командной оболочки
void shell_run(void);

// [ДЕНЬ 10] Test Runner Entry Point
void test_init(void);

// [ДЕНЬ 10] Stress Test Handler
void handle_stress(int argc, char args[MAX_ARGS][MAX_ARG_LEN]);

#endif