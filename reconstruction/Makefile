# bleem! demo v1.4 重建工程
#
# 交叉编译器: ~/.local/mingw 下的 Ubuntu mingw-w64 (无 root 解包安装)。
# 系统若装有 gcc-mingw-w64 也可直接 make CC=i686-w64-mingw32-gcc。

SYSROOT   := $(HOME)/.local/mingw/usr
CC        ?= $(SYSROOT)/bin/i686-w64-mingw32-gcc-win32
MINGW_SYS := $(SYSROOT)

CFLAGS := -m32 -Os -Wall -Wextra -Wno-unused-variable -std=gnu11 \
          -Iinclude -Isrc \
          -B$(MINGW_SYS)/lib/gcc/i686-w64-mingw32/13-win32 \
          -isystem $(MINGW_SYS)/i686-w64-mingw32/include \
          --sysroot=$(MINGW_SYS)
LDFLAGS_BOOT := -nostdlib -nostartfiles -e _bleem_start \
                -Wl,--subsystem,windows,--image-base,0x400000 \
                -L$(MINGW_SYS)/i686-w64-mingw32/lib \
                -L$(MINGW_SYS)/lib/gcc/i686-w64-mingw32/13-win32
LIBS := -lkernel32 -luser32

BUILD := build
OBJS_REF := $(BUILD)/stub_entry.o $(BUILD)/lzss.o $(BUILD)/modules.o

all: $(BUILD)/demo_rewrite.exe $(OBJS_REF)

$(BUILD):
	mkdir -p $(BUILD)

# 引导桩: 与原 FUN_00401021 等价的独立 PE (不含 CRT)
$(BUILD)/demo_rewrite.exe: src/loader.c include/bleem.h | $(BUILD)
	$(CC) $(CFLAGS) $< $(LDFLAGS_BOOT) $(LIBS) -o $@
	@file $@

# 参考重建模块 (桩/解压器/模块加载器) —— 编译验证
$(BUILD)/%.o: src/%.c include/bleem.h src/internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# 完整链路实验: 重建 loader + 原版追加 payload (布局对照用, 见 README)
payload: $(BUILD)/demo_rewrite.exe data/payload.bin
	python3 tools/append_payload.py $(BUILD)/demo_rewrite.exe \
	        data/payload.bin $(BUILD)/demo_relinked.exe

data/payload.bin:
	python3 tools/extract_payload.py ../demo-v1_4.EXE data/payload.bin data/stub.bin

clean:
	rm -rf $(BUILD)

.PHONY: all clean payload
