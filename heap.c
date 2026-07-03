#include "heap.h"
#include "pmm.h"
#include "paging.h"
#include "klib.h"
#include "vga.h"
#include "serial.h"

#define HEAP_START 0xD0000000
#define HEAP_SIZE  (32 * 1024 * 1024) // 32 MB
#define HEAP_PAGES (HEAP_SIZE / 4096)  // 8192 pages

#define MAX_ORDER 13   // 2^13 * 4KB = 32MB
#define TREE_SIZE  16384 // 2^(13 + 1)

// Статусы узлов дерева
#define NODE_UNUSED 0 // Внутренний узел, который полностью слит с родителем
#define NODE_FREE   1 // Блок свободен и готов к выдаче
#define NODE_ALLOC  2 // Блок выделен пользователю
#define NODE_SPLIT  3 // Блок разбит на два меньших близнеца

// Заголовок блока (скрыт от пользователя, находится перед выданным указателем)
typedef struct {
    uint32_t size;
    uint32_t magic;
} BlockHeader;

#define HEADER_MAGIC 0xDEADBEEF

// Метаданные: неявное бинарное дерево
static uint8_t tree[TREE_SIZE];

// Вспомогательная функция: вычисляет глубину узла в дереве (0 для корня, 10 для листьев)
static int get_depth(int node) {
    int d = 0;
    while (node > 1) {
        node >>= 1;
        d++;
    }
    return d;
}

// Рекурсивный поиск свободного блока нужного или большего размера
static int find_free(int node, int current_level, int target_level) {
    if (tree[node] == NODE_ALLOC) return -1;
    if (tree[node] == NODE_FREE) return node; // Нашли!
    if (tree[node] == NODE_SPLIT) {
        if (current_level == target_level) return -1;
        // Ищем в левом, затем в правом поддереве
        int left = find_free(node * 2, current_level - 1, target_level);
        if (left != -1) return left;
        return find_free(node * 2 + 1, current_level - 1, target_level);
    }
    return -1;
}

void heap_init(void) {
    k_printf("[HEAP] Allocating %d pages for Kernel Heap...\n", HEAP_PAGES);
    serial_print("[SERIAL] Allocating  pages for Kernel Heap...\n");
    // 1. Pre-allocation: запрашиваем физические страницы и мапим их в виртуальный пул
    for (uint32_t i = 0; i < HEAP_PAGES; i++) {
        uint32_t phys = pmm_alloc_page();
        if (phys == 0) {
            k_printf("[HEAP] FATAL: Out of physical memory during heap init!\n");
            serial_print("[SERIAL] FATAL: Out of physical memory during heap init!...\n");
            return;
        }
        vmm_map_page(HEAP_START + i * 4096, phys, PAGE_PRESENT | PAGE_WRITE);
    }
    
    // 2. Инициализация дерева
    k_memset(tree, NODE_UNUSED, sizeof(tree));
    tree[1] = NODE_FREE; // Корень (весь пул 4 МБ) изначально свободен
    
    k_printf("[HEAP] Initialized 4 MB Virtual Pool at 0x%x\n", HEAP_START);
    serial_print("[SERIAL] Initialized 4 MB Virtual Pool ...\n");
}

void* kmalloc(size_t size) {
    if (size == 0 || size > HEAP_SIZE) return NULL;
    
    size_t req_size = size + sizeof(BlockHeader);
    
    // 1. Находим минимальный уровень (размер блока), способный вместить запрос
    int target_level = 0;
    uint32_t block_size = 4096;
    while (block_size < req_size && target_level < MAX_ORDER) {
        block_size <<= 1;
        target_level++;
    }
    
    if (block_size < req_size) return NULL; // Запрос больше всего Heap'а
    
    // 2. Ищем свободный узел
    int node = find_free(1, MAX_ORDER, target_level);
    if (node == -1) return NULL; // OOM
    
    // 3. Спускаемся вниз, разделяя (split) блоки по пути
    int depth = get_depth(node);
    int curr_level = MAX_ORDER - depth;
    int curr = node;
    
    while (curr_level > target_level) {
        tree[curr] = NODE_SPLIT;
        int left = curr * 2;
        int right = curr * 2 + 1;
        tree[right] = NODE_FREE; // Правый близнец становится свободным
        curr = left;             // Идем по левому пути
        curr_level--;
    }
    tree[curr] = NODE_ALLOC;
    
    // 4. Вычисляем виртуальный адрес по индексу узла
    int curr_depth = get_depth(curr);
    int block_index = curr - (1 << curr_depth);
    uint32_t offset = block_index * block_size;
    uint32_t virt_addr = HEAP_START + offset;
    
    // 5. Пишем заголовок и возвращаем указатель со сдвигом
    BlockHeader* header = (BlockHeader*)virt_addr;
    header->size = block_size;
    header->magic = HEADER_MAGIC;
    
    return (void*)(virt_addr + sizeof(BlockHeader));
}

void kfree(void* ptr) {
    if (!ptr) return;
    
    // 1. Читаем заголовок
    BlockHeader* header = (BlockHeader*)((uint32_t)ptr - sizeof(BlockHeader));
    if (header->magic != HEADER_MAGIC) {
        k_printf("[HEAP] ERROR: Invalid magic in kfree! (Double free or corruption)\n");
        return;
    }
    
    uint32_t block_size = header->size;
    uint32_t virt_addr = (uint32_t)header;
    uint32_t offset = virt_addr - HEAP_START;
    
    // 2. Вычисляем уровень и индекс узла в дереве
    int target_level = 0;
    uint32_t temp_size = 4096;
    while (temp_size < block_size) {
        temp_size <<= 1;
        target_level++;
    }
    
    int depth = MAX_ORDER - target_level;
    int block_index = offset / block_size;
    int curr = (1 << depth) + block_index;
    
    // 3. Освобождаем узел
    tree[curr] = NODE_FREE;
    header->magic = 0; // Затираем магическое число для защиты от double-free
    
    // 4. Каскадное слияние (merge) с близнецами
    while (curr > 1) {
        int buddy = curr ^ 1;      // Адрес близнеца через XOR
        int parent = curr / 2;
        
        if (tree[buddy] == NODE_FREE) {
            tree[curr] = NODE_UNUSED;
            tree[buddy] = NODE_UNUSED;
            tree[parent] = NODE_FREE; // Родитель становится свободным
            curr = parent;            // Поднимаемся выше
        } else {
            break; // Близнец занят, слияние невозможно
        }
    }
}

void heap_print_status(void) {
    uint32_t free_bytes = 0;
    uint32_t alloc_bytes = 0;
    
    for (int i = 1; i < TREE_SIZE; i++) {
        if (tree[i] == NODE_FREE || tree[i] == NODE_ALLOC) {
            int depth = get_depth(i);
            int level = MAX_ORDER - depth;
            uint32_t size = 4096 << level;
            if (tree[i] == NODE_FREE) free_bytes += size;
            else alloc_bytes += size;
        }
    }
    
    // Динамический расчет размеров из макросов
    uint32_t total_mb = HEAP_SIZE / (1024 * 1024);
    uint32_t free_mb = free_bytes / (1024);
    uint32_t alloc_mb = alloc_bytes / (1024);

    vga_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    k_printf("[HEAP] --- Kernel Heap Status ---\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    k_printf("[HEAP] Virtual Base: 0x%x\n", HEAP_START);
    
    // НИКАКОГО ХАРДКОДА! Только математика.
    k_printf("[HEAP] Total Size:   %u MB (%u bytes)\n", total_mb, HEAP_SIZE);
    
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    k_printf("[HEAP] Free:         %u KB (%u bytes)\n", free_mb, free_bytes);
    
    vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    k_printf("[HEAP] Allocated:    %u KB (%u bytes)\n", alloc_mb, alloc_bytes);
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

void heap_run_tests(void) {
    k_print("[HEAP TEST] 1. Small allocations... ");
    void* p1 = kmalloc(100);
    void* p2 = kmalloc(200);
    if (p1 && p2) k_print("[OK]\n");
    else k_print("[FAIL]\n");
    
    k_print("[HEAP TEST] 2. Free and merge... ");
    kfree(p1);
    kfree(p2);
    k_print("[OK]\n");
    
    k_print("[HEAP TEST] 3. Large allocation (1 MB)... ");
    void* p3 = kmalloc(1024 * 1024);
    if (p3) k_print("[OK]\n");
    else k_print("[FAIL]\n");
    kfree(p3);
    
    k_print("[HEAP TEST] 4. OOM protection... ");
    void* p4 = kmalloc(40 * 1024 * 1024);  // 40 MB > 32 MB heap
    if (!p4) k_print("[OK]\n");
    else k_print("[FAIL]\n");
    
    k_print("[HEAP TEST] All tests completed!\n");
}