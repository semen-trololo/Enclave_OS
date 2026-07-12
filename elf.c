#include "elf.h"
#include "vfs.h"
#include "pmm.h"
#include "paging.h"
#include "vma.h"
#include "klib.h"
#include "serial.h"
#include "config.h"

uint32_t elf_load(vfs_node_t* file_node, task_t* task) {
    if (!file_node || !task || !task->pdir_virt) {
        serial_print("[ELF] FATAL: NULL file_node or task\n");
        return 0;
    }

    elf32_header_t elf_header;
    int32_t bytes_read = vfs_read(file_node, 0, sizeof(elf32_header_t), (uint8_t*)&elf_header);

    if (bytes_read != sizeof(elf32_header_t)) {
        serial_printf("[ELF] FATAL: Failed to read ELF header (read %d bytes)\n", bytes_read);
        return 0;
    }

    uint32_t magic = *(uint32_t*)elf_header.e_ident;
    if (magic != ELF_MAGIC) {
        serial_printf("[ELF] FATAL: Invalid ELF magic: 0x%x (expected 0x%x)\n", magic, ELF_MAGIC);
        return 0;
    }

    if (elf_header.e_type != ET_EXEC) {
        serial_printf("[ELF] FATAL: Not an executable file (type: %d)\n", elf_header.e_type);
        return 0;
    }

    if (elf_header.e_machine != EM_386) {
        serial_printf("[ELF] FATAL: Not x86 architecture (machine: %d)\n", elf_header.e_machine);
        return 0;
    }

    serial_printf("[ELF] Valid ELF header: entry=0x%x, phnum=%d\n",
                  elf_header.e_entry, elf_header.e_phnum);

    for (uint16_t i = 0; i < elf_header.e_phnum; i++) {
        elf32_phdr_t phdr;
        uint32_t phdr_offset = elf_header.e_phoff + (i * elf_header.e_phentsize);

        bytes_read = vfs_read(file_node, phdr_offset, sizeof(elf32_phdr_t), (uint8_t*)&phdr);
        if (bytes_read != sizeof(elf32_phdr_t)) {
            serial_printf("[ELF] FATAL: Failed to read Program Header %d\n", i);
            return 0;
        }

        if (phdr.p_type != PT_LOAD) {
            continue;
        }

        serial_printf("[ELF] PT_LOAD segment %d: vaddr=0x%x, filesz=%u, memsz=%u, flags=0x%x\n",
                      i, phdr.p_vaddr, phdr.p_filesz, phdr.p_memsz, phdr.p_flags);

        // Создание VMA для сегмента
        uint32_t vma_flags = 0;
        if (phdr.p_flags & PF_R) vma_flags |= VMA_READ;
        if (phdr.p_flags & PF_W) vma_flags |= VMA_WRITE;
        if (phdr.p_flags & PF_X) vma_flags |= VMA_EXEC;

        if (vma_add(task, phdr.p_vaddr, phdr.p_vaddr + phdr.p_memsz, vma_flags) != 0) {
            serial_print("[ELF] FATAL: OOM adding VMA for segment\n");
            return 0;
        }

        uint32_t start_page = phdr.p_vaddr & 0xFFFFF000;
        uint32_t end_page = (phdr.p_vaddr + phdr.p_memsz + 0xFFF) & 0xFFFFF000;

        for (uint32_t addr = start_page; addr < end_page; addr += 4096) {
            uint32_t phys = pmm_alloc_page();
            if (phys == 0) {
                serial_print("[ELF] FATAL: OOM allocating page for ELF segment\n");
                return 0; 
            }

            k_memset((void*)PHYS_TO_VIRT(phys), 0, 4096);

            uint32_t pte_flags = PAGE_PRESENT | PAGE_USER;
            if (phdr.p_flags & PF_W) pte_flags |= PAGE_WRITE;

            // 🛡️ FIX: Проверяем результат маппинга. 
            // Если VMM не смог выделить Page Table (OOM), мы ОБЯЗАНЫ освободить 
            // выделенную страницу данных, иначе она навсегда потеряется в PMM.
            if (vmm_map_page_in_pd(task->pdir_virt, addr, phys, pte_flags) != 0) {
                pmm_free_page(phys); 
                serial_print("[ELF] FATAL: OOM mapping page for ELF segment\n");
                return 0; 
            }
        }

        if (phdr.p_filesz > 0) {
            uint32_t bytes_to_copy = phdr.p_filesz;
            if (bytes_to_copy > phdr.p_memsz) {
                serial_printf("[ELF] WARNING: filesz > memsz in segment %d. Clamping.\n", i);
                bytes_to_copy = phdr.p_memsz;
            }

            uint32_t offset_in_file = phdr.p_offset;
            uint32_t dest_addr = phdr.p_vaddr;

            while (bytes_to_copy > 0) {
                uint32_t page_offset = dest_addr & 0xFFF;
                uint32_t chunk_size = 4096 - page_offset;
                if (chunk_size > bytes_to_copy) chunk_size = bytes_to_copy;

                uint32_t pde = task->pdir_virt[dest_addr >> 22];
                if (!(pde & PAGE_PRESENT)) {
                    serial_print("[ELF] FATAL: PDE not present during copy\n");
                    return 0;
                }

                uint32_t pt_phys = pde & 0xFFFFF000;
                uint32_t* pt = (uint32_t*)PHYS_TO_VIRT(pt_phys);
                uint32_t pte = pt[(dest_addr >> 12) & 0x3FF];

                if (!(pte & PAGE_PRESENT)) {
                    serial_print("[ELF] FATAL: PTE not present during copy\n");
                    return 0;
                }

                uint32_t page_phys = pte & 0xFFFFF000;
                uint8_t* dest_virt = (uint8_t*)PHYS_TO_VIRT(page_phys) + page_offset;

                int32_t read_bytes = vfs_read(file_node, offset_in_file, chunk_size, dest_virt);
                if (read_bytes != (int32_t)chunk_size) {
                    serial_printf("[ELF] FATAL: Failed to read segment data (read %d, expected %u)\n",
                                  read_bytes, chunk_size);
                    return 0;
                }

                offset_in_file += chunk_size;
                dest_addr += chunk_size;
                bytes_to_copy -= chunk_size;
            }
        }

        serial_printf("[ELF] Segment loaded: 0x%x - 0x%x\n", start_page, end_page);
    }

    serial_printf("[ELF] Load complete. Entry point: 0x%x\n", elf_header.e_entry);
    return elf_header.e_entry;
}