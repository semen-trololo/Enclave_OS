#ifndef SHELL_H
#define SHELL_H

// Запуск бесконечного цикла командной оболочки
void shell_run(void);

// [ДЕНЬ 10] Test Runner Entry Point
void test_init(void);

// [ДЕНЬ 10] Stress Test Handler
void handle_stress(int argc, char args[4][64]);

#endif
