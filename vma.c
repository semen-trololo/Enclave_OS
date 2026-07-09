// src/vma.c
#include "vma.h"
#include "heap.h"
#include "klib.h"
#include "serial.h"

// ============================================================================
// ДОБАВЛЕНИЕ VMA (Сортированный связный список)
// ============================================================================
void vma_add(task_t* task, uint32_t start, uint32_t end, uint32_t flags) {
    if (!task) return;

    // Выделяем память под новую ноду
    vma_node_t* new_node = (vma_node_t*)kmalloc(sizeof(vma_node_t));
    if (!new_node) {
        serial_print("[VMA] FATAL: OOM allocating VMA node!\n");
        return;
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
