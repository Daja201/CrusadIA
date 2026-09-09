bits 32

extern fault_handler
extern schedule_handler

%macro ISR_NOERRCODE 1
  global isr%1
  isr%1:
    cli
    push byte 0
    push byte %1
    jmp isr_common_stub
%endmacro

%macro ISR_ERRCODE 1
  global isr%1
  isr%1:
    cli
    push byte %1
    jmp isr_common_stub
%endmacro

ISR_NOERRCODE 0
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 4
ISR_NOERRCODE 5
ISR_NOERRCODE 6
ISR_NOERRCODE 7
ISR_ERRCODE   8
ISR_NOERRCODE 9
ISR_ERRCODE   10
ISR_ERRCODE   11
ISR_ERRCODE   12
ISR_ERRCODE   13
ISR_ERRCODE   14
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_ERRCODE   17
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_ERRCODE   30
ISR_NOERRCODE 31
;
;ISR_NOERRCODE 32
ISR_NOERRCODE 33
ISR_NOERRCODE 34
ISR_NOERRCODE 35
ISR_NOERRCODE 36
ISR_NOERRCODE 37
ISR_NOERRCODE 38
ISR_NOERRCODE 39
ISR_NOERRCODE 40
ISR_NOERRCODE 41
ISR_NOERRCODE 42
ISR_NOERRCODE 43
ISR_NOERRCODE 44
ISR_NOERRCODE 45
ISR_NOERRCODE 46
ISR_NOERRCODE 47

isr_common_stub:
    pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x18
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push esp
    call fault_handler
    add esp, 4
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8
    ;sti
    iret

global isr32
isr32:
    cli
    push byte 0      ; Dummy error kód
    push byte 32     ; Číslo přerušení (IRQ0)
    pusha            ; Uloží EAX až EDI (8 registrů)
    push ds          ; Uloží segmenty
    push es
    push fs
    push gs
    mov ax, 0x18     ; Nahraje kernel data segment (ujisti se, že v create_task používáš taky 0x10)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push esp         ; Předáme ukazatel na současný zásobník do C
    call schedule_handler
    mov esp, eax     ; <<< PŘEPNUTÍ ZÁSOBNÍKU >>>
    mov al, 0x20     ; Odeslání EOI (End of Interrupt) do PIC
    out 0x20, al     ; Pokud toto chybí, timer už znovu netikne
    pop gs           ; Obnovíme segmenty (z NOVÉHO zásobníku)
    pop fs
    pop es
    pop ds
    popa             ; Obnovíme registry EAX-EDI
    add esp, 8       ; Přeskočíme chybový kód (0) a číslo (32)
    iret
section .note.GNU-stack
    align 4
    db 0
