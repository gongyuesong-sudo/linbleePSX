import gdb

class Entry(gdb.Breakpoint):
    def __init__(self):
        super().__init__('call_engine', gdb.BP_BREAKPOINT, internal=False)
    def stop(self):
        esp = int(gdb.parse_and_eval('$esp'))
        entry = int.from_bytes(gdb.inferiors()[0].read_memory(esp + 4, 4).tobytes(), 'little')
        cb = int.from_bytes(gdb.inferiors()[0].read_memory(esp + 8, 4).tobytes(), 'little')
        gdb.write('PRE stub: entry=%x cb=%x\n' % (entry, cb))
        gdb.execute('stepi')
        gdb.write('POST stub: pc=%x eax=%x\n' % (
            int(gdb.parse_and_eval('$pc')), int(gdb.parse_and_eval('$eax'))))
        gdb.execute('x/8xw $esp')
        return False

Entry()
gdb.execute('run decrypted/stream_10a4c_L71cbf_relocated.bin')
gdb.execute('quit')
