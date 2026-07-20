//paging.c

#include "paging.h"
#include "pmm.h"
#include "klib.h"
#include "framebuffer.h"
#include "serial.h"
#include "isr.h"
#include "config.h"
#include "vma.h"
#include "task.h"

extern uint32_t boot_page_directory[];
extern uint32_t boot_page_tables[];
extern uint8_t boot_page_tables_hh[];
extern uint8_t fb_page_table[];
extern uint8_t boot_stack[];
extern uint8_t boot_stack_top[];
extern uint8_t _kernel_start[];
extern uint8_t _kernel_end[];

// ============================================================================
// [ДЕНЬ 16] KERNEL STACK ALLOCATOR (Hardware Guard Page)
// ============================================================================
#define KERNEL_STACK_SLOTS (KERNEL_STACK_POOL_SIZE / (KERNEL_STACK_SLOT_PAGES * 4096))
static uint8_t kstack_bitmap[KERNEL_STACK_SLOTS / 8 + 1];

uint32_t vmm_alloc_kernel_stack(void) {
    uint32_t eflags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(eflags));
    
    for (int i = 0; i < KERNEL_STACK_SLOTS; i++) {
        if (!(kstack_bitmap[i / 8] & (1 << (i % 8)))) {
            kstack_bitmap[i / 8] |= (1 << (i % 8));
            __asm__ volatile("push %0; popf" : : "r"(eflags));
            
            uint32_t base_virt = KERNEL_STACK_POOL_START + i * (KERNEL_STACK_SLOT_PAGES * 4096);
            
            // Page 0 is Guard (unmapped). Ensure it's clear.
            uint32_t* pd = (uint32_t*)PHYS_TO_VIRT((uint32_t)boot_page_directory);
            vmm_unmap_page_in_pd(pd, base_virt);
            
            // Map Pages 1-4 (Data)
            for (int p = 1; p <= 4; p++) {
                uint32_t phys = pmm_alloc_page();
                if (phys == 0) {
                    // Rollback on OOM
                    for (int q = 1; q < p; q++) {
                        uint32_t v = base_virt + q * 4096;
                        uint32_t dir_index = v >> 22;
                        uint32_t table_index = (v >> 12) & 0x3FF;
                        uint32_t pt_phys = pd[dir_index] & 0xFFFFF000;
                        uint32_t* pt = (uint32_t*)PHYS_TO_VIRT(pt_phys);
                        if (pt[table_index] & PAGE_PRESENT) {
                            pmm_free_page(pt[table_index] & 0xFFFFF000);
                            pt[table_index] = 0;
                            __asm__ volatile("invlpg (%0)" : : "r"(v) : "memory");
                        }
                    }
                    __asm__ volatile("pushf; pop %0; cli" : "=r"(eflags));
                    kstack_bitmap[i / 8] &= ~(1 << (i % 8));
                    __asm__ volatile("push %0; popf" : : "r"(eflags));
                    return 0;
                }
                // Map as Kernel Only (NO PAGE_USER)
                vmm_map_page(base_virt + p * 4096, phys, PAGE_PRESENT | PAGE_WRITE);
            }
            
            return base_virt + (KERNEL_STACK_SLOT_PAGES * 4096); // Return TOP of stack
        }
    }
    __asm__ volatile("push %0; popf" : : "r"(eflags));
    return 0;
}

void vmm_free_kernel_stack(uint32_t stack_top) {
    if (stack_top == 0) return;
    
    uint32_t base_virt = stack_top - (KERNEL_STACK_SLOT_PAGES * 4096);
    int i = (base_virt - KERNEL_STACK_POOL_START) / (KERNEL_STACK_SLOT_PAGES * 4096);
    
    if (i < 0 || i >= KERNEL_STACK_SLOTS) return;
    
    uint32_t eflags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(eflags));
    kstack_bitmap[i / 8] &= ~(1 << (i % 8));
    __asm__ volatile("push %0; popf" : : "r"(eflags));
    
    uint32_t* pd = (uint32_t*)PHYS_TO_VIRT((uint32_t)boot_page_directory);
    for (int p = 1; p <= 4; p++) {
        uint32_t v = base_virt + p * 4096;
        uint32_t dir_index = v >> 22;
        uint32_t table_index = (v >> 12) & 0x3FF;
        
        if (pd[dir_index] & PAGE_PRESENT) {
            uint32_t pt_phys = pd[dir_index] & 0xFFFFF000;
            uint32_t* pt = (uint32_t*)PHYS_TO_VIRT(pt_phys);
            if (pt[table_index] & PAGE_PRESENT) {
                pmm_free_page(pt[table_index] & 0xFFFFF000);
                pt[table_index] = 0;
                __asm__ volatile("invlpg (%0)" : : "r"(v) : "memory");
            }
        }
    }
}

// ============================================================================
// IRQ SAFETY HELPERS (Для защиты критических секций в Page Fault Handler)
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
// [V1] SAFE USER WRITE FAULT RESOLVER (CoW + soft read-only)
// ============================================================================
// Возвращает:
//   PF_RESOLVED     - fault успешно обработан, можно возвращаться в процесс
//   PF_NOT_HANDLED  - это не present write fault / нужна другая обработка
//   PF_ERR_NO_VMA   - адрес не покрыт VMA
//   PF_ERR_PERM     - нарушение прав доступа / W^X
//   PF_ERR_OOM      - не хватило физической памяти для CoW copy
//   PF_ERR_BADPD    - повреждённый / недопустимый page directory
// ============================================================================
#define PF_RESOLVED      1
#define PF_NOT_HANDLED   0
#define PF_ERR_NO_VMA   -1
#define PF_ERR_PERM     -2
#define PF_ERR_OOM      -3
#define PF_ERR_BADPD    -4

static int vmm_handle_user_write_fault(task_t* task, uint32_t addr) {
    if (!task || !task->pdir_virt) {
        return PF_ERR_BADPD;
    }

    if (addr >= KERNEL_SPACE_START) {
        return PF_ERR_PERM;
    }

    //
    // Запрещаем модифицировать boot_page_directory через этот путь.
    // User processes должны иметь собственный Page Directory.
    //
    if (VIRT_TO_PHYS((uint32_t)task->pdir_virt) ==
        VIRT_TO_PHYS((uint32_t)boot_page_directory)) {
        return PF_ERR_BADPD;
    }

    //
    // Zero Trust: адрес должен принадлежать валидной VMA.
    //
    vma_node_t* vma = vma_find(task, addr);
    if (!vma) {
        return PF_ERR_NO_VMA;
    }

    if (!(vma->flags & VMA_READ)) {
        return PF_ERR_PERM;
    }

    if (!(vma->flags & VMA_WRITE)) {
        return PF_ERR_PERM;
    }

    //
    // W^X enforcement: память не может быть одновременно writable и executable.
    //
    if ((vma->flags & VMA_WRITE) && (vma->flags & VMA_EXEC)) {
        return PF_ERR_PERM;
    }

    uint32_t dir_index   = addr >> 22;
    uint32_t table_index = (addr >> 12) & 0x3FF;

    uint32_t pde = task->pdir_virt[dir_index];

    if (!(pde & PAGE_PRESENT)) {
        return PF_NOT_HANDLED;
    }

    //
    // 4MB pages в user write fault path не поддерживаем.
    //
    if (pde & PAGE_PS) {
        return PF_ERR_BADPD;
    }

    uint32_t pt_phys = pde & 0xFFFFF000;
    if (pt_phys == 0) {
        return PF_ERR_BADPD;
    }

    uint32_t* pt = (uint32_t*)PHYS_TO_VIRT(pt_phys);
    uint32_t pte = pt[table_index];

    if (!(pte & PAGE_PRESENT)) {
        return PF_NOT_HANDLED;
    }

    //
    // User write fault должен происходить только по user-странице.
    //
    if (!(pte & PAGE_USER)) {
        return PF_ERR_PERM;
    }

    uint32_t old_phys = pte & 0xFFFFF000;
    if (old_phys == 0) {
        return PF_ERR_BADPD;
    }

    uint32_t eflags = read_eflags();
    disable_interrupts();

    //
    // Основной случай: Copy-on-Write страница.
    //
    if (pte & PAGE_COW) {
        uint16_t ref = pmm_get_refcount(old_phys);

        //
        // Если владелец остался один, копировать страницу не нужно.
        // Достаточно снять COW и восстановить PAGE_WRITE.
        //
        // ref == 0 теоретически означает corruption accounting,
        // но для безопасности процесса трактуем как last-owner case.
        //
        if (ref <= 1) {
            uint32_t new_flags = (pte & 0xFFF) & ~PAGE_COW;
            new_flags |= PAGE_WRITE;

            pt[table_index] = old_phys | new_flags;
            __asm__ volatile("invlpg (%0)" : : "r"(addr) : "memory");

            load_eflags(eflags);
            return PF_RESOLVED;
        }

        //
        // Страница всё ещё используется несколькими процессами.
        // Создаём приватную копию.
        //
        uint32_t new_phys = pmm_alloc_page();
        if (new_phys == 0) {
            load_eflags(eflags);
            return PF_ERR_OOM;
        }

        k_memcpy((void*)PHYS_TO_VIRT(new_phys),
                 (void*)PHYS_TO_VIRT(old_phys),
                 4096);

        pmm_dec_ref(old_phys);

        uint32_t new_flags = (pte & 0xFFF) & ~PAGE_COW;
        new_flags |= PAGE_WRITE;

        pt[table_index] = new_phys | new_flags;
        __asm__ volatile("invlpg (%0)" : : "r"(addr) : "memory");

        load_eflags(eflags);
        return PF_RESOLVED;
    }

    //
    // Дополнительный случай: страница present, read-only, не CoW.
    // Такое возможно как soft read-only mapping.
    //
    // Если страница приватная (refcount <= 1), можно безопасно
    // восстановить PAGE_WRITE после проверки VMA.
    //
    if (!(pte & PAGE_WRITE)) {
        uint16_t ref = pmm_get_refcount(old_phys);

        if (ref <= 1) {
            pt[table_index] = old_phys | ((pte & 0xFFF) | PAGE_WRITE);
            __asm__ volatile("invlpg (%0)" : : "r"(addr) : "memory");

            load_eflags(eflags);
            return PF_RESOLVED;
        }

        //
        // Shared read-only страница без PAGE_COW — нарушение accounting.
        // Не делаем её writable.
        //
        load_eflags(eflags);
        return PF_ERR_PERM;
    }

    //
    // Страница уже writable. Fault мог произойти из-за stale TLB.
    //
    __asm__ volatile("invlpg (%0)" : : "r"(addr) : "memory");
    load_eflags(eflags);
    return PF_RESOLVED;
}

// ============================================================================
// PAGE FAULT HANDLER (INT 14)
// ============================================================================
// [V1 FIX]
//   - Ring 0 теперь может безопасно писать в user CoW страницы.
//   - Ring 3 CoW write обрабатывается тем же resolver'ом.
//   - Demand paging выполняется только для !present faults.
//   - Present write fault без CoW больше не маскируется под demand paging.
//   - VMA/W^X проверки обязательны для всех user memory faults.
// ============================================================================
void page_fault_handler(struct regs* r) {
    uint32_t faulting_address;
    __asm__ volatile("mov %%cr2, %0" : "=r"(faulting_address));

    int present  = r->err_code & 0x1;
    int rw       = r->err_code & 0x2;
    int us       = r->err_code & 0x4;
    int reserved = r->err_code & 0x8;
    int id       = r->err_code & 0x10;

    //
    // Если current_task отсутствует, это фатальная ошибка ядра.
    //
    if (!current_task) {
        serial_print("\n[PF] === FATAL: Page fault with no current_task ===\n");
        serial_printf("[PF] Address: 0x%x | EIP: 0x%x\n",
                      faulting_address, r->eip);
        while (1) { __asm__ volatile("cli; hlt"); }
    }

    //
    // 1. NULL Pointer Guard.
    //
    if (faulting_address < 0x1000) {
        if (us) {
            serial_printf("[PF] SIGSEGV: NULL Pointer Dereference in PID %d\n",
                          current_task->pid);
            task_kill_current("NULL Pointer Dereference");
            while (1) { __asm__ volatile("cli; hlt"); }
        } else {
            serial_print("\n[PF] === FATAL: Kernel NULL Pointer Dereference ===\n");
            serial_printf("[PF] EIP: 0x%x\n", r->eip);
            while (1) { __asm__ volatile("cli; hlt"); }
        }
    }

    //
    // 2. Reserved bit corruption.
    //
    if (reserved) {
        if (us) {
            serial_printf("[PF] SIGSEGV: Reserved bit corruption in PID %d (addr 0x%x)\n",
                          current_task->pid, faulting_address);
            task_kill_current("Reserved bit corruption");
            while (1) { __asm__ volatile("cli; hlt"); }
        } else {
            serial_print("\n[PF] === FATAL: Reserved bit corruption in kernel ===\n");
            serial_printf("[PF] Address: 0x%x | EIP: 0x%x\n",
                          faulting_address, r->eip);
            while (1) { __asm__ volatile("cli; hlt"); }
        }
    }

    //
    // 3. Address space sanity.
    //
    if (!current_task->pdir_virt) {
        if (us) {
            serial_printf("[PF] SIGSEGV: Invalid address space in PID %d\n",
                          current_task->pid);
            task_kill_current("Invalid address space");
            while (1) { __asm__ volatile("cli; hlt"); }
        } else {
            serial_print("\n[PF] === FATAL: Kernel fault with NULL pdir_virt ===\n");
            serial_printf("[PF] Address: 0x%x | EIP: 0x%x\n",
                          faulting_address, r->eip);
            while (1) { __asm__ volatile("cli; hlt"); }
        }
    }

    //
    // 4. Kernel Space Protection.
    //    Ring 3 не имеет права трогать kernel space.
    //
    if (faulting_address >= KERNEL_SPACE_START && us) {
        serial_printf("[PF] SIGSEGV: Ring 3 attempted access to Kernel Space (0x%x) in PID %d\n",
                      faulting_address, current_task->pid);
        task_kill_current("Attempted access to Kernel Space");
        while (1) { __asm__ volatile("cli; hlt"); }
    }

    //
    // 5. Ring 0 faults.
    //
    if (!us) {
        //
        // 5.1 Kernel Stack Overflow Guard.
        //
        if (faulting_address >= KERNEL_STACK_POOL_START &&
            faulting_address < KERNEL_STACK_POOL_START + KERNEL_STACK_POOL_SIZE) {
            serial_printf("\n[PF] === FATAL: Kernel Stack Overflow in PID %d ===\n",
                          current_task->pid);
            serial_printf("[PF] Address: 0x%x | EIP: 0x%x\n",
                          faulting_address, r->eip);
            task_kill_current("Kernel Stack Overflow");
            while (1) { __asm__ volatile("cli; hlt"); }
        }

        //
        // 5.2 Kernel Heap Demand Paging.
        //
        if (!present && !reserved &&
            faulting_address >= KERNEL_HEAP_VIRT &&
            faulting_address < KERNEL_HEAP_END) {

            uint32_t phys = pmm_alloc_page();
            if (phys != 0) {
                k_memset((void*)PHYS_TO_VIRT(phys), 0, 4096);
                uint32_t virt_page = faulting_address & 0xFFFFF000;
                vmm_map_page(virt_page, phys, PAGE_PRESENT | PAGE_WRITE);
                return;
            } else {
                serial_print("\n[PF] === FATAL: Kernel OOM ===\n");
                while (1) { __asm__ volatile("cli; hlt"); }
            }
        }

        //
        // 5.3 Ring 0 -> User Space faults.
        //
        if (faulting_address < KERNEL_SPACE_START) {

            //
            // Ring 0 не должен работать с user memory, если текущая задача
            // использует boot_page_directory.
            //
            if (VIRT_TO_PHYS((uint32_t)current_task->pdir_virt) ==
                VIRT_TO_PHYS((uint32_t)boot_page_directory)) {
                serial_printf("[PF] SIGSEGV: Ring 0 user access with boot PD in PID %d (addr 0x%x)\n",
                              current_task->pid, faulting_address);
                task_kill_current("Ring 0 user access with boot PD");
                while (1) { __asm__ volatile("cli; hlt"); }
            }

            //
            // 5.3.1 Present write fault.
            //       Это основной путь для Ring 0 CoW write.
            //
            if (present && rw) {
                int rc = vmm_handle_user_write_fault(current_task, faulting_address);

                if (rc == PF_RESOLVED) {
                    return;
                }

                if (rc == PF_ERR_NO_VMA) {
                    serial_printf("[PF] SIGSEGV: Kernel write to unmapped user memory (0x%x) in PID %d\n",
                                  faulting_address, current_task->pid);
                    task_kill_current("Kernel write to unmapped user memory");
                } else if (rc == PF_ERR_OOM) {
                    serial_printf("[PF] OOM Kill: CoW failed in PID %d\n",
                                  current_task->pid);
                    task_kill_current("Out of Memory (CoW Failed)");
                } else if (rc == PF_ERR_PERM) {
                    serial_printf("[PF] SIGSEGV: Kernel write permission violation (0x%x) in PID %d\n",
                                  faulting_address, current_task->pid);
                    task_kill_current("Kernel write permission violation");
                } else {
                    serial_printf("[PF] SIGSEGV: Kernel write to invalid user page (0x%x) in PID %d\n",
                                  faulting_address, current_task->pid);
                    task_kill_current("Kernel write to invalid user page");
                }

                while (1) { __asm__ volatile("cli; hlt"); }
            }

            //
            // 5.3.2 Not-present fault.
            //       Ring 0 demand paging для stack forging / copy-to-user.
            //
            if (!present) {
                vma_node_t* vma = vma_find(current_task, faulting_address);

                if (!vma) {
                    serial_printf("[PF] SIGSEGV: Kernel accessed unmapped user memory (0x%x) in PID %d\n",
                                  faulting_address, current_task->pid);
                    task_kill_current("Kernel accessed unmapped user memory");
                    while (1) { __asm__ volatile("cli; hlt"); }
                }

                if (!(vma->flags & VMA_READ)) {
                    serial_printf("[PF] SIGSEGV: Kernel read from non-readable VMA (0x%x) in PID %d\n",
                                  faulting_address, current_task->pid);
                    task_kill_current("Kernel read from non-readable VMA");
                    while (1) { __asm__ volatile("cli; hlt"); }
                }

                if (rw && !(vma->flags & VMA_WRITE)) {
                    serial_printf("[PF] SIGSEGV: Kernel write to read-only user memory (0x%x) in PID %d\n",
                                  faulting_address, current_task->pid);
                    task_kill_current("Kernel write to read-only user memory");
                    while (1) { __asm__ volatile("cli; hlt"); }
                }

                if ((vma->flags & VMA_WRITE) && (vma->flags & VMA_EXEC)) {
                    serial_printf("[PF] SIGSEGV: W^X violation in kernel user access (0x%x) in PID %d\n",
                                  faulting_address, current_task->pid);
                    task_kill_current("W^X violation in kernel user access");
                    while (1) { __asm__ volatile("cli; hlt"); }
                }

                uint32_t phys = pmm_alloc_page();
                if (phys == 0) {
                    serial_printf("[PF] OOM Kill: Physical memory exhausted in PID %d\n",
                                  current_task->pid);
                    task_kill_current("Out of Memory (OOM Kill)");
                    while (1) { __asm__ volatile("cli; hlt"); }
                }

                k_memset((void*)PHYS_TO_VIRT(phys), 0, 4096);

                uint32_t virt_page = faulting_address & 0xFFFFF000;
                uint32_t flags = PAGE_PRESENT | PAGE_USER;

                if (vma->flags & VMA_WRITE) {
                    flags |= PAGE_WRITE;
                }

                if (vmm_map_page_in_pd(current_task->pdir_virt, virt_page, phys, flags) != 0) {
                    pmm_free_page(phys);
                    serial_printf("[PF] OOM Kill: VMM PT alloc failed in PID %d\n",
                                  current_task->pid);
                    task_kill_current("Out of Memory (VMM PT Alloc Failed)");
                    while (1) { __asm__ volatile("cli; hlt"); }
                }

                return;
            }

            //
            // 5.3.3 Present read fault или другая нестандартная ситуация.
            //
            serial_printf("[PF] SIGSEGV: Kernel access to protected user memory (0x%x) in PID %d\n",
                          faulting_address, current_task->pid);
            task_kill_current("Kernel access to protected user memory");
            while (1) { __asm__ volatile("cli; hlt"); }
        }

        //
        // 5.4 Все остальные Ring 0 faults — фатальны.
        //
        serial_print("\n[PF] === FATAL PAGE FAULT ===\n");
        serial_printf("[PF] Address: 0x%x | EIP: 0x%x\n",
                      faulting_address, r->eip);
        serial_printf("[PF] Code: P:%d W:%d U:%d R:%d I:%d\n",
                      present, rw, us, reserved, id);
        serial_print("[PF] System Halted.\n");
        while (1) { __asm__ volatile("cli; hlt"); }
    }

    //
    // 6. Ring 3 faults.
    //

    //
    // Ring 3 процесс не должен иметь boot_page_directory как свой PD.
    //
    if (VIRT_TO_PHYS((uint32_t)current_task->pdir_virt) ==
        VIRT_TO_PHYS((uint32_t)boot_page_directory)) {
        serial_printf("[PF] SIGSEGV: Ring 3 with boot address space in PID %d\n",
                      current_task->pid);
        task_kill_current("Ring 3 with boot address space");
        while (1) { __asm__ volatile("cli; hlt"); }
    }

    //
    // 6.1 Present write fault.
    //     Основной путь для Ring 3 CoW write.
    //
    if (present && rw) {
        int rc = vmm_handle_user_write_fault(current_task, faulting_address);

        if (rc == PF_RESOLVED) {
            return;
        }

        if (rc == PF_ERR_NO_VMA) {
            serial_printf("[PF] SIGSEGV: Write to unmapped memory (0x%x) in PID %d\n",
                          faulting_address, current_task->pid);
            task_kill_current("Write to unmapped memory (No VMA)");
        } else if (rc == PF_ERR_OOM) {
            serial_printf("[PF] OOM Kill: CoW failed in PID %d\n",
                          current_task->pid);
            task_kill_current("Out of Memory (CoW Failed)");
        } else {
            serial_printf("[PF] SIGSEGV: Write permission violation (0x%x) in PID %d\n",
                          faulting_address, current_task->pid);
            task_kill_current("Write permission violation (W^X)");
        }

        while (1) { __asm__ volatile("cli; hlt"); }
    }

    //
    // 6.2 Not-present fault.
    //     User space demand paging.
    //
    if (!present) {
        vma_node_t* vma = vma_find(current_task, faulting_address);

        if (!vma) {
            serial_printf("[PF] SIGSEGV: Access to unmapped memory (0x%x) in PID %d\n",
                          faulting_address, current_task->pid);
            task_kill_current("Access to unmapped memory (No VMA)");
            while (1) { __asm__ volatile("cli; hlt"); }
        }

        if (!(vma->flags & VMA_READ)) {
            serial_printf("[PF] SIGSEGV: Access to non-readable VMA (0x%x) in PID %d\n",
                          faulting_address, current_task->pid);
            task_kill_current("Access to non-readable VMA");
            while (1) { __asm__ volatile("cli; hlt"); }
        }

        if (rw && !(vma->flags & VMA_WRITE)) {
            serial_printf("[PF] SIGSEGV: Write to Read-Only memory (0x%x) in PID %d\n",
                          faulting_address, current_task->pid);
            task_kill_current("Write to Read-Only memory (W^X violation)");
            while (1) { __asm__ volatile("cli; hlt"); }
        }

        if ((vma->flags & VMA_WRITE) && (vma->flags & VMA_EXEC)) {
            serial_printf("[PF] SIGSEGV: W^X violation (0x%x) in PID %d\n",
                          faulting_address, current_task->pid);
            task_kill_current("W^X violation");
            while (1) { __asm__ volatile("cli; hlt"); }
        }

        uint32_t phys = pmm_alloc_page();
        if (phys == 0) {
            serial_printf("[PF] OOM Kill: Physical memory exhausted in PID %d\n",
                          current_task->pid);
            task_kill_current("Out of Memory (OOM Kill)");
            while (1) { __asm__ volatile("cli; hlt"); }
        }

        uint32_t flags = PAGE_PRESENT | PAGE_USER;
        if (vma->flags & VMA_WRITE) {
            flags |= PAGE_WRITE;
        }

        k_memset((void*)PHYS_TO_VIRT(phys), 0, 4096);

        uint32_t virt_page = faulting_address & 0xFFFFF000;

        if (vmm_map_page_in_pd(current_task->pdir_virt, virt_page, phys, flags) != 0) {
            pmm_free_page(phys);
            serial_printf("[PF] OOM Kill: VMM PT alloc failed in PID %d\n",
                          current_task->pid);
            task_kill_current("Out of Memory (VMM PT Alloc Failed)");
            while (1) { __asm__ volatile("cli; hlt"); }
        }

        return;
    }

    //
    // 6.3 Present read fault или другая нестандартная ситуация.
    //
    serial_printf("[PF] SIGSEGV: Access to protected memory (0x%x) in PID %d\n",
                  faulting_address, current_task->pid);
    task_kill_current("Access to protected memory");
    while (1) { __asm__ volatile("cli; hlt"); }
}
// ============================================================================
// INITIALIZATION
// ============================================================================
void paging_init(void) {
    serial_print("[VMM] Reserving kernel structures in PMM...\n");
    pmm_reserve_region(0x00100000, 0x01000000);
    
    pmm_reserve_region(VIRT_TO_PHYS((uint32_t)boot_page_directory), 
                       VIRT_TO_PHYS((uint32_t)boot_page_directory) + 4096);
    pmm_reserve_region(VIRT_TO_PHYS((uint32_t)boot_page_tables), 
                       VIRT_TO_PHYS((uint32_t)boot_page_tables) + (128 * 4096));
    pmm_reserve_region(VIRT_TO_PHYS((uint32_t)boot_page_tables_hh), 
                       VIRT_TO_PHYS((uint32_t)boot_page_tables_hh) + (128 * 4096));
    pmm_reserve_region(VIRT_TO_PHYS((uint32_t)fb_page_table), 
                       VIRT_TO_PHYS((uint32_t)fb_page_table) + 4096);
    pmm_reserve_region(VIRT_TO_PHYS((uint32_t)boot_stack), 
                       VIRT_TO_PHYS((uint32_t)boot_stack_top));

    serial_print("[VMM] Building Direct Map (512MB)...\n");
    uint32_t total_ram_pages = (512 * 1024 * 1024) / 4096; 
    for (uint32_t i = 0; i < total_ram_pages; i++) {
        uint32_t phys = i * 4096;
        uint32_t virt = phys + KERNEL_SPACE_START;
        vmm_map_page(virt, phys, PAGE_PRESENT | PAGE_WRITE);
    }
    
    serial_print("[VMM] Mapping Framebuffer...\n");
    uint32_t fb_pages = (FB_SIZE_MB * 1024 * 1024) / 4096; 
    for (uint32_t i = 0; i < fb_pages; i++) { 
        vmm_map_page(FB_VIRT_BASE + i * 4096, FB_PHYS_BASE + i * 4096, 
                     PAGE_PRESENT | PAGE_WRITE | PAGE_PCD | PAGE_USER);
    }
    
    isr_register_handler(14, page_fault_handler);
    serial_print("[VMM] Page Fault Handler registered (INT 14).\n");
    
    uint32_t pd_phys = VIRT_TO_PHYS((uint32_t)boot_page_directory);
    __asm__ volatile("mov %0, %%cr3" : : "r"(pd_phys));
    serial_print("[VMM] CR3 reloaded. Paging active.\n");
}

// ============================================================================
// KERNEL SPACE MAPPING (boot_page_directory)
// ============================================================================
void vmm_map_page(uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t dir_index = virt >> 22;
    uint32_t table_index = (virt >> 12) & 0x3FF;

    // ✅ FIX: Гарантированно используем виртуальный адрес Higher Half
    uint32_t* virt_boot_pd = (uint32_t*)PHYS_TO_VIRT((uint32_t)boot_page_directory);
    uint32_t pde = virt_boot_pd[dir_index];

    if (!(pde & PAGE_PRESENT)) {
        uint32_t new_pt_phys = pmm_alloc_page();
        if (new_pt_phys == 0) {
            serial_print("[VMM] FATAL OOM in vmm_map_page (Kernel)!\n");
            while(1) __asm__("cli; hlt");
        }
        k_memset((void*)PHYS_TO_VIRT(new_pt_phys), 0, 4096); 
        virt_boot_pd[dir_index] = new_pt_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }

    uint32_t pt_phys = virt_boot_pd[dir_index] & 0xFFFFF000; 
    uint32_t* pt = (uint32_t*)PHYS_TO_VIRT(pt_phys); 
    pt[table_index] = phys | flags;

    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

// ============================================================================
// USER SPACE MAPPING (Custom Page Directory)
// ============================================================================
int vmm_map_page_in_pd(uint32_t* pd_virt, uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t dir_index = virt >> 22;
    uint32_t table_index = (virt >> 12) & 0x3FF;

    uint32_t pde = pd_virt[dir_index];

    if (pde & PAGE_PS) {
        serial_print("[VMM] FATAL: 4MB Page detected in PDE!\n");
        while(1) __asm__("cli; hlt"); 
    }

    if (!(pde & PAGE_PRESENT)) {
        uint32_t new_pt_phys = pmm_alloc_page();
        if (new_pt_phys == 0) {
            serial_print("[VMM] OOM in vmm_map_page_in_pd (PT alloc failed)!\n");
            return -1; // 🛡️ FIX: Возвращаем ошибку, а не делаем silent return
        }
        k_memset((void*)PHYS_TO_VIRT(new_pt_phys), 0, 4096); 
        pd_virt[dir_index] = new_pt_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
        __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    }

    uint32_t pt_phys = pd_virt[dir_index] & 0xFFFFF000; 
    uint32_t* pt = (uint32_t*)PHYS_TO_VIRT(pt_phys); 
    
    // Защита от утечки PMM при пере-маппинге (если PTE уже был занят)
    if (pt[table_index] & PAGE_PRESENT) {
        uint32_t old_phys = pt[table_index] & 0xFFFFF000;
        pmm_free_page(old_phys);
    }

    pt[table_index] = phys | flags;

    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    return 0; // Успех
}

// ============================================================================
// UNMAPPING
// ============================================================================
void vmm_unmap_page_in_pd(uint32_t* pd_virt, uint32_t virt) {
    uint32_t dir_index = virt >> 22;
    uint32_t table_index = (virt >> 12) & 0x3FF;

    uint32_t pde = pd_virt[dir_index];
    if (!(pde & PAGE_PRESENT)) return; 

    uint32_t pt_phys = pde & 0xFFFFF000;
    uint32_t* pt = (uint32_t*)PHYS_TO_VIRT(pt_phys);

    if (pt[table_index] & PAGE_PRESENT) {
        pt[table_index] = 0; 
        __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    }
}

void vmm_unmap_and_free_page_in_pd(uint32_t* pd_virt, uint32_t virt) {
    uint32_t dir_index = virt >> 22;
    uint32_t table_index = (virt >> 12) & 0x3FF;

    uint32_t pde = pd_virt[dir_index];
    if (!(pde & PAGE_PRESENT)) return; 

    uint32_t pt_phys = pde & 0xFFFFF000;
    uint32_t* pt = (uint32_t*)PHYS_TO_VIRT(pt_phys);

    if (pt[table_index] & PAGE_PRESENT) {
        uint32_t phys = pt[table_index] & 0xFFFFF000;
        pmm_free_page(phys); // ✅ Возвращаем страницу в PMM!
        pt[table_index] = 0; 
        __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    }
}

void vmm_protect_page_in_pd(uint32_t* pd_virt, uint32_t virt, uint32_t flags) {
    uint32_t dir_index = virt >> 22;
    uint32_t table_index = (virt >> 12) & 0x3FF;

    uint32_t pde = pd_virt[dir_index];
    if (!(pde & PAGE_PRESENT)) return; 

    uint32_t pt_phys = pde & 0xFFFFF000;
    uint32_t* pt = (uint32_t*)PHYS_TO_VIRT(pt_phys);

    if (pt[table_index] & PAGE_PRESENT) {
        uint32_t phys = pt[table_index] & 0xFFFFF000;
        // Сохраняем физический адрес, меняем только флаги
        pt[table_index] = phys | flags; 
        __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    }
}

uint32_t* vmm_create_address_space(void) {
    uint32_t phys_pd = pmm_alloc_page();
    if (phys_pd == 0) return 0;
    
    uint32_t* virt_pd = (uint32_t*)PHYS_TO_VIRT(phys_pd);
    k_memset(virt_pd, 0, 4096);
    
    // ✅ FIX: boot_page_directory может иметь физический адрес в линкере.
    // Обязательно приводим его к виртуальному адресу Higher Half перед чтением.
    uint32_t* virt_boot_pd = (uint32_t*)PHYS_TO_VIRT((uint32_t)boot_page_directory);
    
    // Clone Kernel Space (768-1023)
    for (int i = 768; i < 1024; i++) {
        virt_pd[i] = virt_boot_pd[i];
    }
    
    return virt_pd;
}

void vmm_switch_pdir(uint32_t phys_pd) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(phys_pd) : "memory");
}

// ============================================================================
// [ДЕНЬ 14] ADDRESS SPACE CLONING (Copy-on-Write Implementation)
// ============================================================================
uint32_t* vmm_clone_address_space(uint32_t* parent_pd_virt) {
    if (!parent_pd_virt) return NULL;
    
    uint32_t phys_pd = pmm_alloc_page();
    if (phys_pd == 0) return NULL;
    
    uint32_t* child_pd_virt = (uint32_t*)PHYS_TO_VIRT(phys_pd);
    k_memset(child_pd_virt, 0, 4096);
    
    // ✅ FIX: boot_page_directory может иметь физический адрес в линкере.
    // Обязательно приводим его к виртуальному адресу Higher Half перед чтением.
    uint32_t* virt_boot_pd = (uint32_t*)PHYS_TO_VIRT((uint32_t)boot_page_directory);
    
    // Клонируем Kernel Space (768-1023) из глобального boot_page_directory
    for (int i = 768; i < 1024; i++) {
        child_pd_virt[i] = virt_boot_pd[i];
    }
    
    // Клонируем User Space (0-767) с Copy-on-Write из parent_pd_virt
    for (uint32_t i = 0; i < 768; i++) {
        uint32_t pde = parent_pd_virt[i];
        if (!(pde & PAGE_PRESENT)) continue;
        
        if (pde & PAGE_PS) {
            // 4MB страница — НЕ поддерживаем CoW для 4MB страниц (редкость в User Space)
            // Просто копируем PDE как есть (без увеличения refcount для простоты)
            child_pd_virt[i] = pde;
            // Увеличиваем refcount для ВСЕХ 1024 4KB страниц внутри 4MB региона
            uint32_t phys = pde & 0xFFC00000;
            for (uint32_t p = 0; p < 1024; p++) {
                pmm_inc_ref(phys + (p * 4096));
            }
        } else {
            // 4KB страницы — создаем новую Page Table для ребенка
            uint32_t pt_phys = pmm_alloc_page();
            if (pt_phys == 0) {
                vmm_destroy_address_space(child_pd_virt);
                return NULL;
            }
            
            uint32_t parent_pt_phys = pde & 0xFFFFF000;
            uint32_t* parent_pt = (uint32_t*)PHYS_TO_VIRT(parent_pt_phys);
            uint32_t* child_pt = (uint32_t*)PHYS_TO_VIRT(pt_phys);
            
            k_memset(child_pt, 0, 4096);
            
            // Копируем все PTE с CoW marking
            for (uint32_t j = 0; j < 1024; j++) {
                uint32_t pte = parent_pt[j]; 
                if (!(pte & PAGE_PRESENT)) continue;
                
                uint32_t phys = pte & 0xFFFFF000;
                pmm_inc_ref(phys); // Увеличиваем refcount
                
                // Снимаем PAGE_WRITE и добавляем PAGE_COW
                uint32_t new_flags = (pte & 0xFFF) & ~PAGE_WRITE;
                new_flags |= PAGE_COW;
                
                child_pt[j] = phys | new_flags;
                parent_pt[j] = phys | new_flags; // Родитель тоже становится CoW
            }
            
            child_pd_virt[i] = pt_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
        }
    }
    
    // Инвалидация TLB для родителя (он теперь CoW)
    uint32_t parent_pd_phys = VIRT_TO_PHYS((uint32_t)parent_pd_virt);
    __asm__ volatile("mov %0, %%cr3" : : "r"(parent_pd_phys) : "memory");
    
    return child_pd_virt;
} // ✅ Закрывающая скобка функции

void vmm_destroy_address_space(uint32_t* pdir_virt) {
    if (!pdir_virt) return;
    
    uint32_t* virt_boot_pd = (uint32_t*)PHYS_TO_VIRT((uint32_t)boot_page_directory);
    if (pdir_virt == virt_boot_pd) return;
    
    for (uint32_t i = 0; i < 768; i++) {
        if (pdir_virt[i] & PAGE_PRESENT) {
            if (pdir_virt[i] & PAGE_PS) {
                uint32_t page_phys = pdir_virt[i] & 0xFFC00000;
                for(uint32_t p = 0; p < 1024; p++) {
                    pmm_dec_ref(page_phys + (p * 4096));
                }
            } else {
                uint32_t pt_phys = pdir_virt[i] & 0xFFFFF000;
                uint32_t* pt_virt = (uint32_t*)PHYS_TO_VIRT(pt_phys);
                
                for (uint32_t j = 0; j < 1024; j++) {
                    if (pt_virt[j] & PAGE_PRESENT) {
                        uint32_t page_phys = pt_virt[j] & 0xFFFFF000;
                        
                        // 🛡️ FIX: pmm_dec_ref() ВНУТРИ СЕБЯ вызывает pmm_free_page(), 
                        // когда refcount достигает 0. Повторный вызов здесь вызывал Double Free!
                        pmm_dec_ref(page_phys);
                    }
                }
                pmm_dec_ref(pt_phys); // Free Page Table (тоже через dec_ref для консистентности)
            }
            pdir_virt[i] = 0; // Prevent Double Free
        }
    }
    
    uint32_t pdir_phys = VIRT_TO_PHYS((uint32_t)pdir_virt);
    pmm_free_page(pdir_phys); // Free Page Directory
}
