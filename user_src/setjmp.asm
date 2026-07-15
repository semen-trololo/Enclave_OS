; ============================================================================
; setjmp.asm — Non-local jumps для TinyCC error recovery (NASM / Intel Syntax)
; TinyCC использует setjmp/longjmp для возврата в точку до начала парсинга
; при обнаружении синтаксической ошибки.
;
; jmp_buf layout (6 × uint32_t = 24 байта):
;   [0]  = EBX  (callee-saved)
;   [4]  = ESI  (callee-saved)
;   [8]  = EDI  (callee-saved)
;   [12] = EBP  (callee-saved)
;   [16] = ESP  (после возврата из setjmp, без return address)
;   [20] = EIP  (return address из setjmp)
;
; ABI: cdecl (аргументы на стеке, caller чистит стек)
; ============================================================================

section .text
global setjmp
global longjmp

; ----------------------------------------------------------------------------
; int setjmp(jmp_buf env)
; Сохраняет callee-saved регистры + ESP + EIP. Всегда возвращает 0.
; ----------------------------------------------------------------------------
setjmp:
    ; [esp]     = return address (куда вернуться после setjmp)
    ; [esp + 4] = env (указатель на jmp_buf)
    
    mov edx, [esp + 4]      ; edx = env
    
    ; Сохраняем callee-saved регистры
    mov [edx],      ebx     ; env[0] = EBX
    mov [edx + 4],  esi     ; env[1] = ESI
    mov [edx + 8],  edi     ; env[2] = EDI
    mov [edx + 12], ebp     ; env[3] = EBP
    
    ; Сохраняем ESP, каким он будет ПОСЛЕ возврата из setjmp
    ; (то есть без return address на стеке)
    lea ecx, [esp + 4]      ; ecx = ESP после ret
    mov [edx + 16], ecx     ; env[4] = ESP
    
    ; Сохраняем EIP возврата
    mov ecx, [esp]          ; ecx = return address
    mov [edx + 20], ecx     ; env[5] = EIP
    
    ; Возвращаем 0 (стандарт setjmp)
    xor eax, eax
    ret


; ----------------------------------------------------------------------------
; void longjmp(jmp_buf env, int val)  __attribute__((noreturn))
; Восстанавливает регистры и "возвращает" управление в setjmp.
; setjmp "вернет" val (или 1, если val == 0).
; ----------------------------------------------------------------------------
longjmp:
    ; [esp]     = return address (не используется — мы не вернемся)
    ; [esp + 4] = env
    ; [esp + 8] = val
    
    mov edx, [esp + 4]      ; edx = env
    mov eax, [esp + 8]      ; eax = val
    
    ; POSIX: если val == 0, longjmp возвращает 1 вместо 0
    ; (чтобы отличить прямой возврат setjmp от longjmp-возврата)
    test eax, eax
    jnz .val_ok
    mov eax, 1
.val_ok:
    
    ; Восстанавливаем callee-saved регистры
    mov ebx, [edx]          ; EBX
    mov esi, [edx + 4]      ; ESI
    mov edi, [edx + 8]      ; EDI
    mov ebp, [edx + 12]     ; EBP
    
    ; Восстанавливаем стек (таким, каким он был при входе в setjmp + 4)
    mov esp, [edx + 16]     ; ESP
    
    ; Прыгаем на сохраненный return address
    ; Процессор "вернется" из setjmp, но теперь EAX = val
    jmp DWORD [edx + 20]    ; EIP
    ; noreturn: никогда не достигаем этой точки