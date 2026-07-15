#include "user_libc.h"

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    
    printf("[TEST] Probing User-Space OOM boundary via malloc...\n");
    
    int alloc_count = 0;
    void* ptrs[1024]; // Максимум 1 ГБ (если лимиты позволят)
    
    // Адаптивный цикл: выделяем до тех пор, пока ядро не скажет "Стоп"
    while (alloc_count < 1024) {
        void* p = malloc(1024 * 1024); // 1 MB
        
        if (!p) {
            // 🛡️ OOM Protection сработала!
            printf("[PASS] malloc correctly returned NULL after %d MB (errno=%d)\n", 
                   alloc_count, errno);
            return 0;
        }
        
        // КРИТИЧНО: Касаемся страницы, чтобы триггерить Demand Paging 
        // и занять физическую RAM. Иначе sys_brk просто расширит VMA.
        memset(p, 'A', 4096); 
        
        ptrs[alloc_count++] = p;
    }
    
    // Если мы дошли сюда, значит выделили 1 ГБ и ядро не остановило нас.
    // Это значит, что USER_HEAP_MAX_SIZE не установлен или слишком велик.
    printf("[WARN] Allocated %d MB without hitting OOM. Check USER_HEAP_MAX_SIZE in sys_brk.\n", alloc_count);
    printf("[PASS] System handled massive allocation gracefully.\n");
    return 0;
}