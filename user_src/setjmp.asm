; ============================================================================
; SETJMP/LONGJMP — Non-local Jumps для Enclave Operating System
; Реализация POSIX setjmp/longjmp для i386 (32-bit x86)
; Критично для TinyCC error recovery (обработка синтаксических ошибок)
; ============================================================================

section .text
global setjmp
global longjmp

; ============================================================================
; int setjmp(jmp_buf env)
; Сохраняет callee-saved регистры и возвращает 0
; jmp_buf — массив из 6 int: [EBX, ESI, EDI, EBP, ESP, EIP]
; ============================================================================
setjmp:
    ; Получаем указатель на jmp_buf (первый аргумент)
    mov eax, [esp + 4]        ; eax = env
    
    ; Сохраняем callee-saved регистры (согласно cdecl calling convention)
    mov [eax + 0], ebx        ; env[0] = EBX
    mov [eax + 4], esi        ; env[1] = ESI
    mov [eax + 8], edi        ; env[2] = EDI
    mov [eax + 12], ebp       ; env[3] = EBP (frame pointer)
    
    ; Сохраняем ESP (указатель стека на момент вызова setjmp)
    ; esp+4 = стек после push аргумента, но до call (то есть стек на момент возврата)
    lea ecx, [esp + 4]        ; ecx = скорректированный ESP
    mov [eax + 16], ecx       ; env[4] = ESP
    
    ; Сохраняем EIP (instruction pointer)
    ; Return address лежит на вершине стека (положен инструкцией call)
    mov ecx, [esp]            ; ecx = return address
    mov [eax + 20], ecx       ; env[5] = EIP
    
    ; Возвращаем 0 (прямой вызов setjmp)
    xor eax, eax
    ret

; ============================================================================
; void longjmp(jmp_buf env, int val)
; Восстанавливает регистры и прыгает на сохраненный EIP
; Если val == 0, возвращает 1 (POSIX требование)
; ============================================================================
longjmp:
    ; Получаем аргументы (используем edx для env, eax для val)
    mov edx, [esp + 4]        ; edx = env (указатель на jmp_buf)
    mov eax, [esp + 8]        ; eax = val (возвращаемое значение)
    
    ; Восстанавливаем callee-saved регистры
    mov ebx, [edx + 0]        ; EBX
    mov esi, [edx + 4]        ; ESI
    mov edi, [edx + 8]        ; EDI
    mov ebp, [edx + 12]       ; EBP
    mov esp, [edx + 16]       ; ESP (восстанавливаем стек)
    
    ; POSIX: если val == 0, возвращаем 1
    test eax, eax
    jnz .not_zero
    mov eax, 1
.not_zero:
    
    ; Прыгаем на сохраненный EIP (return address из setjmp)
    ; Это имитирует возврат из setjmp, но с ненулевым значением
    jmp [edx + 20]            ; Прыжок на сохраненный return address