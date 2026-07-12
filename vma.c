// src/vma.c
#include "vma.h"
#include "heap.h"
#include "klib.h"
#include "serial.h"
#include <stdbool.h>

bool vma_intersects(task_t* task, uint32_t start, uint32_t end, vma_node_t* ignore_vma) {
    if (!task) return false;
    vma_node_t* curr = task->vma_head;
    while (curr) {
        if (curr == ignore_vma) {
            curr = curr->next;
            continue;
        }
        // Проверка пересечения: [start, end) и [curr->start, curr->end)
        if (start < curr->end && end > curr->start) {
            return true;
        }
        // Оптимизация: список отсортирован. Если curr->start >= end, дальше пересечений не будет.
        if (curr->start >= end) {
            break;
        }
        curr = curr->next;
    }
    return false;
}

uint32_t vma_find_free_area(task_t* task, uint32_t size) {
    if (!task) return 0;
    
    uint32_t current_addr = USER_MMAP_START;
    vma_node_t* curr = task->vma_head;

    while (curr) {
        // Пропускаем VMA, которые находятся ниже зоны mmap
        if (curr->end <= USER_MMAP_START) {
            curr = curr->next;
            continue;
        }
        
        // Проверяем промежуток перед текущей VMA
        if (curr->start > current_addr) {
            if (curr->start - current_addr >= size) {
                return current_addr; // Нашли дырку!
            }
        }
        
        // Сдвигаем указатель за конец текущей VMA
        current_addr = curr->end;
        curr = curr->next;
    }
    
    // Проверяем, влезет ли область после последней VMA
    if (current_addr + size <= USER_MMAP_START + USER_MMAP_MAX_SIZE) {
        return current_addr;
    }
    
    return 0; // Виртуальная память в зоне mmap закончилась
}

int vma_unmap_range(task_t* task, uint32_t start, uint32_t end) {
    if (!task) return -1;
    
    vma_node_t** curr_ptr = &task->vma_head;
    vma_node_t* curr = task->vma_head;

    while (curr) {
        // 1. Нет пересечения
        if (curr->end <= start || curr->start >= end) {
            curr_ptr = &curr->next;
            curr = curr->next;
            continue;
        }

        // 2. Полное поглощение (VMA целиком внутри munmap)
        if (curr->start >= start && curr->end <= end) {
            vma_node_t* to_free = curr;
            curr = curr->next;
            *curr_ptr = curr;
            kfree(to_free);
            continue; // curr_ptr не меняется, он уже смотрит на следующий
        }

        // 3. Обрезание начала (munmap задел только начало VMA)
        if (start <= curr->start && end > curr->start && end < curr->end) {
            curr->start = end;
            curr_ptr = &curr->next;
            curr = curr->next;
            continue;
        }

        // 4. Обрезание конца (munmap задел только конец VMA)
        if (start > curr->start && start < curr->end && end >= curr->end) {
            curr->end = start;
            curr_ptr = &curr->next;
            curr = curr->next;
            continue;
        }

        // 5. РАЗДЕЛЕНИЕ (Split: munmap вырезал кусок из середины VMA)
        if (start > curr->start && end < curr->end) {
            vma_node_t* new_node = (vma_node_t*)kmalloc(sizeof(vma_node_t));
            if (!new_node) {
                serial_print("[VMA] FATAL: OOM splitting VMA in munmap!\n");
                return -12; // -ENOMEM. Строгий отказ, атомарность сохранена.
            }
            
            new_node->start = end;
            new_node->end = curr->end;
            new_node->flags = curr->flags;
            new_node->next = curr->next;
            
            curr->end = start;
            curr->next = new_node;
            
            // Переходим к следующей ноде (new_node остается, он вне зоны unmap)
            curr_ptr = &new_node->next;
            curr = new_node->next;
            continue;
        }

        curr_ptr = &curr->next;
        curr = curr->next;
    }
    return 0;
}

// ============================================================================
// ДОБАВЛЕНИЕ VMA (Сортированный связный список)
// ============================================================================
int vma_add(task_t* task, uint32_t start, uint32_t end, uint32_t flags) {
    if (!task) return -1; // -EINVAL (базовая проверка)
    // ✅ Zero Trust: Запрещаем создавать User VMA в Kernel Space
    if (start >= KERNEL_SPACE_START || end > KERNEL_SPACE_START) {
        serial_printf("[VMA] FATAL: Attempt to create VMA in Kernel Space (0x%x - 0x%x)!\n", start, end);
        return -1; // -EINVAL
    }

    // Выделяем память под новую ноду
    vma_node_t* new_node = (vma_node_t*)kmalloc(sizeof(vma_node_t));
    if (!new_node) {
        serial_print("[VMA] FATAL: OOM allocating VMA node!\n");
        return -12; // -ENOMEM (Возвращаем ошибку ядру!)
    }

    new_node->start = start & 0xFFFFF000; // Page-align down
    new_node->end = (end + 0xFFF) & 0xFFFFF000; // Page-align up
    new_node->flags = flags;
    new_node->next = NULL;

    // Вставляем в сортированный список
    if (!task->vma_head || task->vma_head->start > new_node->start) {
        // Вставка в начало
        new_node->next = task->vma_head;
        task->vma_head = new_node;
    } else {
        // Поиск позиции для вставки
        vma_node_t* current = task->vma_head;
        while (current->next && current->next->start < new_node->start) {
            current = current->next;
        }
        new_node->next = current->next;
        current->next = new_node;
    }

    serial_printf("[VMA] Added: 0x%x - 0x%x (flags: 0x%x)\n",
                  new_node->start, new_node->end, new_node->flags);
                  
    return 0; // Успех
}
// ============================================================================
// ПОИСК VMA (Линейный поиск по сортированному списку)
// ============================================================================
vma_node_t* vma_find(task_t* task, uint32_t addr) {
    if (!task) return NULL;

    uint32_t page_addr = addr & 0xFFFFF000;
    vma_node_t* current = task->vma_head;

    while (current) {
        // Проверяем, попадает ли адрес в диапазон [start, end)
        if (page_addr >= current->start && page_addr < current->end) {
            return current;
        }
        // Поскольку список отсортирован, если start > addr, дальше искать бессмысленно
        if (current->start > page_addr) {
            break;
        }
        current = current->next;
    }

    return NULL; // Адрес не принадлежит ни одной VMA
}

// ============================================================================
// УНИЧТОЖЕНИЕ ВСЕХ VMA (Очистка памяти)
// ============================================================================
void vma_destroy_all(task_t* task) {
    if (!task) return;

    vma_node_t* current = task->vma_head;
    while (current) {
        vma_node_t* next = current->next;
        kfree(current);
        current = next;
    }

    task->vma_head = NULL;
}
