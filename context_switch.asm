[bits 32]
global switch_context
global ret_from_fork

; ============================================================================
; CONTEXT SWITCH (Core Scheduler Primitive)
; ============================================================================
switch_context:
    ; 1. Сохраняем callee-saved регистры старой задачи
    push ebx
    push esi
    push edi
    push ebp

    ; Стек сейчас: [ESP+0]=EBP, [ESP+4]=EDI, [ESP+8]=ESI, [ESP+12]=EBX
    ; Аргументы (были pushed до call):
    ; [ESP+16] = Return Address (EIP)
    ; [ESP+20] = old_esp (arg 1)
    ; [ESP+24] = new_esp (arg 2)
    ; [ESP+28] = new_cr3 (arg 3)

    ; 2. Сохраняем ESP старой задачи в её PCB
    mov eax, [esp + 20]   ; EAX = &old_task->esp
    mov [eax], esp        ; *old_esp = current_ESP

    ; 3. Читаем аргументы для новой задачи ДО смены ESP!
    mov ebx, [esp + 24]   ; EBX = new_esp
    mov ecx, [esp + 28]   ; ECX = new_cr3

    ; 4. ТЕЛЕПОРТАЦИЯ! Меняем стек
    mov esp, ebx          

    ; 5. CR3 SWITCH (ИЗОЛЯЦИЯ ПАМЯТИ)
    ; Загружаем новый Page Directory в CR3.
    ; Это аппаратно сбрасывает TLB и меняет адресное пространство.
    mov cr3, ecx          

    ; 6. Восстанавливаем регистры новой задачи
    pop ebp
    pop edi
    pop esi
    pop ebx

    ; 7. УСТАНОВКА CR0.TS (TASK SWITCHED)
    ; "Взводим курок": следующая FPU/SSE инструкция новой задачи 
    ; гарантированно вызовет исключение #NM (INT 7).
    mov eax, cr0
    or eax, 0x8       ; Бит 3 = TS (Task Switched)
    mov cr0, eax

    ; 8. Возврат (прыжок в EIP новой задачи)
    ret

; ============================================================================
; [ДЕНЬ 14] FORK RETURN TRAMPOLINE
; ============================================================================
; Точка входа для ребенка после switch_context.
; switch_context сделал pop ebp, edi, esi, ebx и ret, прыгнув сюда.
; На стеке лежит: [Pointer to struct regs]
ret_from_fork:
    ; 1. Достаем указатель на struct regs из стека и загружаем его в ESP.
    ; Это "телепортация" стека: мы переключаемся с фейкового фрейма на 
    ; скопированный ISR Frame (контекст INT 0x80 родителя).
    pop esp
    
    ; Теперь стек выглядит точно так же, как после isr_common_stub:
    ; [ESP+0]  = DS (pushed by isr_common_stub)
    ; [ESP+4]  = ES
    ; [ESP+8]  = FS
    ; [ESP+12] = GS
    ; [ESP+16] = EDI (pusha)
    ; ...
    ; [ESP+48] = int_no (0x80)
    ; [ESP+52] = err_code (0)
    ; [ESP+56] = EIP (Ring 3)
    ; [ESP+60] = CS (0x1B)
    ; [ESP+64] = EFLAGS
    ; [ESP+68] = ESP (Ring 3)
    ; [ESP+72] = SS (0x23)
    
    ; 2. Восстанавливаем сегментные регистры (как в isr_common_stub)
    pop eax
    mov gs, ax
    pop eax
    mov fs, ax
    pop eax
    mov es, ax
    pop eax
    mov ds, ax
    
    ; 3. Восстанавливаем регистры общего назначения
    popa
    
    ; 4. Убираем err_code и int_no (как в isr_common_stub)
    add esp, 8
    
    ; 5. Возврат в Ring 3!
    ; iret достает EIP, CS, EFLAGS, ESP, SS из ISR Frame и прыгает в user space.
    ; EAX уже установлен в 0 (в task_fork через child_r->eax), поэтому ребенок
    ; видит sys_fork() == 0.
    iret