// include/vma.h
#ifndef VMA_H
#define VMA_H

#include <stdint.h>
#include <stdbool.h> // Убедись, что bool подключен
#include "task.h" // Для task_t

// ============================================================================
// VMA FLAGS (Права доступа, маппятся на биты PTE)
// ============================================================================
#define VMA_READ    0x01
#define VMA_WRITE   0x02
#define VMA_EXEC    0x04
#define VMA_COW     0x08 // Copy-on-Write (для будущего fork)

// ============================================================================
// VMA NODE (Элемент связного списка)
// ============================================================================
typedef struct vma_node {
    uint32_t start;         // Начальный виртуальный адрес (включительно, page-aligned)
    uint32_t end;           // Конечный виртуальный адрес (исключительно, page-aligned)
    uint32_t flags;         // Комбинация VMA_READ | VMA_WRITE | VMA_EXEC
    struct vma_node* next;  // Следующая нода (список отсортирован по start)
} vma_node_t;

// ============================================================================
// VMA API
// ============================================================================
// Добавить новую VMA в список процесса.
// Список автоматически сортируется по адресу start.
// Возвращает: 0 при успехе, -ENOMEM при нехватке памяти (OOM).
int vma_add(task_t* task, uint32_t start, uint32_t end, uint32_t flags);

// Найти VMA, которая покрывает указанный адрес.
// Возвращает NULL, если адрес не принадлежит ни одной VMA.
vma_node_t* vma_find(task_t* task, uint32_t addr);

// Освободить все VMA процесса.
// Вызывается Grim Reaper'ом при смерти задачи.
void vma_destroy_all(task_t* task);

// Проверка пересечения диапазона [start, end) с существующими VMA.
// ignore_vma позволяет игнорировать саму себя (например, при расширении кучи).
bool vma_intersects(task_t* task, uint32_t start, uint32_t end, vma_node_t* ignore_vma);

// Поиск свободной "дырки" для mmap, начиная с USER_MMAP_START.
// Возвращает выровненный адрес или 0, если места нет.
uint32_t vma_find_free_area(task_t* task, uint32_t size);

// Удаление/разделение VMA, попадающих в диапазон [start, end).
// Возвращает 0 при успехе, -ENOMEM если не хватило памяти для Split VMA.
int vma_unmap_range(task_t* task, uint32_t start, uint32_t end);

// [DAY 31] S1 FIX: Изменение прав доступа для диапазона [start, end)
// с автоматическим Split VMA при частичном покрытии.
// Сохраняет VMA_COW при обновлении флагов.
// Возвращает 0 при успехе, -ENOMEM при OOM (split node allocation).
int vma_protect_range(task_t* task, uint32_t start, uint32_t end, uint32_t new_flags);

// [ДЕНЬ 14] Клонирование списка VMA при fork().
// Создает глубокую копию всех VMA-нод для ребенка.
// Возвращает 0 при успехе, -ENOMEM при OOM.
int vma_clone(task_t* child_task, task_t* parent_task);

#endif // VMA_H
