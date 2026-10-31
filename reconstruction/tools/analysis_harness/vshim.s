    .text
    .globl sX1
    .globl sX2
    .globl sVP2
# ---- stub FUN_00055895 double-call chain, ground truth:
#  call1: [table+0](buf=0x2a0000, table, 0x4f) must return 0x104 and store call2 target at [buf]
#  call2: [buf](buf+0x2a or buf+0x21) return r; FUN_00055895 = (r & 0xffff as unsigned sub 0xf386) sign? + 0xc7a
#         entry branch wants FUN == 0  →  r = 0xE70C
# calling convention: callee pops (call1: 3 args = ret 12; call2: 1 arg = ret 4)
sX1:
    movl $sX2, %ecx
    movl %ecx, (%edi)           # EDI live register = frame slot for call2 target
    movl $0x104, %eax
    ret $12
sX2:
    pushl %ebx
    movl 8(%esp), %ebx          # arg at esp+8: retaddr at +4 after self pushl ebx
    movl $0x124, (%ebx)         # dwOSVersionInfoSize (harmless fill)
    movl $4, 4(%ebx)            # major
    movl $0, 8(%ebx)            # minor
    movl $0x535, 12(%ebx)       # build
    movl $2, 16(%ebx)           # platform = WIN32_WINDOWS
    xorl %eax, %eax             # attempt4: FUN = (0-0xf386)+0xc7a != 0 → fallthrough to call view+0xfa00
    popl %ebx
    ret $4
sVP2:
    movl 4(%esp), %eax
    movl $sX2, %edx
    movl %edx, (%eax)
    movl $0x104, %eax
    ret $8
