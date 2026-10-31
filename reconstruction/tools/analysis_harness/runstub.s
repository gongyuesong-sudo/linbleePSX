	.text
	.globl run_stub
# void run_stub(uint32_t stub_va, uint32_t *new_esp)
run_stub:
	pushl %ebp
	movl %esp, %ebp
	pushl %ebx
	pushl %esi
	pushl %edi
	movl 8(%ebp), %eax        # stub_va
	movl 12(%ebp), %ecx       # fake esp
	movl %esp, %ebx
	movl %ecx, %esp
	call *%eax
	movl %ebx, %esp
	popl %edi
	popl %esi
	popl %ebx
	popl %ebp
	ret

	.globl stub_back
stub_back:
	movl %ebx, %esp
	ret

	.globl run_stub2
# void run_stub2(uint32_t stub_va, uint32_t esp_at_entry, uint32_t entry_eax)
# jmp (no push): the pre-placed RET slot at esp+4 is popped by the stub's final RET.
# Entry registers mimic FUN_00401021 at the call: EAX = view base, EDI = scratch ptr.
run_stub2:
	pushl %ebp
	movl %esp, %ebp
	pushl %ebx
	pushl %esi
	pushl %edi
	movl 8(%ebp), %eax        # stub_va
	movl 12(%ebp), %ecx       # esp
	movl 16(%ebp), %edi       # entry EAX = view base
	movl %esp, %ebx
	movl %ecx, %esp
	xchgl %eax, %edi          # eax = view base (stos source); edi = jump target
	jmp *%edi
