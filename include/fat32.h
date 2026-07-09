#ifndef FAT32_H
#define FAT32_H

#include <stdint.h>
#include "vfs.h"
#include "ata.h"

// ==========================================
// КОНСТАНТЫ FAT32
// ==========================================
#define FAT32_ATTR_READ_ONLY 0x01
#define FAT32_ATTR_HIDDEN    0x02
#define FAT32_ATTR_SYSTEM    0x04
#define FAT32_ATTR_VOLUME_ID 0x08
#define FAT32_ATTR_DIRECTORY 0x10
#define FAT32_ATTR_ARCHIVE   0x20
#define FAT32_ATTR_LFN       0x0F // READ_ONLY | HIDDEN | SYSTEM | VOLUME_ID

#define FAT32_EOF      0x0FFFFFF8
#define FAT32_BAD      0x0FFFFFF7
#define FAT32_FREE     0x00000000

#define FAT32_ENTRY_DELETED 0xE5
#define FAT32_ENTRY_END     0x00

// ==========================================
// СТРУКТУРЫ ДАННЫХ (Аппаратные)
// ==========================================

// BIOS Parameter Block (FAT32 специфичный)
typedef struct __attribute__((packed)) {
    uint8_t  jump[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entry_count;   // 0 для FAT32
    uint16_t total_sectors_16;   // 0 для FAT32
    uint8_t  media_type;
    uint16_t fat_size_16;        // 0 для FAT32
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    // FAT32 Extended BPB
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;       // Обычно 2
    uint16_t fs_info;
    uint16_t backup_boot;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_sig;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
} fat32_bpb_t;

// Классическая 8.3 запись (32 байта)
typedef struct __attribute__((packed)) {
    char     name[11];           // "FILENAMEEXT" (space padded)
    uint8_t  attr;
    uint8_t  reserved;
    uint8_t  ctime_tenths;
    uint16_t ctime;
    uint16_t cdate;
    uint16_t adate;
    uint16_t cluster_hi;
    uint16_t mtime;
    uint16_t mdate;
    uint16_t cluster_lo;
    uint32_t file_size;
} fat32_dir_entry_t;

// VFAT Long File Name запись (32 байта)
typedef struct __attribute__((packed)) {
    uint8_t  order;              // Bit 6 = last LFN entry (first physically)
    uint16_t name1[5];           // UCS-2 characters 1-5
    uint8_t  attr;               // Always 0x0F
    uint8_t  type;               // Always 0x00
    uint8_t  checksum;           // Checksum of 8.3 name
    uint16_t name2[6];           // UCS-2 characters 6-11
    uint16_t zero;               // Always 0x0000
    uint16_t name3[2];           // UCS-2 characters 12-13
} fat32_lfn_entry_t;

// ==========================================
// КОНТЕКСТЫ (Для VFS private_data)
// ==========================================

// Глобальный контекст файловой системы (один на раздел)
typedef struct {
    uint32_t partition_lba;      // Абсолютный LBA начала раздела
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint32_t fat_size_32;
    uint32_t root_cluster;
    uint32_t first_data_sector;
    uint32_t fat1_lba;           // Абсолютный LBA начала FAT1
} fat32_fs_t;

// Приватные данные для каждого vfs_node_t (файла или папки)
typedef struct {
    fat32_fs_t* fs;
    uint32_t start_cluster;
    uint32_t size;
} fat32_node_data_t;

// ==========================================
// API
// ==========================================
void fat32_init(void);

#endif // FAT32_H
