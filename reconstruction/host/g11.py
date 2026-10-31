import gdb, struct

seen = {}
LOOP = 0x400014

class Bp(gdb.Breakpoint):
    def __init__(self):
        super().__init__('*%d' % LOOP, gdb.BP_BREAKPOINT, internal=False)
        self.n = 0
    def stop(self):
        self.n += 1
        esp = int(gdb.parse_and_eval('$esp'))
        edi = int(gdb.parse_and_eval('$edi'))
        try:
            inf = gdb.inferiors()[0]
            raw = inf.read_memory(esp, 24).tobytes()
            dws = struct.unpack('<6I', raw)
            key = tuple(dws)
        except Exception as e:
            print('loop%d read fail %s esp=%x' % (self.n, e, esp), flush=True)
            return False
        if key in seen:
            print('LOOP% esp=%x args=%s SAME-AS-#%d -> infinite loop' % (esp, [hex(x) for x in dws], seen[key]), flush=True)
            gdb.execute('finish')  # try to escape
            self.n = 0
            return False
        seen[key] = self.n
        if self.n <= 12:
            print('LOOP% esp=%x edi=%x args=%s' % (self.n, esp, edi, [hex(x) for x in dws]), flush=True)
        if self.n > 60:
            print('LOOP cap 60 — args uniq so far; last=%s' % ([hex(x) for x in dws],), flush=True)
            gdb.execute('delete %d' % self.number)
        return False

class Entry(gdb.Breakpoint):
    def __init__(self):
        super().__init__('call_engine', gdb.BP_BREAKPOINT, internal=False)
        self.armed = 0
    def stop(self):
        if self.armed:
            return False
        self.armed = 1
        Bp()
        return False

Entry()
gdb.execute('run decrypted/stream_10a4c_L71cbf_relocated.bin')
try:
    print('STOPPED pc=%x esp=%x eax=%x edi=%x' % (
        int(gdb.parse_and_eval('$pc')), int(gdb.parse_and_eval('$esp')),
        int(gdb.parse_and_eval('$eax')), int(gdb.parse_and_eval('$edi'))), flush=True)
    print(gdb.execute('x/12xw $esp', to_string=True), flush=True)
except Exception as e:
    print('end', e, flush=True)
gdb.execute('quit')
