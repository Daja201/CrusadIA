.text
.globl setjmp
.globl longjmp
setjmp:
    movl 4(%esp), %eax
    movl (%esp), %ecx
    movl %ebx, (%eax)
    movl %esi, 4(%eax)
    movl %edi, 8(%eax)
    movl %ebp, 12(%eax)
    leal 4(%esp), %edx
    movl %edx, 16(%eax)
    movl %ecx, 20(%eax)
    xorl %eax, %eax
    ret
longjmp:
    movl 4(%esp), %eax
    movl 8(%esp), %edx
    movl (%eax), %ebx
    movl 4(%eax), %esi
    movl 8(%eax), %edi
    movl 12(%eax), %ebp
    movl 20(%eax), %ecx
    movl 16(%eax), %esp
    testl %edx, %edx
    jnz 1f
    movl $1, %edx
1:  movl %edx, %eax
    jmp *%ecx
