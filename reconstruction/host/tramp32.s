    .text
    .globl call_engine
/* call_engine(entry, cb): engine entry contract (H-2 disasm-proven):
 *   at img+0x4: EAX = cb  AND  [esp] = cb  (pusha keeps [esp]=cb; later
 *   `call [esp]` @img+0x191 = invoke cb slot0 = BIOS-map callback).
 * Final engine RET pops our stub_ret placed just under cb. */
call_engine:
    movl 4(%esp), %edx        /* entry */
    movl 8(%esp), %eax        /* cb */
    pushl %eax                /* [esp]=cb at engine entry (arg0 view) */
    pushl $stub_ret           /* final engine RET -> here */
    /* reorder: engine sees esp->cb. currently esp->stub_ret, +4->cb. swap: */
    movl (%esp), %ecx
    movl 4(%esp), %eax
    movl %eax, (%esp)         /* [esp] = cb */
    movl %ecx, 4(%esp)        /* [esp+4] = stub_ret */
    xorl %ecx, %ecx
    xorl %ebx, %ebx
    xorl %esi, %esi
    xorl %edi, %edi
    jmp *%edx
stub_ret:
    addl $8, %esp
    ret
    .section .note.GNU-stack,"",@progbits
