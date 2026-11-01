/*
 * vk_host.c —— 任务003: X11/Wayland 窗口 + Vulkan 显示 (无着色器合成路径)
 *
 * 设计: CPU 侧合成 RGBA8 → 线性 staging image (buffer→image copy) →
 *   vkCmdBlitImage(nearest) 到交换链图像 → present。
 *   零着色器依赖 => 无需 SPIR-V 工具链, 布局转换全部显式 barrier,
 *   在任何 ICD (asahi 真 GPU / lavapipe 软渲) 上行为确定。
 *
 * 原 demo 的 DirectDraw/Direct3D 面在端口中即由本层(Vulkan)替代,
 * 且按 FULL-FEATURE 策略直接支持硬件加速 (原 demo 禁用 D3D)。
 */
#include "bleem_native.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>

#define MAX_IMGS 8

typedef struct {
    SDL_Window   *win;
    VkInstance    inst;
    VkPhysicalDevice pd;
    VkDevice      dev;
    uint32_t      qfam;
    VkQueue       queue;
    VkSurfaceKHR  surf;
    VkSwapchainKHR sc;
    VkFormat      fmt;
    void         *hw;   /* 清洁房楔5: vk_hw_t* compute 扫描通道 */
    VkExtent2D    ext;
    VkImage       imgs[MAX_IMGS];
    VkImageView   views[MAX_IMGS];
    VkImageLayout lays[MAX_IMGS];
    uint32_t      n_imgs;
    VkCommandPool cp;
    VkCommandBuffer cb;
    VkSemaphore   sem_avail, sem_done;
    VkFence       fence;
    /* staging */
    VkImage       stg;
    VkDeviceMemory stg_mem;
    uint32_t      stg_w, stg_h;
    uint32_t      frame;
} vk_host;

static void vk_die(const char *what, VkResult r)
{
    fprintf(stderr, "vulkan %s failed: %d\n", what, (int)r);
}

#define VKTRY(call) do { VkResult r_ = (call); if (r_ != VK_SUCCESS) { vk_die(#call, r_); return -1; } } while (0)

static int find_queue(vk_host *h)
{
    uint32_t n = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(h->pd, &n, NULL);
    VkQueueFamilyProperties *p = calloc(n, sizeof(*p));
    vkGetPhysicalDeviceQueueFamilyProperties(h->pd, &n, p);
    int found = -1;
    for (uint32_t i = 0; i < n; i++) {
        VkBool32 ok = VK_FALSE;
        if (!(p[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
        vkGetPhysicalDeviceSurfaceSupportKHR(h->pd, i, h->surf, &ok);
        if (ok) { found = (int)i; break; }
    }
    free(p);
    if (found < 0) { fprintf(stderr, "no graphics+present queue\n"); return -1; }
    h->qfam = (uint32_t)found;
    return 0;
}

static int create_staging(vk_host *h, uint32_t w, uint32_t hgt)
{
    if (h->stg && h->stg_w == w && h->stg_h == hgt) return 0;
    if (h->stg) { vkDestroyImage(h->dev, h->stg, NULL); vkFreeMemory(h->dev, h->stg_mem, NULL); h->stg = NULL; }
    VkImageCreateInfo ii = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = { w, hgt, 1 }, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_LINEAR,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED };
    VKTRY(vkCreateImage(h->dev, &ii, NULL, &h->stg));
    VkMemoryRequirements mr; vkGetImageMemoryRequirements(h->dev, h->stg, &mr);
    VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mr.size,
        .memoryTypeIndex = (uint32_t)(ffs((int)(mr.memoryTypeBits &
            ((1u<<VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)|(1u<<VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)))) - 1) };
    /* 精确找类型: 上式仅为快捷写法, 遍历更稳 */
    VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(h->pd, &mp);
    ai.memoryTypeIndex = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < mp.memoryTypeCount && ai.memoryTypeIndex == 0xFFFFFFFFu; i++)
        if ((mr.memoryTypeBits >> i) & 1) {
            VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            if ((mp.memoryTypes[i].propertyFlags & want) == want) ai.memoryTypeIndex = i;
        }
    if (ai.memoryTypeIndex == 0xFFFFFFFFu) { fprintf(stderr, "no host-visible mem\n"); return -1; }
    VKTRY(vkAllocateMemory(h->dev, &ai, NULL, &h->stg_mem));
    VKTRY(vkBindImageMemory(h->dev, h->stg, h->stg_mem, 0));
    h->stg_w = w; h->stg_h = hgt;
    return 0;
}

static void barrier(VkCommandBuffer cb, VkImage img, VkImageLayout from, VkImageLayout to)
{
    VkImageMemoryBarrier b = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = from == VK_IMAGE_LAYOUT_PREINITIALIZED ? 0 : VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = to == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL || to == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
                         ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_MEMORY_READ_BIT,
        .oldLayout = from, .newLayout = to, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = img,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    if (to == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    if (to == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) b.dstAccessMask = 0; /* 交给 present */
    VkPipelineStageFlags s = from == VK_IMAGE_LAYOUT_PREINITIALIZED ? VK_PIPELINE_STAGE_HOST_BIT
                             : VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkPipelineStageFlags d = (to == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT
                             : VK_PIPELINE_STAGE_TRANSFER_BIT;
    vkCmdPipelineBarrier(cb, s, d, 0, 0, NULL, 0, NULL, 1, &b);
}

int vk_host_present(vk_host *h, const uint32_t *rgba, int w, int hgt)
{
    if (create_staging(h, (uint32_t)w, (uint32_t)hgt) != 0) return -1;
    void *px;
    VkSubresourceLayout sl;
    VkImageSubresource sub = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
    vkGetImageSubresourceLayout(h->dev, h->stg, &sub, &sl);
    if (vkMapMemory(h->dev, h->stg_mem, 0, sl.size, 0, &px) != VK_SUCCESS) return -1;
    for (int y = 0; y < hgt; y++)
        memcpy((uint8_t*)px + sl.rowPitch * (size_t)y, rgba + (size_t)y * (size_t)w, (size_t)w * 4);
    vkUnmapMemory(h->dev, h->stg_mem);

    VKTRY(vkWaitForFences(h->dev, 1, &h->fence, VK_TRUE, UINT64_MAX));
    vkResetFences(h->dev, 1, &h->fence);
    vkResetCommandBuffer(h->cb, 0);

    uint32_t idx = 0;
    VkResult ar = vkAcquireNextImageKHR(h->dev, h->sc, UINT64_MAX, h->sem_avail, NULL, &idx);
    if (ar == VK_ERROR_OUT_OF_DATE_KHR) return 0;   /* 简化: 视为跳过帧 */
    if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) { vk_die("acquire", ar); return -1; }

    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    VKTRY(vkBeginCommandBuffer(h->cb, &bi));

    /* staging: PREINITIALIZED -> TRANSFER_DST -> copy -> TRANSFER_SRC */
    barrier(h->cb, h->stg, VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy region = { .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageExtent = { (uint32_t)w, (uint32_t)hgt, 1 } };
    /* 直接用 host 内存不可 copy; 走 buffer→image 需 VkBuffer。改为 image→image:
     * staging 已含数据, 只需转 TRANSFER_SRC。 */
    (void)region;
    barrier(h->cb, h->stg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    /* swapchain 图像: UNDEFINED/PRESENT -> TRANSFER_DST */
    barrier(h->cb, h->imgs[idx], h->lays[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageBlit bl = { .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .srcOffsets = { {0,0,0}, {(int32_t)w, (int32_t)hgt, 1} },
        .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .dstOffsets = { {0,0,0}, {(int32_t)h->ext.width, (int32_t)h->ext.height, 1} } };
    vkCmdBlitImage(h->cb, h->stg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   h->imgs[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bl, VK_FILTER_NEAREST);
    barrier(h->cb, h->imgs[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    h->lays[idx] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VKTRY(vkEndCommandBuffer(h->cb));

    VkPipelineStageFlags wait = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1, .pWaitSemaphores = &h->sem_avail,
        .pWaitDstStageMask = &wait, .commandBufferCount = 1, .pCommandBuffers = &h->cb,
        .signalSemaphoreCount = 1, .pSignalSemaphores = &h->sem_done };
    VKTRY(vkQueueSubmit(h->queue, 1, &si, h->fence));
    VkPresentInfoKHR pi = { .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1, .pWaitSemaphores = &h->sem_done,
        .swapchainCount = 1, .pSwapchains = &h->sc, .pImageIndices = &idx };
    vkQueuePresentKHR(h->queue, &pi);
    h->frame++;
    return 0;
}

int vk_host_init(vk_host *h, int width, int height, const char *driver, const char *title)
{
    memset(h, 0, sizeof(*h));
    if (driver && *driver && strcmp(driver, "auto") != 0) setenv("SDL_VIDEODRIVER", driver, 1);
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return -1; }
    if (!h->win) {
        Uint32 flags = SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN;
        if (driver && strcmp(driver, "x11") == 0) flags |= SDL_WINDOW_ALLOW_HIGHDPI;
        h->win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  width, height, flags);
    }
    if (!h->win) { fprintf(stderr, "CreateWindow: %s\n", SDL_GetError()); return -1; }

    unsigned cnt = 0; SDL_Vulkan_GetInstanceExtensions(h->win, &cnt, NULL);
    const char **exts = calloc(cnt + 1, sizeof(char*));
    SDL_Vulkan_GetInstanceExtensions(h->win, &cnt, exts);
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "bleem_port", .apiVersion = VK_API_VERSION_1_0 };
    VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app, .enabledExtensionCount = cnt, .ppEnabledExtensionNames = exts };
    if (vkCreateInstance(&ici, NULL, &h->inst) != VK_SUCCESS) { fprintf(stderr, "vkCreateInstance fail\n"); return -1; }
    free((void*)exts);

    uint32_t nd = 0; vkEnumeratePhysicalDevices(h->inst, &nd, NULL);
    if (!nd) { fprintf(stderr, "no vulkan device\n"); return -1; }
    VkPhysicalDevice *devs = calloc(nd, sizeof(*devs));
    vkEnumeratePhysicalDevices(h->inst, &nd, devs);
    h->pd = devs[0];
    for (uint32_t i = 0; i < nd; i++) {
        VkPhysicalDeviceProperties pp; vkGetPhysicalDeviceProperties(devs[i], &pp);
        if (pp.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
            pp.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) { h->pd = devs[i]; break; }
    }
    free(devs);

    if (SDL_Vulkan_CreateSurface(h->win, h->inst, &h->surf) != SDL_TRUE) { fprintf(stderr, "CreateSurface\n"); return -1; }
    if (find_queue(h) != 0) return -1;
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qi = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = h->qfam, .queueCount = 1, .pQueuePriorities = &prio };
    const char *dexts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo di = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &qi,
        .enabledExtensionCount = 1, .ppEnabledExtensionNames = dexts };
    VKTRY(vkCreateDevice(h->pd, &di, NULL, &h->dev));
    vkGetDeviceQueue(h->dev, h->qfam, 0, &h->queue);

    VkSurfaceCapabilitiesKHR caps; vkGetPhysicalDeviceSurfaceCapabilitiesKHR(h->pd, h->surf, &caps);
    uint32_t nf = 0; vkGetPhysicalDeviceSurfaceFormatsKHR(h->pd, h->surf, &nf, NULL);
    VkSurfaceFormatKHR *fs = calloc(nf, sizeof(*fs));
    vkGetPhysicalDeviceSurfaceFormatsKHR(h->pd, h->surf, &nf, fs);
    h->fmt = fs[0].format;
    for (uint32_t i = 0; i < nf; i++)
        if (fs[i].format == VK_FORMAT_B8G8R8A8_UNORM) { h->fmt = fs[i].format; break; }
    free(fs);

    h->ext = caps.currentExtent;
    if (h->ext.width == UINT32_MAX || h->ext.width == 0) { h->ext.width = (uint32_t)width; h->ext.height = (uint32_t)height; }
    uint32_t imgc = caps.minImageCount + 1;
    if (caps.maxImageCount && imgc > caps.maxImageCount) imgc = caps.maxImageCount;
    VkSwapchainCreateInfoKHR sci = { .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = h->surf, .minImageCount = imgc, .imageFormat = h->fmt,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, .imageExtent = h->ext,
        .imageArrayLayers = 1, .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform, .compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR, .clipped = VK_TRUE, .oldSwapchain = NULL };
    VKTRY(vkCreateSwapchainKHR(h->dev, &sci, NULL, &h->sc));
    vkGetSwapchainImagesKHR(h->dev, h->sc, &h->n_imgs, NULL);
    if (h->n_imgs > MAX_IMGS) h->n_imgs = MAX_IMGS;
    vkGetSwapchainImagesKHR(h->dev, h->sc, &h->n_imgs, h->imgs);
    for (uint32_t i = 0; i < h->n_imgs; i++) h->lays[i] = VK_IMAGE_LAYOUT_UNDEFINED;
    if (getenv("VK_EVIDENCE")) {   /* task-056 M-3: 呈现链标记实证 */
        VkPhysicalDeviceProperties pp; vkGetPhysicalDeviceProperties(h->pd, &pp);
        fprintf(stderr, "VK_EV drv=%s device=%s swapchain_imgs=%u fmt=%u extent=%ux%u\n",
                SDL_GetCurrentVideoDriver(), pp.deviceName, h->n_imgs, (unsigned)h->fmt,
                (unsigned)sci.imageExtent.width, (unsigned)sci.imageExtent.height);
        fflush(stderr);
    }

    VkCommandPoolCreateInfo cpi = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = h->qfam };
    VKTRY(vkCreateCommandPool(h->dev, &cpi, NULL, &h->cp));
    VkCommandBufferAllocateInfo cbai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = h->cp, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VKTRY(vkAllocateCommandBuffers(h->dev, &cbai, &h->cb));
    VkSemaphoreCreateInfo si0 = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fi0 = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT };
    VKTRY(vkCreateSemaphore(h->dev, &si0, NULL, &h->sem_avail));
    VKTRY(vkCreateSemaphore(h->dev, &si0, NULL, &h->sem_done));
    VKTRY(vkCreateFence(h->dev, &fi0, NULL, &h->fence));
    return 0;
}

void vk_host_shutdown(vk_host *h)
{
    if (h->dev) vkDeviceWaitIdle(h->dev);
    if (h->stg) { vkDestroyImage(h->dev, h->stg, NULL); vkFreeMemory(h->dev, h->stg_mem, NULL); }
    if (h->fence) vkDestroyFence(h->dev, h->fence, NULL);
    if (h->sem_avail) vkDestroySemaphore(h->dev, h->sem_avail, NULL);
    if (h->sem_done) vkDestroySemaphore(h->dev, h->sem_done, NULL);
    if (h->cp) vkDestroyCommandPool(h->dev, h->cp, NULL);
    if (h->sc) vkDestroySwapchainKHR(h->dev, h->sc, NULL);
    if (h->dev) vkDestroyDevice(h->dev, NULL);
    if (h->surf) vkDestroySurfaceKHR(h->inst, h->surf, NULL);
    if (h->inst) vkDestroyInstance(h->inst, NULL);
    if (h->win) SDL_DestroyWindow(h->win);
    SDL_Quit();
    memset(h, 0, sizeof(*h));
}

/* ---- CLI: --selftest [--driver x11|wayland] ---- */
int cmd_selftest(int argc, char **argv)
{
    const char *driver = "auto";
    for (int i = 1; i < argc - 1; i++)
        if (strcmp(argv[i], "--driver") == 0) driver = argv[i + 1];
    vk_host h;
    if (vk_host_init(&h, 640, 480, driver, "bleem! native port (selftest)") != 0) {
        fprintf(stderr, "selftest FAILED (driver=%s)\n", driver);
        return 1;
    }
    uint32_t fb[640 * 480];
    for (int f = 0; f < 3; f++) {
        for (int y = 0; y < 480; y++)
            for (int x = 0; x < 640; x++)
                fb[y * 640 + x] = 0xFF000000u | ((uint32_t)(x + f * 40) & 0xFF) |
                                  (((uint32_t)(y * 2) & 0xFF) << 8) | (0x80u << 16);
        if (vk_host_present(&h, fb, 640, 480) != 0) { vk_host_shutdown(&h); return 1; }
    }
    vk_host_shutdown(&h);
    printf("selftest OK (driver=%s, frames=3)\n", driver);
    (void)argc;
    return 0;
}

/* 供 resources.c: 把已合成的一帧 RGBA 呈现到真窗口 (auto 驱动) */
int vk_host_show_frame(const uint32_t *fb, int w, int hgt)
{
    vk_host hv;
    if (vk_host_init(&hv, w, hgt, "auto", "bleem! native port (ui)") != 0) return -1;
    int rc = vk_host_present(&hv, fb, w, hgt);
    SDL_Delay(150);              /* 给合成器一帧时间 */
    vk_host_shutdown(&hv);
    return rc;
}

/* ---- 常驻窗口会话 API (供 --window 交互模式) ---- */
void *vk_open(int w, int h, const char *title)
{
    vk_host *hv = calloc(1, sizeof(*hv));
    if (!hv) return NULL;
    if (vk_host_init(hv, w, h, "auto", title) != 0) { free(hv); return NULL; }
    return hv;
}
int vk_push(void *v, const uint32_t *fb, int w, int hgt)
{
    return vk_host_present((vk_host *)v, fb, w, hgt);
}
int vk_alive(void *v)
{
    vk_host *hv = (vk_host *)v;
    (void)hv;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) return 0;
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) return 0;
    }
    return 1;
}
void vk_close(void *v)
{
    if (!v) return;
    vk_host_shutdown((vk_host *)v);
    free(v);
}

/* ============ 清洁房楔5 (task-132): GPU 硬件扫描通道 ============
 * vk_host_present_gpu16: PSX VRAM(16bpp) 上载 device buffer → compute
 * (psx_gpu_scanout.comp: 5551→RGBA8, 零 CPU 逐像素转换/零读回) → buffer→image
 * copy → 既有 blit/present。CPU 侧仅一次 DMA 风格 memcpy 上载。 */
#include "psx_gpu_scanout_spv.h"

typedef struct {
    VkBuffer        inb, outb;        /* in=1MB VRAM, out=w*h*4 */
    VkDeviceMemory  inb_m, outb_m;
    void           *inb_map;
    VkImage         outimg;           /* R8G8B8A8 | B8G8R8A8 依 host.fmt */
    VkDeviceMemory  outimg_m;
    VkImageView     outview;
    VkShaderModule  mod;
    VkDescriptorSetLayout dsl;
    VkPipelineLayout        pl;
    VkDescriptorPool        dp;
    VkDescriptorSet         ds;
    VkPipeline              pipe;
    uint32_t out_w, out_h;
    int      ready;
} vk_hw_t;

static int hw_alloc_buf(vk_host *h, VkDeviceSize sz, VkBufferUsageFlags use,
                        VkBuffer *b, VkDeviceMemory *m, void **map)
{
    VkBufferCreateInfo bi = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sz, .usage = use, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    if (vkCreateBuffer(h->dev, &bi, NULL, b) != VK_SUCCESS) return -1;
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements(h->dev, *b, &mr);
    VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size };
    VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(h->pd, &mp);
    ai.memoryTypeIndex = 0xFFFFFFFFu;
    VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < mp.memoryTypeCount && ai.memoryTypeIndex == 0xFFFFFFFFu; i++)
        if ((mr.memoryTypeBits >> i) & 1 && (mp.memoryTypes[i].propertyFlags & want) == want) ai.memoryTypeIndex = i;
    if (ai.memoryTypeIndex == 0xFFFFFFFFu) return -1;
    if (vkAllocateMemory(h->dev, &ai, NULL, m) != VK_SUCCESS) return -1;
    if (vkBindBufferMemory(h->dev, *b, *m, 0) != VK_SUCCESS) return -1;
    if (map) { if (vkMapMemory(h->dev, *m, 0, sz, 0, map) != VK_SUCCESS) return -1; }
    return 0;
}

static int hw_init(vk_host *h)
{
    vk_hw_t *w = calloc(1, sizeof *w);
    if (!w) return -1;
    if (hw_alloc_buf(h, 1024u * 512u * 2u, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     &w->inb, &w->inb_m, &w->inb_map) != 0) { free(w); return -1; }
    VkShaderModuleCreateInfo mi = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof psx_scanout_spv, .pCode = psx_scanout_spv };
    if (vkCreateShaderModule(h->dev, &mi, NULL, &w->mod) != VK_SUCCESS) { free(w); return -1; }
    VkDescriptorSetLayoutBinding bnd[2] = {
        { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
        { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT } };
    VkDescriptorSetLayoutCreateInfo dli = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2, .pBindings = bnd };
    if (vkCreateDescriptorSetLayout(h->dev, &dli, NULL, &w->dsl) != VK_SUCCESS) { free(w); return -1; }
    VkPushConstantRange pc = { .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = 24 };
    VkPipelineLayoutCreateInfo pl = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &w->dsl, .pushConstantRangeCount = 1, .pPushConstantRanges = &pc };
    if (vkCreatePipelineLayout(h->dev, &pl, NULL, &w->pl) != VK_SUCCESS) { free(w); return -1; }
    VkComputePipelineCreateInfo cp = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                   .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = w->mod, .pName = "main" },
        .layout = w->pl };
    if (vkCreateComputePipelines(h->dev, NULL, 1, &cp, NULL, &w->pipe) != VK_SUCCESS) { free(w); return -1; }
    h->hw = w;
    return 0;
}

static int hw_ensure_out(vk_host *h, uint32_t w, uint32_t hh)
{
    vk_hw_t *hw = (vk_hw_t *)h->hw;
    if (hw->ready && hw->out_w == w && hw->out_h == hh) return 0;
    if (hw->outb) { vkDestroyBuffer(h->dev, hw->outb, NULL); vkFreeMemory(h->dev, hw->outb_m, NULL); hw->outb = 0; }
    if (hw->outimg) { vkDestroyImageView(h->dev, hw->outview, NULL); vkDestroyImage(h->dev, hw->outimg, NULL);
                      vkFreeMemory(h->dev, hw->outimg_m, NULL); hw->outimg = 0; }
    if (hw_alloc_buf(h, (VkDeviceSize)w * hh * 4u,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     &hw->outb, &hw->outb_m, NULL) != 0) return -1;
    VkImageCreateInfo ii = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = h->fmt, .extent = { w, hh, 1 }, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_LINEAR,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED };
    if (vkCreateImage(h->dev, &ii, NULL, &hw->outimg) != VK_SUCCESS) return -1;
    VkMemoryRequirements mr; vkGetImageMemoryRequirements(h->dev, hw->outimg, &mr);
    VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size };
    VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(h->pd, &mp);
    ai.memoryTypeIndex = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < mp.memoryTypeCount && ai.memoryTypeIndex == 0xFFFFFFFFu; i++)
        if ((mr.memoryTypeBits >> i) & 1 &&
            (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) ai.memoryTypeIndex = i;
    if (ai.memoryTypeIndex == 0xFFFFFFFFu)
        for (uint32_t i = 0; i < mp.memoryTypeCount && ai.memoryTypeIndex == 0xFFFFFFFFu; i++)
            if ((mr.memoryTypeBits >> i) & 1) ai.memoryTypeIndex = i;
    if (vkAllocateMemory(h->dev, &ai, NULL, &hw->outimg_m) != VK_SUCCESS) return -1;
    if (vkBindImageMemory(h->dev, hw->outimg, hw->outimg_m, 0) != VK_SUCCESS) return -1;
    VkImageViewCreateInfo vi = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = hw->outimg,
        .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = h->fmt,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    if (vkCreateImageView(h->dev, &vi, NULL, &hw->outview) != VK_SUCCESS) return -1;
    if (!hw->dp) {
        VkDescriptorPoolSize ps = { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 4 };
        VkDescriptorPoolCreateInfo pi = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 4, .poolSizeCount = 1, .pPoolSizes = &ps };
        if (vkCreateDescriptorPool(h->dev, &pi, NULL, &hw->dp) != VK_SUCCESS) return -1;
    } else vkResetDescriptorPool(h->dev, hw->dp, 0);
    VkDescriptorSetAllocateInfo dsai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = hw->dp, .descriptorSetCount = 1, .pSetLayouts = &hw->dsl };
    if (vkAllocateDescriptorSets(h->dev, &dsai, &hw->ds) != VK_SUCCESS) return -1;
    VkDescriptorBufferInfo bi[2] = { { .buffer = hw->inb, .offset = 0, .range = VK_WHOLE_SIZE },
                                     { .buffer = hw->outb, .offset = 0, .range = VK_WHOLE_SIZE } };
    VkWriteDescriptorSet wr[2] = {
        { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = hw->ds, .dstBinding = 0,
          .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &bi[0] },
        { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = hw->ds, .dstBinding = 1,
          .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &bi[1] } };
    vkUpdateDescriptorSets(h->dev, 2, wr, 0, NULL);
    hw->out_w = w; hw->out_h = hh; hw->ready = 1;
    return 0;
}

int vk_host_present_gpu16(void *v, const uint16_t *vram, int ox, int oy, int w, int hgt, int disp_on)
{
    vk_host *h = (vk_host *)v;
    if (!h || w <= 0 || hgt <= 0) return -1;
    if (!h->hw && hw_init(h) != 0) return -1;
    if (hw_ensure_out(h, (uint32_t)w, (uint32_t)hgt) != 0) return -1;
    vk_hw_t *hw = (vk_hw_t *)h->hw;
    memcpy(hw->inb_map, vram, 1024u * 512u * 2u);          /* 上载 (host-coherent, 一次) */
    VKTRY(vkWaitForFences(h->dev, 1, &h->fence, VK_TRUE, UINT64_MAX));
    vkResetFences(h->dev, 1, &h->fence);
    vkResetCommandBuffer(h->cb, 0);
    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    VKTRY(vkBeginCommandBuffer(h->cb, &bi));
    /* 宿主写 → compute 读 屏障 */
    VkBufferMemoryBarrier bb = { .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = hw->inb, .offset = 0, .size = VK_WHOLE_SIZE };
    vkCmdPipelineBarrier(h->cb, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, NULL, 1, &bb, 0, NULL);
    vkCmdBindPipeline(h->cb, VK_PIPELINE_BIND_POINT_COMPUTE, hw->pipe);
    vkCmdBindDescriptorSets(h->cb, VK_PIPELINE_BIND_POINT_COMPUTE, hw->pl, 0, 1, &hw->ds, 0, NULL);
    struct { int32_t ox, oy, w, hgt; uint32_t row, on, pad; } pc = { ox, oy, w, hgt, 1024u, (uint32_t)(disp_on ? 1 : 0), 0 };
    vkCmdPushConstants(h->cb, hw->pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, &pc);
    vkCmdDispatch(h->cb, (uint32_t)((w + 15) / 16), (uint32_t)((hgt + 15) / 16), 1);
    /* compute → copy 屏障 + outimg 布局 */
    VkBufferMemoryBarrier bb2 = { .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = hw->outb, .offset = 0, .size = VK_WHOLE_SIZE };
    vkCmdPipelineBarrier(h->cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, NULL, 1, &bb2, 0, NULL);
    barrier(h->cb, hw->outimg, VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy cpy = { .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageExtent = { (uint32_t)w, (uint32_t)hgt, 1 } };
    vkCmdCopyBufferToImage(h->cb, hw->outb, hw->outimg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cpy);
    barrier(h->cb, hw->outimg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    uint32_t idx = 0;
    VkResult ar = vkAcquireNextImageKHR(h->dev, h->sc, UINT64_MAX, h->sem_avail, NULL, &idx);
    if (ar == VK_ERROR_OUT_OF_DATE_KHR) { vkEndCommandBuffer(h->cb); return 0; }
    if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) { vk_die("acquire", ar); return -1; }
    barrier(h->cb, h->imgs[idx], h->lays[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageBlit bl = { .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .srcOffsets = { {0,0,0}, {w, hgt, 1} },
        .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .dstOffsets = { {0,0,0}, { (int32_t)h->ext.width, (int32_t)h->ext.height, 1 } } };
    vkCmdBlitImage(h->cb, hw->outimg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   h->imgs[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bl, VK_FILTER_NEAREST);
    barrier(h->cb, h->imgs[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    h->lays[idx] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VKTRY(vkEndCommandBuffer(h->cb));
    VkPipelineStageFlags wait = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1, .pWaitSemaphores = &h->sem_avail, .pWaitDstStageMask = &wait,
        .commandBufferCount = 1, .pCommandBuffers = &h->cb,
        .signalSemaphoreCount = 1, .pSignalSemaphores = &h->sem_done };
    VKTRY(vkQueueSubmit(h->queue, 1, &si, h->fence));
    VkPresentInfoKHR pi2 = { .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1, .pWaitSemaphores = &h->sem_done,
        .swapchainCount = 1, .pSwapchains = &h->sc, .pImageIndices = &idx };
    vkQueuePresentKHR(h->queue, &pi2);
    h->frame++;
    return 0;
}
