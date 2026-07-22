//tmpfs.c

#include "tmpfs.h"
#include "heap.h"
#include "klib.h"
#include "serial.h"
#include "vfs.h"

// 🛡️ OOM PROTECTION: Hard limit on tmpfs file size (40 MB)
// Защищает Kernel Heap от исчерпания багованными или злонамеренными Ring 3 процессами.
// TODO (Day 20+): После интеграции TinyCC внедрить гибридную систему квот:
//   1. Per-file quota: TMPFS_MAX_FILE_SIZE (40 MB)
//   2. Global tmpfs quota: tmpfs_total_allocated <= 25% от pmm_get_free_pages()
//   3. Динамический счетчик tmpfs_total_allocated с cli/sti защитой

// 🛡️ OOM PROTECTION: Hard limit on tmpfs file size (25 MB)
// Компromise между потребностями TinyCC и ограничениями Buddy System
// TODO (Day 30+): Оптимизация выделения памяти — заменить kmalloc на Page Cache
#define TMPFS_MAX_FILE_SIZE (25 * 1024 * 1024)


// Внутренняя структура для хранения данных файла в Heap
typedef struct {
    uint8_t* data;
    uint32_t size;
    uint32_t capacity;
} tmpfs_file_data_t;

vfs_node_t* tmpfs_root = NULL;

// Forward declarations
static void tmpfs_close(vfs_node_t* node);
static int tmpfs_unlink(vfs_node_t* parent, const char* name);

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
            
            // 🛡️ CRITICAL FIX: Синхронизация с VFS node->size
            // Без этого sys_lseek(SEEK_END) вернет старый размер файла!
            node->size = 0;
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

    // 🛡️ OOM PROTECTION: Reject writes that exceed the quota
    if (new_size > TMPFS_MAX_FILE_SIZE) {
        serial_printf("[TMPFS] ENOSPC: Write rejected (size %u exceeds limit %u)\n", 
                      new_size, TMPFS_MAX_FILE_SIZE);
        return -28; // ENOSPC
    }

    // 🛡️ ADAPTIVE GROWTH STRATEGY:
    if (new_size > fdata->capacity) {
        uint32_t new_capacity;
        
        if (new_size < 1024 * 1024) {
            // Маленькие файлы: capacity = next_power_of_2(new_size)
            new_capacity = 1;
            while (new_capacity < new_size) new_capacity <<= 1;
        } else {
            // Большие файлы: capacity = new_size + 25%
            new_capacity = new_size + (new_size / 4);
        }
        
        if (new_capacity > TMPFS_MAX_FILE_SIZE) new_capacity = TMPFS_MAX_FILE_SIZE;
        
        uint8_t* new_data = (uint8_t*)kmalloc(new_capacity);
        if (!new_data) return -12; // ENOMEM

        if (fdata->data && fdata->size > 0) {
            k_memcpy(new_data, fdata->data, fdata->size);
        }
        
        if (fdata->data) kfree(fdata->data); 
        
        fdata->data = new_data;
        fdata->capacity = new_capacity;
    }

    // 🛡️ TRUE POSIX SPARSE FILES: Заполняем "дырки" нулями
    // Если запись происходит за пределами текущего размера файла (offset > size),
    // промежуток между старым size и новым offset ДОЛЖЕН быть заполнен нулями.
    // Без этого чтение "дырки" вернет мусор из kmalloc.
    if (offset > fdata->size) {
        k_memset(fdata->data + fdata->size, 0, offset - fdata->size);
    }

    k_memcpy(fdata->data + offset, buffer, size);
    if (new_size > fdata->size) {
        fdata->size = new_size;
        
        // 🛡️ CRITICAL FIX: Синхронизация с VFS node->size
        // Это гарантирует, что sys_lseek(SEEK_END) и sys_fstat(st_size)
        // всегда видят актуальный размер файла.
        node->size = new_size;
    }

    return size;
}

static vfs_node_t* tmpfs_create(vfs_node_t* parent, const char* name, uint32_t mode) {
    vfs_node_t* new_node = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    if (!new_node) return NULL;

    k_memset(new_node, 0, sizeof(vfs_node_t));
    k_strncpy(new_node->name, name, VFS_MAX_FILENAME - 1);
    new_node->permissions = mode & 07777;
    new_node->parent = parent;

    if (mode & S_IFDIR) {
        /* === DIRECTORY === */
        new_node->flags        = FS_DIRECTORY;
        new_node->private_data = NULL;
        new_node->readdir      = vfs_generic_readdir;
        new_node->finddir      = vfs_generic_finddir;
        new_node->create       = tmpfs_create;
        new_node->unlink       = tmpfs_unlink;
        new_node->open         = tmpfs_open;   /* safe: private_data==NULL → no-op */
        new_node->close        = tmpfs_close;  /* safe: private_data==NULL → no-op */
    } else {
        /* === FILE (existing behavior, unchanged) === */
        new_node->flags = FS_FILE;

        tmpfs_file_data_t* fdata = (tmpfs_file_data_t*)kmalloc(sizeof(tmpfs_file_data_t));
        if (!fdata) {
            kfree(new_node);
            return NULL;
        }

        fdata->data     = NULL;
        fdata->size     = 0;
        fdata->capacity = 0;
        new_node->private_data = fdata;

        new_node->read  = tmpfs_read;
        new_node->write = tmpfs_write;
        new_node->open  = tmpfs_open;
        new_node->close = tmpfs_close;
    }

    /* Atomic commit: link to tree ONLY after all resources secured */
    if (!parent->first_child) {
        parent->first_child = new_node;
    } else {
        vfs_node_t* sibling = parent->first_child;
        while (sibling->next_sibling) sibling = sibling->next_sibling;
        sibling->next_sibling = new_node;
    }

    return new_node;
}

// 🛡️ TRUE POSIX CLOSE: Освобождение ресурсов для "сирот" (Orphaned Nodes)
static void tmpfs_close(vfs_node_t* node) {
    // Вызывается из sys_close (vfs.c), когда ref_count падает до 0.
    // Если файл просто закрыт, но существует в дереве, данные НЕ освобождаем!
    // Если файл был удален (is_unlinked == true), освобождаем только private_data.
    // Сам vfs_node_t освобождается в sys_close после вызова node->close().
    if (node->is_unlinked) {
        tmpfs_file_data_t* fdata = (tmpfs_file_data_t*)node->private_data;
        if (fdata) {
            if (fdata->data) kfree(fdata->data);
            kfree(fdata);
        }
        node->private_data = NULL;
        
        // ❌ УБРАЛИ: kfree(node) — это делает sys_close после вызова tmpfs_close
    }
}

static int tmpfs_unlink(vfs_node_t* parent, const char* name) {
    vfs_node_t* prev = NULL;
    vfs_node_t* curr = parent->first_child;

    while (curr) {
        if (k_strcmp(curr->name, name) == 0) {
            /* POSIX: нельзя удалить непустую директорию */
            if ((curr->flags & FS_DIRECTORY) && curr->first_child) {
                return -ENOTEMPTY;
            }

            if (prev) prev->next_sibling = curr->next_sibling;
            else parent->first_child = curr->next_sibling;

            curr->parent       = NULL;
            curr->next_sibling = NULL;
            curr->is_unlinked  = 1;

            return 0;
        }
        prev = curr;
        curr = curr->next_sibling;
    }

    return -ENOENT;
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
    tmpfs_root->close = tmpfs_close; // На случай, если корневую ноду когда-либо закроют
    
    // 1. Гарантируем, что точка монтирования /tmp существует в основном дереве VFS
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
        serial_print("[TMPFS] Successfully mounted at /tmp with 25MB quota per file.\n");
    }
}
