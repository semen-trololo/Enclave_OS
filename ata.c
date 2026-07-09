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

// Wait for drive to be ready (BSY=0, DRDY=1)
static int ata_wait_ready(void) {
    for (int i = 0; i < 100000; i++) {
        uint8_t status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRDY)) {
            return 0; // Success
        }
        io_delay();
    }
    serial_printf("ATA: Timeout waiting for drive ready\n");
    return -1; // Timeout
}

// Wait for data request (DRQ=1)
static int ata_wait_drq(void) {
    for (int i = 0; i < 100000; i++) {
        uint8_t status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
        if (status & ATA_SR_DRQ) {
            return 0; // Success
        }
        if (status & (ATA_SR_ERR | ATA_SR_DF)) {
            serial_printf("ATA: Drive error during wait_drq\n");
            return -1; // Error
        }
        io_delay();
    }
    serial_printf("ATA: Timeout waiting for DRQ\n");
    return -1; // Timeout
}

// Initialize ATA subsystem
void ata_init(void) {
    k_memset(partitions, 0, sizeof(partitions));
    partition_count_value = 0;
    
    serial_printf("ATA: Initializing Primary IDE Bus (PIO mode)\n");
    
    // Try to identify the drive
    ata_identify_data_t identify_data;
    if (ata_identify(&identify_data) == 0) {
        serial_printf("ATA: Drive detected - %s\n", identify_data.model);
        serial_printf("ATA: Serial: %s\n", identify_data.serial);
        serial_printf("ATA: LBA Capacity: %u sectors\n", identify_data.max_lba);
    } else {
        serial_printf("ATA: No drive detected on Primary IDE Bus\n");
    }
}

// Read IDENTIFY data from drive
int ata_identify(ata_identify_data_t* data) {
    if (data == NULL) return -1;
    
    // Select drive 0 (Master)
    outb(ATA_PRIMARY_IO + ATA_REG_DRIVE, 0xA0); // LBA mode, drive 0
    io_delay();
    
    // Send IDENTIFY command
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    io_delay();
    
    // Check if drive exists
    uint8_t status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    if (status == 0) {
        return -1; // No drive
    }
    
    // Wait for BSY to clear
    if (ata_wait_ready() < 0) {
        return -1;
    }
    
    // Check for ATAPI (LBA_MID and LBA_HI should be 0 for ATA)
    uint8_t lba_mid = inb(ATA_PRIMARY_IO + ATA_REG_LBA_MID);
    uint8_t lba_hi = inb(ATA_PRIMARY_IO + ATA_REG_LBA_HI);
    if (lba_mid != 0 || lba_hi != 0) {
        serial_printf("ATA: ATAPI device detected (not supported)\n");
        return -1; // ATAPI, not ATA
    }
    
    // Wait for DRQ
    if (ata_wait_drq() < 0) {
        return -1;
    }
    
    // Read 256 words (512 bytes)
    uint16_t* buf = (uint16_t*)data;
    for (int i = 0; i < 256; i++) {
        buf[i] = inw(ATA_PRIMARY_IO + ATA_REG_DATA);
    }
    
    // Convert ASCII strings (byte-swapped)
    for (int i = 0; i < 40; i += 2) {
        char tmp = data->model[i];
        data->model[i] = data->model[i + 1];
        data->model[i + 1] = tmp;
    }
    data->model[39] = '\0';
    
    for (int i = 0; i < 20; i += 2) {
        char tmp = data->serial[i];
        data->serial[i] = data->serial[i + 1];
        data->serial[i + 1] = tmp;
    }
    data->serial[19] = '\0';
    
    return 0;
}

// Read sectors using PIO mode
int ata_read_sectors(uint32_t lba, uint8_t sector_count, void* buffer) {
    if (buffer == NULL) return -1;
    if (sector_count == 0 || sector_count > 256) return -1;
    if (lba >= 0x10000000) return -1; // LBA28 limit
    
    if (ata_wait_ready() < 0) {
        return -1;
    }
    
    // Set sector count
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
    
    // Read data
    uint16_t* buf = (uint16_t*)buffer;
    for (uint8_t s = 0; s < sector_count; s++) {
        if (ata_wait_drq() < 0) {
            return -1;
        }
        
        // Read 256 words (512 bytes)
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
    serial_printf("ATA: Write not implemented (Read-Only mode)\n");
    return -1;
}

// Scan MBR and register partitions
int partition_scan(void) {
    uint8_t mbr[512];
    
    if (ata_read_sectors(0, 1, mbr) < 0) {
        serial_printf("Partition: Failed to read MBR\n");
        return -1;
    }
    
    // Check MBR signature (0xAA55 at offset 510)
    uint16_t signature = *(uint16_t*)&mbr[510];
    if (signature != 0xAA55) {
        serial_printf("Partition: Invalid MBR signature (0x%04X)\n", signature);
        return -1;
    }
    
    serial_printf("Partition: MBR signature valid\n");
    
    // Parse partition table (4 entries, each 16 bytes, starting at offset 446)
    partition_entry_t* table = (partition_entry_t*)&mbr[446];
    partition_count_value = 0;
    
    for (int i = 0; i < 4; i++) {
        if (table[i].type == 0x00) {
            continue; // Empty partition
        }
        
        serial_printf("Partition %d: Type=0x%02X, LBA=%u, Size=%u sectors\n",
                      i, table[i].type, table[i].lba_start, table[i].sector_count);
        
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
    
    serial_printf("Partition: Found %d active partitions\n", partition_count_value);
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
