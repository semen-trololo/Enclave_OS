#include "tmpfs.h"
#include "heap.h"
#include "klib.h"
#include "serial.h"
#include "vfs.h"

// Внутренняя структура для хранения данных файла в Heap
typedef struct {
    uint8_t* data;
    uint32_t size;
    uint32_t capacity;
} tmpfs_file_data_t;

vfs_node_t* tmpfs_root = NULL;

// ============================================================================
// КОЛЛБЭКИ TMPFS
// ============================================================================

// ✅ Обработка O_TRUNC и будущих флагов
static int tmpfs_open(vfs_node_t* node, uint32_t flags) {
    if (flags & O_TRUNC) {
        tmpfs_file_data_t* fdata = (tmpfs_file_data_t*)node->private_data;
        if (fdata) {
            // Обнуляем размер, но оставляем capacity для переиспользования буфера
            fdata->size = 0; 
        }
    }
    return 0;
}

static int tmpfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    tmpfs_file_data_t* fdata = (tmpfs_file_data_t*)node->private_data;
    if (!fdata || !fdata->data) return 0;

    if (offset >= fdata->size) return 0;
    if (offset + size > fdata->size) size = fdata->size - offset;

    k_memcpy(buffer, fdata->data + offset, size);
    return size;
}

static int tmpfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, const uint8_t* buffer) {
    tmpfs_file_data_t* fdata = (tmpfs_file_data_t*)node->private_data;
    if (!fdata) return -1;

    uint32_t new_size = offset + size;

    // Динамическое расширение с перевыделением
    if (new_size > fdata->capacity) {
        uint32_t new_capacity = new_size * 2; 
        uint8_t* new_data = (uint8_t*)kmalloc(new_capacity);
        if (!new_data) return -12; // ENOMEM

        if (fdata->data && fdata->size > 0) {
            k_memcpy(new_data, fdata->data, fdata->size);
        }
        
        if (fdata->data) kfree(fdata->data); 
        
        fdata->data = new_data;
        fdata->capacity = new_capacity;
    }

    k_memcpy(fdata->data + offset, buffer, size);
    if (new_size > fdata->size) fdata->size = new_size;

    return size;
}

static vfs_node_t* tmpfs_create(vfs_node_t* parent, const char* name, uint32_t mode) {
    vfs_node_t* new_node = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    if (!new_node) return NULL;
    
    k_memset(new_node, 0, sizeof(vfs_node_t));
    k_strncpy(new_node->name, name, VFS_MAX_FILENAME - 1);
    new_node->flags = FS_FILE;
    new_node->permissions = mode & 07777; 
    new_node->parent = parent;

    if (!parent->first_child) {
        parent->first_child = new_node;
    } else {
        vfs_node_t* sibling = parent->first_child;
        while (sibling->next_sibling) sibling = sibling->next_sibling;
        sibling->next_sibling = new_node;
    }

    tmpfs_file_data_t* fdata = (tmpfs_file_data_t*)kmalloc(sizeof(tmpfs_file_data_t));
    if (!fdata) {
        kfree(new_node);
        return NULL;
    }
    fdata->data = NULL;
    fdata->size = 0;
    fdata->capacity = 0;
    new_node->private_data = fdata;

    new_node->read = tmpfs_read;
    new_node->write = tmpfs_write;
    new_node->open = tmpfs_open; 

    return new_node;
}

static int tmpfs_unlink(vfs_node_t* parent, const char* name) {
    vfs_node_t* prev = NULL;
    vfs_node_t* curr = parent->first_child;

    while (curr) {
        if (k_strcmp(curr->name, name) == 0) {
            if (prev) prev->next_sibling = curr->next_sibling;
            else parent->first_child = curr->next_sibling;

            tmpfs_file_data_t* fdata = (tmpfs_file_data_t*)curr->private_data;
            if (fdata) {
                if (fdata->data) kfree(fdata->data);
                kfree(fdata);
            }
            kfree(curr);
            
            //serial_printf("[TMPFS_UNLINK] <<< Success! Memory freed.\n");
            return 0;
        }
        prev = curr;
        curr = curr->next_sibling;
    }
    return -2; // ENOENT
}

// ============================================================================
// ИНИЦИАЛИЗАЦИЯ И МОНТИРОВАНИЕ (ИСТИННЫЙ MOUNTPOINT)
// ============================================================================
void tmpfs_init(void) {
    serial_print("[TMPFS] Initializing in-memory Writable FS...\n");
    
    tmpfs_root = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    if (!tmpfs_root) {
        serial_print("[TMPFS] FATAL: OOM allocating tmpfs_root!\n");
        return;
    }
    k_memset(tmpfs_root, 0, sizeof(vfs_node_t));
    k_strncpy(tmpfs_root->name, "tmpfs_root", 10); // Имя корня самой ФС
    tmpfs_root->flags = FS_DIRECTORY;
    
    tmpfs_root->create = tmpfs_create;
    tmpfs_root->unlink = tmpfs_unlink;
    tmpfs_root->readdir = vfs_generic_readdir;
    tmpfs_root->finddir = vfs_generic_finddir;
    
    // 1. Гарантируем, что точка монтирования /tmp существует в основном дереве VFS
    // (Если initrd не создал её, создаем сами)
    vfs_node_t* tmp_dir = vfs_findnode("/tmp");
    if (!tmp_dir) {
        tmp_dir = vfs_create_node("tmp", FS_DIRECTORY, vfs_root, NULL);
        if (!tmp_dir) {
            serial_print("[TMPFS] FATAL: Failed to create /tmp mountpoint!\n");
            kfree(tmpfs_root);
            tmpfs_root = NULL;
            return;
        }
    }
    
    // 2. Монтируем tmpfs поверх /tmp (устанавливает флаг FS_MOUNTPOINT)
    if (vfs_mount("/tmp", tmpfs_root) != 0) {
        serial_print("[TMPFS] FATAL: vfs_mount failed!\n");
    } else {
        serial_print("[TMPFS] Successfully mounted at /tmp\n");
    }
}