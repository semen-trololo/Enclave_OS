#include "user_libc.h"

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
// 🛡️ Используем #define, так как static массивы требуют Constant Expression
#define NUM_FILES 1000
#define FILE_SIZE 1024

void _start() {
    crc32_init();
    printf("[TEST] VFS Stress: 1000 files in tmpfs...\n");
    
    char filename[32];
    
    // 🛡️ Статические буферы: не выделяются через malloc, не раздувают кучу
    static uint8_t write_buf[FILE_SIZE];
    static uint8_t read_buf[FILE_SIZE];
    
    int errors = 0;

    // === PHASE 1: WRITE ===
    for (int i = 0; i < NUM_FILES; i++) {
        snprintf(filename, sizeof(filename), "/tmp/t_%03d", i);

        uint8_t pattern = (uint8_t)(i & 0xFF);
        memset(write_buf, pattern, FILE_SIZE);

        
        int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644); // ✅ Добавлен mode
        if (fd < 0) {
            printf("[FAIL] open for write failed: %s\n", filename);
            exit(1); 
        }

        if (write(fd, write_buf, FILE_SIZE) != FILE_SIZE) {
            errors++;
        }
        close(fd);
    }

    // === PHASE 2: READ & VERIFY ===
    for (int i = 0; i < NUM_FILES; i++) {
        snprintf(filename, sizeof(filename), "/tmp/t_%03d", i);

        int fd = open(filename, O_RDONLY);
        if (fd < 0) { errors++; continue; }

        ssize_t bytes_read = read(fd, read_buf, FILE_SIZE);
        close(fd);

        if (bytes_read != FILE_SIZE) { errors++; continue; }

        uint32_t read_crc = crc32(read_buf, FILE_SIZE);
        
        uint8_t pattern = (uint8_t)(i & 0xFF);
        memset(write_buf, pattern, FILE_SIZE);
        uint32_t expected_crc = crc32(write_buf, FILE_SIZE);

        if (read_crc != expected_crc) {
            errors++;
        }
    }

    // === PHASE 3: CLEANUP (Unlink) ===
    for (int i = 0; i < NUM_FILES; i++) {
        snprintf(filename, sizeof(filename), "/tmp/t_%03d", i);
        unlink(filename);
    }

    if (errors == 0) {
        printf("[PASS] VFS Stress OK. %d files verified.\n", NUM_FILES);
        exit(0);
    } else {
        printf("[FAIL] VFS Stress failed with %d errors.\n", errors);
        exit(2);
    }
}