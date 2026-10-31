import gdb

hits = {}

def probe(addr, name, depth=0):
    class P(gdb.Breakpoint):
        def __init__(self):
            super().__init__('*%d' % addr, gdb.BP_BREAKPOINT, internal=False)
        def stop(self):
            hits[name] = hits.get(name, 0) + 1
            n = hits[name]
            if n <= 6:
                esp = int(gdb.parse_and_eval('$esp'))
                eax = int(gdb.parse_and_eval('$eax'))
                edi = int(gdb.parse_and_eval('$edi'))
                print('HIT %s #%d eax=%x edi=%x esp=%x' % (name, n, eax, edi, esp), flush=True)
                try:
                    print('  ', gdb.execute('x/8xw $esp', to_string=True).strip().replace('\n', ' | '), flush=True)
                    print('  ', gdb.execute('x/3i $pc', to_string=True).strip().replace('\n', ' | '), flush=True)
                except Exception as e:
                    print('   read fail', e, flush=True)
            return False
    return P()

class Entry(gdb.Breakpoint):
    def __init__(self):
        super().__init__('call_engine', gdb.BP_BREAKPOINT, internal=False)
        self.armed = 0
    def stop(self):
        if self.armed:
            return False
        self.armed = 1
        probe(0x400173, 'callee_173')
        probe(0x40017b, 'crash_store')
        probe(0x400164, 'alloc_end')
        probe(0x4002b0, 'stage_2b0')
        probe(0x40003a, 'store38')
        return False

Back = None
gdb.execute('run decrypted/stream_10a4c_L71cbf_relocated.bin')
try:
    pc = int(gdb.parse_and_eval('$pc'))
    print('STOPPED pc=%x' % pc, flush=True)
    print(gdb.execute('x/10xw $esp', to_string=True), flush=True)
except Exception as e:
    print('end', e, flush=True)
print('hits=%s' % hits, flush=True)
gdb.execute('quit')
