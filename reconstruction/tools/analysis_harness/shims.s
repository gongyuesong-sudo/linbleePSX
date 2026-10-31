    .text
    .globl sVP
    .globl sGVE
    .globl sVA
    .globl cVA
    .globl sGS
    .globl sGL
    .globl sGUn
    .globl sGF
    .globl cGS
    .globl cGL
# ---- stdcall shim: VirtualProtect(lp, table, flProtect) ret 12
# side effect required by stub FUN_00055895: [lp] := &GetVersionExA(stdcall); return 0x104
sVP:
    movl 4(%esp), %eax          # lp (0x2a0000)
    movl $sGVE, %edx
    movl %edx, (%eax)           # [lp] = GetVersionExA entry
    movl $0x104, %eax           # old protect = PAGE_EXECUTE_READWRITE
    ret $8

# ---- stdcall shim: GetVersionExA(lpOSVI) ret 4  — OSVERSIONINFOA Win95
sGVE:
    pushl %ebx
    movl 8(%esp), %ebx          # p
    movl $4, 4(%ebx)            # dwMajorVersion
    movl $0, 8(%ebx)            # dwMinorVersion
    movl $0x535, 12(%ebx)       # dwBuildNumber 1333
    movl $2, 16(%ebx)           # dwPlatformId = WIN32_WINDOWS
    movl $0x20390000, 20(%ebx)  # " 9"... write "95 4.00 A"
    movw $0x2035, 21(%ebx)      # "5 "
    movl $0x41203420, 23(%ebx)  # " 4.0"  hmm alignment-tolerant scratch
    movl $0, %eax
    movb $0x41, 26(%ebx)        # 'A'
    movb $0, 27(%ebx)
    movl $1, %eax               # TRUE
    popl %ebx
    ret $4

# ---- stdcall shim: VirtualAlloc(a,sz,type,protect) -> cVA, ret 16
sVA:
    pushl %ebp
    movl %esp, %ebp
    pushl 20(%ebp)
    pushl 16(%ebp)
    pushl 12(%ebp)
    pushl 8(%ebp)
    call cVA
    addl $16, %esp
    popl %ebp
    ret $16

# ---- stdcall: GlobalSize(h) ret 4
sGS:
    movl 4(%esp), %eax
    pushl %eax
    call cGS
    addl $4, %esp
    ret $4
# ---- stdcall: GlobalLock(h) ret 4
sGL:
    movl 4(%esp), %eax
    pushl %eax
    call cGL
    addl $4, %esp
    ret $4
# ---- stdcall: GlobalUnlock/GlobalFree ret 4
sGUn:
    movl $1, %eax
    ret $4
sGF:
    xorl %eax, %eax
    ret $4
