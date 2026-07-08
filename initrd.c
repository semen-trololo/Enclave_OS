#include "initrd.h"
#include "vfs.h"
#include "multiboot.h"
#include "heap.h"
#include "klib.h"
#include "serial.h"
#include "paging.h" 

extern uint32_t multiboot_info_ptr;

typedef struct {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t string;
    uint32_t reserved;
} multiboot_module_t;

// UStar TAR Header (строго 512 байт)
typedef struct {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char padding[12];
} __attribute__((packed)) tar_header_t;

typedef struct {
    uint8_t* data;
    uint32_t size;
} initrd_file_t;

// ==========================================
// ЛОКАЛЬНЫЕ ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
// ==========================================

static int initrd_strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static int initrd_strncmp(const char* s1, const char* s2, int n) {
    for (int i = 0; i < n && s1[i] && s2[i]; i++) {
        if (s1[i] != s2[i]) return (unsigned char)s1[i] - (unsigned char)s2[i];
    }
    return 0;
}

// Безопасное копирование с гарантированным \0
static void initrd_strncpy(char* dest, const char* src, int n) {
    int i = 0;
    while (i < n - 1 && src[i]) { dest[i] = src[i]; i++; }
    dest[i] = '\0';
}

static size_t initrd_strlen(const char* str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

static uint32_t parse_octal(const char* str, int len) {
    uint32_t res = 0;
    for (int i = 0; i < len && str[i] >= '0' && str[i] <= '7'; i++) {
        res = (res << 3) + (str[i] - '0');
    }
    return res;
}

static int32_t initrd_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (!node || !node->private_data || !buffer) return -1;
    initrd_file_t* file = (initrd_file_t*)node->private_data;
    if (offset >= file->size) return 0; 
    
    uint32_t to_read = size;
    if (offset + to_read > file->size) to_read = file->size - offset;
    
    k_memcpy(buffer, file->data + offset, to_read);
    return to_read;
}

static vfs_node_t* get_or_create_dir(vfs_node_t* parent, const char* name) {
    vfs_node_t* child = parent->first_child;
    while (child) {
        if (initrd_strcmp(child->name, name) == 0) return child;
        child = child->next_sibling;
    }
    
    uint32_t flags = FS_DIRECTORY;
    if (parent->flags & FS_SYSTEM) flags |= FS_SYSTEM;
    
    return vfs_create_node(name, flags, parent, 0);
}

// ==========================================
// ГЛАВНАЯ ФУНКЦИЯ ИНИЦИАЛИЗАЦИИ
// ==========================================

void initrd_init(void) {
    serial_print("\n[INITRD] Initializing RAM Disk (TAR UStar)...\n");
    
    multiboot_info_t* mb_info = (multiboot_info_t*)PHYS_TO_VIRT(multiboot_info_ptr);
    
    if (!(mb_info->flags & MULTIBOOT_INFO_MODS) || mb_info->mods_count == 0) {
        serial_print("[INITRD] WARNING: No modules found!\n");
        return;
    }
    
    multiboot_module_t* mods = (multiboot_module_t*)PHYS_TO_VIRT(mb_info->mods_addr);
    uint32_t tar_start = mods[0].mod_start;
    uint32_t tar_end = mods[0].mod_end;
    
    if (tar_start == 0 || tar_end <= tar_start) return;
    
    tar_header_t* header = (tar_header_t*)PHYS_TO_VIRT(tar_start);
    uint32_t files_count = 0;
    
    vfs_node_t* root = vfs_findnode("/");
    if (!root) return;

    // 🛡️ БЕЗОПАСНЫЙ БУФЕР: UStar поддерживает пути до 256 байт (155 prefix + 100 name)
    char full_path[512]; 

    while (header->name[0] != '\0') {
        if (initrd_strncmp(header->magic, "ustar", 5) != 0) break;
        
        // 1. СКЛЕЙКА PREFIX + NAME (Критический фикс UStar)
        full_path[0] = '\0';
        if (header->prefix[0] != '\0') {
            initrd_strncpy(full_path, header->prefix, 155);
            size_t plen = initrd_strlen(full_path);
            if (plen > 0 && full_path[plen - 1] != '/') {
                full_path[plen] = '/';
                full_path[plen + 1] = '\0';
            }
        }
        size_t cur_len = initrd_strlen(full_path);
        initrd_strncpy(full_path + cur_len, header->name, 100);

        // 2. ЗАЩИТА ОТ "./" (Иммунитет, если Makefile не сработал)
        char* clean_path = full_path;
        if (clean_path[0] == '.' && clean_path[1] == '/') clean_path += 2;
        
        // 3. STRIPPING TRAILING SLASH
        size_t len = initrd_strlen(clean_path);
        if (len > 0 && clean_path[len - 1] == '/') {
            clean_path[len - 1] = '\0';
            len--;
        }

        uint32_t size = parse_octal(header->size, 12);
        char typeflag = header->typeflag;

        if (len == 0) goto next_header; // Пропускаем корневую точку

        // 4. ЧИСТЫЙ ПАРСЕР ПУТЕЙ (Без модификации исходной строки)
        vfs_node_t* parent = root;
        int start = 0;
        int i = 0;
        
        while (clean_path[i] != '\0') {
            if (clean_path[i] == '/') {
                if (i > start) {
                    char token[VFS_MAX_FILENAME];
                    int t_len = i - start;
                    if (t_len >= VFS_MAX_FILENAME) t_len = VFS_MAX_FILENAME - 1;
                    k_memcpy(token, &clean_path[start], t_len);
                    token[t_len] = '\0';
                    parent = get_or_create_dir(parent, token);
                }
                start = i + 1;
            }
            i++;
        }
        
        // То, что осталось после последнего слеша - имя файла/директории
        char filename[VFS_MAX_FILENAME];
        int f_len = i - start;
        if (f_len >= VFS_MAX_FILENAME) f_len = VFS_MAX_FILENAME - 1;
        k_memcpy(filename, &clean_path[start], f_len);
        filename[f_len] = '\0';

        if (typeflag == '5') {
            vfs_node_t* dir = get_or_create_dir(parent, filename);
            // Хардкод для /boot (В будущем: парсинг прав доступа из mode)
            if (parent == root && initrd_strcmp(filename, "boot") == 0) {
                dir->flags |= FS_SYSTEM;
            }
        } 
        else if (typeflag == '0' || typeflag == '\0') {
            initrd_file_t* file_ctx = (initrd_file_t*)kmalloc(sizeof(initrd_file_t));
            if (file_ctx) {
                file_ctx->data = (uint8_t*)header + 512; 
                file_ctx->size = size;
                
                uint32_t file_flags = FS_FILE;
                if (parent->flags & FS_SYSTEM) file_flags |= FS_SYSTEM; 
                
                vfs_node_t* file = vfs_create_node(filename, file_flags, parent, file_ctx);
                if (file) {
                    file->size = size;
                    file->read = initrd_read;
                    files_count++;
                } else {
                    kfree(file_ctx);
                }
            }
        }

next_header:
        uint32_t data_blocks = (size + 511) / 512;
        header = (tar_header_t*)((uint8_t*)header + 512 + (data_blocks * 512));
        if ((uint32_t)header >= PHYS_TO_VIRT(tar_end)) break;
    }
    
    serial_printf("[INITRD] Mounted successfully. Files: %d\n", files_count);
}
