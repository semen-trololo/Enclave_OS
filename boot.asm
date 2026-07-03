; ============================================================================
; BARE METAL OS - BOOT LOADER (Higher Half Trampline)
; ============================================================================

MBOOT_PAGE_ALIGN    equ 1<<0
MBOOT_MEM_INFO      equ 1<<1  
MBOOT_HEADER_MAGIC  equ 0x1BADB002
MBOOT_HEADER_FLAGS  equ MBOOT_PAGE_ALIGN | MBOOT_MEM_INFO 
MBOOT_CHECKSUM      equ -(MBOOT_HEADER_MAGIC + MBOOT_HEADER_FLAGS)

section .multiboot
align 4
    dd MBOOT_HEADER_MAGIC
    dd MBOOT_HEADER_FLAGS
    dd MBOOT_CHECKSUM

; ============================================================================
; BSS СЕКЦИИ
; ============================================================================
section .boot.bss nobits
align 4096
global boot_page_directory
boot_page_directory: resb 4096
boot_page_tables:    resb 128 * 4096
fb_page_table:       resb 4096
boot_stack:          resb 16384
boot_stack_top:

section .bss
align 16
global stack_bottom, stack_top
stack_bottom: resb 262144
stack_top:

; ============================================================================
; DATA СЕКЦИЯ (Глобальные параметры загрузки)
; ============================================================================
section .boot.data
global fb_params
fb_params:
    .address    dd 0
    .width      dd 0
    .height     dd 0
    .pitch      dd 0
    .bpp        dd 0

; НОВЫЕ ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ДЛЯ MULTIBOOT
global multiboot_info_ptr
multiboot_info_ptr: dd 0

global multiboot_magic_val
multiboot_magic_val: dd 0

; ============================================================================
; VBE КОНСТАНТЫ И МАКРОСЫ
; ============================================================================
VBE_DISPI_IOPORT_INDEX  equ 0x01CE
VBE_DISPI_IOPORT_DATA   equ 0x01CF

VBE_DISPI_INDEX_ID          equ 0
VBE_DISPI_INDEX_XRES        equ 1
VBE_DISPI_INDEX_YRES        equ 2
VBE_DISPI_INDEX_BPP         equ 3
VBE_DISPI_INDEX_ENABLE      equ 4
VBE_DISPI_INDEX_VIRT_WIDTH  equ 6
VBE_DISPI_INDEX_VIRT_HEIGHT equ 7
VBE_DISPI_INDEX_X_OFFSET    equ 8
VBE_DISPI_INDEX_Y_OFFSET    equ 9

VBE_DISPI_ENABLED       equ 0x01
VBE_DISPI_LFB_ENABLED   equ 0x40

%macro VBE_WRITE 2
    mov dx, VBE_DISPI_IOPORT_INDEX
    mov ax, %1
    out dx, ax
    mov dx, VBE_DISPI_IOPORT_DATA
    mov ax, %2
    out dx, ax
%endmacro

; ============================================================================
; BOOT SECTION (ФИЗИЧЕСКИЕ АДРЕСА)
; ============================================================================
section .boot
global _start
extern kernel_main
extern init_bochs_vbe

_start:
    mov esp, boot_stack_top
    
    ; КРИТИЧЕСКИЙ ФИКС: Сохраняем Multiboot данные в глобальные переменные СРАЗУ!
    ; Это исключает любые проблемы с передачей аргументов через стек.
    mov [multiboot_magic_val], eax
    mov [multiboot_info_ptr], ebx
    
    push eax
    push ebx
    call init_bochs_vbe
    pop ebx
    pop eax

    ; 1. Очищаем Page Directory
    mov edi, boot_page_directory
    xor eax, eax
    mov ecx, 1024
    rep stosd

    ; 2. Заполняем 128 Page Tables для Direct Map (512 МБ)
    mov edi, boot_page_tables
    mov eax, 0x00000003
    mov ecx, 128 * 1024
.fill_pt:
    mov [edi], eax
    add eax, 4096
    add edi, 4
    loop .fill_pt

    ; 3. Заполняем Page Directory (Identity Map 512 МБ)
    mov edi, boot_page_directory
    mov eax, boot_page_tables
    or eax, 0x00000003
    mov ecx, 128
.fill_pd:
    mov [edi], eax
    add eax, 4096
    add edi, 4
    loop .fill_pd

    ; 4. Higher Half Map (ВСЕ 512 МБ в 0xC0000000 - 0xDFFFFFFF)
    ; Копируем те же таблицы, чтобы ядро могло расти.
    mov edi, boot_page_directory + 768*4
    mov eax, boot_page_tables
    or eax, 0x00000003
    mov ecx, 128
.fill_hh_pd:
    mov [edi], eax
    add eax, 4096
    add edi, 4
    loop .fill_hh_pd

    ; 5. Маппинг Framebuffer (0xFD000000 -> Индекс 1012)
    mov edi, fb_page_table
    mov eax, 0xFD00013
    mov ecx, 768
.fill_fb_pt:
    mov [edi], eax
    add eax, 4096
    add edi, 4
    loop .fill_fb_pt

    mov eax, fb_page_table
    or eax, 0x00000003
    mov [boot_page_directory + 1012*4], eax

    ; 6. Включаем MMU
    mov eax, boot_page_directory
    mov cr3, eax
    mov eax, cr0
    or eax, 0x80000010      ; PG + WP
    mov cr0, eax

    ; 7. ПРЫЖОК В HIGHER HALF
    ; Аргументы больше не передаются через стек! Всё в глобальных переменных.
    mov edi, kernel_main
    jmp edi

.halt_loop:
    cli
    hlt
    jmp .halt_loop
    
; ============================================================================
; init_bochs_vbe
; ============================================================================
section .boot
init_bochs_vbe:
    pusha
    mov dx, 0x01CE
    mov ax, 0
    out dx, ax
    mov dx, 0x01CF
    in  ax, dx
    cmp ax, 0xB0C0
    jb .vbe_fail
    cmp ax, 0xB0C6
    jae .vbe_fail

    VBE_WRITE VBE_DISPI_INDEX_ENABLE, 0
    VBE_WRITE VBE_DISPI_INDEX_XRES, 1024
    VBE_WRITE VBE_DISPI_INDEX_YRES, 768
    VBE_WRITE VBE_DISPI_INDEX_BPP, 32
    
    VBE_WRITE VBE_DISPI_INDEX_VIRT_WIDTH, 1024
    VBE_WRITE VBE_DISPI_INDEX_VIRT_HEIGHT, 768
    VBE_WRITE VBE_DISPI_INDEX_X_OFFSET, 0
    VBE_WRITE VBE_DISPI_INDEX_Y_OFFSET, 0

    VBE_WRITE VBE_DISPI_INDEX_ENABLE, (VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED)

    mov dword [fb_params + 0], 0xFD000000
    mov dword [fb_params + 4], 1024
    mov dword [fb_params + 8], 768
    mov dword [fb_params + 12], (1024 * 4)
    mov dword [fb_params + 16], 32
    popa
    ret

.vbe_fail:
    mov dword [fb_params + 0], 0
    popa
    ret