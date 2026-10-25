#!/usr/bin/env python3
"""check_image.py —— 校验渲染输出: 尺寸正确、非单色、多区域有色内容"""
import sys

def main(path):
    with open(path, 'rb') as f:
        assert f.readline().strip() == b'P6', 'not P6'
        w, h = map(int, f.readline().split())
        assert f.readline().strip() == b'255'
        data = f.read()
    assert (w, h) == (960, 540), f'size {w}x{h}'
    assert len(data) == w * h * 3
    colors = set()
    for i in range(0, len(data), 3 * 97):
        colors.add((data[i], data[i + 1], data[i + 2]))
    assert len(colors) > 24, f'too flat: {len(colors)} colors'
    # 图标行区域 (y≈84..180) 应有非背景像素
    row = data[(120 * w + 40) * 3:(121 * w + 60) * 3]
    assert any(b > 0x90 for b in row), 'icon row appears empty'
    print(f'check_image OK: {w}x{h}, {len(colors)} sampled colors')
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
