; ============================================================================
; ENCLAVE OS — Ultra-Safe Diagnostic crt0.asm
; Читает [esp] и выводит его как одну цифру напрямую через sys_write.
; ============================================================================

section .text
global _start
extern main
extern exit

STDOUT equ 1

_start:
    ; 1. Читаем то, что лежит на вершине стека (должно быть argc)
    mov eax, [esp]
    
    ; 2. Превращаем в ASCII цифру (работает для argc < 10)
    add al, '0'
    mov [hex_char], al
    
    ; 3. Выводим цифру напрямую через sys_write (INT 0x80)
    mov eax, 4              ; sys_write
    mov ebx, STDOUT         ; fd = 1
    mov ecx, hex_char       ; buffer
    mov edx, 1              ; count = 1
    int 0x80
    
    ; 4. Выводим пробел и переход на новую строку для читаемости
    mov byte [hex_char], ' '
    mov eax, 4
    mov ebx, STDOUT
    mov ecx, hex_char
    mov edx, 1
    int 0x80
    
    ; 5. Теперь вызываем main как обычно
    mov eax, [esp]          ; eax = argc
    lea ebx, [esp + 4]      ; ebx = argv
    lea ecx, [ebx + eax*4 + 4] ; ecx = envp
    
    push ecx
    push ebx
    push eax
    call main
    
    add esp, 12
    push eax
    call exit

.halt_loop:
    cli
    hlt
    jmp .halt_loop

section .bss
hex_char: resb 1