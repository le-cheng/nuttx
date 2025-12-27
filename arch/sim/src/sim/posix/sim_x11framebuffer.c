/****************************************************************************
 * arch/sim/src/sim/posix/sim_x11framebuffer.c
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/extensions/XShm.h>

#include "sim_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_SIM_X11NWINDOWS
#  define CONFIG_SIM_X11NWINDOWS 1
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef union
{
  struct
    {
      uint16_t blue : 5;
      uint16_t green : 6;
      uint16_t red : 5;
    };
  uint16_t full;
} x11_color16_t;

typedef union
{
  struct
    {
      uint8_t blue;
      uint8_t green;
      uint8_t red;
      uint8_t alpha;
    };
  uint32_t full;
} x11_color32_t;

/* Per-window context structure */

typedef struct
{
  Window window;
  GC gc;
#ifndef CONFIG_SIM_X11NOSHM
  XShmSegmentInfo xshminfo;
#endif
  XImage *image;
  char *framebuffer;
  unsigned short fbpixelwidth;
  unsigned short fbpixelheight;
  int fbbpp;
  int fblen;
  int shmcheckpoint;
  int useshm;
  unsigned char *trans_framebuffer;
  unsigned int offset;
  int initialized;
} x11_window_t;

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* Also used in sim_x11eventloop */

Display *g_display;

/****************************************************************************
 * Private Data
 ****************************************************************************/

static int g_screen;
#ifndef CONFIG_SIM_X11NOSHM
static int g_xerror;
#endif

/* Array of window contexts for multi-window support */

static x11_window_t g_windows[CONFIG_SIM_X11NWINDOWS];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: sim_x11createframe
 ****************************************************************************/

static inline int sim_x11createframe(int displayno, x11_window_t *win)
{
  XGCValues gcval;
  char *argv[2] =
    {
      "nuttx", NULL
    };

  char winname[32];
  char *winname_ptr;
  char *iconname = "NX";
  XTextProperty winprop;
  XTextProperty iconprop;
  XSizeHints hints;
  int xpos;

  /* Generate unique window name */

  snprintf(winname, sizeof(winname), "NuttX FB%d", displayno);
  winname_ptr = winname;

  /* Calculate window position - arrange windows side by side */

  xpos = displayno * (win->fbpixelwidth + 10);

  win->window = XCreateSimpleWindow(g_display, DefaultRootWindow(g_display),
                                    xpos, 0,
                                    win->fbpixelwidth, win->fbpixelheight, 2,
                                    BlackPixel(g_display, g_screen),
                                    BlackPixel(g_display, g_screen));

  if (win->window == None)
    {
      return -ENODEV;
    }

  XStringListToTextProperty(&winname_ptr, 1, &winprop);
  XStringListToTextProperty(&iconname, 1, &iconprop);

  hints.flags  = PSize | PMinSize | PMaxSize | PPosition;
  hints.x      = xpos;
  hints.y      = 0;
  hints.width  = hints.min_width  = hints.max_width  = win->fbpixelwidth;
  hints.height = hints.min_height = hints.max_height = win->fbpixelheight;

  XSetWMProperties(g_display, win->window, &winprop, &iconprop, argv, 1,
                   &hints, NULL, NULL);
  XFree(winprop.value);
  XFree(iconprop.value);

  /* Select window input events */

  XSelectInput(g_display, win->window,
               ButtonPressMask | ButtonReleaseMask |
               PointerMotionMask | ButtonMotionMask |
               KeyPressMask | KeyReleaseMask);

  /* Release queued events on the display */

#if defined(CONFIG_SIM_TOUCHSCREEN) || defined(CONFIG_SIM_AJOYSTICK) || \
    defined(CONFIG_SIM_BUTTONS)
  XAllowEvents(g_display, AsyncBoth, CurrentTime);

  /* Grab mouse button 1, enabling mouse-related events */

  XGrabButton(g_display, Button1, AnyModifier, win->window, 1,
              ButtonPressMask | ButtonReleaseMask | ButtonMotionMask,
              GrabModeAsync, GrabModeAsync, None, None);
#endif

  gcval.graphics_exposures = 0;
  win->gc = XCreateGC(g_display, win->window, GCGraphicsExposures, &gcval);

  if (win->gc == NULL)
    {
      XDestroyWindow(g_display, win->window);
      win->window = None;
      return -ENODEV;
    }

  return 0;
}

/****************************************************************************
 * Name: sim_x11errorhandler
 ****************************************************************************/

#ifndef CONFIG_SIM_X11NOSHM
static int sim_x11errorhandler(Display *display, XErrorEvent *event)
{
  g_xerror = 1;
  return 0;
}
#endif

/****************************************************************************
 * Name: sim_x11traperrors
 ****************************************************************************/

#ifndef CONFIG_SIM_X11NOSHM
static void sim_x11traperrors(void)
{
  g_xerror = 0;
  XSetErrorHandler(sim_x11errorhandler);
}
#endif

/****************************************************************************
 * Name: sim_x11untraperrors
 ****************************************************************************/

#ifndef CONFIG_SIM_X11NOSHM
static int sim_x11untraperrors(void)
{
  XSync(g_display, 0);
  XSetErrorHandler(NULL);
  return g_xerror;
}
#endif

/****************************************************************************
 * Name: sim_x11uninitwindow
 ****************************************************************************/

static void sim_x11uninitwindow(x11_window_t *win)
{
  if (!win->initialized)
    {
      return;
    }

#ifndef CONFIG_SIM_X11NOSHM
  if (win->shmcheckpoint > 4)
    {
      XShmDetach(g_display, &win->xshminfo);
    }

  if (win->shmcheckpoint > 3)
    {
      shmdt(win->xshminfo.shmaddr);
    }

  if (win->shmcheckpoint > 2)
    {
      shmctl(win->xshminfo.shmid, IPC_RMID, 0);
    }
#endif

  if (win->shmcheckpoint > 1)
    {
#ifdef CONFIG_SIM_X11NOSHM
      win->image->data = win->framebuffer;
#endif
      XDestroyImage(win->image);
    }

  /* Un-grab the mouse buttons */

#if defined(CONFIG_SIM_TOUCHSCREEN) || defined(CONFIG_SIM_AJOYSTICK) || \
    defined(CONFIG_SIM_BUTTONS)
  XUngrabButton(g_display, Button1, AnyModifier, win->window);
#endif

  if (win->trans_framebuffer)
    {
      free(win->trans_framebuffer);
      win->trans_framebuffer = NULL;
    }

  win->initialized = 0;
}

/****************************************************************************
 * Name: sim_x11uninit
 ****************************************************************************/

static void sim_x11uninit(void)
{
  int i;

  if (g_display == NULL)
    {
      return;
    }

  for (i = 0; i < CONFIG_SIM_X11NWINDOWS; i++)
    {
      sim_x11uninitwindow(&g_windows[i]);
    }

  XCloseDisplay(g_display);
  g_display = NULL;
}

/****************************************************************************
 * Name: sim_x11cleanupshm
 ****************************************************************************/

#ifndef CONFIG_SIM_X11NOSHM
static void sim_x11cleanupshm(x11_window_t *win)
{
  if (win->shmcheckpoint > 1)
    {
      if (!win->useshm && win->framebuffer)
        {
          free(win->framebuffer);
          win->framebuffer = NULL;
        }
    }

  if (win->shmcheckpoint > 0)
    {
      win->shmcheckpoint = 1;
    }
}
#endif

/****************************************************************************
 * Name: sim_x11mapsharedmem
 ****************************************************************************/

static inline int sim_x11mapsharedmem(x11_window_t *win, int depth,
                                      unsigned int fblen, int fbcount,
                                      int interval)
{
#ifndef CONFIG_SIM_X11NOSHM
  Status result;
#endif
  int fbinterval = 0;

  win->shmcheckpoint = 1;
  win->useshm = 0;

#ifndef CONFIG_SIM_X11NOSHM
  if (XShmQueryExtension(g_display))
    {
      win->useshm = 1;

      sim_x11traperrors();
      win->image = XShmCreateImage(g_display,
                                   DefaultVisual(g_display, g_screen),
                                   depth, ZPixmap, NULL, &win->xshminfo,
                                   win->fbpixelwidth, win->fbpixelheight);
      if (sim_x11untraperrors())
        {
          sim_x11cleanupshm(win);
          goto shmerror;
        }

      if (!win->image)
        {
          syslog(LOG_ERR, "Unable to create image for window.\n");
          return -1;
        }

      win->shmcheckpoint++;

      win->xshminfo.shmid = shmget(IPC_PRIVATE,
                                   win->image->bytes_per_line *
                                   (win->image->height * fbcount +
                                   interval * (fbcount - 1)),
                                   IPC_CREAT | 0777);
      if (win->xshminfo.shmid < 0)
        {
          sim_x11cleanupshm(win);
          goto shmerror;
        }

      win->shmcheckpoint++;

      win->image->data = (char *)shmat(win->xshminfo.shmid, 0, 0);
      if (win->image->data == ((char *) -1))
        {
          sim_x11cleanupshm(win);
          goto shmerror;
        }

      win->shmcheckpoint++;

      win->xshminfo.shmaddr = win->image->data;
      win->xshminfo.readOnly = 0;

      sim_x11traperrors();
      result = XShmAttach(g_display, &win->xshminfo);
      if (sim_x11untraperrors() || !result)
        {
          sim_x11cleanupshm(win);
          goto shmerror;
        }

      win->framebuffer = win->image->data;
      win->shmcheckpoint++;
    }
  else
#endif
  if (!win->useshm)
    {
#ifndef CONFIG_SIM_X11NOSHM
shmerror:
#endif
      win->useshm = 0;

      fbinterval = (depth * win->fbpixelwidth / 8) * interval;
      win->framebuffer = malloc(fblen * fbcount +
                                fbinterval * (fbcount - 1));

      win->image = XCreateImage(g_display,
                                DefaultVisual(g_display, g_screen),
                                depth, ZPixmap, 0, win->framebuffer,
                                win->fbpixelwidth, win->fbpixelheight,
                                8, 0);

      if (win->image == NULL)
        {
          syslog(LOG_ERR, "Unable to create image\n");
          return -1;
        }

      win->shmcheckpoint++;
    }

  return 0;
}

/****************************************************************************
 * Name: sim_x11depth16to32
 ****************************************************************************/

static inline void sim_x11depth16to32(void *d_mem, size_t size,
                                      const void *s_mem)
{
  x11_color32_t *dst = d_mem;
  const x11_color16_t *src = s_mem;

  for (size /= 4; size; size--)
    {
      dst->red = (src->red * 263 + 7) >> 5;
      dst->green = (src->green * 259 + 3) >> 6;
      dst->blue = (src->blue * 263 + 7) >> 5;
      dst->alpha = 0xff;
      dst++;
      src++;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: sim_x11initialize
 *
 * Description:
 *   Initialize an X11 window as a framebuffer.
 *
 * Input Parameters:
 *   displayno - Display number (0, 1, 2, ...)
 *   width     - Window width
 *   height    - Window height
 *   fbmem     - Pointer to framebuffer memory (output)
 *   fblen     - Framebuffer length (output)
 *   bpp       - Bits per pixel (output)
 *   stride    - Bytes per row (output)
 *   fbcount   - Number of framebuffers
 *   interval  - Interval lines between framebuffers
 *
 ****************************************************************************/

int sim_x11initialize(int displayno, unsigned short width, unsigned short height,
                      void **fbmem, size_t *fblen, unsigned char *bpp,
                      unsigned short *stride, int fbcount, int interval)
{
  XWindowAttributes windowattributes;
  x11_window_t *win;
  int fbinterval;
  int depth;

  if (displayno < 0 || displayno >= CONFIG_SIM_X11NWINDOWS)
    {
      syslog(LOG_ERR, "Invalid display number: %d\n", displayno);
      return -EINVAL;
    }

  win = &g_windows[displayno];

  /* Save inputs */

  win->fbpixelwidth  = width;
  win->fbpixelheight = height;

  if (fbcount < 1)
    {
      return -EINVAL;
    }

  /* Open display only once for all windows */

  if (g_display == NULL)
    {
      g_display = XOpenDisplay(NULL);
      if (g_display == NULL)
        {
          syslog(LOG_ERR, "Unable to open display.\n");
          return -ENODEV;
        }

      g_screen = DefaultScreen(g_display);
      atexit(sim_x11uninit);
    }

  /* Create the X11 window for this display */

  if (sim_x11createframe(displayno, win) < 0)
    {
      return -ENODEV;
    }

  /* Determine the supported pixel bpp of the current window */

  XGetWindowAttributes(g_display, DefaultRootWindow(g_display),
                       &windowattributes);

  /* Get the pixel depth. If the depth is 24-bits, use 32 because X expects
   * 32-bit alignment anyway.
   */

  depth = windowattributes.depth;
  if (depth == 24)
    {
      depth = 32;
    }

  if (depth != CONFIG_SIM_FBBPP && depth != 32 && CONFIG_SIM_FBBPP != 16)
    {
      return -1;
    }

  *bpp    = depth;
  *stride = (depth * width / 8);
  *fblen  = (*stride * height);

  /* Map the window to shared memory */

  if (sim_x11mapsharedmem(win, windowattributes.depth,
                          *fblen, fbcount, interval) < 0)
    {
      return -1;
    }

  win->fbbpp = depth;
  win->fblen = *fblen;

  /* Create conversion framebuffer */

  if (depth == 32 && CONFIG_SIM_FBBPP == 16)
    {
      *bpp = CONFIG_SIM_FBBPP;
      *stride = (CONFIG_SIM_FBBPP * width / 8);
      *fblen = (*stride * height);
      fbinterval = *stride * interval;

      win->trans_framebuffer = malloc(*fblen * fbcount +
                                      fbinterval * (fbcount - 1));
      if (win->trans_framebuffer == NULL)
        {
          syslog(LOG_ERR, "Failed to allocate trans_framebuffer\n");
          return -1;
        }

      *fbmem = win->trans_framebuffer;
    }
  else
    {
      *fbmem = win->framebuffer;
    }

  if (interval == 0)
    {
      *fblen *= fbcount;
    }

  win->initialized = 1;

  syslog(LOG_INFO, "X11 window %d initialized: %dx%d\n",
         displayno, width, height);

  return 0;
}

/****************************************************************************
 * Name: sim_x11openwindow
 ****************************************************************************/

int sim_x11openwindow(int displayno)
{
  x11_window_t *win;

  if (g_display == NULL)
    {
      return -ENODEV;
    }

  if (displayno < 0 || displayno >= CONFIG_SIM_X11NWINDOWS)
    {
      return -EINVAL;
    }

  win = &g_windows[displayno];

  if (!win->initialized)
    {
      return -ENODEV;
    }

  XMapWindow(g_display, win->window);
  XSync(g_display, 0);

  return 0;
}

/****************************************************************************
 * Name: sim_x11closewindow
 ****************************************************************************/

int sim_x11closewindow(int displayno)
{
  x11_window_t *win;

  if (g_display == NULL)
    {
      return -ENODEV;
    }

  if (displayno < 0 || displayno >= CONFIG_SIM_X11NWINDOWS)
    {
      return -EINVAL;
    }

  win = &g_windows[displayno];

  if (!win->initialized)
    {
      return -ENODEV;
    }

  XUnmapWindow(g_display, win->window);
  XSync(g_display, 0);

  return 0;
}

/****************************************************************************
 * Name: sim_x11setoffset
 ****************************************************************************/

int sim_x11setoffset(int displayno, unsigned int offset)
{
  x11_window_t *win;

  if (g_display == NULL)
    {
      return -ENODEV;
    }

  if (displayno < 0 || displayno >= CONFIG_SIM_X11NWINDOWS)
    {
      return -EINVAL;
    }

  win = &g_windows[displayno];

  if (!win->initialized)
    {
      return -ENODEV;
    }

  if (win->fbbpp == 32 && CONFIG_SIM_FBBPP == 16)
    {
      win->image->data = win->framebuffer + (offset << 1);
      win->offset = offset;
    }
  else
    {
      win->image->data = win->framebuffer + offset;
    }

  return 0;
}

/****************************************************************************
 * Name: sim_x11cmap
 ****************************************************************************/

#ifdef CONFIG_FB_CMAP
int sim_x11cmap(int displayno, unsigned short first, unsigned short len,
                unsigned char *red, unsigned char *green,
                unsigned char *blue, unsigned char *transp)
{
  Colormap cmap;
  int ndx;

  if (g_display == NULL)
    {
      return -ENODEV;
    }

  if (displayno < 0 || displayno >= CONFIG_SIM_X11NWINDOWS)
    {
      return -EINVAL;
    }

  /* Convert each color to X11 scaling */

  cmap = DefaultColormap(g_display, g_screen);
  for (ndx = first; ndx < first + len; ndx++)
    {
      XColor color;

      /* Convert to RGB. In the NuttX cmap, each component
       * ranges from 0-255; for X11 the range is 0-65536
       */

      color.red   = (short)(*red++) << 8;
      color.green = (short)(*green++) << 8;
      color.blue  = (short)(*blue++) << 8;
      color.flags = DoRed | DoGreen | DoBlue;

      /* Then allocate a color for this selection */

      if (!XAllocColor(g_display, cmap, &color))
        {
          syslog(LOG_ERR, "Failed to allocate color%d\n", ndx);
          return -1;
        }
    }

  return 0;
}
#endif

/****************************************************************************
 * Name: sim_x11update
 ****************************************************************************/

int sim_x11update(int displayno)
{
  x11_window_t *win;

  if (g_display == NULL)
    {
      return -ENODEV;
    }

  if (displayno < 0 || displayno >= CONFIG_SIM_X11NWINDOWS)
    {
      return -EINVAL;
    }

  win = &g_windows[displayno];

  if (!win->initialized)
    {
      return -ENODEV;
    }

#ifndef CONFIG_SIM_X11NOSHM
  if (win->useshm)
    {
      XShmPutImage(g_display, win->window, win->gc, win->image, 0, 0, 0, 0,
                   win->fbpixelwidth, win->fbpixelheight, 0);
    }
  else
#endif
    {
      XPutImage(g_display, win->window, win->gc, win->image, 0, 0, 0, 0,
                win->fbpixelwidth, win->fbpixelheight);
    }

  if (win->fbbpp == 32 && CONFIG_SIM_FBBPP == 16)
    {
      sim_x11depth16to32(win->image->data,
                         win->fblen,
                         win->trans_framebuffer + win->offset);
    }

  XSync(g_display, 0);

  return 0;
}

/****************************************************************************
 * Name: sim_x11getdisplayno
 *
 * Description:
 *   Get the display number for a given X11 window handle.
 *   Used for multi-window coordinate translation.
 *
 ****************************************************************************/

int sim_x11getdisplayno(unsigned long window)
{
  int i;

  for (i = 0; i < CONFIG_SIM_X11NWINDOWS; i++)
    {
      if (g_windows[i].initialized && g_windows[i].window == window)
        {
          return i;
        }
    }

  return 0;  /* Default to first display */
}

/****************************************************************************
 * Name: sim_x11getwidth
 *
 * Description:
 *   Get the configured framebuffer width.
 *   Used for multi-window coordinate translation.
 *
 ****************************************************************************/

unsigned short sim_x11getwidth(void)
{
  return CONFIG_SIM_FBWIDTH;
}