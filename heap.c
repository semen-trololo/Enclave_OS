#include "heap.h"
#include "pmm.h"
#include "paging.h"
#include "klib.h"
#include "serial.h"
#include "config.h" // ✅ SSOT

// ============================================================================
// КОНСТАНТЫ И КОНФИГУРАЦИЯ (SSOT)
// ============================================================================
#define HEAP_START KERNEL_HEAP_VIRT
#define HEAP_SIZE  KERNEL_HEAP_SIZE
#define HEAP_END   KERNEL_HEAP_END

#define HEAP_PAGES (HEAP_SIZE / 4096)  

#define MAX_ORDER 15   // 2^15 * 4096 = 128 MB max block
#define TREE_SIZE  65536  // 2^(MAX_ORDER + 1) nodes in implicit binary tree

#define NODE_UNUSED 0 
#define NODE_FREE   1 
#define NODE_ALLOC  2 
#define NODE_SPLIT  3 

typedef struct {
    uint32_t size;
    uint32_t magic;
} BlockHeader;

#define HEADER_MAGIC 0xDEADBEEF

static uint8_t tree[TREE_SIZE];

static uint32_t heap_total_allocs = 0;
static uint32_t heap_total_frees = 0;

// ============================================================================
// IRQ SAFETY HELPERS
// ============================================================================
static inline uint32_t read_eflags(void) {
    uint32_t flags;
    __asm__ volatile("pushf ; pop %0" : "=r"(flags));
    return flags;
}

static inline void load_eflags(uint32_t flags) {
    __asm__ volatile("push %0 ; popf" : : "r"(flags));
}

static inline void disable_interrupts(void) {
    __asm__ volatile("cli");
}

// ============================================================================
// ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
// ============================================================================
static inline int get_depth(int node) {
    if (node <= 0) return 0;
    return 31 - __builtin_clz(node);
}

static int find_free(int node, int current_level, int target_level) {
    if (tree[node] == NODE_ALLOC) return -1;
    if (tree[node] == NODE_FREE) return node; 
    
    if (tree[node] == NODE_SPLIT) {
        if (current_level == target_level) return -1;
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
    
    heap_total_allocs = 0;
    heap_total_frees = 0;
    
    k_memset(tree, NODE_UNUSED, sizeof(tree));
    tree[1] = NODE_FREE; 
    
    serial_printf("[HEAP] Virtual pool ready: 0x%x - 0x%x (%u MB)\n", 
                  HEAP_START, HEAP_END, HEAP_SIZE / (1024*1024));
}

// ============================================================================
// АЛЛОКАЦИЯ (kmalloc) - IRQ SAFE
// ============================================================================
void* kmalloc(size_t size) {
    if (size == 0 || size > HEAP_SIZE) return NULL;
    
    size_t req_size = size + sizeof(BlockHeader);
    
    int target_level = 0;
    uint32_t block_size = 4096;
    while (block_size < req_size && target_level < MAX_ORDER) {
        block_size <<= 1;
        target_level++;
    }
    
    if (block_size < req_size) return NULL; 
    
    uint32_t flags = read_eflags();
    disable_interrupts(); 

    int node = find_free(1, MAX_ORDER, target_level);
    if (node == -1) {
        load_eflags(flags);
        serial_print("[HEAP] OOM: Buddy system full!\n");
        return NULL; 
    }
    
    int depth = get_depth(node);
    int curr_level = MAX_ORDER - depth;
    int curr = node;
    
    while (curr_level > target_level) {
        tree[curr] = NODE_SPLIT;
        int left = curr * 2;
        int right = curr * 2 + 1;
        tree[right] = NODE_FREE; 
        curr = left;             
        curr_level--;
    }
    tree[curr] = NODE_ALLOC;
    
    int curr_depth = get_depth(curr);
    int block_index = curr - (1 << curr_depth);
    uint32_t offset = block_index * block_size;
    uint32_t virt_addr = HEAP_START + offset;
    
    heap_total_allocs++; 
    load_eflags(flags); 
    
    // 🛡️ Lazy Write: Триггерит Page Fault -> VMM выделяет физ. страницу
    BlockHeader* header = (BlockHeader*)virt_addr;
    header->size = block_size;
    header->magic = HEADER_MAGIC;
    
    return (void*)(virt_addr + sizeof(BlockHeader));
}

// ============================================================================
// ОСВОБОЖДЕНИЕ (kfree) - IRQ SAFE + BOUNDS CHECKING
// ============================================================================
void kfree(void* ptr) {
    if (!ptr) return;
    
    BlockHeader* header = (BlockHeader*)((uint32_t)ptr - sizeof(BlockHeader));
    uint32_t virt_addr = (uint32_t)header;
    
    if (virt_addr < HEAP_START || virt_addr >= HEAP_END) {
        serial_printf("[HEAP] FATAL: kfree called with out-of-bounds pointer 0x%x\n", (uint32_t)ptr);
        return;
    }
    
    if (header->magic != HEADER_MAGIC) {
        serial_printf("[HEAP] FATAL: Invalid magic in kfree! (Double free or corruption at 0x%x)\n", (uint32_t)ptr);
        return;
    }
    
    uint32_t block_size = header->size;
    uint32_t offset = virt_addr - HEAP_START;
    
    int target_level = 0;
    uint32_t temp_size = 4096;
    while (temp_size < block_size) {
        temp_size <<= 1;
        target_level++;
    }
    
    int depth = MAX_ORDER - target_level;
    int block_index = offset / block_size;
    int curr = (1 << depth) + block_index;
    
    uint32_t flags = read_eflags();
    disable_interrupts(); 
    
    tree[curr] = NODE_FREE;
    header->magic = 0; 
    heap_total_frees++; 
    
    while (curr > 1) {
        int buddy = curr ^ 1;      
        int parent = curr / 2;
        
        if (tree[buddy] == NODE_FREE) {
            tree[curr] = NODE_UNUSED;
            tree[buddy] = NODE_UNUSED;
            tree[parent] = NODE_FREE; 
            curr = parent;            
        } else {
            break; 
        }
    }
    
    load_eflags(flags);
}

// ============================================================================
// HEAP ACCOUNTING API
// ============================================================================
uint32_t heap_get_alloc_count(void) { return heap_total_allocs; }
uint32_t heap_get_free_count(void) { return heap_total_frees; }
int32_t heap_check_balance(void) { 
    uint32_t flags;
    // 🛡️ FIX: Защищаем чтение от прерываний (PIT/Keyboard), 
    // чтобы избежать "Torn Read" между allocs и frees.
    __asm__ volatile("pushf; pop %0; cli" : "=r"(flags));
    int32_t balance = (int32_t)(heap_total_allocs - heap_total_frees);
    __asm__ volatile("push %0; popf" : : "r"(flags));
    return balance;
}

// ============================================================================
// ДИАГНОСТИКА
// ============================================================================
void heap_print_fragmentation(void) {
    uint32_t free_blocks[MAX_ORDER + 1] = {0};
    uint32_t alloc_blocks[MAX_ORDER + 1] = {0};
    
    // Проходим по всему дереву Buddy System
    for (int i = 1; i < TREE_SIZE; i++) {
        if (tree[i] == NODE_FREE || tree[i] == NODE_ALLOC) {
            int depth = get_depth(i);
            int level = MAX_ORDER - depth;
            if (level >= 0 && level <= MAX_ORDER) {
                if (tree[i] == NODE_FREE) free_blocks[level]++;
                else alloc_blocks[level]++;
            }
        }
    }
    
    k_printf("\n--- [ Kernel Heap Fragmentation Matrix ] ---\n");
    k_printf(" Block Size | Free Blocks | Alloc Blocks\n");
    k_printf("--------------------------------------------\n");
    serial_printf("\n--- [ Kernel Heap Fragmentation Matrix ] ---\n");
    serial_printf(" Block Size | Free Blocks | Alloc Blocks\n");
    serial_printf("--------------------------------------------\n");
    
    uint32_t total_free_kb = 0;
    uint32_t total_alloc_kb = 0;
    
    for (int level = 0; level <= MAX_ORDER; level++) {
        uint32_t size_kb = (4096 << level) / 1024;
        
        if (free_blocks[level] > 0 || alloc_blocks[level] > 0) {
            // 🛡️ FIX: Используем только простые %u без модификаторов ширины
            k_printf(" %u KB | %u | %u\n", 
                     size_kb, free_blocks[level], alloc_blocks[level]);
            serial_printf(" %u KB | %u | %u\n", 
                     size_kb, free_blocks[level], alloc_blocks[level]);
            total_free_kb += free_blocks[level] * size_kb;
            total_alloc_kb += alloc_blocks[level] * size_kb;
        }
    }
    k_printf("--------------------------------------------\n");
    k_printf(" Total Free:  %u KB (%u MB)\n", total_free_kb, total_free_kb / 1024);
    k_printf(" Total Alloc: %u KB (%u MB)\n", total_alloc_kb, total_alloc_kb / 1024);
    serial_printf("--------------------------------------------\n");
    serial_printf(" Total Free:  %u KB (%u MB)\n", total_free_kb, total_free_kb / 1024);
    serial_printf(" Total Alloc: %u KB (%u MB)\n", total_alloc_kb, total_alloc_kb / 1024);
    
    // Оценка фрагментации
    if (free_blocks[MAX_ORDER] == 0 && free_blocks[MAX_ORDER-1] == 0 && 
        free_blocks[MAX_ORDER-2] == 0 && total_free_kb > 1024) {
        k_printf(" [WARNING] Severe fragmentation! No large contiguous blocks.\n");
        serial_printf(" [WARNING] Severe fragmentation! No large contiguous blocks.\n");
    } else {
        k_printf(" [OK] Heap is relatively healthy.\n");
        serial_printf(" [OK] Heap is relatively healthy.\n");
    }
    k_printf("--------------------------------------------\n\n");
    serial_printf("--------------------------------------------\n\n");
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
    
    uint32_t total_kb = HEAP_SIZE / 1024;
    uint32_t free_kb = free_bytes / 1024;
    uint32_t alloc_kb = alloc_bytes / 1024;

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

    
    serial_printf("\n--- [ Kernel Heap Status ] ---\n");
    serial_printf(" Base:      0x%x\n", HEAP_START);
    serial_printf(" Total:     %u KB (%u MB)\n", total_kb, total_kb / 1024);
    serial_printf(" Free:      %u KB\n", free_kb);
    serial_printf(" Allocated: %u KB\n", alloc_kb);
    serial_printf("----------------------------\n\n");
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
    void* p4 = kmalloc(40 * 1024 * 1024);  
    if (!p4) k_print("[OK]\n");
    else k_print("[FAIL]\n");
    
    k_print("[HEAP TEST] All tests completed!\n");
}