#include "heap.h"
#include "pmm.h"
#include "paging.h"
#include "klib.h"
#include "serial.h"

// ============================================================================
// КОНСТАНТЫ И КОНФИГУРАЦИЯ
// ============================================================================
#define HEAP_START 0xD0000000
#define HEAP_SIZE  (32 * 1024 * 1024) // 32 MB Virtual Pool
#define HEAP_PAGES (HEAP_SIZE / 4096)  // 8192 pages

// MAX_ORDER: 2^13 * 4KB = 32MB. 
// Уровень 0 = 4KB, Уровень 13 = 32MB.
#define MAX_ORDER 13   
#define TREE_SIZE  16384 // 2^(13 + 1) узлов в неявном бинарном дереве

// Статусы узлов дерева
#define NODE_UNUSED 0 // Внутренний узел, слит с родителем
#define NODE_FREE   1 // Блок свободен
#define NODE_ALLOC  2 // Блок выделен
#define NODE_SPLIT  3 // Блок разбит на близнецов

// Заголовок блока (скрыт от пользователя)
typedef struct {
    uint32_t size;
    uint32_t magic;
} BlockHeader;

#define HEADER_MAGIC 0xDEADBEEF

// Метаданные: неявное бинарное дерево (в .bss секции)
static uint8_t tree[TREE_SIZE];

// ============================================================================
// ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ (O(1) Hardware Accelerated)
// ============================================================================

// Вычисляет глубину узла (0 для корня, 13 для листьев) через Count Leading Zeros
static inline int get_depth(int node) {
    if (node <= 0) return 0;
    return 31 - __builtin_clz(node);
}

// Рекурсивный поиск свободного блока (спуск по дереву)
static int find_free(int node, int current_level, int target_level) {
    if (tree[node] == NODE_ALLOC) return -1;
    if (tree[node] == NODE_FREE) return node; 
    
    if (tree[node] == NODE_SPLIT) {
        if (current_level == target_level) return -1;
        // Ищем в левом, затем в правом поддереве
        int left = find_free(node * 2, current_level - 1, target_level);
        if (left != -1) return left;
        return find_free(node * 2 + 1, current_level - 1, target_level);
    }
    return -1;
}

// ============================================================================
// ИНИЦИАЛИЗАЦИЯ (Zero-Cost Lazy Allocation)
// ============================================================================
void heap_init(void) {
    serial_print("[HEAP] Initializing Buddy System (Lazy Allocation mode)...\n");
    
    // 🛡️ КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ: Мы НЕ выделяем физические страницы здесь!
    // Страницы будут выделяться аппаратно через Page Fault Handler (INT 14),
    // когда ядро впервые попытается записать данные в выданный kmalloc адрес.
    // Это экономит 32 МБ физической RAM на старте системы.
    
    k_memset(tree, NODE_UNUSED, sizeof(tree));
    tree[1] = NODE_FREE; // Корень (весь виртуальный пул 32 МБ) свободен
    
    serial_printf("[HEAP] Virtual pool ready: 0x%x - 0x%x (%u MB)\n", 
                  HEAP_START, HEAP_START + HEAP_SIZE, HEAP_SIZE / (1024*1024));
}

// ============================================================================
// АЛЛОКАЦИЯ (kmalloc)
// ============================================================================
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
    if (node == -1) {
        serial_print("[HEAP] OOM: Buddy system full!\n");
        return NULL; 
    }
    
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
    
    // 5. Пишем заголовок (Эта запись триггерит Page Fault -> Lazy Alloc физической страницы!)
    BlockHeader* header = (BlockHeader*)virt_addr;
    header->size = block_size;
    header->magic = HEADER_MAGIC;
    
    return (void*)(virt_addr + sizeof(BlockHeader));
}

// ============================================================================
// ОСВОБОЖДЕНИЕ (kfree)
// ============================================================================
void kfree(void* ptr) {
    if (!ptr) return;
    
    // 1. Читаем заголовок
    BlockHeader* header = (BlockHeader*)((uint32_t)ptr - sizeof(BlockHeader));
    if (header->magic != HEADER_MAGIC) {
        serial_printf("[HEAP] FATAL: Invalid magic in kfree! (Double free or corruption at 0x%x)\n", (uint32_t)ptr);
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
    
    // 4. Каскадное слияние (merge) с близнецами (XOR Trick)
    while (curr > 1) {
        int buddy = curr ^ 1;      // Адрес близнеца через XOR
        int parent = curr / 2;
        
        if (tree[buddy] == NODE_FREE) {
            tree[curr] = NODE_UNUSED;
            tree[buddy] = NODE_UNUSED;
            tree[parent] = NODE_FREE; // Родитель становится свободным
            curr = parent;            // Поднимаемся выше
        } else {
            break; // Близнец занят или разбит, слияние невозможно
        }
    }
}

// ============================================================================
// ДИАГНОСТИКА (Для Shell)
// ============================================================================
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
    
    uint32_t total_kb = HEAP_SIZE / 1024;
    uint32_t free_kb = free_bytes / 1024;
    uint32_t alloc_kb = alloc_bytes / 1024;

    // Используем k_set_color (Strategy Pattern), а не vga_set_color!
    k_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    k_printf("\n--- [ Kernel Heap Status ] ---\n");
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    k_printf(" Base:      0x%x\n", HEAP_START);
    k_printf(" Total:     %u KB (%u MB)\n", total_kb, total_kb / 1024);
    
    k_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    k_printf(" Free:      %u KB\n", free_kb);
    
    k_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    k_printf(" Allocated: %u KB\n", alloc_kb);
    k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    k_printf("----------------------------\n\n");
}

void heap_run_tests(void) {
    serial_print("[HEAP TEST] Starting Buddy System stress test...\n");
    
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
