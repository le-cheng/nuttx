/****************************************************************************
 * arch/sim/src/sim/sim_framebuffer.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/clock.h>
#include <nuttx/wqueue.h>
#include <nuttx/video/fb.h>

#include "sim_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#undef FB_FMT
#if CONFIG_SIM_FBBPP == 1
#  define FB_FMT FB_FMT_Y1
#elif CONFIG_SIM_FBBPP == 4
#  define FB_FMT FB_FMT_RGB4
#elif CONFIG_SIM_FBBPP == 8
#  define FB_FMT FB_FMT_RGB8
#elif CONFIG_SIM_FBBPP == 16
#  define FB_FMT FB_FMT_RGB16_565
#elif CONFIG_SIM_FBBPP == 24
#  define FB_FMT FB_FMT_RGB24
#elif CONFIG_SIM_FBBPP == 32
#  define FB_FMT FB_FMT_RGB32
#else
#  error "Unsupported BPP"
#endif

/* Framebuffer characteristics in bytes */

#define FB_WIDTH ((CONFIG_SIM_FBWIDTH * CONFIG_SIM_FBBPP + 7) / 8)
#define FB_SIZE  (FB_WIDTH * CONFIG_SIM_FBHEIGHT)

#ifndef CONFIG_SIM_X11NWINDOWS
#  define CONFIG_SIM_X11NWINDOWS 1
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Per-display context structure */

struct sim_fb_s
{
  struct fb_vtable_s vtable;
  struct fb_videoinfo_s videoinfo;
  struct fb_planeinfo_s planeinfo;
  int displayno;
  int power;
  int initialized;
#ifndef CONFIG_SIM_X11FB
  uint8_t framebuffer[FB_SIZE];
#endif
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int sim_getvideoinfo(struct fb_vtable_s *vtable,
                            struct fb_videoinfo_s *vinfo);
static int sim_getplaneinfo(struct fb_vtable_s *vtable, int planeno,
                            struct fb_planeinfo_s *pinfo);

#ifdef CONFIG_FB_CMAP
static int sim_getcmap(struct fb_vtable_s *vtable,
                       struct fb_cmap_s *cmap);
static int sim_putcmap(struct fb_vtable_s *vtable,
                       const struct fb_cmap_s *cmap);
#endif

#ifdef CONFIG_FB_HWCURSOR
static int sim_getcursor(struct fb_vtable_s *vtable,
                         struct fb_cursorattrib_s *attrib);
static int sim_setcursor(struct fb_vtable_s *vtable,
                         struct fb_setcursor_s *settings);
#endif

static int sim_openwindow(struct fb_vtable_s *vtable);
static int sim_closewindow(struct fb_vtable_s *vtable);
static int sim_getpower(struct fb_vtable_s *vtable);
static int sim_setpower(struct fb_vtable_s *vtable, int power);

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Array of framebuffer objects for multi-display support */

static struct sim_fb_s g_fb[CONFIG_SIM_X11NWINDOWS];

#ifdef CONFIG_FB_HWCURSOR
static struct fb_cursorpos_s g_cpos;
#ifdef CONFIG_FB_HWCURSORSIZE
static struct fb_cursorsize_s g_csize;
#endif
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: sim_getfb
 ****************************************************************************/

static inline struct sim_fb_s *sim_getfb(struct fb_vtable_s *vtable)
{
  return (struct sim_fb_s *)vtable;
}

/****************************************************************************
 * Name: sim_openwindow
 ****************************************************************************/

static int sim_openwindow(struct fb_vtable_s *vtable)
{
  struct sim_fb_s *fb = sim_getfb(vtable);
  int ret = OK;

  ginfo("vtable=%p displayno=%d\n", vtable, fb->displayno);

#ifdef CONFIG_SIM_X11FB
  ret = sim_x11openwindow(fb->displayno);
#endif

  return ret;
}

/****************************************************************************
 * Name: sim_closewindow
 ****************************************************************************/

static int sim_closewindow(struct fb_vtable_s *vtable)
{
  struct sim_fb_s *fb = sim_getfb(vtable);
  int ret = OK;

  ginfo("vtable=%p displayno=%d\n", vtable, fb->displayno);

#ifdef CONFIG_SIM_X11FB
  ret = sim_x11closewindow(fb->displayno);
#endif

  return ret;
}

/****************************************************************************
 * Name: sim_getvideoinfo
 ****************************************************************************/

static int sim_getvideoinfo(struct fb_vtable_s *vtable,
                            struct fb_videoinfo_s *vinfo)
{
  struct sim_fb_s *fb = sim_getfb(vtable);

  ginfo("vtable=%p vinfo=%p\n", vtable, vinfo);
  if (vtable && vinfo)
    {
      memcpy(vinfo, &fb->videoinfo, sizeof(struct fb_videoinfo_s));
      return OK;
    }

  gerr("ERROR: Returning EINVAL\n");
  return -EINVAL;
}

/****************************************************************************
 * Name: sim_getplaneinfo
 ****************************************************************************/

static int sim_getplaneinfo(struct fb_vtable_s *vtable, int planeno,
                            struct fb_planeinfo_s *pinfo)
{
  struct sim_fb_s *fb = sim_getfb(vtable);

  ginfo("vtable=%p planeno=%d pinfo=%p\n", vtable, planeno, pinfo);
  if (vtable && planeno == 0 && pinfo)
    {
#if CONFIG_SIM_FB_INTERVAL_LINE > 0
      int display = pinfo->display;
#endif
      memcpy(pinfo, &fb->planeinfo, sizeof(struct fb_planeinfo_s));

#if CONFIG_SIM_FB_INTERVAL_LINE > 0
      if (display - fb->planeinfo.display > 0)
        {
          pinfo->display = display;
          pinfo->fbmem = fb->planeinfo.fbmem + fb->planeinfo.stride *
             (CONFIG_SIM_FB_INTERVAL_LINE + CONFIG_SIM_FBHEIGHT) *
             (display - fb->planeinfo.display);
        }
#endif

      return OK;
    }

  gerr("ERROR: Returning EINVAL\n");
  return -EINVAL;
}

/****************************************************************************
 * Name: sim_getcmap
 ****************************************************************************/

#ifdef CONFIG_FB_CMAP
static int sim_getcmap(struct fb_vtable_s *vtable,
                       struct fb_cmap_s *cmap)
{
  int len;
  int i;

  ginfo("vtable=%p cmap=%p len=%d\n", vtable, cmap, cmap->len);
  if (vtable && cmap)
    {
      for (i = cmap->first, len = 0; i < 256 && len < cmap->len; i++, len++)
        {
          cmap->red[i]    = i;
          cmap->green[i]  = i;
          cmap->blue[i]   = i;
#ifdef CONFIG_FB_TRANSPARENCY
          cmap->transp[i] = i;
#endif
        }

      cmap->len = len;
      return OK;
    }

  gerr("ERROR: Returning EINVAL\n");
  return -EINVAL;
}
#endif

/****************************************************************************
 * Name: sim_putcmap
 ****************************************************************************/

#ifdef CONFIG_FB_CMAP
static int sim_putcmap(struct fb_vtable_s *vtable,
                       const struct fb_cmap_s *cmap)
{
  struct sim_fb_s *fb = sim_getfb(vtable);

#ifdef CONFIG_SIM_X11FB
  return sim_x11cmap(fb->displayno, cmap->first, cmap->len, cmap->red,
                     cmap->green, cmap->blue, NULL);
#else
  ginfo("vtable=%p cmap=%p len=%d\n", vtable, cmap, cmap->len);
  if (vtable && cmap)
    {
      return OK;
    }

  gerr("ERROR: Returning EINVAL\n");
  return -EINVAL;
#endif
}
#endif

/****************************************************************************
 * Name: sim_getcursor
 ****************************************************************************/

#ifdef CONFIG_FB_HWCURSOR
static int sim_getcursor(struct fb_vtable_s *vtable,
                         struct fb_cursorattrib_s *attrib)
{
  ginfo("vtable=%p attrib=%p\n", vtable, attrib);
  if (vtable && attrib)
    {
#ifdef CONFIG_FB_HWCURSORIMAGE
      attrib->fmt      = FB_FMT;
#endif
      ginfo("pos:      (x=%d, y=%d)\n", g_cpos.x, g_cpos.y);
      attrib->pos      = g_cpos;
#ifdef CONFIG_FB_HWCURSORSIZE
      attrib->mxsize.h = CONFIG_SIM_FBHEIGHT;
      attrib->mxsize.w = CONFIG_SIM_FBWIDTH;
      ginfo("size:     (h=%d, w=%d)\n", g_csize.h, g_csize.w);
      attrib->size     = g_csize;
#endif
      return OK;
    }

  gerr("ERROR: Returning EINVAL\n");
  return -EINVAL;
}
#endif

/****************************************************************************
 * Name: sim_setcursor
 ****************************************************************************/

#ifdef CONFIG_FB_HWCURSOR
static int sim_setcursor(struct fb_vtable_s *vtable,
                         struct fb_setcursor_s *settings)
{
  ginfo("vtable=%p settings=%p\n", vtable, settings);
  if (vtable && settings)
    {
      ginfo("flags:   %02x\n", settings->flags);
      if ((settings->flags & FB_CUR_SETPOSITION) != 0)
        {
          g_cpos = settings->pos;
          ginfo("pos:     (h:%d, w:%d)\n", g_cpos.x, g_cpos.y);
        }

#ifdef CONFIG_FB_HWCURSORSIZE
      if ((settings->flags & FB_CUR_SETSIZE) != 0)
        {
          g_csize = settings->size;
          ginfo("size:    (h:%d, w:%d)\n", g_csize.h, g_csize.w);
        }
#endif

#ifdef CONFIG_FB_HWCURSORIMAGE
      if ((settings->flags & FB_CUR_SETIMAGE) != 0)
        {
          ginfo("image:   (h:%d, w:%d) @ %p\n",
                settings->img.height, settings->img.width,
                settings->img.image);
        }
#endif
      return OK;
    }

  gerr("ERROR: Returning EINVAL\n");
  return -EINVAL;
}
#endif

/****************************************************************************
 * Name: sim_getpower
 ****************************************************************************/

static int sim_getpower(struct fb_vtable_s *vtable)
{
  struct sim_fb_s *fb = sim_getfb(vtable);

  ginfo("vtable=%p power=%d\n", vtable, fb->power);
  return fb->power;
}

/****************************************************************************
 * Name: sim_setpower
 ****************************************************************************/

static int sim_setpower(struct fb_vtable_s *vtable, int power)
{
  struct sim_fb_s *fb = sim_getfb(vtable);

  ginfo("vtable=%p power=%d\n", vtable, power);
  if (power < 0)
    {
      gerr("ERROR: power=%d < 0\n", power);
      return -EINVAL;
    }

  fb->power = power;
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: sim_x11loop
 ****************************************************************************/

#ifdef CONFIG_SIM_X11FB
void sim_x11loop(void *arg)
{
  union fb_paninfo_u info;
  int i;
  uint64_t now = host_gettime(false);
  static uint64_t last;

  if (now - last >= 16000000)
    {
      last = now;

      /* Update all initialized displays */

      for (i = 0; i < CONFIG_SIM_X11NWINDOWS; i++)
        {
          struct sim_fb_s *fb = &g_fb[i];

          if (!fb->initialized)
            {
              continue;
            }

          fb_notify_vsync(&fb->vtable);
          if (fb_paninfo_count(&fb->vtable, FB_NO_OVERLAY) > 1)
            {
              fb_remove_paninfo(&fb->vtable, FB_NO_OVERLAY);
            }

          if (fb_peek_paninfo(&fb->vtable, &info, FB_NO_OVERLAY) == OK)
            {
              sim_x11setoffset(i, info.planeinfo.yoffset *
                               info.planeinfo.stride);
            }

          sim_x11update(i);
        }
    }
}
#endif

/****************************************************************************
 * Name: up_fbinitialize
 *
 * Description:
 *   Initialize the framebuffer video hardware associated with the display.
 *
 * Input Parameters:
 *   display - In the case of hardware with multiple displays, this
 *     specifies the display.  Normally this is zero.
 *
 * Returned Value:
 *   Zero is returned on success; a negated errno value is returned on any
 *   failure.
 *
 ****************************************************************************/

int up_fbinitialize(int display)
{
  struct sim_fb_s *fb;
  int ret = OK;

  ginfo("display=%d\n", display);

  if (display < 0 || display >= CONFIG_SIM_X11NWINDOWS)
    {
      gerr("ERROR: Invalid display number: %d\n", display);
      return -EINVAL;
    }

  fb = &g_fb[display];

  /* Check if already initialized */

  if (fb->initialized)
    {
      return OK;
    }

  /* Initialize the fb structure */

  memset(fb, 0, sizeof(struct sim_fb_s));

  fb->displayno = display;
  fb->power = 100;

  /* Setup video info */

  fb->videoinfo.fmt     = FB_FMT;
  fb->videoinfo.xres    = CONFIG_SIM_FBWIDTH;
  fb->videoinfo.yres    = CONFIG_SIM_FBHEIGHT;
  fb->videoinfo.nplanes = 1;

  /* Setup vtable */

  fb->vtable.getvideoinfo = sim_getvideoinfo;
  fb->vtable.getplaneinfo = sim_getplaneinfo;
#ifdef CONFIG_FB_CMAP
  fb->vtable.getcmap      = sim_getcmap;
  fb->vtable.putcmap      = sim_putcmap;
#endif
#ifdef CONFIG_FB_HWCURSOR
  fb->vtable.getcursor    = sim_getcursor;
  fb->vtable.setcursor    = sim_setcursor;
#endif
  fb->vtable.open         = sim_openwindow;
  fb->vtable.close        = sim_closewindow;
  fb->vtable.getpower     = sim_getpower;
  fb->vtable.setpower     = sim_setpower;

#ifdef CONFIG_SIM_X11FB
  fb->planeinfo.xres_virtual = CONFIG_SIM_FBWIDTH;
  fb->planeinfo.yres_virtual = CONFIG_SIM_FBHEIGHT *
                               CONFIG_SIM_FRAMEBUFFER_COUNT;

  ret = sim_x11initialize(display, CONFIG_SIM_FBWIDTH, CONFIG_SIM_FBHEIGHT,
                          &fb->planeinfo.fbmem, &fb->planeinfo.fblen,
                          &fb->planeinfo.bpp, &fb->planeinfo.stride,
                          CONFIG_SIM_FRAMEBUFFER_COUNT,
                          CONFIG_SIM_FB_INTERVAL_LINE);
  if (ret < 0)
    {
      gerr("ERROR: sim_x11initialize failed: %d\n", ret);
      return ret;
    }
#else
  fb->planeinfo.fbmem  = fb->framebuffer;
  fb->planeinfo.fblen  = FB_SIZE;
  fb->planeinfo.stride = FB_WIDTH;
  fb->planeinfo.bpp    = CONFIG_SIM_FBBPP;
#endif

  fb->planeinfo.display = display;
  fb->initialized = 1;

  ginfo("Display %d initialized: %dx%d\n", display,
        CONFIG_SIM_FBWIDTH, CONFIG_SIM_FBHEIGHT);

  return ret;
}

/****************************************************************************
 * Name: up_fbgetvplane
 *
 * Description:
 *   Return a reference to the framebuffer object for the specified video
 *   plane of the specified plane.  Many OSDs support multiple planes of
 *   video.
 *
 * Input Parameters:
 *   display - In the case of hardware with multiple displays, this
 *     specifies the display.  Normally this is zero.
 *   vplane - Identifies the plane being queried.
 *
 * Returned Value:
 *   A non-NULL pointer to the frame buffer access structure is returned on
 *   success; NULL is returned on any failure.
 *
 ****************************************************************************/

struct fb_vtable_s *up_fbgetvplane(int display, int vplane)
{
  ginfo("display=%d vplane=%d\n", display, vplane);

  if (display < 0 || display >= CONFIG_SIM_X11NWINDOWS)
    {
      gerr("ERROR: Invalid display number: %d\n", display);
      return NULL;
    }

  if (vplane != 0)
    {
      gerr("ERROR: Invalid vplane: %d\n", vplane);
      return NULL;
    }

  if (!g_fb[display].initialized)
    {
      gerr("ERROR: Display %d not initialized\n", display);
      return NULL;
    }

  return &g_fb[display].vtable;
}

/****************************************************************************
 * Name: up_fbuninitialize
 *
 * Description:
 *   Uninitialize the framebuffer support for the specified display.
 *
 * Input Parameters:
 *   display - In the case of hardware with multiple displays, this
 *     specifies the display.  Normally this is zero.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void up_fbuninitialize(int display)
{
  ginfo("display=%d\n", display);

  if (display >= 0 && display < CONFIG_SIM_X11NWINDOWS)
    {
      g_fb[display].initialized = 0;
    }
}
