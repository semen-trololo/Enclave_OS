section .text
global _start
extern main
extern exit

_start:
    ; Стек при старте ELF выглядит так: [esp] = argc, [esp+4] = argv, [esp+8...] = envp
    mov eax, [esp]          ; eax = argc
    lea ebx, [esp + 4]      ; ebx = argv
    lea ecx, [esp + eax*4 + 8] ; ecx = envp (пропускаем argc и argv + NULL terminator)

    push ecx                ; envp
    push ebx                ; argv
    push eax                ; argc
    call main
    
    add esp, 12             ; Чистим стек после main
    push eax                ; Передаем exit_code из main в exit()
    call exit               ; exit() сделает sys_exit и nunca вернется

.halt_loop:
    cli
    hlt
    jmp .halt_loop