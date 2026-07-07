#include "initrd.h"
#include "vfs.h"
#include "multiboot.h"
#include "heap.h"
#include "klib.h"
#include "serial.h"
#include "paging.h" // Для макроса PHYS_TO_VIRT

// Внешняя переменная из kernel.c (сохраненный физический адрес Multiboot info)
extern uint32_t multiboot_info_ptr;

// Структура модуля Multiboot (GRUB)
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

// Контекст для файлов Initrd (хранит указатель на сырые данные в RAM)
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

static void initrd_strncpy(char* dest, const char* src, int n) {
    int i = 0;
    while (i < n - 1 && src[i]) { dest[i] = src[i]; i++; }
    dest[i] = '\0';
}

// Парсинг восьмеричного числа из ASCII-строки TAR
static uint32_t parse_octal(const char* str, int len) {
    uint32_t res = 0;
    for (int i = 0; i < len && str[i] >= '0' && str[i] <= '7'; i++) {
        res = (res << 3) + (str[i] - '0');
    }
    return res;
}

// Полиморфная функция чтения для Initrd
static int32_t initrd_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (!node || !node->private_data || !buffer) return -1;
    initrd_file_t* file = (initrd_file_t*)node->private_data;
    if (offset >= file->size) return 0; // EOF
    
    uint32_t to_read = size;
    if (offset + to_read > file->size) to_read = file->size - offset;
    
    k_memcpy(buffer, file->data + offset, to_read);
    return to_read;
}

// Вспомогательная функция: найти или создать директорию по пути
static vfs_node_t* get_or_create_dir(vfs_node_t* parent, const char* name) {
    vfs_node_t* child = parent->first_child;
    while (child) {
        if (initrd_strcmp(child->name, name) == 0) return child;
        child = child->next_sibling;
    }
    
    uint32_t flags = FS_DIRECTORY;
    // 🔒 НАСЛЕДОВАНИЕ ФЛАГА БЕЗОПАСНОСТИ
    if (parent->flags & FS_SYSTEM) {
        flags |= FS_SYSTEM;
    }
    
    return vfs_create_node(name, flags, parent, 0);
}

// ==========================================
// ГЛАВНАЯ ФУНКЦИЯ ИНИЦИАЛИЗАЦИИ
// ==========================================

void initrd_init(void) {
    serial_print("\n[INITRD] =======================================\n");
    serial_print("[INITRD] Initializing RAM Disk (with DEBUG)...\n");
    serial_print("[INITRD] =======================================\n");
    
    // ⚠️ ВАЖНО: multiboot_info_ptr - это ФИЗИЧЕСКИЙ адрес. 
    // Используем PHYS_TO_VIRT, иначе после включения Paging будет Page Fault!
    multiboot_info_t* mb_info = (multiboot_info_t*)PHYS_TO_VIRT(multiboot_info_ptr);
    
    if (!(mb_info->flags & MULTIBOOT_INFO_MODS)) {
        serial_print("[INITRD] FATAL: MULTIBOOT_INFO_MODS flag not set!\n");
        return;
    }
    
    if (mb_info->mods_count == 0) {
        serial_print("[INITRD] WARNING: No modules found! Check grub.cfg.\n");
        return;
    }
    
    serial_printf("[INITRD] Found %d module(s) in Multiboot info.\n", mb_info->mods_count);
    
    multiboot_module_t* mods = (multiboot_module_t*)PHYS_TO_VIRT(mb_info->mods_addr);
    uint32_t tar_start = mods[0].mod_start;
    uint32_t tar_end = mods[0].mod_end;
    
    serial_printf("[INITRD] Module 0: Start=0x%x, End=0x%x, Size=%d bytes\n", 
                  tar_start, tar_end, tar_end - tar_start);
    
    if (tar_start == 0 || tar_end <= tar_start) {
        serial_print("[INITRD] FATAL: Invalid module boundaries!\n");
        return;
    }
    
    tar_header_t* header = (tar_header_t*)PHYS_TO_VIRT(tar_start);
    uint32_t files_count = 0;
    
    // Получаем корень VFS для создания путей
    vfs_node_t* root = vfs_findnode("/");
    if (!root) {
        serial_print("[INITRD] FATAL: VFS Root not found!\n");
        return;
    }
    serial_print("[INITRD] VFS Root node acquired.\n\n");

    // ==========================================
    // ГЛАВНЫЙ ЦИКЛ ПАРСИНГА TAR-АРХИВА
    // ==========================================
    while (header->name[0] != '\0') {
        if (initrd_strncmp(header->magic, "ustar", 5) != 0) {
            serial_print("[INITRD] WARNING: Invalid TAR magic, stopping.\n");
            break;
        }
        
        char full_path[VFS_MAX_FILENAME];
        initrd_strncpy(full_path, header->name, VFS_MAX_FILENAME - 1);
        full_path[VFS_MAX_FILENAME - 1] = '\0'; 
        
        // 🔍 DEBUG: Печатаем сырое имя из TAR-хедера
        serial_print("[INITRD] RAW: '");
        serial_print(header->name);
        serial_print("'");

        // 🛠️ ИСПРАВЛЕНИЕ: Убираем префикс "./", который добавляет команда `tar ... .`
        if (full_path[0] == '.' && full_path[1] == '/') {
            int k = 0;
            while (full_path[k + 2]) {
                full_path[k] = full_path[k + 2];
                k++;
            }
            full_path[k] = '\0';
        }
        
        uint32_t size = parse_octal(header->size, 12);
        char typeflag = header->typeflag;

        int len = 0; 
        while(full_path[len]) len++;
        
        if (len == 0) {
            serial_print(" -> SKIPPED (Empty path / Root dot)\n");
            uint32_t data_blocks = (size + 511) / 512;
            header = (tar_header_t*)((uint8_t*)header + 512 + (data_blocks * 512));
            if ((uint32_t)header >= PHYS_TO_VIRT(tar_end)) break;
            continue; 
        }
        
        // Убираем завершающий слеш у директорий (TAR часто добавляет его)
        if (len > 0 && full_path[len - 1] == '/') {
            full_path[len - 1] = '\0';
            len--;
        }
        
        // 🔍 DEBUG: Печатаем очищенный путь и тип
        serial_print(" -> STRIPPED: '");
        serial_print(full_path);
        serial_print("' | Type: ");
        serial_putc(typeflag);
        serial_print("\n");
        
        // Разделяем путь на родителя и имя файла
        char* last_slash = 0;
        for (int i = 0; i < len; i++) {
            if (full_path[i] == '/') last_slash = &full_path[i];
        }
        
        vfs_node_t* parent = root;
        char filename[VFS_MAX_FILENAME];
        
        if (last_slash) {
            *last_slash = '\0'; // Временно обрываем строку
            // Создаем промежуточные директории
            char* token = full_path;
            while (*token) {
                char* next_slash = token;
                while (*next_slash && *next_slash != '/') next_slash++;
                char saved = *next_slash;
                *next_slash = '\0';
                if (*token) {
                    parent = get_or_create_dir(parent, token);
                }
                *next_slash = saved;
                token = next_slash;
                if (*token == '/') token++;
            }
            initrd_strncpy(filename, last_slash + 1, VFS_MAX_FILENAME - 1);
            filename[VFS_MAX_FILENAME - 1] = '\0';
        } else {
            initrd_strncpy(filename, full_path, VFS_MAX_FILENAME - 1);
            filename[VFS_MAX_FILENAME - 1] = '\0';
        }
        
        if (typeflag == '5') {
            vfs_node_t* dir = get_or_create_dir(parent, filename);
            // 🔒 Жестко помечаем /boot как системную директорию
            if (parent == root && initrd_strcmp(filename, "boot") == 0) {
                dir->flags |= FS_SYSTEM;
                serial_print("[INITRD] -> DIR CREATED: '");
                serial_print(filename);
                serial_print("' (MARKED AS FS_SYSTEM)\n");
            } else {
                serial_print("[INITRD] -> DIR CREATED: '");
                serial_print(filename);
                serial_print("'\n");
            }
        } 
        else if (typeflag == '0' || typeflag == '\0') {
            initrd_file_t* file_ctx = (initrd_file_t*)kmalloc(sizeof(initrd_file_t));
            if (file_ctx) {
                file_ctx->data = (uint8_t*)header + 512; // Данные сразу за хедером
                file_ctx->size = size;
                
                uint32_t file_flags = FS_FILE;
                // 🔒 НАСЛЕДОВАНИЕ ФЛАГА БЕЗОПАСНОСТИ
                if (parent->flags & FS_SYSTEM) {
                    file_flags |= FS_SYSTEM; 
                }
                
                vfs_node_t* file = vfs_create_node(filename, file_flags, parent, file_ctx);
                if (file) {
                    file->size = size;
                    file->read = initrd_read;
                    files_count++;
                    serial_print("[INITRD] -> FILE CREATED: '");
                    serial_print(filename);
                    if (file_flags & FS_SYSTEM) serial_print("' (FS_SYSTEM inherited)\n");
                    else serial_print("'\n");
                } else {
                    serial_print("[INITRD] -> FATAL: vfs_create_node returned NULL!\n");
                    kfree(file_ctx);
                }
            } else {
                serial_print("[INITRD] -> FATAL: kmalloc failed for file_ctx!\n");
            }
        }
        
        // Переход к следующему хедеру (округление размера до 512 байт)
        uint32_t data_blocks = (size + 511) / 512;
        header = (tar_header_t*)((uint8_t*)header + 512 + (data_blocks * 512));
        
        if ((uint32_t)header >= PHYS_TO_VIRT(tar_end)) break;
    }
    
    serial_print("\n[INITRD] =======================================\n");
    serial_printf("[INITRD] Parsed TAR successfully. Files: %d\n", files_count);
    
    // 🔍 ЖЕСТКАЯ ДИАГНОСТИКА ДЕРЕВА VFS
    serial_print("[INITRD] === VFS ROOT CHILDREN DEBUG ===\n");
    vfs_node_t* dbg_child = root->first_child;
    if (!dbg_child) {
        serial_print("[INITRD] ROOT IS EMPTY! (Nodes were not attached)\n");
    } else {
        while(dbg_child) {
            serial_print("  -> ");
            serial_print(dbg_child->name);
            if (dbg_child->flags & FS_DIRECTORY) serial_print(" [DIR]");
            if (dbg_child->flags & FS_FILE) serial_print(" [FILE]");
            if (dbg_child->flags & FS_SYSTEM) serial_print(" [SYS]");
            serial_print("\n");
            
            // Рекурсивно печатаем детей (только первый уровень вложенности)
            vfs_node_t* sub_child = dbg_child->first_child;
            while (sub_child) {
                serial_print("      -> ");
                serial_print(sub_child->name);
                if (sub_child->flags & FS_DIRECTORY) serial_print(" [DIR]");
                if (sub_child->flags & FS_FILE) serial_print(" [FILE]");
                if (sub_child->flags & FS_SYSTEM) serial_print(" [SYS]");
                serial_print("\n");
                sub_child = sub_child->next_sibling;
            }
            
            dbg_child = dbg_child->next_sibling;
        }
    }
    serial_print("[INITRD] =======================================\n\n");
}