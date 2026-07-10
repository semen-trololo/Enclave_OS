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

static int tmpfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    tmpfs_file_data_t* fdata = (tmpfs_file_data_t*)node->fs_data;
    if (!fdata || !fdata->data) return 0;

    if (offset >= fdata->size) return 0;
    if (offset + size > fdata->size) size = fdata->size - offset;

    k_memcpy(buffer, fdata->data + offset, size);
    return size;
}

static int tmpfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, const uint8_t* buffer) {
    tmpfs_file_data_t* fdata = (tmpfs_file_data_t*)node->fs_data;
    if (!fdata) return -1;

    uint32_t new_size = offset + size;

    // ✅ СТРЕСС-ТЕСТ HEAP: Динамическое расширение с перевыделением
    if (new_size > fdata->capacity) {
        uint32_t new_capacity = new_size * 2; // Аллоцируем с запасом x2
        uint8_t* new_data = (uint8_t*)kmalloc(new_capacity);
        if (!new_data) return -12; // ENOMEM

        if (fdata->data && fdata->size > 0) {
            k_memcpy(new_data, fdata->data, fdata->size);
        }
        
        if (fdata->data) kfree(fdata->data); // Освобождаем старый буфер!
        
        fdata->data = new_data;
        fdata->capacity = new_capacity;
    }

    k_memcpy(fdata->data + offset, buffer, size);
    if (new_size > fdata->size) fdata->size = new_size;

    return size;
}

static vfs_node_t* tmpfs_create(vfs_node_t* parent, const char* name) {
    vfs_node_t* new_node = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    if (!new_node) return NULL;
    
    k_memset(new_node, 0, sizeof(vfs_node_t));
    k_strncpy(new_node->name, name, 255);
    new_node->flags = FS_FILE;
    new_node->parent = parent;

    // Добавляем в LCRS дерево (Left-Child Right-Sibling)
    if (!parent->child) {
        parent->child = new_node;
    } else {
        vfs_node_t* sibling = parent->child;
        while (sibling->sibling) sibling = sibling->sibling;
        sibling->sibling = new_node;
    }

    // Создаем fs_data
    tmpfs_file_data_t* fdata = (tmpfs_file_data_t*)kmalloc(sizeof(tmpfs_file_data_t));
    if (!fdata) {
        kfree(new_node);
        return NULL;
    }
    fdata->data = NULL;
    fdata->size = 0;
    fdata->capacity = 0;
    new_node->fs_data = fdata;

    // Назначаем коллбэки
    new_node->read = tmpfs_read;
    new_node->write = tmpfs_write;

    serial_printf("[TMPFS] Created file: /tmp/%s\n", name);
    return new_node;
}

static int tmpfs_unlink(vfs_node_t* parent, const char* name) {
    vfs_node_t* prev = NULL;
    vfs_node_t* curr = parent->child;

    while (curr) {
        if (k_strcmp(curr->name, name) == 0) {
            // Удаляем из дерева
            if (prev) prev->sibling = curr->sibling;
            else parent->child = curr->sibling;

            // Освобождаем память (Тест на утечки Heap!)
            tmpfs_file_data_t* fdata = (tmpfs_file_data_t*)curr->fs_data;
            if (fdata) {
                if (fdata->data) kfree(fdata->data);
                kfree(fdata);
            }
            kfree(curr);
            
            serial_printf("[TMPFS] Unlinked file: /tmp/%s\n", name);
            return 0;
        }
        prev = curr;
        curr = curr->sibling;
    }
    return -2; // ENOENT
}

// ============================================================================
// ИНИЦИАЛИЗАЦИЯ И МОНТИРОВАНИЕ
// ============================================================================
void tmpfs_init(void) {
    serial_print("[TMPFS] Initializing in-memory Writable FS...\n");
    
    tmpfs_root = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    k_memset(tmpfs_root, 0, sizeof(vfs_node_t));
    k_strncpy(tmpfs_root->name, "tmp", 3);
    tmpfs_root->flags = FS_DIRECTORY;
    
    tmpfs_root->create = tmpfs_create;
    tmpfs_root->unlink = tmpfs_unlink;
    
    // Здесь нужно примонтировать tmpfs_root к корневой ноде VFS как "tmp"
    // Если у тебя есть функция vfs_mount или ты вручную добавляешь в root->child:
    extern vfs_node_t* vfs_root; // Или как у тебя называется корень VFS
    if (vfs_root) {
        if (!vfs_root->child) {
            vfs_root->child = tmpfs_root;
        } else {
            vfs_node_t* sib = vfs_root->child;
            while(sib->sibling) sib = sib->sibling;
            sib->sibling = tmpfs_root;
        }
        tmpfs_root->parent = vfs_root;
    }
    
    serial_print("[TMPFS] Mounted at /tmp\n");
}
