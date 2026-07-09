// include/elf.h
#ifndef ELF_H
#define ELF_H

#include <stdint.h>
#include "vfs.h"

// ============================================================================
// ELF MAGIC NUMBER
// ============================================================================
#define ELF_MAGIC 0x464C457F // 0x7F 'E' 'L' 'F' в little-endian

// ============================================================================
// ELF TYPES
// ============================================================================
#define ET_NONE 0      // No file type
#define ET_REL  1      // Relocatable file
#define ET_EXEC 2      // Executable file
#define ET_DYN  3      // Shared object file
#define ET_CORE 4      // Core file

// ============================================================================
// ELF MACHINE TYPES
// ============================================================================
#define EM_NONE  0     // No machine
#define EM_386   3     // Intel 80386
#define EM_ARM   40    // ARM
#define EM_X86_64 62   // AMD x86-64

// ============================================================================
// PROGRAM HEADER TYPES
// ============================================================================
#define PT_NULL    0   // Unused entry
#define PT_LOAD    1   // Loadable segment
#define PT_DYNAMIC 2   // Dynamic linking info
#define PT_INTERP  3   // Path to program interpreter
#define PT_NOTE    4   // Auxiliary info
#define PT_SHLIB   5   // Reserved
#define PT_PHDR    6   // Program header table itself

// ============================================================================
// PROGRAM HEADER FLAGS
// ============================================================================
#define PF_X 0x1       // Execute
#define PF_W 0x2       // Write
#define PF_R 0x4       // Read

// ============================================================================
// ELF32 HEADER (52 байта)
// ============================================================================
typedef struct {
    uint8_t  e_ident[16];    // Magic number and other info
    uint16_t e_type;         // Type of file (ET_EXEC)
    uint16_t e_machine;      // Architecture (EM_386)
    uint32_t e_version;      // Object file version
    uint32_t e_entry;        // Entry point virtual address
    uint32_t e_phoff;        // Program header table file offset
    uint32_t e_shoff;        // Section header table file offset (не используем)
    uint32_t e_flags;        // Processor-specific flags
    uint16_t e_ehsize;       // ELF header size in bytes
    uint16_t e_phentsize;    // Program header table entry size
    uint16_t e_phnum;        // Program header table entry count
    uint16_t e_shentsize;    // Section header table entry size
    uint16_t e_shnum;        // Section header table entry count
    uint16_t e_shstrndx;     // Section header string table index
} __attribute__((packed)) elf32_header_t;

// ============================================================================
// ELF32 PROGRAM HEADER (32 байта)
// ============================================================================
typedef struct {
    uint32_t p_type;         // Type of segment (PT_LOAD)
    uint32_t p_offset;       // File offset of segment
    uint32_t p_vaddr;        // Virtual address in memory
    uint32_t p_paddr;        // Physical address (не используем)
    uint32_t p_filesz;       // Size of segment in file
    uint32_t p_memsz;        // Size of segment in memory
    uint32_t p_flags;        // Segment flags (PF_R, PF_W, PF_X)
    uint32_t p_align;        // Alignment of segment
} __attribute__((packed)) elf32_phdr_t;

// ============================================================================
// ELF LOADER API
// ============================================================================

// Загрузить ELF-файл в адресное пространство процесса.
// Возвращает точку входа (e_entry) или 0 при ошибке.
uint32_t elf_load(vfs_node_t* file_node, uint32_t* pdir_virt);

#endif // ELF_H
