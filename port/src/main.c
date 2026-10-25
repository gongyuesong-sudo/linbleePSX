/*
 * main.c —— bleem_port CLI 入口
 *
 * 用法:
 *   bleem_port                     引导校验 + 功能集横幅 (原生, 无 demo 限制)
 *   bleem_port --selftest [--driver x11|wayland]  Vulkan 窗口自检 (task-003)
 *   bleem_port --render-ui <out.ppm>              UI 渲染输出 (task-004)
 *   bleem_port --payload-selftest <payload.bin>   格式引擎报告 (task-005)
 */
#include "bleem_native.h"
#include <stdio.h>
#include <string.h>

#define DEFAULT_EXE "/home/gill/bleem/demo-v1_4.EXE"

static void print_policy(const bleem_features_t *f)
{
    printf("bleem! native port (Linux) —— full-feature policy, no demo limits:\n");
    printf("  sound=%d dynamic_sound=%d memcard=%d gpu_accel=vulkan:%d color_fmv=%d\n",
           f->sound, f->dynamic_sound, f->memcard, f->gpu_accel, f->color_fmv);
    printf("  time_limit=%ds (0=none)  debugger-checks=REMOVED (protection not ported)\n",
           f->time_limit_sec);
    printf("  display: X11 + Wayland backends, Vulkan presentation\n");
}

int main(int argc, char **argv)
{
    const char *exe = DEFAULT_EXE;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--exe") == 0 && i + 1 < argc) exe = argv[++i];
        else if (strcmp(argv[i], "--selftest") == 0)        return cmd_selftest(argc, argv);
        else if (strcmp(argv[i], "--render-ui") == 0)       return cmd_render_ui(argc, argv);
        else if (strcmp(argv[i], "--window") == 0)          return cmd_window(argc, argv);
        else if (strcmp(argv[i], "--payload-selftest") == 0) return cmd_payload_selftest(argc, argv);
    }

    bleem_image_t img;
    int rc = bleem_boot_open(&img, exe);
    if (rc != BLEEM_OK) { fprintf(stderr, "boot open failed: %d\n", rc); return 1; }
    rc = bleem_boot_verify(&img);
    if (rc != BLEEM_OK) { fprintf(stderr, "boot verify failed: %d\n", rc); bleem_boot_close(&img); return 1; }

    printf("boot: %s mapped %zu bytes; appended payload %llu B; stub @0x%llX magic OK\n",
           img.path, img.len, (unsigned long long)BLEEM_APPEND_SIZE,
           (unsigned long long)BLEEM_STUB_OFF);
    printf("boot: RAND fold over plaintext stub = 0x%08X (diagnostic)\n", img.rand_fold_stub);
    printf("boot: encrypted core at 0x%llX.. unreachable statically (native boundary)\n",
           (unsigned long long)BLEEM_MAIN_ENT_OFF);
    print_policy(&img.feat);
    bleem_boot_close(&img);
    return 0;
}
