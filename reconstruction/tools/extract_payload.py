#!/usr/bin/env python3
"""
extract_payload.py —— 从原 demo-v1_4.EXE 提取尾部追加 payload 与桩。

布局证据 (手工 PE 头解析 + REA binary_overview, 见 docs/01_overview.md):
  最后段 .reloc 结束于文件偏移 0xFA00;
  追加区 [0xFA00, EOF) 共 287,732 字节 (压缩/加密);
  其中明文运行时桩位于 [0x55338, EOF)。

用法:
  python3 extract_payload.py <exe> data/payload.bin data/stub.bin
"""
import sys

APPEND_START = 0xFA00
STUB_START   = 0x55338

def main():
    exe, out_payload, out_stub = sys.argv[1], sys.argv[2], sys.argv[3]
    d = open(exe, 'rb').read()
    payload = d[APPEND_START:]
    stub    = d[STUB_START:]
    assert len(payload) == 287732, f'unexpected appended size {len(payload)}'
    open(out_payload, 'wb').write(payload)
    open(out_stub,    'wb').write(stub)
    print(f'payload.bin {len(payload)} bytes (file off 0x{APPEND_START:X})')
    print(f'stub.bin    {len(stub)} bytes (file off 0x{STUB_START:X})')

if __name__ == '__main__':
    main()
