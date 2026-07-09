#include "ata.h"
#include "port_io.h"
#include "serial.h"
#include "klib.h"
#include <stdint.h>
#include <stdbool.h>

// Global partition table
static partition_info_t partitions[MAX_PARTITIONS];
static int partition_count_value = 0;

// I/O delay function (4x inb to port 0x80)
static void io_delay(void) {
    inb(0x80);
    inb(0x80);
    inb(0x80);
    inb(0x80);
}

// Trim trailing spaces from ATA strings (space-padded, not null-terminated)
static void trim_trailing_spaces(char* str, int max_len) {
    for (int i = max_len - 1; i >= 0 && str[i] == ' '; i--) {
        str[i] = '\0';
    }
}

// Byte-swap ATA strings (stored as little-endian pairs)
static void byte_swap_string(char* str, int len) {
    for (int i = 0; i < len - 1; i += 2) {
        char tmp = str[i];
        str[i] = str[i + 1];
        str[i + 1] = tmp;
    }
    str[len - 1] = '\0'; // Ensure null-termination
    trim_trailing_spaces(str, len);
}

// Wait for BSY to clear (less strict than ata_wait_ready)
static int ata_wait_bsy_clear(void) {
    for (int i = 0; i < 100000; i++) {
        uint8_t status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
        if (!(status & ATA_SR_BSY)) {
            return 0; // Success: BSY cleared
        }
        io_delay();
    }
    serial_printf("[ATA] Timeout: BSY never cleared\n");
    return -1; // Timeout
}

// Wait for drive to be ready (BSY=0, DRDY=1) - for normal operations
static int ata_wait_ready(void) {
    for (int i = 0; i < 100000; i++) {
        uint8_t status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRDY)) {
            return 0; // Success
        }
        io_delay();
    }
    serial_printf("[ATA] Timeout waiting for drive ready\n");
    return -1; // Timeout
}

// Wait for data request (DRQ=1) or error (ERR/DF)
static int ata_wait_drq(void) {
    for (int i = 0; i < 100000; i++) {
        uint8_t status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
        if (status & ATA_SR_DRQ) {
            return 0; // Success: data ready
        }
        if (status & (ATA_SR_ERR | ATA_SR_DF)) {
            uint8_t error = inb(ATA_PRIMARY_IO + ATA_REG_ERROR);
            serial_printf("[ATA] Drive error: status=0x%02X, error=0x%02X\n", 
                          status, error);
            return -1; // Error
        }
        io_delay();
    }
    serial_printf("[ATA] Timeout waiting for DRQ\n");
    return -1; // Timeout
}

// Initialize ATA subsystem (minimal: just clear partition table)
void ata_init(void) {
    k_memset(partitions, 0, sizeof(partitions));
    partition_count_value = 0;
    serial_printf("[ATA] Primary IDE Bus initialized (Polling Mode)\n");
}

// Read IDENTIFY data from drive
int ata_identify(ata_identify_data_t* data) {
    if (data == NULL) return -1;
    
    // 1. Select drive 0 (Master) in LBA mode
    outb(ATA_PRIMARY_IO + ATA_REG_DRIVE, 0xA0);
    io_delay();
    
    // 2. Check if drive exists (status should not be 0)
    uint8_t status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    if (status == 0) {
        serial_printf("[ATA] No drive detected (status=0)\n");
        return -1;
    }
    
    // 3. Wait for BSY to clear
    if (ata_wait_bsy_clear() < 0) {
        return -1;
    }
    
    // 4. Check for ATAPI BEFORE sending IDENTIFY
    // ATAPI devices set signature in LBA_MID/LBA_HI after drive select
    uint8_t lba_mid = inb(ATA_PRIMARY_IO + ATA_REG_LBA_MID);
    uint8_t lba_hi = inb(ATA_PRIMARY_IO + ATA_REG_LBA_HI);
    
    if (lba_mid == 0x14 && lba_hi == 0xEB) {
        serial_printf("[ATA] ATAPI device detected (CD-ROM), skipping IDENTIFY\n");
        return -2; // ATAPI not supported yet
    }
    
    // If non-zero but not ATAPI signature, might be SATA or floating bus
    if (lba_mid != 0 || lba_hi != 0) {
        serial_printf("[ATA] Unknown device signature: MID=0x%02X, HI=0x%02X\n", 
                      lba_mid, lba_hi);
        return -1;
    }
    
    // 5. Send IDENTIFY command
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    io_delay();
    
    // 6. Read status again (some drives return 0 if they don't support IDENTIFY)
    status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    if (status == 0) {
        serial_printf("[ATA] Drive does not support IDENTIFY\n");
        return -1;
    }
    
    // 7. Wait for BSY to clear (not DRDY!)
    if (ata_wait_bsy_clear() < 0) {
        return -1;
    }
    
    // 8. Check for errors
    status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    if (status & ATA_SR_ERR) {
        uint8_t error = inb(ATA_PRIMARY_IO + ATA_REG_ERROR);
        serial_printf("[ATA] IDENTIFY failed: error=0x%02X\n", error);
        return -1;
    }
    
    // 9. Wait for DRQ (data ready)
    if (ata_wait_drq() < 0) {
        return -1;
    }
    
    // 10. Read 256 words (512 bytes)
    uint16_t* buf = (uint16_t*)data;
    for (int i = 0; i < 256; i++) {
        buf[i] = inw(ATA_PRIMARY_IO + ATA_REG_DATA);
    }
    
    // 11. Byte-swap and trim ASCII strings
    byte_swap_string(data->model, 40);
    byte_swap_string(data->serial, 20);
    byte_swap_string(data->firmware, 8);
    
    serial_printf("[ATA] IDENTIFY successful\n");
    return 0;
}

// Read sectors using PIO mode (LBA28)
int ata_read_sectors(uint32_t lba, uint8_t sector_count, void* buffer) {
    if (buffer == NULL) return -1;
    
    // В ATA спецификации 0 в регистре sector count означает 256 секторов.
    // Используем uint16_t для реальной логики цикла, чтобы избежать переполнения.
    uint16_t actual_sector_count = (sector_count == 0) ? 256 : sector_count;
    
    // LBA28 limit check (с учетом реального количества секторов)
    if (lba + actual_sector_count > 0x10000000) {
        serial_printf("[ATA] LBA out of range: lba=%u, count=%u\n", 
                      lba, actual_sector_count);
        return -1;
    }
    
    if (ata_wait_ready() < 0) {
        return -1;
    }
    
    // Set sector count (отправляем оригинальный uint8_t: 0 контроллер поймет как 256)
    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT, sector_count);
    
    // Set LBA address (28-bit)
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_LO, (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_HI, (uint8_t)((lba >> 16) & 0xFF));
    
    // Set drive and LBA mode (bit 6 = LBA, bits 4-7 = drive 0, bits 0-3 = LBA bits 24-27)
    outb(ATA_PRIMARY_IO + ATA_REG_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    
    // Send READ SECTORS command
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_READ_PIO);
    io_delay();
    
    // Read data sector by sector
    uint16_t* buf = (uint16_t*)buffer;
    for (uint16_t s = 0; s < actual_sector_count; s++) {
        if (ata_wait_drq() < 0) {
            serial_printf("[ATA] Read failed at sector %u\n", lba + s);
            return -1;
        }
        
        // Read 256 words (512 bytes) using 16-bit I/O
        for (int i = 0; i < 256; i++) {
            buf[s * 256 + i] = inw(ATA_PRIMARY_IO + ATA_REG_DATA);
        }
    }
    
    return 0;
}

// Write sectors using PIO mode (placeholder for future)
int ata_write_sectors(uint32_t lba, uint8_t sector_count, const void* buffer) {
    (void)lba;
    (void)sector_count;
    (void)buffer;
    serial_printf("[ATA] Write not implemented (Read-Only mode)\n");
    return -1;
}

// Scan MBR and register partitions
int partition_scan(void) {
    uint8_t mbr[512];
    
    if (ata_read_sectors(0, 1, mbr) < 0) {
        serial_printf("[PART] Failed to read MBR\n");
        return -1;
    }
    
    // Check MBR signature (byte-by-byte for portability)
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
        serial_printf("[PART] Invalid MBR signature: 0x%x 0x%x\n", 
              mbr[510], mbr[511]);
        return -1;
    }
    
    serial_printf("[PART] MBR signature valid (0x55 0xAA)\n");
    
    // Parse partition table (4 entries, each 16 bytes, starting at offset 446)
    partition_entry_t* table = (partition_entry_t*)&mbr[446];
    partition_count_value = 0;
    
    for (int i = 0; i < 4; i++) {
        if (table[i].type == 0x00) {
            continue; // Empty partition
        }
        
        serial_printf("[PART] Entry %d: Type=0x%x, LBA=%u, Size=%u sectors (%u MB)\n",
              i, table[i].type, table[i].lba_start, 
              table[i].sector_count, table[i].sector_count / 2048);
        
        // Register partition
        if (partition_count_value < MAX_PARTITIONS) {
            partitions[partition_count_value].id = i;
            partitions[partition_count_value].type = table[i].type;
            partitions[partition_count_value].lba_start = table[i].lba_start;
            partitions[partition_count_value].sector_count = table[i].sector_count;
            partitions[partition_count_value].active = 1;
            partition_count_value++;
        }
    }
    
    serial_printf("[PART] Found %d active partitions\n", partition_count_value);
    return partition_count_value;
}

// Get partition info by ID
partition_info_t* partition_get(int id) {
    if (id < 0 || id >= partition_count_value) {
        return NULL;
    }
    return &partitions[id];
}

// Get total number of partitions
int partition_count(void) {
    return partition_count_value;
}