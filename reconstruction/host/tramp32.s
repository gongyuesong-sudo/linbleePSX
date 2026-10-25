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
    .globl host_service_cb
/* host_service_cb: engine generic host service (call [esp], arg in EAX)
 * EAX==0 -> BIOS base (call bios_map_cb_c); EAX!=0 -> alloc EAX bytes from arena bump */
host_service_cb:
    testl %eax, %eax
    jz  1f
    movl %eax, %ecx
    addl $0xffff, %ecx
    andl $-0x10000, %ecx
    cmpl $0x10000, %ecx
    jae 2f
    movl $0x10000, %ecx
2:  movl arena_bump, %eax
    addl %ecx, arena_bump
    cmpl $0x2e100000, %eax
    jb 3f
    movl $-1, %eax
3:  ret
1:  call bios_map_cb_c
    ret
    .section .note.GNU-stack,"",@progbits
    .globl svc_call
svc_call:
    movl 4(%esp), %eax
    jmp host_service_cb
    .section .note.GNU-stack,"",@progbits
