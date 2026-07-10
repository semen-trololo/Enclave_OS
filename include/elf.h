#ifndef ELF_H
#define ELF_H

#include <stdint.h>
#include "vfs.h"
#include "task.h"

// ELF Magic Number
#define ELF_MAGIC 0x464C457F  // "\x7FELF" in little-endian

// ELF Types
#define ET_EXEC 2

// ELF Machine
#define EM_386 3

// Program Header Types
#define PT_LOAD 1

// Program Header Flags
#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

// ELF Header (32-bit)
typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} elf32_header_t;

// Program Header (32-bit)
typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} elf32_phdr_t;

// ✅ ИСПРАВЛЕНО: Теперь принимает task_t* для создания VMA
uint32_t elf_load(vfs_node_t* file_node, task_t* task);

#endif // ELF_H
