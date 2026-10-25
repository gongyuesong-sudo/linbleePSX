/*
 * lzss.c —— payload 桩解压器 (FUN_0005577a + 位读取器群) 的逐支重建
 *
 * 证据: analysis/stub_pseudocode.txt 0x5577a/0x55960/0x55986/0x5598c/
 *       0x55992/0x55998; 位读取器的寄存器语义 (EBX=位缓冲 LSB 先出,
 *       ESI=dword 源向低地址回退, 结果 acc=acc*2|bit) 为高置信观察。
 *
 * 输出方向观察 [高置信]: dst 游标从高端向低端写 (puVar8 + -1/-2/-3/-4,
 *   "if (puVar8 < base) return") —— 就地低址展开的典型形态。
 *
 * 令牌结构 [按原伪代码逐支转写; 置信度见各分支]:
 *   控制位=1 且下一层=1 ......... 长匹配:
 *        len4 = get4();  若 len4 != 0:  复制 (len4 [+5]) 字节, 距离
 *             <0x1000: 距离来自 8 位读 [中置信]
 *             >=0x1000: 距离 = (低12位), 高位续读, 长度 += 5 [中置信]
 *        len4 == 0 -> 回退到 2 字节匹配分支
 *   控制位=1 且下一层=0 ......... 4 字节匹配:
 *        dist<0x400 时距离由 2 位续读表 [中置信], 否则取低 10 位、余量>>10
 *   控制位=0 且下一层=1 ......... 3 字节匹配:
 *        dist<0x200 时距离由 1 位续读 [中置信], 否则取低 9 位、余量>>9
 *   控制位=0 且下一层=0 ......... 字面量串:
 *        cnt3 = get3(); 随后 (cnt3+?) 次 get8() 逐字节直写 [中置信]
 *   另有 2 字节匹配: 8 位距离直读 [中置信]
 *
 * [重要] 精确的控制位嵌套顺序只能靠运行明文桩确认; 本文件保留原伪代
 * 码的分支骨架 (LAB 标签对应关系在注释), 供与未来动态 dump 对照。
 */
#include "bleem.h"

/* 与伪代码变量的对应:
 * unaff_EDI = out_base ; *unaff_ESI = out_top_off ; ((int)ESI + ESI[1]) = bits0 */
typedef struct {
    BYTE  *out;          /* unaff_EDI: 输出缓冲基址                     */
    BYTE  *dst;          /* puVar8: 当前写点 (向下生长)                 */
    bleem_bitreader r;   /* EBX/EAX/ESI 位流状态                        */
} bleem_lz;

/* 距离+复制原语 (对应三处 puVar9 = dst - N; dst[i] = puVar9[dist+i]) */
static __inline void bleem_lz_copy(bleem_lz *L, DWORD dist, int n)
{
    const BYTE *src = L->dst - n + dist;   /* 原码: puVar9=dst-N; src=puVar9[dist+k-N] */
    while (n-- > 0) *L->dst-- = *src++;
    L->dst += 1; /* 维持原游标方向 (原码最终停在 puVar9) */
}

/*
 * FUN_0005577a 主体转写。
 * 伪代码骨架 (LAB_0005578b 主循环 / LAB_000557de 字面量串 /
 * LAB_000558cf 3字节匹配 / LAB_00055836 4字节匹配 / LAB_000557f0 串写出):
 */
void bleem_lz_decompress(bleem_lz *L, DWORD out_start_off, const DWORD *bit_dwords)
{
    L->dst = L->out + out_start_off;        /* puVar8 = ESI[0] + EDI      */
    bleem_br_init(&L->r, bit_dwords);       /* uVar6 = 首 dword           */

    for (;;) {
        if (L->dst < L->out) return;        /* "if (puVar8 < unaff_EDI) return" */

        DWORD b0 = bleem_br_get1(&L->r);    /* 控制位 uVar6&1             */

        if (!b0) {
            DWORD b1 = bleem_br_get1(&L->r);
            if (!b1) {
                /* ---- 字面量串 (LAB_000557de -> FUN_0005598c=get3, 然后
                 *      0x55960 get8 逐字节写出, 循环变量 extraout_ECX) ---- */
                int n = (int)bleem_br_getn(&L->r, 3);
                do {
                    *L->dst-- = (BYTE)bleem_br_get8(&L->r);
                } while (n-- > 0);
            } else {
                /* ---- 3 字节匹配 (LAB_000558cf): 距离 9 位窗口 ---- */
                DWORD d9 = bleem_br_getn(&L->r, 9);   /* uVar6 < 0x200 续读 [中置信] */
                bleem_lz_copy(L, d9, 3);
            }
        } else {
            DWORD b1 = bleem_br_get1(&L->r);
            if (b1) {
                DWORD b2 = bleem_br_get1(&L->r);
                if (!b2) {
                    /* ---- 4 字节匹配: 距离 10 位窗口 (<0x400 续读) ---- */
                    DWORD d10 = bleem_br_getn(&L->r, 10);
                    bleem_lz_copy(L, d10, 4);
                } else {
                    /* ---- 长匹配: 4 位长度, 距离编码两档 ---- */
                    DWORD len4 = bleem_br_getn(&L->r, 4);
                    if (len4 == 0) {
                        /* 回退: 2 字节匹配, 距离 8 位 (LAB 汇合) */
                        DWORD d8 = bleem_br_get8(&L->r);
                        bleem_lz_copy(L, d8, 2);
                    } else if (len4 < 0x1000) {
                        /* 距离 4 位续读 + 8 位 [中置信: 原码此处
                         * FUN_00055960(get8) + FUN_00055986(get4)] */
                        (void)bleem_br_get8(&L->r);
                        DWORD dl = bleem_br_getn(&L->r, 4);
                        bleem_lz_copy(L, dl, (int)len4);
                    } else {
                        /* 超长匹配: len = (v>>12), off = v&0xfff, 长度再 +5
                         * (原码 "iVar2 = iVar2 + 5") */
                        (void)bleem_br_get8(&L->r);
                        DWORD dl = bleem_br_getn(&L->r, 4);
                        bleem_lz_copy(L, dl, 5);
                    }
                }
            } else {
                /* b0=1,b1=0: 原码此处与 4 字节匹配路径汇合 (joined_r0x0005581d)
                 * —— 2 字节匹配, 8 位距离 */
                DWORD d8 = bleem_br_get8(&L->r);
                bleem_lz_copy(L, d8, 2);
            }
        }
    }
}
