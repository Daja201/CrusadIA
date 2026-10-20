bits 32
global _start
extern main
extern exit
extern __init_environ

section .text
_start:
    xor ebp, ebp
    mov eax, [esp]
    mov ebx, esp
    add ebx, 4
    lea ecx, [ebx + eax*4 + 4]
    push ecx
    call __init_environ
    add esp, 4
    push ecx
    push ebx
    push eax
    call main
    add esp, 12
    push eax
    call exit
.hang:
    jmp .hang

section .note.GNU-stack
