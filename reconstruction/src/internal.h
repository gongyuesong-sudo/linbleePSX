/*
 * internal.h —— 重建工程内部共享的桩辅助 (对应原桩函数)
 */
#ifndef BLEEM_INTERNAL_H
#define BLEEM_INTERNAL_H

#include "bleem.h"

/* API 间接派发表: image + 0x4022A4 处指针所指函数 (见 0x55716 等) */
typedef int (__cdecl *bleem_api_dispatch_t)(DWORD first, ...);

static __inline bleem_api_dispatch_t bleem_api_table_at(const char *image)
{
    return *(bleem_api_dispatch_t *)(image + BLEEM_API_TABLE_OFF);
}

/* 模块描述符: 视图内块表按 id*0x10 寻址 (块表基址在视图内偏移
 * [未知] —— 原码经 EDI 上下文携带; 重建以 0 为占位并集中在此处,
 * 动态确认解密 payload 后只需改这一个函数)。 */
static __inline DWORD *bleem_module_desc(const char *image, DWORD id)
{
    extern DWORD g_desc_table_rva;      /* 定义见 modules.c 尾部 */
    return (DWORD *)(image + g_desc_table_rva + id * BLEEM_DESC_STRIDE);
}

#endif
