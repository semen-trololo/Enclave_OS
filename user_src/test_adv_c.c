// ==============================================================================
// ENCLAVE OS - Day 21: Advanced C Features & ELF Loader Stress Test
// Этот тест намеренно использует все типы сегментов ELF и конструкции C,
// чтобы проверить границы ELF Loader'а, VMM и User Space ABI.
// ==============================================================================

#include "user_libc.h"

// ==============================================================================
// 1. Секция .bss (Неинициализированные глобальные данные)
// В ELF-файле они весят 0 байт. ELF Loader ОБЯЗАН занулить их в памяти.
// ==============================================================================
int bss_array[100];
char bss_string[64];
struct { int x; int y; } bss_struct;

// ==============================================================================
// 2. Секция .data (Инициализированные глобальные данные)
// Проверяем, что elf.c корректно копирует данные по виртуальным адресам.
// ==============================================================================
int data_var = 0xDEADBEEF;
int data_array[5] = {10, 20, 30, 40, 50};
char data_str[] = "Initialized Data";

// ==============================================================================
// 3. Секция .rodata (Read-Only Data)
// Строковые литералы и константы. Должны быть доступны для чтения.
// ==============================================================================
const char *rodata_str = "Enclave OS String Literal";
const int rodata_array[3] = {100, 200, 300};

// ==============================================================================
// 4. Структуры и указатели (Проверка Alignment и стека)
// ==============================================================================
struct TestStruct {
    char a;       // 1 byte
    int b;        // 4 bytes (требует выравнивания!)
    char c[16];   // 16 bytes
    short d;      // 2 bytes
};

// ==============================================================================
// 5. Рекурсия (Проверка глубины стека и teardown фреймов)
// Компилятор не должен оптимизировать это в цикл (флаг -fno-optimize-sibling-calls).
// ==============================================================================
int factorial(int n) {
    if (n <= 1) return 1;
    
    // Локальные переменные, чтобы гарантировать выделение стекового фрейма
    volatile int local1 = n * 2; 
    volatile int local2 = n + 5;
    
    return local1 + local2 + factorial(n - 1);
}

// ==============================================================================
// MAIN: Запуск тестов
// ==============================================================================
int main(int argc, char *argv[]) {
    int passed = 0;
    int failed = 0;

    // Вывод в Serial/VGA через sys_write(fd=1)
    printf("\n");
    printf("========================================================\n");
    printf(" [TEST] Advanced C Features & ELF Loader Stress Test\n");
    printf("========================================================\n");

    // --- TEST 1: .bss Zero-Fill ---
    printf("[CHECK] 1. .bss Zero-Fill (ELF Loader memsz > filesz)... ");
    int bss_ok = 1;
    for (int i = 0; i < 100; i++) {
        if (bss_array[i] != 0) { bss_ok = 0; break; }
    }
    for (int i = 0; i < 64; i++) {
        if (bss_string[i] != 0) { bss_ok = 0; break; }
    }
    if (bss_struct.x != 0 || bss_struct.y != 0) bss_ok = 0;

    if (bss_ok) { printf("[PASS]\n"); passed++; } 
    else { printf("[FAIL] (Memory not zeroed! Check elf.c)\n"); failed++; }

    // --- TEST 2: .data Initialization ---
    printf("[CHECK] 2. .data Initialization (ELF Loader p_vaddr)... ");
    if (data_var == 0xDEADBEEF && data_array[2] == 30 && data_str[0] == 'I') {
        printf("[PASS]\n"); passed++;
    } else {
        printf("[FAIL] (Data corrupted!)\n"); failed++;
    }

    // --- TEST 3: .rodata Read-Only ---
    printf("[CHECK] 3. .rodata Read-Only... ");
    if (rodata_str[0] == 'E' && rodata_array[1] == 200) {
        printf("[PASS]\n"); passed++;
    } else {
        printf("[FAIL]\n"); failed++;
    }

    // --- TEST 4: Struct Alignment & Pointers ---
    printf("[CHECK] 4. Struct Alignment & Pointers... ");
    struct TestStruct s;
    s.a = 'X';
    s.b = 0x12345678;
    s.d = 0xABCD;
    
    struct TestStruct *ptr = &s;
    if (ptr->a == 'X' && ptr->b == 0x12345678 && ptr->d == (short)0xABCD) {
        printf("[PASS]\n"); passed++;
    } else {
        printf("[FAIL] (Alignment/Pointer issue!)\n"); failed++;
    }

    // --- TEST 5: Loops & Conditions (ALU/Flags) ---
    printf("[CHECK] 5. Loops & Conditions (ALU/Flags)... ");
    int sum = 0;
    for (int i = 1; i <= 100; i++) {
        if (i % 2 == 0) sum += i;
        else sum -= i;
    }
    // Математика: Сумма четных (2550) - Сумма нечетных (2500) = 50
    if (sum == 50) {
        printf("[PASS]\n"); passed++;
    } else {
        printf("[FAIL] (ALU error! sum=%d)\n", sum); failed++;
    }

    // --- TEST 6: Recursion (Stack Depth) ---
    printf("[CHECK] 6. Recursion (Stack Depth)... ");
    int fact = factorial(10);
    // Проверяем, что рекурсия отработала и не вызвала Stack Overflow (SIGSEGV)
    if (fact > 0) {
        printf("[PASS] (Result: %d)\n", fact); passed++;
    } else {
        printf("[FAIL]\n"); failed++;
    }

    // --- TEST 7: W^X Trap (Опционально, закомментировано) ---
    /*
    printf("[CHECK] 7. W^X Trap (Writing to .rodata)... ");
    // Если раскомментировать, процесс должен умереть с SIGSEGV.
    // Это доказывает, что Zero Trust Sandbox и W^X работают.
    char *mutable_rodata = (char *)rodata_str;
    mutable_rodata[0] = 'Z'; 
    printf("[FAIL] (Should have crashed with SIGSEGV!)\n"); failed++;
    */

    // --- SUMMARY ---
    printf("--------------------------------------------------------\n");
    printf(" [SUMMARY] Passed: %d | Failed: %d\n", passed, failed);
    
    if (failed == 0) {
        printf(" [ OK ] All ELF & ABI tests passed successfully!\n");
        printf("========================================================\n\n");
        return 0; // Успех, Init/Shell увидит exit_code = 0
    } else {
        printf(" [ERR ] Some tests failed. Check ELF Loader!\n");
        printf("========================================================\n\n");
        return 1; // Ошибка
    }
}