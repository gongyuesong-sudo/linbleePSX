	.text
	.globl run_decoder
	.type run_decoder,@function
# int run_decoder(void *dst, void *stream, uint32_t fn, void *sentinel, uint32_t *resbuf)
run_decoder:
	pushl %ebx
	pushl %ebp
	pushl %esi
	pushl %edi
	movl 20(%esp), %edi      # dst
	movl 24(%esp), %esi      # stream
	movl 28(%esp), %eax      # fn
	movl 32(%esp), %ecx      # sentinel (run_back)
	pushl %ecx
	jmpl *%eax
	.size run_decoder,.-run_decoder

	.globl run_back
run_back:
	# entered via decoder RET; pop saved regs and return to C
	popl %edi                 # discard leftover? careful: after RET, esp points to resbuf slot
	# actually: stack now (top->): resbuf, stream, dst, retaddr_to_C ; edi holds final cursor
	movl %edi, %ebx           # keep final edi
	popl %edi                 # no: don't pop args... fix: args are still pushed
	# re-adjust: we popped one arg already; put it back
	# simpler: back up
	decl ???
