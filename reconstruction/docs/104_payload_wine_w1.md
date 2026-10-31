# 104 — ③战役楔1：demo payload Wine 带盘首发侦察（task-123，SESSION-134）

战役分岔定案：③主战场回迁 demo payload（BLEEM 宿主在原生 Linux+Wine
跑通真实 boot 流）。本楔 = 侦察楔，零 port/rewrite 语义改动，四条战线
全部实跑取证：

## 1. PBP 盘资产判读（psgame/D 打击者1945 II SLPS-01637, 193MB）
- 头 0x00 `PBP\0`+ver1.0，size/offset 表字段错乱（变体容器）；
- `0x10000` = **PSISOIMG0000 明文描述符**、`0x10401` = `_SLPS_01637` 明文；
- **全文件扫描：零 CD001、零 PS-X EXE、零 SYSTEM.CNF/BOOT=、零 raw 扇区
  sync 模式** → 数据主体高熵（distinct=256）= **自制加密/压缩容器**，
  非标准 PBP/ISO/BIN，不可直接使用；7z 不识别；
- DuckStation 验证通道断：Qt xcb/wayland 插件缺失起不来（AppImage 环境面）。
→ 下一楔需要**标准 ISO/BIN+cue**（用户供给）或深逆该容器格式。

## 2. Wine 假 CD（e: → fakecd 目录 stub）实验
- BLEEM s3 strings 定案文件 API 探测面：`:\SYSTEM.CNF`、`:\PSX.EXE`、
  `BOOT`、`DiscID %010u`、`bleem! CD-ROM Settings`；
- stub 盘（SYSTEM.CNF + 合成 PS-X EXE 头）+ dosdevices/e: 软链；
- **strace 78k 行实跑判读：Wine 读了 `fakecd/.windows-label`（e: 可见），
  BLEEM 对 `SYSTEM.CNF` 零访问** —— 或 BLEEM 只认 DRIVE_CDROM 盘符，
  或未走到 CD 枚举（被对话框墙挡住，见 §4）。两判未分 = 下楔分辨。

## 3. 真 CD 型盘符路线 = 权限硬墙（入账）
xorriso -as mkisofs 造 370KB ISO ✓ + `udisksctl loop-setup` → /dev/loop68 ✓，
但 **/dev/loopN = root:disk 0660，用户 1000 不在 disk 组 → Wine openat
EACCES** → 无 root 环境该路线死。候选替代：systemd-run UserGroup=？（用户
unit 不可改属组）/ 找 udev 规则组 = 均出权限边界，封存。

## 4. 首发真实进度 = GUI 对话框墙（本楔最大战果）
- demo_patched 在 Wine 10.0 首发到达 **Win32 对话框**：窗口
  `"bleem, llc."` 422x256（mutter 外框），**非黑屏非 zombie** —— 战役
  首个 GUI 里程碑；
- 对话框对 Return 注入（--window 定向=无效，Win32 MessageBox 不吃外框
  合成事件；global XTEST 行为不稳）不关，进程 ~67s 自退（无崩溃日志）；
- 文本读取三通道全断：x11grab 在 wayland 合成下抓 X 区域=纯黑（mutter
  合成面外屏 +2348 屏区同样黑）；winedbg gdb 脚本无输出；WINEDEBUG
  +msgbox/+dialog 通道零行；
- s3 内候选文案：DirectX 6.0 要求 / 16-bit 色深提示（MessageBox 系）。
→ 下楔 ③W2 = **XTEST 鼠标点击坐标流**（windowmove 进主屏 + 按
MessageBox 按钮几何点位 click）+ `+win` 通道读窗口类，分辨对话框语义。

## 5. 环境坑入账
- `xdotool`（~/.local/bin）需 `LD_LIBRARY_PATH=/home/gill/.local/lib`
  （libxdo.so.3 在彼处），缺库时**静默失败输出空**——本楔曾把它当成功
  信号浪费三轮（教训：工具空输出=先验工具本身活着）。
