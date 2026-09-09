bits 32
global loader
extern kmain

MAGIC_NUMBER      equ 0x1BADB002
FLAGS             equ (1<<0) | (1<<1) | (1<<2)
CHECKSUM          equ -(MAGIC_NUMBER + FLAGS)
KERNEL_STACK_SIZE equ 65536

section .multiboot
    align 4
    dd MAGIC_NUMBER
    dd FLAGS
    dd CHECKSUM    
    dd 0
    dd 0
    dd 0
    dd 0
    dd 0 
    dd 0    
    dd 1920
    dd 1080
    dd 32

section .bss
    align 16

kernel_stack:
    resb KERNEL_STACK_SIZE

section .text
    align 4

loader:
    lea esp, [kernel_stack + KERNEL_STACK_SIZE]
    mov ebp, esp
    push ebx
    push eax
    call kmain
    add esp, 8

.hang:
    cli
    hlt
    jmp .hang

section .note.GNU-stack