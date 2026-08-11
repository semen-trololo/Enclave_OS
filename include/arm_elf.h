#ifndef ARM_ELF_H
#define ARM_ELF_H

// ============================================================================
// ARM ELF Loader (Day 54: Task 2.4)
// ============================================================================
// Zero Trust ELF parser and loader for ARM user processes.
//
// Architecture:
//   - Validate ELF header (magic, class, machine, type)
//   - Parse PT_LOAD program headers
//   - Enforce W^X at segment level
//   - Map segments with correct HAL_PAGE_* flags
//   - Allocate and map user stack
//   - Return entry point from ELF header
// ============================================================================

#include <stdint.h>
#include <stddef.h>

// ============================================================================
// ELF HEADER CONSTANTS
// ============================================================================

// ELF magic number
#define ELFMAG0         0x7f
#define ELFMAG1         'E'
#define ELFMAG2         'L'
#define ELFMAG3         'F'
#define ELFMAG          "\177ELF"
#define SELFMAG         4

// ELF class (32-bit)
#define ELFCLASS32      1

// ELF data encoding (little-endian)
#define ELFDATA2LSB     1

// ELF version
#define EV_CURRENT      1

// ELF type (executable)
#define ET_EXEC         2

// ELF machine (ARM)
#define EM_ARM          40

// Program header type
#define PT_NULL         0
#define PT_LOAD         1
#define PT_DYNAMIC      2
#define PT_INTERP       3
#define PT_NOTE         4
#define PT_SHLIB        5
#define PT_PHDR         6

// Program header flags
#define PF_X            0x1     // Execute
#define PF_W            0x2     // Write
#define PF_R            0x4     // Read

// ============================================================================
// ELF STRUCTURES (32-bit, little-endian)
// ============================================================================

// ELF file header
typedef struct {
    uint8_t     e_ident[16];    // ELF identification
    uint16_t    e_type;         // Object file type
    uint16_t    e_machine;      // Architecture
    uint32_t    e_version;      // Object file version
    uint32_t    e_entry;        // Entry point virtual address
    uint32_t    e_phoff;        // Program header table file offset
    uint32_t    e_shoff;        // Section header table file offset
    uint32_t    e_flags;        // Processor-specific flags
    uint16_t    e_ehsize;       // ELF header size in bytes
    uint16_t    e_phentsize;    // Program header table entry size
    uint16_t    e_phnum;        // Program header table entry count
    uint16_t    e_shentsize;    // Section header table entry size
    uint16_t    e_shnum;        // Section header table entry count
    uint16_t    e_shstrndx;     // Section header string table index
} Elf32_Ehdr;

// Program header
typedef struct {
    uint32_t    p_type;         // Segment type
    uint32_t    p_offset;       // Segment file offset
    uint32_t    p_vaddr;        // Segment virtual address
    uint32_t    p_paddr;        // Segment physical address
    uint32_t    p_filesz;       // Segment size in file
    uint32_t    p_memsz;        // Segment size in memory
    uint32_t    p_flags;        // Segment flags
    uint32_t    p_align;        // Segment alignment
} Elf32_Phdr;

// ============================================================================
// LOADER RESULT STRUCTURE
// ============================================================================

typedef struct {
    uint32_t    ttbr0_phys;     // Physical address of L1 table (for TTBR0)
    uint32_t    entry_point;    // Virtual address from ELF e_entry
    uint32_t    user_sp;        // Virtual address of user stack top
} arm_elf_load_result_t;

// ============================================================================
// ELF LOADER API
// ============================================================================

// Load ARM ELF binary into isolated address space.
//
// Parameters:
//   elf_image   - pointer to ELF file in kernel memory
//   elf_size    - size of ELF file in bytes
//   result      - output structure (ttbr0_phys, entry_point, user_sp)
//
// Returns:
//   0 on success
//   -1 on validation error (invalid ELF, W^X violation, OOM, etc.)
//
// Zero Trust: All ELF data is validated before use.
// W^X: Segments with PF_W|PF_X are rejected.
// User space: Segments must have p_vaddr < 0xC0000000.
// ============================================================================

int arm_elf_load(const uint8_t *elf_image, size_t elf_size,
                 arm_elf_load_result_t *result);

#endif // ARM_ELF_H
