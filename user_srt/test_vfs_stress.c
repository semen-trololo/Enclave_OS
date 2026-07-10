// user_src/test_vfs_stress.c
#include "user_syscalls.h"

// Флаги (должны совпадать с ядром)
#define O_WRONLY    0x01
#define O_RDONLY    0x00
#define O_CREAT     0x100
#define O_TRUNC     0x200

// Простой CRC32 (ISO 3309)
static uint32_t crc32_table[256];
static void crc32_init() {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320 : (crc >> 1);
        crc32_table[i] = crc;
    }
}
static uint32_t crc32(const uint8_t* data, uint32_t len) {
    uint32_t crc = 0xFFFFFFFF;
    while (len--) crc = crc32_table[(crc ^ *data++) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFF;
}

// Обертка для unlink (syscall 10)
static inline int sys_unlink(const char* path) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(10), "b"(path));
    return ret;
}

void _start() {
    crc32_init();
    
    const int NUM_FILES = 1000;
    const int FILE_SIZE = 1024; // 1 KB
    char filename[32];
    uint8_t write_buf[FILE_SIZE];
    uint8_t read_buf[FILE_SIZE];
    
    int errors = 0;

    // === PHASE 1: WRITE ===
    for (int i = 0; i < NUM_FILES; i++) {
        // Формируем имя /tmp/t_XXX
        int len = 0;
        const char* prefix = "/tmp/t_";
        while(*prefix) filename[len++] = *prefix++;
        // Простая конвертация int to string
        filename[len++] = '0' + (i / 100);
        filename[len++] = '0' + ((i / 10) % 10);
        filename[len++] = '0' + (i % 10);
        filename[len] = '\0';

        // Заполняем буфер паттерном (байт = i % 256)
        uint8_t pattern = (uint8_t)(i & 0xFF);
        for (int j = 0; j < FILE_SIZE; j++) write_buf[j] = pattern;

        int fd = syscall3(5, (int)filename, O_WRONLY | O_CREAT | O_TRUNC, 0); // 5 = sys_open
        if (fd < 0) {
            // Вывод ошибки (упрощенно)
            sys_exit(1); 
        }

        sys_write(fd, write_buf, FILE_SIZE);
        
        // Сразу закрываем, чтобы освободить open_file_t (но не данные в tmpfs)
        syscall1(6, fd); // 6 = sys_close
    }

    // === PHASE 2: READ & VERIFY ===
    for (int i = 0; i < NUM_FILES; i++) {
        int len = 0;
        const char* prefix = "/tmp/t_";
        while(*prefix) filename[len++] = *prefix++;
        filename[len++] = '0' + (i / 100);
        filename[len++] = '0' + ((i / 10) % 10);
        filename[len++] = '0' + (i % 10);
        filename[len] = '\0';

        int fd = syscall3(5, (int)filename, O_RDONLY, 0);
        if (fd < 0) { errors++; continue; }

        int bytes_read = sys_read(fd, read_buf, FILE_SIZE);
        syscall1(6, fd); // close

        if (bytes_read != FILE_SIZE) { errors++; continue; }

        // Считаем CRC прочитанного
        uint32_t read_crc = crc32(read_buf, FILE_SIZE);

        // Считаем CRC ожидаемого
        uint8_t pattern = (uint8_t)(i & 0xFF);
        for (int j = 0; j < FILE_SIZE; j++) write_buf[j] = pattern;
        uint32_t expected_crc = crc32(write_buf, FILE_SIZE);

        if (read_crc != expected_crc) {
            errors++;
        }
    }

    // === PHASE 3: CLEANUP (Unlink) ===
    // Это критично для heap_check_balance()!
    for (int i = 0; i < NUM_FILES; i++) {
        int len = 0;
        const char* prefix = "/tmp/t_";
        while(*prefix) filename[len++] = *prefix++;
        filename[len++] = '0' + (i / 100);
        filename[len++] = '0' + ((i / 10) % 10);
        filename[len++] = '0' + (i % 10);
        filename[len] = '\0';

        sys_unlink(filename);
    }

    // Если ошибок 0 - выходим с кодом 0 (PASS)
    sys_exit(errors == 0 ? 0 : 2);
}
