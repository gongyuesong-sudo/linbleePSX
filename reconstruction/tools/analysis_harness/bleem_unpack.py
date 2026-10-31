# Faithful Python transcription of bleem! v1.4 demo self-extractor (stub @0x5577a)
# Bit engine: descending dword stream, EBX buffer, persistent CF (RCR carry chain).
import struct, sys

def bleem_rand_fold(data, seed):
    # verified on stub region [0x55338,0x55df4) seed 0x55338 -> 0x4D24AF3D
    acc = (seed ^ 0x52414E44) & 0xFFFFFFFF  # 'RAND'
    for i in range(0, len(data) & ~3, 4):
        acc ^= struct.unpack_from('<I', data, i)[0]
    return acc

ESIOFF=0
class Engine:
    def __init__(self, img, img_off, stream_base, dst_base, memtop=0):
        self.d = img
        self.io = img_off          # img bytes correspond to file offsets >= img_off
        self.dst = dst_base        # absolute (file/image coord) output base
        self.PAD = 0x20000
        self.out = bytearray(0x400000)
        self.F=lambda a: self.d[a - self.io]
        self.dword=lambda a: struct.unpack_from('<I', self.d, a - self.io)[0]
        # entry: EBP=EDI=dst; EDI+=[sb]; ESI=sb+4+[sb+4]; STD
        self.ebp = dst_base
        self.edi = dst_base + self.dword(stream_base)
        self.esi = stream_base + self.dword(stream_base + 4) + ESIOFF
        self.ebx = self.dword(self.esi); self.esi -= 4
        self.cf = 0
        self.hi_water = self.edi
        self.lo_water = self.edi

    def _shr(self):
        self.cf = self.ebx & 1; self.ebx >>= 1
    def _rcr(self):
        n = self.ebx & 1
        self.ebx = (self.cf << 31) | (self.ebx >> 1)
        self.cf = n
    def _refill_rcr(self):
        self.ebx = self.dword(self.esi); self.esi -= 4; self._rcr()

    def shift_out(self):          # generic consume at main-loop sites
        if self.ebx == 0: self._refill_rcr()
        else: self._shr()
        return self.cf

    def get8(self, acc=0):        # @0x55960 — hardware-faithful
        if self.ebx >= 0x100:     # fast path: value = buffer low 8 bits (bit0 = LSB)
            v = (acc & ~0xFF) | (self.ebx & 0xFF)   # mov al,bl
            self.cf = (self.ebx >> 7) & 1           # SHR 8 updates CF with bit7
            self.ebx >>= 8
            return v & 0xFFFFFFFF
        # slow path: 8 chained consumes, MSB-first accumulation (adc eax,eax)
        # JZ-tail = refill+RCR; the shr-bit that zeroed the buffer is phantom (re-injected at bit31)
        eax = acc
        for j in range(8):
            if self.ebx == 0:
                if j == 0 or j == 1 and eax == acc:
                    # @0x5599f: load, al=new&0xff (returned raw byte), rcr, shr 7
                    self.ebx = self.dword(self.esi); self.esi -= 4
                    v = self.ebx & 0xFF
                    self._rcr(); self.cf=(self.ebx>>6)&1; self.ebx >>= 7
                    return (acc & ~0xFF) | v
                self._refill_rcr()
            else:
                self._shr()
                if j == 0 and self.ebx == 0:
                    # @0x5599f tail after first-shift zeroing
                    self.ebx = self.dword(self.esi); self.esi -= 4
                    v = self.ebx & 0xFF
                    self._rcr(); self.cf=(self.ebx>>6)&1; self.ebx >>= 7
                    return (acc & ~0xFF) | v
            eax = ((eax << 1) | self.cf) & 0xFFFFFFFF
        return eax

    def bit_acc(self, acc):       # @0x55986/8c/92/98: 1-bit ADC with acc
        if self.ebx == 0: self._refill_rcr()
        else: self._shr()
        return ((acc << 1) | self.cf) & 0xFFFFFFFF

    # ---- main token loop (@0x5577a) ----
    def run(self, max_bytes=0x200000):
        # note: EDI counts DOWN from hi to ebp; "done" when EDI < EBP (JC 5580c)
        steps = 0
        while self.edi >= self.ebp:
            steps += 1
            if steps > 4000000: raise RuntimeError('runaway')
            b = self.shift_out()
            if b == 0:
                # 0x557ae: second level
                b2 = self.shift_out()
                if b2 == 0:
                    # short literal run: acc0=0 CALL get3?? -> uses 0x557de: EAX=0;5598c(1bit)+2more?
                    # 0x557de: XOR EAX,EAX; CALL 0x5598c -> ONE bit with acc; then loop ECX+=... 
                    # actual: ECX=0; EAX=get1bit; ECX+=EAX then write ECX+1 bytes? Reconstruct:
                    # 557de: xor eax; call 5598c (EAX=bit); ecx=0; jmp 557f0: ecx+=eax; call get8; [edi]=al; edi--; ecx--; js->loopend ; 557fd: call get8; store; edi--; ecx--; jns 557fd
                    eax = self.bit_acc(0)
                    ecx = eax
                    while True:
                        eax = self.get8(0) & 0xFF
                        self.put(self.edi, eax); self.edi -= 1
                        ecx -= 1
                        if ecx < 0: break
                        eax = self.get8(0) & 0xFF
                        self.put(self.edi, eax); self.edi -= 1
                        ecx -= 1
                        if ecx >= 0:
                            # loop back to 0x557fd (keeps writing until ecx<0)
                            continue
                        break
                else:
                    # 0x557c6 word match: SUB EDI,2; dist = get8; copy 2 bytes from EDI+2+.. 
                    self.edi -= 2
                    eax = self.get8(0) & 0xFF
                    for k in (2, 1):
                        v = self.get(self.edi + eax + k)
                        self.put(self.edi + k, v)
            else:
                b2 = self.shift_out()
                if b2 == 0:
                    # 0x558cf: 3/4-byte match
                    b3 = self.shift_out()
                    if b3 == 1:
                        # 4-byte
                        self.edi -= 4
                        if self.ebx >= 0x400:
                            eax = self.ebx & 0x3FF; self.cf=(self.ebx>>9)&1; self.ebx >>= 10
                        else:
                            eax = self.get8(0) & 0xFF
                            eax = self.bit_acc(eax)
                        for k in (4, 3, 2, 1):
                            self.put(self.edi + k, self.get(self.edi + eax + k))
                    else:
                        # 3-byte
                        self.edi -= 3
                        if self.ebx >= 0x200:
                            eax = self.ebx & 0x1FF; self.cf=(self.ebx>>8)&1; self.ebx >>= 9
                        else:
                            eax = self.get8(0) & 0xFF
                            eax = self.bit_acc(eax)
                        for k in (3, 2, 1):
                            self.put(self.edi + k, self.get(self.edi + eax + k))
                else:
                    b3 = self.shift_out()
                    if b3 == 0:
                        # 0x5584b long match: len=get8+5, dist 12-bit fast or get8+1bit
                        eax = self.get8(0) & 0xFF
                        ecx = eax + 5
                        if self.ebx >= 0x1000:
                            dist = self.ebx & 0xFFF; self.cf=(self.ebx>>11)&1; self.ebx >>= 12
                        else:
                            d8 = self.get8(0) & 0xFF
                            dist = self.bit_acc(d8)
                        # rep movsb downward: [edi+k] = [edi+dist+k]? moves ECX bytes from EDI+dist
                        for k in range(ecx):
                            self.put(self.edi - k, self.get(self.edi + dist - k))
                        self.edi -= ecx
                    else:
                        # long literal run: ECX=8; acc0=0; get8; ECX+=... writes ECX+9? mirror 0x557e9
                        eax = self.get8(0) & 0xFF
                        ecx = 8 + eax
                        while True:
                            self.put(self.edi, self.get8(0) & 0xFF); self.edi -= 1
                            ecx -= 1
                            if ecx < 0: break
                            while True:
                                self.put(self.edi, self.get8(0) & 0xFF); self.edi -= 1
                                ecx -= 1
                                if ecx >= 0: continue
                                break
                            break
        return self.edi

    def get(self, addr):
        o = addr - self.dst + self.PAD
        if 0 <= o < len(self.out): return self.out[o]
        return 0
    def put(self, addr, v):
        o = addr - self.dst + self.PAD
        if not (0 <= o < len(self.out)): raise RuntimeError(f'write far out {addr:#x}')
        self.out[o] = v & 0xFF
        if addr > self.hi_water: self.hi_water = addr
        if addr < self.lo_water: self.lo_water = addr

if __name__ == '__main__':
    img = open('/home/gill/bleem/demo-v1_4.EXE','rb').read()
    # stream e2: [0x10a4e, 0x55338) -> dst 0x5a244
    e = Engine(img, 0, 0x10a4e, 0x5a244)
    print('edi_start', hex(e.edi), 'esi_start', hex(e.esi))
    try:
        end = e.run()
        print('done, edi=', hex(end), 'wrote [', hex(e.lo_water), ',', hex(e.hi_water), ')')
        ln = e.hi_water - e.ebp
        fold = bleem_rand_fold(e.out[0:ln], 0x5a244)
        print('len', hex(ln), 'fold', hex(fold), 'expect 0x4d709eca')
    except Exception as ex:
        import traceback; traceback.print_exc()
