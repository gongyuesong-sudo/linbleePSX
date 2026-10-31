	.text
	.globl run_decoder
	.type run_decoder,@function
# void run_decoder(void *resbuf, void *dst, void *stream, uint32_t fn, void *sentinel)
run_decoder:
	pushl %ebx
	pushl %ebp
	pushl %esi
	pushl %edi
	movl  24(%esp), %edi      # dst
	movl  28(%esp), %esi      # stream header VA
	movl  32(%esp), %eax      # fn (decoder VA)
	movl  36(%esp), %ecx      # sentinel (&run_back)
	pushl %ecx
	jmpl *%eax

	.globl run_back
run_back:                     # reached via decoder RET; esp = after 4 pushes
	movl  20(%esp), %eax      # resbuf
	movl  %edi, 0(%eax)       # final EDI cursor (absolute)
	movl  %esi, 4(%eax)       # final ESI (absolute)
	popl %edi
	popl %esi
	popl %ebp
	popl %ebx
	ret
