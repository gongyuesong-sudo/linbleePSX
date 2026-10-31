import gdb, struct

hits = {}

def probe(addr, name):
    class P(gdb.Breakpoint):
        def __init__(self):
            super().__init__('*%d' % addr, gdb.BP_BREAKPOINT, internal=False)
        def stop(self):
            hits[name] = hits.get(name, 0) + 1
            n = hits[name]
            if n <= 5:
                r = {k: int(gdb.parse_and_eval('$'+k)) for k in ('eax','ebx','ecx','edx','esi','edi','esp')}
                inf = gdb.inferiors()[0]
                line = 'HIT %s #%d eax=%x edx=%x ecx=%x esi=%x edi=%x esp=%x' % (name, n, r['eax'], r['edx'], r['ecx'], r['esi'], r['edi'], r['esp'])
                try:
                    if r['edx']:
                        vd = int.from_bytes(inf.read_memory(r['edx'], 4).tobytes(), 'little')
                        line += ' [edx]=%x' % vd
                        if vd:
                            vt = [int.from_bytes(inf.read_memory(vd+i*4,4).tobytes(),'little') for i in range(8)]
                            line += ' vtable=' + ','.join('%x' % x for x in vt)
                except Exception as e:
                    line += ' readfail'
                print(line, flush=True)
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
        probe(0x4019b3, 'com_site_19b3')
        probe(0x403c169-0x400000+0x400000, 'com_site_3c169') if False else probe(0x43c169, 'com_site_3c169')
        probe(0x43c1b3, 'com_site_3c1b3')
        return False

Entry()
gdb.execute('run decrypted/stream_10a4c_L71cbf_relocated.bin')
try:
    print('STOPPED pc=%x eax=%x edx=%x esp=%x' % (
        int(gdb.parse_and_eval('$pc')), int(gdb.parse_and_eval('$eax')),
        int(gdb.parse_and_eval('$edx')), int(gdb.parse_and_eval('$esp'))), flush=True)
except Exception as e:
    print('end', e, flush=True)
print('hits=%s' % hits, flush=True)
gdb.execute('quit')
