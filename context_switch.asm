[bits 32]
global switch_context

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

    ; 5. ✅ CR3 SWITCH (ИЗОЛЯЦИЯ ПАМЯТИ)
    ; Загружаем новый Page Directory в CR3.
    ; Это аппаратно сбрасывает TLB и меняет адресное пространство.
    mov cr3, ecx          

    ; 6. Восстанавливаем регистры новой задачи
    pop ebp
    pop edi
    pop esi
    pop ebx

    ; 7. Возврат (прыжок в EIP новой задачи)
    ret