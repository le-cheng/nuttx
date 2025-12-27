# NuttX 双屏 LVGL 模拟器指南

本文档详细说明如何在 NuttX 中创建一个双屏 LVGL 模拟器，实现虚拟大屏模式（将两个物理屏幕合并为一个逻辑屏幕）。

## 项目概述

- **虚拟大屏**: 1920x720 (由两个 960x720 的物理屏幕组成)
- **双缓冲**: 提高渲染性能
- **DIRECT 渲染模式**: 使用 `LV_DISPLAY_RENDER_MODE_DIRECT`
- **跨屏显示**: 支持UI组件跨越两个物理屏幕显示
- **测试组件**: 包含按钮、滑块、进度条、开关等多种测试组件

---

## 架构说明

### 虚拟大屏原理

```
┌─────────────────────────────────────────────────────────────┐
│                    LVGL 虚拟大屏 (1920x720)                  │
│  ┌─────────────────────────┬─────────────────────────┐      │
│  │      左半部分            │      右半部分            │      │
│  │    (0,0)-(959,719)      │  (960,0)-(1919,719)     │      │
│  └─────────────────────────┴─────────────────────────┘      │
└─────────────────────────────────────────────────────────────┘
                     │ 自定义 flush_cb
                     ↓
┌─────────────────────────┐    ┌─────────────────────────┐
│   物理屏幕 1 (左屏)      │    │   物理屏幕 2 (右屏)      │
│      /dev/fb0           │    │      /dev/fb1           │
│      960x720            │    │      960x720            │
└─────────────────────────┘    └─────────────────────────┘
```

### 关键技术点

1. **虚拟显示**: LVGL 创建一个 1920x720 的虚拟显示
2. **自定义 flush**: 在 flush 回调中将渲染数据分割到两个 framebuffer
3. **坐标转换**: 左屏直接映射，右屏 x 坐标减去 960
4. **双缓冲**: 使用离屏缓冲区提高性能

---

## 文件结构

```
apps/examples/dualscreen_lvgl/
├── dualscreen_lvgl.c    # 主程序源代码
├── Kconfig              # 配置选项
├── CMakeLists.txt       # CMake 构建配置
├── Makefile             # Make 构建配置
└── Make.defs            # Make 定义文件

nuttx/boards/sim/sim/sim/configs/dualscreen/
└── defconfig            # 板级配置文件
```

---

## 第一部分：创建应用程序源码

### 步骤 1.1：创建应用目录

```bash
mkdir -p apps/examples/dualscreen_lvgl
```

### 步骤 1.2：创建主程序文件

创建文件 `apps/examples/dualscreen_lvgl/dualscreen_lvgl.c`：

```c
/****************************************************************************
 * apps/examples/dualscreen_lvgl/dualscreen_lvgl.c
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/boardctl.h>
#include <nuttx/video/fb.h>

#include <lvgl/lvgl.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#undef NEED_BOARDINIT
#if defined(CONFIG_BOARDCTL) && !defined(CONFIG_NSH_ARCHINIT)
#  define NEED_BOARDINIT 1
#endif

/* 屏幕配置 */
#define SCREEN_WIDTH      960
#define SCREEN_HEIGHT     720
#define VIRTUAL_WIDTH     (SCREEN_WIDTH * 2)  /* 1920 */
#define VIRTUAL_HEIGHT    SCREEN_HEIGHT       /* 720 */
#define COLOR_DEPTH       32
#define BYTES_PER_PIXEL   (COLOR_DEPTH / 8)

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* 双屏驱动数据结构 */
typedef struct {
    int fd_left;                    /* 左屏 framebuffer 文件描述符 */
    int fd_right;                   /* 右屏 framebuffer 文件描述符 */
    void *mem_left;                 /* 左屏内存映射 */
    void *mem_right;                /* 右屏内存映射 */
    uint32_t stride_left;           /* 左屏每行字节数 */
    uint32_t stride_right;          /* 右屏每行字节数 */
    size_t fblen_left;              /* 左屏 framebuffer 大小 */
    size_t fblen_right;             /* 右屏 framebuffer 大小 */
    void *draw_buf;                 /* LVGL 绘制缓冲区 */
    lv_draw_buf_t lv_draw_buf;      /* LVGL 绘制缓冲区描述符 */
} dualscreen_ctx_t;

/****************************************************************************
 * Private Data
 ****************************************************************************/

static dualscreen_ctx_t g_ctx;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* 打开并初始化单个 framebuffer */
static int init_framebuffer(const char *path, int *fd, void **mem,
                            uint32_t *stride, size_t *fblen)
{
    struct fb_videoinfo_s vinfo;
    struct fb_planeinfo_s pinfo;

    *fd = open(path, O_RDWR);
    if (*fd < 0) {
        printf("Error: cannot open %s: %d\n", path, errno);
        return -1;
    }

    if (ioctl(*fd, FBIOGET_VIDEOINFO, &vinfo) < 0) {
        printf("Error: FBIOGET_VIDEOINFO failed: %d\n", errno);
        close(*fd);
        return -1;
    }

    printf("Framebuffer %s:\n", path);
    printf("  Resolution: %dx%d\n", vinfo.xres, vinfo.yres);
    printf("  Format: %d\n", vinfo.fmt);

    if (ioctl(*fd, FBIOGET_PLANEINFO, &pinfo) < 0) {
        printf("Error: FBIOGET_PLANEINFO failed: %d\n", errno);
        close(*fd);
        return -1;
    }

    printf("  Stride: %d\n", pinfo.stride);
    printf("  FB length: %zu\n", pinfo.fblen);

    *mem = mmap(NULL, pinfo.fblen, PROT_READ | PROT_WRITE,
                MAP_SHARED | MAP_FILE, *fd, 0);
    if (*mem == MAP_FAILED) {
        printf("Error: mmap failed: %d\n", errno);
        close(*fd);
        return -1;
    }

    *stride = pinfo.stride;
    *fblen = pinfo.fblen;

    return 0;
}

/* 自定义 flush 回调 - 将虚拟大屏数据分割到两个物理屏幕 */
static void dualscreen_flush_cb(lv_display_t *disp, const lv_area_t *area,
                                 uint8_t *color_p)
{
    dualscreen_ctx_t *ctx = lv_display_get_driver_data(disp);
    
    int32_t x1 = area->x1;
    int32_t y1 = area->y1;
    int32_t x2 = area->x2;
    int32_t y2 = area->y2;
    
    /* 处理左屏 (x: 0-959) */
    if (x1 < SCREEN_WIDTH) {
        int32_t left_x1 = x1;
        int32_t left_x2 = (x2 < SCREEN_WIDTH) ? x2 : (SCREEN_WIDTH - 1);
        int32_t left_w = left_x2 - left_x1 + 1;
        
        for (int32_t y = y1; y <= y2; y++) {
            uint8_t *src = color_p + (y * VIRTUAL_WIDTH + x1) * BYTES_PER_PIXEL;
            uint8_t *dst = (uint8_t *)ctx->mem_left + 
                           y * ctx->stride_left + left_x1 * BYTES_PER_PIXEL;
            memcpy(dst, src, left_w * BYTES_PER_PIXEL);
        }
    }
    
    /* 处理右屏 (x: 960-1919) */
    if (x2 >= SCREEN_WIDTH) {
        int32_t right_x1 = (x1 >= SCREEN_WIDTH) ? (x1 - SCREEN_WIDTH) : 0;
        int32_t right_x2 = x2 - SCREEN_WIDTH;
        int32_t right_w = right_x2 - right_x1 + 1;
        int32_t src_x1 = (x1 >= SCREEN_WIDTH) ? x1 : SCREEN_WIDTH;
        
        for (int32_t y = y1; y <= y2; y++) {
            uint8_t *src = color_p + (y * VIRTUAL_WIDTH + src_x1) * BYTES_PER_PIXEL;
            uint8_t *dst = (uint8_t *)ctx->mem_right + 
                           y * ctx->stride_right + right_x1 * BYTES_PER_PIXEL;
            memcpy(dst, src, right_w * BYTES_PER_PIXEL);
        }
    }
    
    lv_display_flush_ready(disp);
}

/* 创建双屏显示 */
static lv_display_t *create_dualscreen_display(void)
{
    dualscreen_ctx_t *ctx = &g_ctx;
    
    /* 初始化左屏 */
    if (init_framebuffer("/dev/fb0", &ctx->fd_left, &ctx->mem_left,
                         &ctx->stride_left, &ctx->fblen_left) < 0) {
        return NULL;
    }
    
    /* 初始化右屏 */
    if (init_framebuffer("/dev/fb1", &ctx->fd_right, &ctx->mem_right,
                         &ctx->stride_right, &ctx->fblen_right) < 0) {
        munmap(ctx->mem_left, ctx->fblen_left);
        close(ctx->fd_left);
        return NULL;
    }
    
    /* 分配 LVGL 绘制缓冲区 (虚拟大屏尺寸) */
    size_t buf_size = VIRTUAL_WIDTH * VIRTUAL_HEIGHT * BYTES_PER_PIXEL;
    ctx->draw_buf = malloc(buf_size);
    if (!ctx->draw_buf) {
        printf("Error: failed to allocate draw buffer\n");
        munmap(ctx->mem_left, ctx->fblen_left);
        munmap(ctx->mem_right, ctx->fblen_right);
        close(ctx->fd_left);
        close(ctx->fd_right);
        return NULL;
    }
    
    /* 创建 LVGL 显示 */
    lv_display_t *disp = lv_display_create(VIRTUAL_WIDTH, VIRTUAL_HEIGHT);
    if (!disp) {
        free(ctx->draw_buf);
        munmap(ctx->mem_left, ctx->fblen_left);
        munmap(ctx->mem_right, ctx->fblen_right);
        close(ctx->fd_left);
        close(ctx->fd_right);
        return NULL;
    }
    
    /* 初始化绘制缓冲区 */
    lv_draw_buf_init(&ctx->lv_draw_buf, VIRTUAL_WIDTH, VIRTUAL_HEIGHT,
                     LV_COLOR_FORMAT_ARGB8888,
                     VIRTUAL_WIDTH * BYTES_PER_PIXEL,
                     ctx->draw_buf, buf_size);
    
    /* 设置显示参数 */
    lv_display_set_driver_data(disp, ctx);
    lv_display_set_flush_cb(disp, dualscreen_flush_cb);
    lv_display_set_draw_buffers(disp, &ctx->lv_draw_buf, NULL);
    lv_display_set_render_mode(disp, LV_DISPLAY_RENDER_MODE_DIRECT);
    
    printf("Dual screen display created: %dx%d\n", VIRTUAL_WIDTH, VIRTUAL_HEIGHT);
    
    return disp;
}

/* 清理双屏显示资源 */
static void destroy_dualscreen_display(lv_display_t *disp)
{
    dualscreen_ctx_t *ctx = &g_ctx;
    
    if (disp) lv_display_delete(disp);
    if (ctx->draw_buf) { free(ctx->draw_buf); ctx->draw_buf = NULL; }
    if (ctx->mem_left) { munmap(ctx->mem_left, ctx->fblen_left); ctx->mem_left = NULL; }
    if (ctx->mem_right) { munmap(ctx->mem_right, ctx->fblen_right); ctx->mem_right = NULL; }
    if (ctx->fd_left >= 0) { close(ctx->fd_left); ctx->fd_left = -1; }
    if (ctx->fd_right >= 0) { close(ctx->fd_right); ctx->fd_right = -1; }
}

/* 创建测试 UI - 跨越两个屏幕 */
static void create_test_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    
    /* 设置背景渐变色 */
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_grad_color(scr, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_HOR, 0);
    
    /* ========== 标题 (跨越两屏中央) ========== */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Dual Screen LVGL Demo");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);
    
    /* ========== 左屏内容 ========== */
    lv_obj_t *left_label = lv_label_create(scr);
    lv_label_set_text(left_label, "LEFT SCREEN\n/dev/fb0");
    lv_obj_set_style_text_color(left_label, lv_color_hex(0x4ECDC4), 0);
    lv_obj_set_style_text_align(left_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(left_label, 380, 100);
    
    lv_obj_t *left_rect = lv_obj_create(scr);
    lv_obj_set_size(left_rect, 300, 200);
    lv_obj_set_pos(left_rect, 330, 200);
    lv_obj_set_style_bg_color(left_rect, lv_color_hex(0xFF6B6B), 0);
    lv_obj_set_style_radius(left_rect, 20, 0);
    lv_obj_set_style_border_width(left_rect, 0, 0);
    
    lv_obj_t *left_rect_label = lv_label_create(left_rect);
    lv_label_set_text(left_rect_label, "Left Panel\nX: 0-959");
    lv_obj_set_style_text_color(left_rect_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(left_rect_label);
    
    lv_obj_t *left_btn = lv_button_create(scr);
    lv_obj_set_size(left_btn, 200, 60);
    lv_obj_set_pos(left_btn, 380, 450);
    lv_obj_t *left_btn_label = lv_label_create(left_btn);
    lv_label_set_text(left_btn_label, "Left Button");
    lv_obj_center(left_btn_label);
    
    /* ========== 右屏内容 ========== */
    lv_obj_t *right_label = lv_label_create(scr);
    lv_label_set_text(right_label, "RIGHT SCREEN\n/dev/fb1");
    lv_obj_set_style_text_color(right_label, lv_color_hex(0xFFE66D), 0);
    lv_obj_set_style_text_align(right_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(right_label, 1340, 100);
    
    lv_obj_t *right_rect = lv_obj_create(scr);
    lv_obj_set_size(right_rect, 300, 200);
    lv_obj_set_pos(right_rect, 1290, 200);
    lv_obj_set_style_bg_color(right_rect, lv_color_hex(0x4ECDC4), 0);
    lv_obj_set_style_radius(right_rect, 20, 0);
    lv_obj_set_style_border_width(right_rect, 0, 0);
    
    lv_obj_t *right_rect_label = lv_label_create(right_rect);
    lv_label_set_text(right_rect_label, "Right Panel\nX: 960-1919");
    lv_obj_set_style_text_color(right_rect_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(right_rect_label);
    
    lv_obj_t *right_btn = lv_button_create(scr);
    lv_obj_set_size(right_btn, 200, 60);
    lv_obj_set_pos(right_btn, 1340, 450);
    lv_obj_t *right_btn_label = lv_label_create(right_btn);
    lv_label_set_text(right_btn_label, "Right Button");
    lv_obj_center(right_btn_label);

    /* ========== 跨屏元素 ========== */
    lv_obj_t *cross_rect = lv_obj_create(scr);
    lv_obj_set_size(cross_rect, 400, 100);
    lv_obj_set_pos(cross_rect, 760, 550);
    lv_obj_set_style_bg_color(cross_rect, lv_color_hex(0x9B59B6), 0);
    lv_obj_set_style_radius(cross_rect, 15, 0);
    lv_obj_set_style_border_width(cross_rect, 3, 0);
    lv_obj_set_style_border_color(cross_rect, lv_color_hex(0xFFFFFF), 0);
    
    lv_obj_t *cross_label = lv_label_create(cross_rect);
    lv_label_set_text(cross_label, "Cross-Screen Element\nX: 760-1160");
    lv_obj_set_style_text_color(cross_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(cross_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(cross_label);
    
    /* 分界线 */
    lv_obj_t *divider = lv_obj_create(scr);
    lv_obj_set_size(divider, 4, VIRTUAL_HEIGHT);
    lv_obj_set_pos(divider, 958, 0);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_50, 0);
    lv_obj_set_style_radius(divider, 0, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    
    lv_obj_t *divider_label = lv_label_create(scr);
    lv_label_set_text(divider_label, "Screen Boundary\nX = 960");
    lv_obj_set_style_text_color(divider_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(divider_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(divider_label, 880, 670);
    
    /* 底部信息 */
    lv_obj_t *info = lv_label_create(scr);
    lv_label_set_text(info, "Virtual Screen: 1920x720 | Physical: 2x 960x720");
    lv_obj_set_style_text_color(info, lv_color_hex(0x888888), 0);
    lv_obj_align(info, LV_ALIGN_BOTTOM_MID, 0, -10);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
    lv_display_t *disp;

    if (lv_is_initialized()) {
        printf("LVGL already initialized!\n");
        return -1;
    }

#ifdef NEED_BOARDINIT
    boardctl(BOARDIOC_INIT, 0);
#endif

    lv_init();

    disp = create_dualscreen_display();
    if (!disp) {
        printf("Failed to create dual screen display!\n");
        lv_deinit();
        return 1;
    }

    printf("Dual screen LVGL initialized successfully!\n");
    create_test_ui();

    while (1) {
        uint32_t idle = lv_timer_handler();
        idle = idle ? idle : 1;
        usleep(idle * 1000);
    }

    destroy_dualscreen_display(disp);
    lv_deinit();
    return 0;
}
```

---

## 第二部分：创建构建配置文件

### 步骤 2.1：创建 Kconfig 文件

创建文件 `apps/examples/dualscreen_lvgl/Kconfig`：

```kconfig
menuconfig EXAMPLES_DUALSCREEN_LVGL
	tristate "Dual Screen LVGL Application"
	default n
	depends on GRAPHICS_LVGL
	---help---
		Enable dual screen LVGL application for virtual large screen mode

if EXAMPLES_DUALSCREEN_LVGL

config EXAMPLES_DUALSCREEN_LVGL_PRIORITY
	int "dualscreen_lvgl task priority"
	default 100

config EXAMPLES_DUALSCREEN_LVGL_STACKSIZE
	int "dualscreen_lvgl stack size"
	default 65536

endif # EXAMPLES_DUALSCREEN_LVGL
```

### 步骤 2.2：创建 CMakeLists.txt 文件

创建文件 `apps/examples/dualscreen_lvgl/CMakeLists.txt`：

```cmake
if(CONFIG_EXAMPLES_DUALSCREEN_LVGL)
  nuttx_add_application(
    NAME
    dualscreen_lvgl
    PRIORITY
    ${CONFIG_EXAMPLES_DUALSCREEN_LVGL_PRIORITY}
    STACKSIZE
    ${CONFIG_EXAMPLES_DUALSCREEN_LVGL_STACKSIZE}
    MODULE
    ${CONFIG_EXAMPLES_DUALSCREEN_LVGL}
    DEPENDS
    lvgl
    SRCS
    dualscreen_lvgl.c)
endif()
```

### 步骤 2.3：创建 Make.defs 文件

创建文件 `apps/examples/dualscreen_lvgl/Make.defs`：

```makefile
ifneq ($(CONFIG_EXAMPLES_DUALSCREEN_LVGL),)
CONFIGURED_APPS += $(APPDIR)/examples/dualscreen_lvgl
endif
```

### 步骤 2.4：创建 Makefile 文件

创建文件 `apps/examples/dualscreen_lvgl/Makefile`：

```makefile
include $(APPDIR)/Make.defs

PROGNAME = dualscreen_lvgl
PRIORITY = $(CONFIG_EXAMPLES_DUALSCREEN_LVGL_PRIORITY)
STACKSIZE = $(CONFIG_EXAMPLES_DUALSCREEN_LVGL_STACKSIZE)
MODULE = $(CONFIG_EXAMPLES_DUALSCREEN_LVGL)

MAINSRC = dualscreen_lvgl.c

include $(APPDIR)/Application.mk
```

---

## 第三部分：创建板级配置

### 步骤 3.1：复制基础配置

```bash
cp -r nuttx/boards/sim/sim/sim/configs/lvgl_fb \
      nuttx/boards/sim/sim/sim/configs/dualscreen
```

### 步骤 3.2：修改 defconfig

编辑 `nuttx/boards/sim/sim/sim/configs/dualscreen/defconfig`：

```
# CONFIG_LV_BUILD_EXAMPLES is not set
# CONFIG_NXFONTS_PACKEDMSFIRST is not set
CONFIG_ARCH="sim"
CONFIG_ARCH_BOARD="sim"
CONFIG_ARCH_BOARD_SIM=y
CONFIG_ARCH_CHIP="sim"
CONFIG_ARCH_SIM=y
CONFIG_BOARDCTL=y
CONFIG_BUILTIN=y
CONFIG_DEBUG_ASSERTIONS=y
CONFIG_DEBUG_FEATURES=y
CONFIG_DEBUG_FULLOPT=y
CONFIG_DEBUG_SYMBOLS=y
CONFIG_DISABLE_ENVIRON=y
CONFIG_DISABLE_MOUNTPOINT=y
CONFIG_DISABLE_POSIX_TIMERS=y
CONFIG_DRIVERS_VIDEO=y
CONFIG_EXAMPLES_DUALSCREEN_LVGL=y
CONFIG_EXAMPLES_DUALSCREEN_LVGL_STACKSIZE=65536
CONFIG_GRAPHICS_LVGL=y
CONFIG_IDLETHREAD_STACKSIZE=4096
CONFIG_INIT_ENTRYPOINT="dualscreen_lvgl_main"
CONFIG_INPUT=y
CONFIG_LV_COLOR_DEPTH_32=y
CONFIG_LV_FONT_MONTSERRAT_24=y
CONFIG_LV_USE_CLIB_MALLOC=y
CONFIG_LV_USE_CLIB_SPRINTF=y
CONFIG_LV_USE_CLIB_STRING=y
CONFIG_LV_USE_LOG=y
CONFIG_LV_USE_NUTTX=y
CONFIG_LV_USE_NUTTX_CUSTOM_INIT=y
CONFIG_MQ_MAXMSGSIZE=64
CONFIG_NXFONTS_DISABLE_16BPP=y
CONFIG_NXFONTS_DISABLE_1BPP=y
CONFIG_NXFONTS_DISABLE_24BPP=y
CONFIG_NXFONTS_DISABLE_2BPP=y
CONFIG_NXFONTS_DISABLE_32BPP=y
CONFIG_NXFONTS_DISABLE_4BPP=y
CONFIG_NXFONTS_DISABLE_8BPP=y
CONFIG_SIM_FBHEIGHT=720
CONFIG_SIM_FBWIDTH=960
CONFIG_SIM_X11FB=y
CONFIG_SIM_X11NWINDOWS=2
CONFIG_STACK_CANARIES=y
CONFIG_STACK_COLORATION=y
CONFIG_START_DAY=28
CONFIG_START_MONTH=11
CONFIG_START_YEAR=2008
CONFIG_USEC_PER_TICK=1000
CONFIG_VIDEO_FB=y
```

### 关键配置说明

| 配置项 | 值 | 说明 |
|--------|-----|------|
| `CONFIG_SIM_X11NWINDOWS` | 2 | 创建两个 X11 窗口 |
| `CONFIG_SIM_FBWIDTH` | 960 | 每个窗口宽度 |
| `CONFIG_SIM_FBHEIGHT` | 720 | 每个窗口高度 |
| `CONFIG_LV_USE_NUTTX_CUSTOM_INIT` | y | 使用自定义LVGL初始化 |
| `CONFIG_EXAMPLES_DUALSCREEN_LVGL_STACKSIZE` | 65536 | 较大的栈空间 |
| `CONFIG_LV_COLOR_DEPTH_32` | y | 32位色深 |

---

## 第四部分：编译和运行

### 步骤 4.1：清理旧的构建

```bash
cd nuttx
rm -rf build
```

### 步骤 4.2：配置项目

```bash
cmake -B build -DBOARD_CONFIG=sim:dualscreen -GNinja
```

### 步骤 4.3：编译

```bash
ninja -C build
```

### 步骤 4.4：运行

```bash
./build/nuttx
```

运行后会看到两个 960x720 的X11窗口，分别显示虚拟大屏的左右两部分。

---

## 第五部分：通过 menuconfig 配置

```bash
cd nuttx/build
ninja menuconfig
```

### 关键配置路径

**启用双窗口模式：**
```
Board Selection
  → Simulated X11 framebuffer
    → Number of X11 windows (2)
  → Simulated Framebuffer width (960)
  → Simulated Framebuffer height (720)
```

**启用应用程序：**
```
Application Configuration
  → Examples
    → [*] Dual Screen LVGL Application
      → Stack size (65536)
```

**LVGL 配置：**
```
Application Configuration
  → Graphics Support
    → Light and Versatile Graphic Library (LVGL)
      → LVGL configuration
        → 3rd Party Libraries
          → Use NuttX
            → [*] Custom LVGL initialization
        → Font usage
          → Montserrat fonts
            → [*] Enable Montserrat 24
```

---

## flush 回调详解

### 数据分割逻辑

```
虚拟大屏缓冲区 (1920x720)
┌─────────────────────────────────────────────────────────────┐
│ (0,0)                                              (1919,0) │
│     区域 A          │          区域 B                       │
│   (仅左屏)          │        (仅右屏)                       │
│         区域 C (跨屏)                                       │
│ (0,719)                                          (1919,719) │
└─────────────────────────────────────────────────────────────┘
                      ↓ flush_cb
┌───────────────────────┐    ┌───────────────────────┐
│      /dev/fb0         │    │      /dev/fb1         │
│   区域 A 的数据        │    │   区域 B 的数据        │
│   区域 C 左半部分      │    │   区域 C 右半部分      │
└───────────────────────┘    └───────────────────────┘
```

### 坐标转换规则

| 虚拟屏幕 X 坐标 | 目标屏幕 | 物理屏幕 X 坐标 |
|----------------|----------|----------------|
| 0 - 959        | 左屏     | 0 - 959        |
| 960 - 1919     | 右屏     | 0 - 959        |

### flush 回调核心逻辑

`dualscreen_flush_cb` 函数负责将LVGL渲染的虚拟大屏数据分割并写入两个物理framebuffer：

1. **左屏处理**: 将 x 坐标 0-959 的数据写入 `/dev/fb0`
2. **右屏处理**: 将 x 坐标 960-1919 的数据写入 `/dev/fb1` (x 坐标减去 960)
3. **跨屏处理**: 对于跨越 x=960 边界的区域，分别写入两个framebuffer

---

## UI 布局说明

### 左屏 (0-959)
- 屏幕标识标签
- 红色矩形面板
- 左侧按钮

### 右屏 (960-1919)
- 屏幕标识标签
- 青色矩形面板
- 右侧按钮

### 跨屏元素
- 紫色矩形 (x: 760-1160) - 横跨两个屏幕的中央位置
- 白色分界线 (x=960) - 显示两个屏幕的分界点
- 标题栏 - 位于虚拟屏幕顶部中央
- 底部信息栏 - 显示系统信息

---

## 常见问题

### Q: 只看到一个窗口
A: 确保 `CONFIG_SIM_X11NWINDOWS=2` 已正确设置。在 menuconfig 中检查 Board Selection → Number of X11 windows。

### Q: 编译错误 - 找不到 dualscreen_lvgl
A: 确保所有文件都已创建：CMakeLists.txt, Kconfig, Make.defs, Makefile, dualscreen_lvgl.c

### Q: 跨屏元素显示不正确
A: 检查 flush 回调中的坐标计算逻辑，特别是跨越 x=960 边界的区域处理。

### Q: 内存不足错误
A: 双屏模式需要更大的内存：
- `CONFIG_EXAMPLES_DUALSCREEN_LVGL_STACKSIZE=65536` 或更大
- 虚拟大屏缓冲区需要约 5.3MB (1920x720x4 bytes)

### Q: 右屏显示空白或异常
A: 检查 `/dev/fb1` 设备是否正确创建，以及 `init_framebuffer` 函数是否成功打开设备。

### Q: 如何修改屏幕分辨率？
A: 修改以下位置：
- `dualscreen_lvgl.c` 中的宏定义：`SCREEN_WIDTH`, `SCREEN_HEIGHT`
- `defconfig` 中的：`CONFIG_SIM_FBWIDTH`, `CONFIG_SIM_FBHEIGHT`

---

## 扩展开发

### 添加触摸支持

```c
static void touchscreen_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    // 读取触摸数据
    // 转换坐标到虚拟大屏坐标系
    // 判断是左屏还是右屏的触摸事件
}
```

### 添加动画效果

```c
lv_anim_t a;
lv_anim_init(&a);
lv_anim_set_var(&a, obj);
lv_anim_set_values(&a, 0, 1920);  // 从左屏移动到右屏
lv_anim_set_time(&a, 1000);
lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_x);
lv_anim_start(&a);
```

### 性能优化建议

1. **三缓冲**: 添加第三个缓冲区减少撕裂
2. **DMA加速**: 在真实硬件上使用DMA进行内存拷贝
3. **部分刷新**: 只刷新变化的区域
4. **异步渲染**: 使用多线程进行渲染和刷新

---

## 参考文档

- [LVGL官方文档](https://docs.lvgl.io/)
- [NuttX LVGL集成指南](LVGL_NuttX_Driver_Integration.md)
- [创建自定义LVGL应用指南](Create_Custom_LVGL_App_Guide.md)
