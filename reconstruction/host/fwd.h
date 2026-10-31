/* fwd.h — H-25 批1 转发器 API（task-064, SESSION-73）
 * 在真机金标 RAM 终态上, 按 BIOS rootcall 真 ABI 直调 BIOS 服务。
 * 语义与 docs/48/49 探针一致; docs/49 的 26 个可转发核服务应返回 0。 */
#ifndef BLEEM_FWD_H
#define BLEEM_FWD_H
#include <stdint.h>

/* 初始化: JIT + ROM + GOLDEN_RAM 整幅。返回 0 成功。bios/golden 为 NULL 用默认路径。 */
int  fwd_init(const char *bios_path, const char *golden_ram_path);

/* 解析 vec(0xA0/0xB0/0xC0)+cmd 的 handler 入口(相对偏移语义)。0=成功。 */
int  fwd_entry(uint32_t vec, uint32_t cmd, uint32_t *pc);

/* 转发一次调用: a0..a3 入参(NULL=全零), v0 回读, budget=块预算(0=默认 200000)。
 * 返回: 0=服务干净返回(v0/blk 有效); 1=需设备语境/自旋超时; 2=表槽缺失; 3=未初始化。 */
int  fwd_call(uint32_t vec, uint32_t cmd, const uint32_t a0a3[4],
              uint32_t *v0, uint32_t budget, uint32_t *blocks_out);

#endif
