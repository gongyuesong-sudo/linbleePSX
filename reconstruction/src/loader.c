/*
 * loader.c —— demo-v1_4.EXE 引导桩的忠实重建
 *
 * 证据 (REA/Ghidra, 地址均为原镜像 VA):
 *   0x401000 entry        : 2 字节 jmp -> 0x401021 (thunk)
 *   0x401021 FUN_00401021 : 引导主体 (完整反汇编见 analysis/fn_401021.json)
 *
 * 原指令流与对应关系:
 *   GetVersionExA(&g_osvi)                  ; g_osvi.dwOSVersionInfoSize 预置 148 (0x401134)
 *   要求 dwPlatformId == VER_PLATFORM_WIN32_WINDOWS(1)  ; 0x401144 == 1
 *   两次 DEC 的“计数器” g_error_code (初值 2, 0x401124):
 *     平台 OK -> 1 ; MapViewOfFile OK -> 0 ; 然后 CALL payload(view+0x55338),
 *     把返回的 EAX 写回 g_error_code。任何失败路径带着非零计数进入错误框。
 *   错误文本 = [g_error_code*4 + 0x4011C4]  (0x4011C8..0x4011DC 六个指针)
 *   MessageBoxA(NULL, msg, "bleem, llc.", MB_OK|MB_ICONHAND|MB_TASKMODAL)
 *   ExitProcess(0)  —— 无论成败, 恒以 0 退出。
 */
#include "bleem.h"

/* ---- 原数据段全局 (.text 内联数据, 地址注释保留原 VA) ---------------- */

static LONG    g_error_code = BLEEM_ERR_INITIAL;      /* 0x401124, 初值=2 */
static HANDLE  g_hFile      = INVALID_HANDLE_VALUE;   /* 0x401128          */
static HANDLE  g_hMap       = NULL;                   /* 0x40112C          */
static LPVOID  g_pView      = NULL;                   /* 0x401130          */

static OSVERSIONINFOA g_osvi = { sizeof(OSVERSIONINFOA) }; /* 0x401134 (cb=148) */

/* 0x4011E0: 自身路径缓冲 (GetModuleFileNameA, 0x104) */
static CHAR g_module_path[MAX_PATH];

/* 错误消息表 —— 原表 [0] 槽为 NULL (0x4011C4), [1..6] 见下。 */
static const char *const g_error_table[7] = {
    NULL,                    /* 0x4011C4: 空槽, 原码 0 永不查表 */
    BLEEM_MSG_GENERIC,       /* 0x4011C8  code=1 */
    BLEEM_MSG_NOT_WIN9X,     /* 0x4011CC  code=2 */
    BLEEM_MSG_NOMEM,         /* 0x4011D0  code=3 */
    BLEEM_MSG_REINSTALL,     /* 0x4011D4  code=4 */
    BLEEM_MSG_SYSDEBUGGER,   /* 0x4011D8  code=5 */
    BLEEM_MSG_DEBUGGER,      /* 0x4011DC  code=6 */
};

/* 作者彩蛋字符串: 原版直接排在 entry 的 jmp 之后 (0x401002)。
 * 保留同名符号; 是否落进 .text 取决于链接布局, 语义仅为静态署名。 */
static const char bleem_credit[] = BLEEM_CREDIT_TEXT;

/* ---- payload 桩调用 (原: LEA EDX,[EAX+0x55338]; CALL EDX) ------------ */
/*
 * ABI 观察: 调用时 EAX=映射视图基址 (LEA 之前 EAX 未被破坏),
 * 返回后 EAX 被写回 g_error_code —— 即返回值在 EAX。
 * 除 EAX/EDX 外调用约定未知 (原码无 stdcall 清栈: 直接 CALL EDX,
 * 之后顺序执行), 重建为 cdecl 风格裸调用并声明 caller-saved 全毁。
 */
static int call_payload_stub(LPVOID view)
{
    void *target = (char *)view + PAYLOAD_STUB_FILE_OFF;
    register void *edx_target __asm__("edx") = target;
    register void *eax_view   __asm__("eax") = view;
    register int   eax_ret    __asm__("eax");
    __asm__ __volatile__ (
        "call *%%edx"
        : "=a" (eax_ret)
        : "0" (eax_view), "d" (edx_target)
        : "cc", "memory", "ebx", "ecx", "esi", "edi"
    );
    (void)bleem_credit;
    return eax_ret;
}

/* ---- 主体 (原 FUN_00401021 @0x401021) -------------------------------- */
static void bleem_boot(void)
{
    /* 0x401021: GetVersionExA; 平台必须是 Win9x (VER_PLATFORM_WIN32_WINDOWS) */
    if (GetVersionExA(&g_osvi) && g_osvi.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS) {
        --g_error_code;                                   /* 0x401041 DEC */

        if (GetModuleFileNameA(NULL, g_module_path, MAX_PATH)) {  /* 0x401053 */
            g_hFile = CreateFileA(g_module_path,          /* 0x401077      */
                        GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (g_hFile != INVALID_HANDLE_VALUE) {
                g_hMap = CreateFileMappingA(g_hFile, NULL, /* 0x401091     */
                        PAGE_READONLY, 0, 0, NULL);   /* 原 push 0x2 */
                if (g_hMap) {
                    g_pView = MapViewOfFile(g_hMap,        /* 0x4010A8     */
                        FILE_MAP_READ, 0, 0, 0);
                    if (g_pView) {
                        --g_error_code;                   /* 0x4010B6 DEC  */
                        /* 0x4010BC/0x4010C2: CALL view+0x55338, EAX=view  */
                        /* 原码: DEC 已使计数=0, CALL 保留标志位, 随后
                         * JNZ+MOV —— 成功路径上恒把返回值写回计数。      */
                        g_error_code = call_payload_stub(g_pView);

                        UnmapViewOfFile(g_pView);         /* 0x4010D1     */
                    }
                    CloseHandle(g_hMap);                  /* 0x4010DC     */
                }
                CloseHandle(g_hFile);                     /* 0x4010E7     */
            }
        }
    }

    if (g_error_code != 0) {                              /* 0x4010EC     */
        MessageBeep(MB_ICONHAND);                         /* 0x4010FA     */
        MessageBoxA(NULL, g_error_table[g_error_code],
                    BLEEM_TITLE_TEXT, BLEEM_MSGBOX_STYLE);/* 0x401117     */
    }
    ExitProcess(0);                                       /* 0x40111E     */
}

/* 原 entry (0x401000): jmp FUN_00401021。
 * 重建提供裸入口 _bleem_start (Makefile 里 -e _bleem_start, -nostdlib)。 */
#if defined(__GNUC__) && defined(__i386__)
__attribute__((noreturn))
void _bleem_start(void)
{
    bleem_boot();
    for (;;) { }   /* 理论不可达: bleem_boot 以 ExitProcess 收尾 */
}
#else
int WINAPI WinMain(HINSTANCE h, HINSTANCE p, LPSTR c, int n)
{
    (void)h; (void)p; (void)c; (void)n;
    bleem_boot();
    return 0;
}
#endif
