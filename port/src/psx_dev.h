/*
 * psx_dev.h —— PSX 薄引擎楔4: 设备寄存器堆 + MMIO 钩接 (task-110)
 *
 * 语义金标准 = 本仓库已定案真值资产:
 *   docs/57 —— I_STAT 写 AND 清 / EVTGEN 每 64 次 I_STAT 读翻转置位 /
 *              GPU STATUS(0x1F801810) 读置 0xB0000000
 *   docs/62 —— C2 私有窗: 写 0x1F801044 + 状态读回 0x1F80104A(lhu) 双相位
 *              bit0/bit1 握手(最小状态机化, 字节级对拍=楔5)
 *   docs/63 —— Timer 0x1100/1110/1120/1130 读推进(常数 0x400 = 原测速实证值)
 *   docs/39/41 —— GPU 端口族 0x1F801800 系 + rootcall 0x600/0x674 契约地址
 */
#ifndef PSX_DEV_H
#define PSX_DEV_H

#include <stdint.h>

typedef struct psx_dev_s {
    /* IRQ (docs/57) */
    uint32_t i_mask;            /* 0x1F801074 */
    uint32_t vframes;           /* task-135 帧节拍注入计数 */
    uint32_t sysvec[0x20];      /* InstallHandler 向量表 (syscall code 0x20-0x3F) */
    uint32_t i_stat;            /* 0x1F801070 写 AND 清 */
    uint32_t istat_reads;       /* EVTGEN 节拍计数 */
    /* 楔8b-① CDROM 驱动器最小面(0x1F801820-27): 上电复位 ACK + 命令 state machine
     * 清洁房楔3 (task-130) 升级: 盘在位响应 FIFO(8) + 命令族 + ReadN 帧缓冲 */
    uint8_t  cd_out[4];         /* CDREAD 数据出口(读消费清零) */
    uint32_t cd_cmd;            /* 最近命令 */
    uint32_t cd_cmds;           /* 命令计数(观测/自测) */
    void    *disc;              /* psx_disc_t* (attach; NULL=无盘面原样) */
    uint8_t  cd_resp[8];        /* 响应 FIFO (含首字节 ACK 0x11) */
    int      cd_resp_n, cd_resp_rp;
    uint8_t  cd_param[5];       /* param0(=命令写入字节高 8 位) + param1..4(0x1824-27) */
    uint32_t cd_loc;            /* SetLoc/Seek 目标 (24-bit 域内 LBA) */
    uint8_t  cd_mode;           /* SetMode */
    uint8_t  cd_sect[2048];     /* ReadN 首扇帧缓冲 (DMA 接线供 task-131) */
    int      cd_sect_valid;
    uint32_t cd_bytes;          /* 最近 ReadN 装载字节数 */
    uint32_t ram_size;          /* 0x1F801060 RAM_SIZE(楔6d: BIOS 写后回读) */
    /* Timer (docs/63): cnt=读推进模型 M1; ctrl 常规 R/W */
    uint32_t timer_cnt[4];      /* 0x1100/1110/1120/1130 */
    uint32_t timer_ctrl[4];     /* 0x1104/1114/1124/1134 */
    uint32_t timer_target[4];   /* 0x1108/1118/1128/1138 */
    /* C2 私有握手窗 (docs/62) */
    uint32_t reg1044;
    uint32_t phase104a;         /* 双相位: 0→bit0, 1→bit1, 循环 */
    void    *gpu;               /* 清洁房楔5: psx_gpu_t* (GP0/GP1 口转发, NULL=观测面) */
    /* GPU (docs/39 端口族): GP0 FIFO 深 4 */
    uint32_t gp0_fifo[4];
    int      gp0_wp, gp0_rp, gp0_n;
    uint32_t gp1_last;
    /* rootcall 观测 (docs/41 契约) */
    uint32_t rc_calls, rc_last_cmd;
} psx_dev_t;

#define PSX_TIMER_ADV 0x400u    /* docs/63 H_TIMEROK 实证步长 */

void     psx_dev_init(psx_dev_t *d);
uint32_t psx_dev_read (psx_dev_t *d, uint32_t off /* = gpa-0x1F800000 */, int size);
void     psx_dev_write(psx_dev_t *d, uint32_t off, uint32_t val, int size);
/* 楔6c 战役 trace 面: MMIO 访问直方图(全局单实例), top-N 打印+复位 */
void     psx_dev_trace_reset(void);
void     psx_dev_trace_dump(int topn);
/* main.c 子命令入口 (--dev-selftest): 0=PASS */
int      cmd_dev_selftest(int argc, char **argv);
/* 清洁房楔5: GPU 附着 (GP0/GP1 口转发) */
void     psx_dev_gpu_attach(void *dev, void *gpu);
/* 帧节拍注入 (task-135): 真机 VBlank 源 = I_STAT bit0 置位 (IRQ 链走 irq_check)
 * + 四路 33.87MHz/4 计数器粗步长推进 (v1 仅根节拍, 精确分频后续楔) */
void     psx_dev_vframe(psx_dev_t *d);
/* 清洁房楔3: 盘装载附着 + 集成探针 (psx_cdrom selftest 消费; 0=PASS) */
void     psx_dev_cd_attach(psx_dev_t *d, void *disc /* psx_disc_t* */);
int      psx_cd_dev_integration_probe(void);

#endif /* PSX_DEV_H */
