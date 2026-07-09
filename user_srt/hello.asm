; user_src/hello.asm
; Тестовая программа: "Hello from User Space!"
; Использует системные вызовы через INT 0x80

section .data
    msg db "Hello from User Space!", 10, 0  ; 10 = '\n'
    msg_len equ $ - msg - 1  ; Без нулевого термина

section .text
    global _start

_start:
    ; sys_write(1, msg, msg_len)
    mov eax, 4          ; SYS_WRITE = 4
    mov ebx, 1          ; FD = 1 (stdout)
    mov ecx, msg        ; Buffer
    mov edx, msg_len    ; Length
    int 0x80            ; Syscall

    ; sys_exit(0)
    mov eax, 1          ; SYS_EXIT = 1
    mov ebx, 0          ; Exit code = 0
    int 0x80            ; Syscall