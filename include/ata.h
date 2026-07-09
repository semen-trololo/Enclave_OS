#ifndef ATA_H
#define ATA_H

#include <stdint.h>

// ATA Port I/O Addresses (Primary IDE Bus)
#define ATA_PRIMARY_IO       0x1F0
#define ATA_PRIMARY_CTRL     0x3F6
#define ATA_PRIMARY_IRQ      14

// ATA Registers (offset from ATA_PRIMARY_IO)
#define ATA_REG_DATA         0x00
#define ATA_REG_ERROR        0x01
#define ATA_REG_FEATURES     0x01
#define ATA_REG_SECCOUNT     0x02
#define ATA_REG_LBA_LO       0x03
#define ATA_REG_LBA_MID      0x04
#define ATA_REG_LBA_HI       0x05
#define ATA_REG_DRIVE        0x06
#define ATA_REG_STATUS       0x07
#define ATA_REG_COMMAND      0x07

// Status Register Bits
#define ATA_SR_BSY  0x80  // Busy
#define ATA_SR_DRDY 0x40  // Drive Ready
#define ATA_SR_DF   0x20  // Drive Fault
#define ATA_SR_DRQ  0x08  // Data Request
#define ATA_SR_ERR  0x01  // Error

// ATA Commands
#define ATA_CMD_READ_PIO    0x20
#define ATA_CMD_WRITE_PIO   0x30
#define ATA_CMD_IDENTIFY    0xEC

// IDENTIFY Device Data Structure (512 bytes)
typedef struct __attribute__((packed)) {
    uint16_t config;              // General configuration bits
    uint16_t cylinders;           // Number of logical cylinders
    uint16_t specific_config;     // Specific configuration
    uint16_t heads;               // Number of logical heads
    uint16_t vendor_words[2];     // Vendor specific (obsolete)
    uint16_t sectors_per_track;   // Number of logical sectors per track
    uint16_t vendor_unique[3];    // Vendor unique
    char     serial[20];          // Serial number (ASCII, space padded)
    uint16_t vendor_words2[3];    // Vendor specific (obsolete)
    char     firmware[8];         // Firmware revision
    char     model[40];           // Model number (ASCII, space padded)
    uint16_t sectors_per_int;     // Sectors per interrupt
    uint16_t dword_io;            // DWORD I/O supported
    uint16_t capabilities;        // Capabilities
    uint16_t vendor_words3[2];    // Vendor specific (obsolete)
    uint16_t pio_timing;          // PIO timing mode
    uint16_t dma_timing;          // DMA timing mode
    uint16_t field_valid;         // Field validity
    uint16_t cur_cylinders;       // Current logical cylinders
    uint16_t cur_heads;           // Current logical heads
    uint16_t cur_sectors;         // Current logical sectors per track
    uint32_t cur_capacity;        // Current capacity in sectors
    uint16_t multi_sector;        // Multi-sector setting
    uint32_t lba_capacity;        // LBA capacity (if supported)
    uint16_t vendor_words4[48];   // Vendor specific
    uint16_t major_version;       // Major version number
    uint16_t minor_version;       // Minor version number
    uint16_t command_sets[6];     // Command set/feature supported
    uint32_t max_lba;             // Max LBA address
    uint16_t vendor_words5[23];   // Vendor specific
    uint16_t removable_status;    // Removable media status
    uint16_t security_status;     // Security status
    uint16_t vendor_words6[127];  // Vendor specific
} ata_identify_data_t;

// Partition Entry Structure (MBR)
typedef struct __attribute__((packed)) {
    uint8_t  status;        // 0x80 = active (bootable), 0x00 = inactive
    uint8_t  chs_first[3];  // CHS of first sector (obsolete)
    uint8_t  type;          // Partition type (0x0B/0x0C = FAT32)
    uint8_t  chs_last[3];   // CHS of last sector (obsolete)
    uint32_t lba_start;     // LBA of first sector
    uint32_t sector_count;  // Number of sectors
} partition_entry_t;

// Partition Information
typedef struct {
    int      id;            // Partition number (0-3)
    uint8_t  type;          // Partition type
    uint32_t lba_start;     // Starting LBA
    uint32_t sector_count;  // Size in sectors
    int      active;        // Is partition registered?
} partition_info_t;

#define MAX_PARTITIONS 4

// API Functions
void ata_init(void);
int  ata_identify(ata_identify_data_t* data);
int  ata_read_sectors(uint32_t lba, uint8_t sector_count, void* buffer);
int  ata_write_sectors(uint32_t lba, uint8_t sector_count, const void* buffer);

// Partition Management
int  partition_scan(void);
partition_info_t* partition_get(int id);
int  partition_count(void);

#endif // ATA_H
