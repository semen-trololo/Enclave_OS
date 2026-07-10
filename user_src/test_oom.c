// user_src/test_oom.c
#include "user_syscalls.h"

void _start() {
    // 1. Получаем текущую границу кучи
    uint32_t current_brk = sys_brk(0);
    
    // 2. Пытаемся откусить 100 МБ (больше, чем лимит USER_HEAP_MAX_SIZE / 2 для OOM protection)
    uint32_t target_brk = current_brk + (100 * 1024 * 1024);
    int res = sys_brk(target_brk);
    
    if (res == 0) {
        // Если ядро по какой-то ошибке разрешило, пытаемся затронуть страницы,
        // чтобы триггерить Page Fault и реактивный OOM Trap.
        volatile char* ptr = (char*)current_brk;
        for (uint32_t i = 0; i < (100 * 1024 * 1024); i += 4096) {
            ptr[i] = 'A';
        }
    }
    
    // Если дошли сюда, OOM protection не сработала!
    sys_exit(1);
}
