#!/usr/bin/env python3
"""
append_payload.py —— 把原版追加 payload 拼接到重建 EXE 尾部,
复现“自身映射 + 文件偏移当入口”的完整链路。

重建 loader 以 [自身文件偏移 0x55338] 为桩入口 —— 只有当输出 PE 的
.raw 布局与原文件完全一致时该偏移才有意义, 因此本脚本仅用于
**布局对照实验** (docs/05_payload.md), 不产出可发布二进制。
"""
import sys

def main():
    exe_in, payload, exe_out = sys.argv[1], sys.argv[2], sys.argv[3]
    blob = open(exe_in, 'rb').read() + open(payload, 'rb').read()
    open(exe_out, 'wb').write(blob)
    print(f'{exe_out}: {len(blob)} bytes')

if __name__ == '__main__':
    main()
