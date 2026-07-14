; ============================================================================
; crt0.asm — C Runtime Startup для Bare Metal OS (NASM / Intel Syntax)
; Точка входа _start, вызываемая ядром после sys_exec.
; Забирает argc/argv со стека (POSIX ABI), вызывает main() и делает exit().
; ============================================================================

section .text
global _start
extern main
extern exit

_start:
    ; Стек сформирован ядром (sys_exec_handler):
    ; [esp]      -> argc
    ; [esp + 4]  -> argv
    ; [esp + 8]  -> envp

    pop eax         ; eax = argc
    pop ebx         ; ebx = argv
    pop ecx         ; ecx = envp

    ; Вызов main(argc, argv, envp) по соглашению cdecl (аргументы в стек справа налево)
    push ecx        ; envp (3-й аргумент)
    push ebx        ; argv (2-й аргумент)
    push eax        ; argc (1-й аргумент)

    call main       ; eax = main(argc, argv, envp)

    ; Завершаем процесс с кодом возврата из main
    push eax        ; exit_code
    call exit       ; exit(eax)

    ; Защита от возврата из exit (noreturn)
.halt:
    cli
    hlt
    jmp .halt