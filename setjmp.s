bits 32

global setjmp
global longjmp

setjmp:
    mov eax, [esp+4]
    mov ecx, [esp]
    mov [eax], ebx
    mov [eax+4], esi
    mov [eax+8], edi
    mov [eax+12], ebp
    mov [eax+16], esp
    mov [eax+20], ecx
    xor eax, eax
    ret

longjmp:
    mov eax, [esp+4]
    mov edx, [esp+8]
    mov ebx, [eax]
    mov esi, [eax+4]
    mov edi, [eax+8]
    mov ebp, [eax+12]
    mov ecx, [eax+20]
    mov esp, [eax+16]
    test edx, edx
    jnz .nz
    mov edx, 1
.nz:
    mov eax, edx
    jmp ecx
