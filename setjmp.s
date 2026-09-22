bits 32

global setjmp
global longjmp

; jmp_buf layout: ebx esi edi ebp esp eip   (see setjmp.h)
; The saved esp is the value the caller has AFTER setjmp returns (esp+4).
; The old code saved esp *before* the return-address pop, so after a longjmp
; the caller resumed with esp 4 bytes too low. With frame pointers (-O0) that
; goes unnoticed, with optimisation (-O1/-O2, needed for TinyCC) it corrupts
; the caller's stack.
setjmp:
    mov eax, [esp+4]
    mov ecx, [esp]
    mov [eax], ebx
    mov [eax+4], esi
    mov [eax+8], edi
    mov [eax+12], ebp
    lea edx, [esp+4]
    mov [eax+16], edx
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
