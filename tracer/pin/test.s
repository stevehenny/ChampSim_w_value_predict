    .global _start
    .section .data
msg:
    .ascii "hello from bare program\n"
len = . - msg

    .section .text
_start:
    /* write(1, msg, len) */
    mov $1, %rax        /* syscall: write */
    mov $1, %rdi        /* fd = 1 (stdout) */
    lea msg(%rip), %rsi /* buf */
    mov $len, %rdx      /* count */
    syscall

    /* exit(0) */
    mov $60, %rax       /* syscall: exit */
    xor %rdi, %rdi      /* status=0 */
    syscall