#include "fat32.h"
#include "heap.h"
#include "klib.h"
#include "serial.h"
#include "port_io.h"

// Статический буфер для чтения FAT (избегаем аллокаций на стеке)
static uint8_t fat_sector_buffer[512];
static uint32_t fat_buffer_sector = 0;

// ==========================================
// ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
// ==========================================

// Конвертация UCS-2 (UTF-16 BMP) в UTF-8
static int ucs2_to_utf8(uint16_t ucs2, char* out) {
    if (ucs2 < 0x80) {
        out[0] = ucs2;
        return 1;
    } else if (ucs2 < 0x800) {
        out[0] = 0xC0 | (ucs2 >> 6);
        out[1] = 0x80 | (ucs2 & 0x3F);
        return 2;
    } else {
        out[0] = 0xE0 | (ucs2 >> 12);
        out[1] = 0x80 | ((ucs2 >> 6) & 0x3F);
        out[2] = 0x80 | (ucs2 & 0x3F);
        return 3;
    }
}

// Вычисление контрольной суммы 8.3 имени для верификации LFN
static uint8_t lfn_checksum(const char* name) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) {
        sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + (uint8_t)name[i];
    }
    return sum;
}

// Трансляция Cluster -> LBA
static uint32_t cluster_to_lba(fat32_fs_t* fs, uint32_t cluster) {
    return fs->partition_lba + fs->first_data_sector +
    (cluster - 2) * fs->sectors_per_cluster;
}

// Чтение следующего кластера из FAT
static uint32_t fat32_next_cluster(fat32_fs_t* fs, uint32_t cluster) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fs->fat1_lba + (fat_offset / 512);
    uint32_t ent_offset = fat_offset % 512;

    if (fat_sector != fat_buffer_sector) {
        if (ata_read_sectors(fat_sector, 1, fat_sector_buffer) < 0) return FAT32_EOF;
        fat_buffer_sector = fat_sector;
    }

    uint32_t next = *(uint32_t*)&fat_sector_buffer[ent_offset];
    return next & 0x0FFFFFFF; // Маскируем верхние 4 бита
}

// ==========================================
// VFS CALLBACKS
// ==========================================

static int32_t fat32_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    fat32_node_data_t* data = (fat32_node_data_t*)node->private_data;
    if (!data || !data->fs || offset >= node->size) return 0;

    if (offset + size > node->size) size = node->size - offset;

    uint32_t bytes_read = 0;
    uint32_t cluster = data->start_cluster;

    // Пропускаем кластеры до offset
    uint32_t skip_clusters = offset / (data->fs->sectors_per_cluster * 512);
    for (uint32_t i = 0; i < skip_clusters; i++) {
        cluster = fat32_next_cluster(data->fs, cluster);
        if (cluster >= FAT32_EOF) return bytes_read;
    }

    uint8_t sec_buf[512];
    uint32_t cluster_offset = offset % (data->fs->sectors_per_cluster * 512);

    while (bytes_read < size && cluster < FAT32_EOF) {
        uint32_t lba = cluster_to_lba(data->fs, cluster);
        for (uint8_t s = 0; s < data->fs->sectors_per_cluster; s++) {
            if (cluster_offset >= 512) {
                cluster_offset -= 512;
                continue;
            }
            if (ata_read_sectors(lba + s, 1, sec_buf) < 0) return bytes_read;

            uint32_t to_copy = 512 - cluster_offset;
            if (to_copy > size - bytes_read) to_copy = size - bytes_read;

            k_memcpy(buffer + bytes_read, sec_buf + cluster_offset, to_copy);
            bytes_read += to_copy;
            cluster_offset = 0;
            if (bytes_read >= size) return bytes_read;
        }
        cluster = fat32_next_cluster(data->fs, cluster);
    }
    return bytes_read;
}

static int32_t fat32_readdir(vfs_node_t* node, uint32_t index, dirent_t* entry) {
    fat32_node_data_t* data = (fat32_node_data_t*)node->private_data;
    if (!data || !data->fs) return -1;

    uint32_t cluster = data->start_cluster;
    uint32_t current_index = 0;

    char lfn_buffer[256];
    k_memset(lfn_buffer, 0, sizeof(lfn_buffer));
    uint8_t expected_checksum = 0;
    bool lfn_active = false;

    while (cluster < FAT32_EOF) {
        uint32_t lba = cluster_to_lba(data->fs, cluster);
        for (uint8_t s = 0; s < data->fs->sectors_per_cluster; s++) {
            uint8_t sec_buf[512];
            if (ata_read_sectors(lba + s, 1, sec_buf) < 0) return -1;

            for (int i = 0; i < 16; i++) {
                fat32_dir_entry_t* dir = (fat32_dir_entry_t*)&sec_buf[i * 32];

                if (dir->name[0] == FAT32_ENTRY_END) return -1;
                if ((uint8_t)dir->name[0] == FAT32_ENTRY_DELETED) continue;

                if (dir->attr == FAT32_ATTR_LFN) {
                    fat32_lfn_entry_t* lfn = (fat32_lfn_entry_t*)dir;
                    if (lfn->order & 0x40) {
                        k_memset(lfn_buffer, 0, sizeof(lfn_buffer));
                        expected_checksum = lfn->checksum;
                        lfn_active = true;
                    }
                    if (lfn_active && lfn->checksum == expected_checksum) {
                        int pos = ((lfn->order & 0x1F) - 1) * 13;
                        for (int j = 0; j < 5; j++) if (lfn->name1[j] != 0xFFFF) pos += ucs2_to_utf8(lfn->name1[j], lfn_buffer + pos);
                        for (int j = 0; j < 6; j++) if (lfn->name2[j] != 0xFFFF) pos += ucs2_to_utf8(lfn->name2[j], lfn_buffer + pos);
                        for (int j = 0; j < 2; j++) if (lfn->name3[j] != 0xFFFF) pos += ucs2_to_utf8(lfn->name3[j], lfn_buffer + pos);
                    }
                    continue;
                }

                // 8.3 Entry
                if (lfn_active && lfn_checksum(dir->name) != expected_checksum) {
                    lfn_active = false; // Checksum mismatch, ignore LFN
                }

                if (dir->attr & FAT32_ATTR_VOLUME_ID) {
                    lfn_active = false;
                    continue;
                }

                if (current_index == index) {
                    if (lfn_active && lfn_buffer[0] != '\0') {
                        k_strncpy(entry->name, lfn_buffer, VFS_MAX_FILENAME - 1);
                    } else {
                        // Fallback to 8.3
                        int pos = 0;
                        for(int j=0; j<8 && dir->name[j]!=' '; j++) entry->name[pos++] = dir->name[j];
                        if(dir->name[8] != ' ') {
                            entry->name[pos++] = '.';
                            for(int j=8; j<11 && dir->name[j]!=' '; j++) entry->name[pos++] = dir->name[j];
                        }
                        entry->name[pos] = '\0';
                    }
                    entry->ino = (dir->cluster_hi << 16) | dir->cluster_lo;
                    return 0;
                }

                current_index++;
                lfn_active = false;
            }
        }
        cluster = fat32_next_cluster(data->fs, cluster);
    }
    return -1;
}

static struct vfs_node* fat32_finddir(vfs_node_t* node, const char* name) {
    fat32_node_data_t* data = (fat32_node_data_t*)node->private_data;
    if (!data || !data->fs) return NULL;

    uint32_t cluster = data->start_cluster;
    char lfn_buffer[256];
    uint8_t expected_checksum = 0;
    bool lfn_active = false;

    while (cluster < FAT32_EOF) {
        uint32_t lba = cluster_to_lba(data->fs, cluster);
        for (uint8_t s = 0; s < data->fs->sectors_per_cluster; s++) {
            uint8_t sec_buf[512];
            if (ata_read_sectors(lba + s, 1, sec_buf) < 0) return NULL;

            for (int i = 0; i < 16; i++) {
                fat32_dir_entry_t* dir = (fat32_dir_entry_t*)&sec_buf[i * 32];
                if (dir->name[0] == FAT32_ENTRY_END) return NULL;
                if ((uint8_t)dir->name[0] == FAT32_ENTRY_DELETED) continue;

                if (dir->attr == FAT32_ATTR_LFN) {
                    fat32_lfn_entry_t* lfn = (fat32_lfn_entry_t*)dir;
                    if (lfn->order & 0x40) {
                        k_memset(lfn_buffer, 0, sizeof(lfn_buffer));
                        expected_checksum = lfn->checksum;
                        lfn_active = true;
                    }
                    if (lfn_active && lfn->checksum == expected_checksum) {
                        int pos = ((lfn->order & 0x1F) - 1) * 13;
                        for (int j = 0; j < 5; j++) if (lfn->name1[j] != 0xFFFF) pos += ucs2_to_utf8(lfn->name1[j], lfn_buffer + pos);
                        for (int j = 0; j < 6; j++) if (lfn->name2[j] != 0xFFFF) pos += ucs2_to_utf8(lfn->name2[j], lfn_buffer + pos);
                        for (int j = 0; j < 2; j++) if (lfn->name3[j] != 0xFFFF) pos += ucs2_to_utf8(lfn->name3[j], lfn_buffer + pos);
                    }
                    continue;
                }

                if (dir->attr & FAT32_ATTR_VOLUME_ID) { lfn_active = false; continue; }

                char final_name[256];
                if (lfn_active && lfn_checksum(dir->name) == expected_checksum && lfn_buffer[0] != '\0') {
                    k_strncpy(final_name, lfn_buffer, 255);
                } else {
                    int pos = 0;
                    for(int j=0; j<8 && dir->name[j]!=' '; j++) final_name[pos++] = dir->name[j];
                    if(dir->name[8] != ' ') {
                        final_name[pos++] = '.';
                        for(int j=8; j<11 && dir->name[j]!=' '; j++) final_name[pos++] = dir->name[j];
                    }
                    final_name[pos] = '\0';
                }

                if (k_strcmp(final_name, name) == 0) {
                    uint32_t flags = (dir->attr & FAT32_ATTR_DIRECTORY) ? FS_DIRECTORY : FS_FILE;
                    vfs_node_t* child = vfs_create_node(final_name, flags, node, NULL);

                    fat32_node_data_t* child_data = (fat32_node_data_t*)kmalloc(sizeof(fat32_node_data_t));
                    child_data->fs = data->fs;
                    child_data->start_cluster = (dir->cluster_hi << 16) | dir->cluster_lo;
                    child_data->size = dir->file_size;
                    child->private_data = child_data;
                    child->size = dir->file_size;

                    if (flags & FS_DIRECTORY) {
                        child->readdir = fat32_readdir;
                        child->finddir = fat32_finddir;
                    } else {
                        child->read = fat32_read;
                    }
                    return child;
                }
                lfn_active = false;
            }
        }
        cluster = fat32_next_cluster(data->fs, cluster);
    }
    return NULL;
}

// ==========================================
// ИНИЦИАЛИЗАЦИЯ И МОНТИРОВАНИЕ
// ==========================================
void fat32_init(void) {
    serial_print("[FAT32] Searching for FAT32 partition...\n");

    partition_info_t* part = NULL;
    for (int i = 0; i < partition_count(); i++) {
        partition_info_t* p = partition_get(i);
        if (p && (p->type == 0x0B || p->type == 0x0C)) {
            part = p;
            break;
        }
    }

    if (!part) {
        serial_print("[FAT32] No FAT32 partition found.\n");
        return;
    }

    uint8_t boot_sector[512];
    if (ata_read_sectors(part->lba_start, 1, boot_sector) < 0) {
        serial_print("[FAT32] Failed to read Boot Sector.\n");
        return;
    }

    fat32_bpb_t* bpb = (fat32_bpb_t*)boot_sector;
    if (bpb->bytes_per_sector != 512) {
        serial_printf("[FAT32] Unsupported sector size: %u\n", bpb->bytes_per_sector);
        return;
    }

    fat32_fs_t* fs = (fat32_fs_t*)kmalloc(sizeof(fat32_fs_t));
    fs->partition_lba = part->lba_start;
    fs->bytes_per_sector = bpb->bytes_per_sector;
    fs->sectors_per_cluster = bpb->sectors_per_cluster;
    fs->reserved_sectors = bpb->reserved_sectors;
    fs->num_fats = bpb->num_fats;
    fs->fat_size_32 = bpb->fat_size_32;
    fs->root_cluster = bpb->root_cluster;
    fs->first_data_sector = fs->reserved_sectors + (fs->num_fats * fs->fat_size_32);
    fs->fat1_lba = fs->partition_lba + fs->reserved_sectors;

    serial_printf("[FAT32] Mounted partition %d (LBA %u)\n", part->id, part->lba_start);
    serial_printf("[FAT32] Cluster size: %u bytes\n", fs->sectors_per_cluster * 512);

    // Создаем корневую ноду FAT32
    vfs_node_t* fat32_root = vfs_create_node("fat32_root", FS_DIRECTORY, NULL, NULL);

    fat32_node_data_t* root_data = (fat32_node_data_t*)kmalloc(sizeof(fat32_node_data_t));
    root_data->fs = fs;
    root_data->start_cluster = fs->root_cluster;
    root_data->size = 0;
    fat32_root->private_data = root_data;
    fat32_root->readdir = fat32_readdir;
    fat32_root->finddir = fat32_finddir;

    // Монтируем в /mnt/c (предполагается, что /mnt/c создан в initrd или VFS)
    vfs_node_t* mnt_c = vfs_findnode("/mnt/c");
    if (mnt_c) {
        mnt_c->flags |= FS_MOUNTPOINT;
        mnt_c->mountpoint_node = fat32_root;
        serial_print("[FAT32] Successfully mounted to /mnt/c\n");
    } else {
        serial_print("[FAT32] Warning: /mnt/c not found in VFS. Mount skipped.\n");
        // В реальном коде здесь нужно динамически создать /mnt и /mnt/c через vfs_create_node
    }
}
