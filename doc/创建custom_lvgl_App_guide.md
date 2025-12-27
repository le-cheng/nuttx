# 创建自定义 LVGL 应用程序指南

本文档详细说明如何在 NuttX 中创建一个独立的 LVGL 测试程序，包含 960x720 窗口和自定义图形界面。

## 目标

- 创建名为 `mylvglapp` 的独立 LVGL 应用
- 窗口分辨率：960x720
- 直接启动（不通过 NSH）
- 绘制自定义图形、图标和文字

---

## 第一部分：创建应用程序源码

### 步骤 1.1：创建应用目录

```bash
mkdir -p apps/examples/mylvglapp
```

### 步骤 1.2：创建主程序文件

创建文件 `apps/examples/mylvglapp/mylvglapp.c`：

```c
/****************************************************************************
 * apps/examples/mylvglapp/mylvglapp.c
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/boardctl.h>

#include <lvgl/lvgl.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#undef NEED_BOARDINIT
#if defined(CONFIG_BOARDCTL) && !defined(CONFIG_NSH_ARCHINIT)
#  define NEED_BOARDINIT 1
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* 按钮点击回调函数 */
static void btn_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * btn = lv_event_get_target(e);
    
    if(code == LV_EVENT_CLICKED) {
        static int cnt = 0;
        cnt++;
        
        lv_obj_t * label = lv_obj_get_child(btn, 0);
        lv_label_set_text_fmt(label, "Clicked: %d", cnt);
        
        printf("Button clicked %d times\n", cnt);
    }
}

/* 创建自定义 UI 界面 */
static void create_ui(void)
{
    /* 获取当前活动屏幕 */
    lv_obj_t * scr = lv_screen_active();
    
    /* 设置屏幕背景色 */
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x003366), 0);
    
    /* ========== 标题文字 ========== */
    lv_obj_t * title = lv_label_create(scr);
    lv_label_set_text(title, "My LVGL Application");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);
    
    /* ========== 副标题 ========== */
    lv_obj_t * subtitle = lv_label_create(scr);
    lv_label_set_text(subtitle, "NuttX + LVGL Demo - 960x720");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0xCCCCCC), 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 70);
    
    /* ========== 按钮 ========== */
    lv_obj_t * btn = lv_button_create(scr);
    lv_obj_set_size(btn, 200, 60);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, -50);
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t * btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Click Me!");
    lv_obj_center(btn_label);
    
    /* ========== 矩形 ========== */
    lv_obj_t * rect = lv_obj_create(scr);
    lv_obj_set_size(rect, 150, 100);
    lv_obj_align(rect, LV_ALIGN_CENTER, -200, 100);
    lv_obj_set_style_bg_color(rect, lv_color_hex(0xFF6B6B), 0);
    lv_obj_set_style_radius(rect, 10, 0);
    lv_obj_set_style_border_width(rect, 0, 0);
    
    lv_obj_t * rect_label = lv_label_create(rect);
    lv_label_set_text(rect_label, "Red Box");
    lv_obj_set_style_text_color(rect_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(rect_label);
    
    /* ========== 圆形 ========== */
    lv_obj_t * circle = lv_obj_create(scr);
    lv_obj_set_size(circle, 120, 120);
    lv_obj_align(circle, LV_ALIGN_CENTER, 0, 100);
    lv_obj_set_style_bg_color(circle, lv_color_hex(0x4ECDC4), 0);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(circle, 0, 0);
    
    lv_obj_t * circle_label = lv_label_create(circle);
    lv_label_set_text(circle_label, "Circle");
    lv_obj_set_style_text_color(circle_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(circle_label);
    
    /* ========== 另一个矩形 ========== */
    lv_obj_t * rect2 = lv_obj_create(scr);
    lv_obj_set_size(rect2, 150, 100);
    lv_obj_align(rect2, LV_ALIGN_CENTER, 200, 100);
    lv_obj_set_style_bg_color(rect2, lv_color_hex(0xFFE66D), 0);
    lv_obj_set_style_radius(rect2, 10, 0);
    lv_obj_set_style_border_width(rect2, 0, 0);
    
    lv_obj_t * rect2_label = lv_label_create(rect2);
    lv_label_set_text(rect2_label, "Yellow Box");
    lv_obj_set_style_text_color(rect2_label, lv_color_hex(0x333333), 0);
    lv_obj_center(rect2_label);
    
    /* ========== 进度条 ========== */
    lv_obj_t * bar = lv_bar_create(scr);
    lv_obj_set_size(bar, 400, 25);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -100);
    lv_bar_set_value(bar, 70, LV_ANIM_ON);
    
    lv_obj_t * bar_label = lv_label_create(scr);
    lv_label_set_text(bar_label, "Progress: 70%");
    lv_obj_set_style_text_color(bar_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(bar_label, LV_ALIGN_BOTTOM_MID, 0, -130);
    
    /* ========== 滑块 ========== */
    lv_obj_t * slider = lv_slider_create(scr);
    lv_obj_set_size(slider, 300, 20);
    lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, -50);
    lv_slider_set_value(slider, 50, LV_ANIM_OFF);
    
    /* ========== 底部信息 ========== */
    lv_obj_t * info = lv_label_create(scr);
    lv_label_set_text(info, "Touch the screen or click the button");
    lv_obj_set_style_text_color(info, lv_color_hex(0x888888), 0);
    lv_obj_align(info, LV_ALIGN_BOTTOM_MID, 0, -15);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
    lv_nuttx_dsc_t info;
    lv_nuttx_result_t result;

    /* 检查是否已初始化 */
    if (lv_is_initialized()) {
        printf("LVGL already initialized!\n");
        return -1;
    }

#ifdef NEED_BOARDINIT
    boardctl(BOARDIOC_INIT, 0);
#endif

    /* 初始化 LVGL */
    lv_init();

    /* 初始化 NuttX 显示和输入设备 */
    lv_nuttx_dsc_init(&info);

#ifdef CONFIG_LV_USE_NUTTX_LCD
    info.fb_path = "/dev/lcd0";
#endif

#ifdef CONFIG_INPUT_TOUCHSCREEN
    info.input_path = "/dev/input0";
#endif

    lv_nuttx_init(&info, &result);

    if (result.disp == NULL) {
        printf("Display initialization failed!\n");
        return 1;
    }

    printf("LVGL initialized successfully!\n");
    printf("Display: %dx%d\n", 
           lv_display_get_horizontal_resolution(result.disp),
           lv_display_get_vertical_resolution(result.disp));

    /* 创建 UI 界面 */
    create_ui();

    /* 主循环 */
    while (1) {
        uint32_t idle = lv_timer_handler();
        idle = idle ? idle : 1;
        usleep(idle * 1000);
    }

    /* 清理（实际上不会执行到这里） */
    lv_nuttx_deinit(&result);
    lv_deinit();

    return 0;
}
```

### 步骤 1.3：创建 Kconfig 文件

创建文件 `apps/examples/mylvglapp/Kconfig`：

```kconfig
#
# For a description of the syntax of this configuration file,
# see the file kconfig-language.txt in the NuttX tools repository.
#

menuconfig EXAMPLES_MYLVGLAPP
	tristate "My LVGL Application"
	default n
	depends on GRAPHICS_LVGL
	---help---
		Enable my custom LVGL application

if EXAMPLES_MYLVGLAPP

config EXAMPLES_MYLVGLAPP_PRIORITY
	int "mylvglapp task priority"
	default 100

config EXAMPLES_MYLVGLAPP_STACKSIZE
	int "mylvglapp stack size"
	default 32768

endif # EXAMPLES_MYLVGLAPP
```

### 步骤 1.4：创建 CMakeLists.txt 文件

创建文件 `apps/examples/mylvglapp/CMakeLists.txt`：

```cmake
# ##############################################################################
# apps/examples/mylvglapp/CMakeLists.txt
# ##############################################################################

if(CONFIG_EXAMPLES_MYLVGLAPP)
  nuttx_add_application(
    NAME
    mylvglapp
    PRIORITY
    ${CONFIG_EXAMPLES_MYLVGLAPP_PRIORITY}
    STACKSIZE
    ${CONFIG_EXAMPLES_MYLVGLAPP_STACKSIZE}
    MODULE
    ${CONFIG_EXAMPLES_MYLVGLAPP}
    DEPENDS
    lvgl
    SRCS
    mylvglapp.c)
endif()
```

### 步骤 1.5：创建 Make.defs 文件

创建文件 `apps/examples/mylvglapp/Make.defs`：

```makefile
############################################################################
# apps/examples/mylvglapp/Make.defs
############################################################################

ifneq ($(CONFIG_EXAMPLES_MYLVGLAPP),)
CONFIGURED_APPS += $(APPDIR)/examples/mylvglapp
endif
```

### 步骤 1.6：创建 Makefile 文件

创建文件 `apps/examples/mylvglapp/Makefile`：

```makefile
############################################################################
# apps/examples/mylvglapp/Makefile
############################################################################

include $(APPDIR)/Make.defs

PROGNAME = mylvglapp
PRIORITY = $(CONFIG_EXAMPLES_MYLVGLAPP_PRIORITY)
STACKSIZE = $(CONFIG_EXAMPLES_MYLVGLAPP_STACKSIZE)
MODULE = $(CONFIG_EXAMPLES_MYLVGLAPP)

MAINSRC = mylvglapp.c

include $(APPDIR)/Application.mk
```

---

## 第二部分：创建板级配置

### 步骤 2.1：复制基础配置

```bash
cp -r nuttx/boards/sim/sim/sim/configs/lvgl_fb \
      nuttx/boards/sim/sim/sim/configs/mylvglapp
```

### 步骤 2.2：修改 defconfig

编辑 `nuttx/boards/sim/sim/sim/configs/mylvglapp/defconfig`，修改以下内容：

将：
```
CONFIG_EXAMPLES_LVGLDEMO=y
CONFIG_EXAMPLES_LVGLDEMO_STACKSIZE=32768
CONFIG_INIT_ARGS="\"widgets\""
CONFIG_INIT_ENTRYPOINT="lvgldemo_main"
CONFIG_LV_USE_DEMO_WIDGETS=y
CONFIG_SIM_FBHEIGHT=480
CONFIG_SIM_FBWIDTH=640
```

改为：
```
CONFIG_EXAMPLES_MYLVGLAPP=y
CONFIG_EXAMPLES_MYLVGLAPP_STACKSIZE=32768
CONFIG_INIT_ARGS=""
CONFIG_INIT_ENTRYPOINT="mylvglapp_main"
CONFIG_SIM_FBHEIGHT=720
CONFIG_SIM_FBWIDTH=960
CONFIG_LV_FONT_MONTSERRAT_24=y
```

完整的 defconfig 文件内容：

```
#
# This file is autogenerated: PLEASE DO NOT EDIT IT.
#
# You can use "make menuconfig" to make any modifications to the installed .config file.
# You can then do "make savedefconfig" to generate a new defconfig file that includes your
# modifications.
#
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
CONFIG_EXAMPLES_LVGL_SIMPLE=y
CONFIG_EXAMPLES_LVGL_SIMPLE_STACKSIZE=32768
CONFIG_GRAPHICS_LVGL=y
CONFIG_IDLETHREAD_STACKSIZE=4096
CONFIG_INIT_ENTRYNAME="lvgl_simple"
CONFIG_INIT_ENTRYPOINT="lvgl_simple_main"
CONFIG_INPUT=y
CONFIG_LV_COLOR_DEPTH_32=y
CONFIG_LV_USE_CLIB_MALLOC=y
CONFIG_LV_USE_CLIB_SPRINTF=y
CONFIG_LV_USE_CLIB_STRING=y
CONFIG_LV_USE_LOG=y
CONFIG_LV_USE_NUTTX=y
CONFIG_LV_USE_NUTTX_TOUCHSCREEN=y
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
CONFIG_SIM_TOUCHSCREEN=y
CONFIG_SIM_X11FB=y
CONFIG_STACK_CANARIES=y
CONFIG_STACK_COLORATION=y
CONFIG_START_DAY=28
CONFIG_START_MONTH=11
CONFIG_START_YEAR=2008
CONFIG_USEC_PER_TICK=1000
CONFIG_VIDEO_FB=y

```

---

## 第三部分：编译和运行

### 步骤 3.1：清理旧的构建

```bash
cd nuttx
rm -rf build
```

### 步骤 3.2：配置项目

```bash
cmake -B build -DBOARD_CONFIG=sim:mylvglapp -GNinja
```

### 步骤 3.3：编译

```bash
ninja -C build
```

### 步骤 3.4：运行

```bash
./build/nuttx
```

将会看到一个 960x720 的窗口，显示自定义的 UI 界面。

---

## 第四部分：通过 menuconfig 配置（可选）

如果需要修改配置，可以使用 menuconfig：

```bash
cd nuttx/build
ninja menuconfig
```

### 关键配置路径

**窗口分辨率：**
```
Board Selection
  → Simulated Framebuffer width (960)
  → Simulated Framebuffer height (720)
```

**启用应用程序：**
```
Application Configuration
  → Examples
    → [*] My LVGL Application
```

**LVGL 字体（用于大标题）：**
```
Application Configuration
  → Graphics Support
    → Light and Versatile Graphic Library (LVGL)
      → LVGL configuration
        → Font usage
          → Montserrat fonts
            → [*] Enable Montserrat 24
```

---

## 文件结构总结

创建完成后的文件结构：

```
apps/examples/mylvglapp/
├── CMakeLists.txt
├── Kconfig
├── Make.defs
├── Makefile
└── mylvglapp.c

nuttx/boards/sim/sim/sim/configs/mylvglapp/
└── defconfig
```

---

## UI 界面说明

创建的界面包含以下元素：

| 元素 | 说明 |
|------|------|
| 标题 | 白色大字 "My LVGL Application" |
| 副标题 | 灰色小字显示分辨率信息 |
| 按钮 | 可点击，显示点击次数 |
| 红色矩形 | 圆角矩形，带文字 |
| 青色圆形 | 正圆形，带文字 |
| 黄色矩形 | 圆角矩形，带文字 |
| 进度条 | 显示 70% 进度 |
| 滑块 | 可拖动的滑块控件 |

---

## 常见问题

### Q: 编译时找不到 mylvglapp

A: 确保 `apps/examples/mylvglapp/` 目录下的所有文件都已创建，特别是 `Make.defs` 文件。

### Q: 窗口大小不是 960x720

A: 检查 defconfig 中的 `CONFIG_SIM_FBWIDTH=960` 和 `CONFIG_SIM_FBHEIGHT=720` 是否正确设置。

### Q: 标题字体太小

A: 确保启用了 `CONFIG_LV_FONT_MONTSERRAT_24=y`，代码中使用了 `lv_font_montserrat_24`。

### Q: 触摸/点击无响应

A: 确保以下配置已启用：
- `CONFIG_INPUT=y`
- `CONFIG_SIM_TOUCHSCREEN=y`
- `CONFIG_LV_USE_NUTTX_TOUCHSCREEN=y`

---

## 扩展建议

1. **添加更多控件**：参考 LVGL 文档添加开关、下拉框、图表等
2. **添加动画**：使用 `lv_anim_*` API 创建动画效果
3. **添加图片**：使用 LVGL 图片转换工具添加自定义图片
4. **添加样式**：创建统一的样式主题
