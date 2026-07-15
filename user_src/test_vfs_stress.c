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

#define NUM_FILES 1000
#define FILE_SIZE 1024

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    
    crc32_init();
    printf("[TEST] VFS Stress: Probing tmpfs capacity...\n");
    
    char filename[32];
    static uint8_t write_buf[FILE_SIZE];
    static uint8_t read_buf[FILE_SIZE];
    
    int errors = 0;
    int created_files = 0;
    int oom_reached = 0;

    // === PHASE 1: WRITE ===
    for (int i = 0; i < NUM_FILES; i++) {
        snprintf(filename, sizeof(filename), "/tmp/t_%03d", i);

        uint8_t pattern = (uint8_t)(i & 0xFF);
        memset(write_buf, pattern, FILE_SIZE);

        int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            // 🛡️ FIX: Проверяем errno, а не fd (fd всегда -1 при ошибке в libc)
            if (errno == 12 || errno == 28) { // ENOMEM or ENOSPC
                printf("[INFO] Kernel Heap/Quota full at %d files (open). Stopping gracefully.\n", i);
                oom_reached = 1;
                break;
            }
            printf("[FAIL] open for write failed: %s (errno=%d)\n", filename, fd);
            errors++;
            break; 
        }

        ssize_t w = write(fd, write_buf, FILE_SIZE);
        if (w != FILE_SIZE) {
            // 🛡️ FIX: Graceful handling of OOM during write
            if (w < 0 && (errno == 12 || errno == 28)) {
                printf("[INFO] Kernel Heap/Quota full at %d files (write). Stopping gracefully.\n", i);
                oom_reached = 1;
                close(fd);
                unlink(filename); // Clean up the empty/partial file to prevent 2-block leak!
                break;
            }
            printf("[FAIL] write failed: %s (w=%d, errno=%d)\n", filename, w, errno);
            errors++;
            close(fd);
            unlink(filename);
            break;
        }
        close(fd);
        created_files = i + 1;
    }

    if (!oom_reached && errors == 0) {
        printf("[INFO] Successfully created %d files.\n", created_files);
    }

    // === PHASE 2: READ & VERIFY ===
    for (int i = 0; i < created_files; i++) {
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

    // === PHASE 3: CLEANUP ===
    for (int i = 0; i < created_files; i++) {
        snprintf(filename, sizeof(filename), "/tmp/t_%03d", i);
        unlink(filename);
    }

    if (errors == 0) {
        printf("[PASS] VFS Stress OK. %d files verified. %s\n", 
               created_files, oom_reached ? "(Heap boundary reached)" : "");
        return 0;
    } else {
        printf("[FAIL] VFS Stress failed with %d errors.\n", errors);
        return 2;
    }
}