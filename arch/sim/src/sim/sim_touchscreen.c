/****************************************************************************
 * arch/sim/src/sim/sim_touchscreen.c
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

#include <sys/types.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <debug.h>
#include <errno.h>

#include <nuttx/input/touchscreen.h>
#include <nuttx/wqueue.h>

#include "sim_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Driver support ***********************************************************/

/* This format is used to construct the /dev/input[n] device driver path.  It
 * defined here so that it will be used consistently in all places.
 */

#define DEV_FORMAT   "/dev/input%d"
#define DEV_NAMELEN  16

#ifdef CONFIG_SIM_MULTI_SCREEN_SUPPORT

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* This structure describes the state of one touchscreen driver instance */

struct sim_touch_event_s
{
  int x;
  int y;
  int buttons;
};

#define TOUCH_BUF_SIZE 16

struct sim_dev_s
{
  int eventloop;
  uint8_t id;
  uint8_t contact;
  uint8_t minor;
  struct touch_lowerhalf_s lower;
  struct work_s work;
  struct sim_touch_event_s buf[TOUCH_BUF_SIZE];
  volatile int head;
  volatile int tail;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct sim_dev_s g_simtouchscreen[CONFIG_SIM_SCREEN_COUNT];

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: sim_tsc_initialize
 *
 * Description:
 *   Configure the simulated touchscreen.  This will register the driver as
 *   /dev/inputN where N is the minor device number
 *
 * Input Parameters:
 *   minor   - The input device minor number
 *
 * Returned Value:
 *   Zero is returned on success.  Otherwise, a negated errno value is
 *   returned to indicate the nature of the failure.
 *
 ****************************************************************************/

int sim_tsc_initialize(int minor)
{
  struct sim_dev_s *priv;
  char devname[DEV_NAMELEN];
  int ret;

  iinfo("minor: %d\n", minor);

  /* Debug-only sanity checks */

  if (minor < 0 || minor >= CONFIG_SIM_SCREEN_COUNT)
    {
      return -EINVAL;
    }

  priv = &g_simtouchscreen[minor];

  /* Initialize the touchscreen device driver instance */

  memset(priv, 0, sizeof(struct sim_dev_s));

  priv->minor = minor;
  priv->lower.maxpoint = 1;

  /* Register the device as an input device */

  snprintf(devname, sizeof(devname), DEV_FORMAT, minor);
  iinfo("Registering %s\n", devname);

  ret = touch_register(&priv->lower, devname, 1);
  if (ret < 0)
    {
      ierr("ERROR: touch_register() failed: %d\n", ret);
      return ret;
    }

  /* Enable X11 event processing from the IDLE loop */

  priv->eventloop = 1;

  /* And return success */

  return OK;
}

/****************************************************************************
 * Name: sim_tsc_uninitialize
 *
 * Description:
 *   Uninitialized the simulated touchscreen
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Return OK if success or negative value of the error.
 *
 ****************************************************************************/

int sim_tsc_uninitialize(void)
{
  struct sim_dev_s *priv;
  char devname[DEV_NAMELEN];
  int i;

  for (i = 0; i < CONFIG_SIM_SCREEN_COUNT; i++)
    {
      priv = &g_simtouchscreen[i];
      if (priv->eventloop == 0) continue;

      /* Stop the event loop (Hmm.. the caller must be sure that there are no
       * open references to the touchscreen driver.  This might better be
       * done in close() using a reference count).
       */

      priv->eventloop = 0;

      /* Un-register the device */

      snprintf(devname, sizeof(devname), DEV_FORMAT, priv->minor);
      iinfo("Un-registering %s\n", devname);

      touch_unregister(&priv->lower, devname);
    }

  return OK;
}

/****************************************************************************
 * Name: sim_touch_worker
 ****************************************************************************/

static void sim_touch_worker(void *arg)
{
  struct sim_dev_s *priv = (struct sim_dev_s *)arg;
  struct touch_sample_s sample;
  bool pendown;
  int next;

  while (priv->head != priv->tail)
    {
      /* Pop event */

      int x = priv->buf[priv->tail].x;
      int y = priv->buf[priv->tail].y;
      int buttons = priv->buf[priv->tail].buttons;

      next = priv->tail + 1;
      if (next >= TOUCH_BUF_SIZE)
        {
          next = 0;
        }

      priv->tail = next;

      /* Logic from sim_buttonevent */

      iinfo("display=%d x=%d y=%d buttons=%02x\n", priv->minor, x, y, buttons);

      /* Any button press will count as pendown. */

      pendown = (buttons != 0);

      /* Handle the change from pen down to pen up */

      if (!pendown)
        {
          /* Ignore the pend up if the pen was already up
           * (CONTACT_NONE == pen up and  already reported.
           *  CONTACT_UP == pen up, but not reported)
           */

          if (priv->contact == TOUCH_UP)
            {
              continue;
            }

          /* Not yet reported */

          priv->contact = TOUCH_UP;
          sample.point[0].flags = TOUCH_UP | TOUCH_ID_VALID;
        }
      else
        {
          /* Save the measurements */

          sample.point[0].x = x;
          sample.point[0].y = y;

          /* Note the availability of new measurements:
           * If this is the first (acknowledged) pen down report, then report
           * this as the first contact.  If flags == TOUCH_DOWN, it will be
           * set to set to TOUCH_MOVE after the contact is first sampled.
           */

          if (priv->contact == TOUCH_UP)
            {
              /* First contact */

              priv->contact = TOUCH_DOWN;
              sample.point[0].flags = TOUCH_DOWN | TOUCH_ID_VALID |
                                      TOUCH_POS_VALID | TOUCH_PRESSURE_VALID;

              /* Indicate the availability of new sample data for this ID */

              priv->id++;
            }
          else
            {
               priv->contact = TOUCH_MOVE;
               sample.point[0].flags = TOUCH_MOVE | TOUCH_ID_VALID |
                                       TOUCH_POS_VALID | TOUCH_PRESSURE_VALID;
            }
        }

      sample.npoints            = 1;
      sample.point[0].h         = 1;
      sample.point[0].w         = 1;
      sample.point[0].pressure  = 42;
      sample.point[0].id        = priv->id;

      /* Report data changes */

      touch_event(priv->lower.priv, &sample);
    }
}

/****************************************************************************
 * Name: sim_buttonevent
 ****************************************************************************/

void sim_buttonevent(int display, int x, int y, int buttons)
{
  struct sim_dev_s  *priv;
  int next;

  if (display < 0 || display >= CONFIG_SIM_SCREEN_COUNT)
    {
      return;
    }

  priv = &g_simtouchscreen[display];

  if (priv->eventloop == 0)
    {
      return;
    }

  next = priv->head + 1;
  if (next >= TOUCH_BUF_SIZE)
    {
      next = 0;
    }

  if (next != priv->tail)
    {
      priv->buf[priv->head].x = x;
      priv->buf[priv->head].y = y;
      priv->buf[priv->head].buttons = buttons;
      priv->head = next;

      work_queue(HPWORK, &priv->work, sim_touch_worker, priv, 0);
    }
}

#else /* CONFIG_SIM_MULTI_SCREEN_SUPPORT */

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* This structure describes the state of one touchscreen driver instance */

struct sim_dev_s
{
  int eventloop;
  uint8_t id;                          /* Current touch point ID */
  uint8_t contact;                     /* Last contact state */
  uint8_t minor;                       /* Minor device number */
  struct touch_lowerhalf_s lower;      /* Touchsrceen lowerhalf */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Only one simulated touchscreen is supported so the driver state
 * structure may as well be pre-allocated.
 */

static struct sim_dev_s g_simtouchscreen;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: sim_tsc_initialize
 *
 * Description:
 *   Configure the simulated touchscreen.  This will register the driver as
 *   /dev/inputN where N is the minor device number
 *
 * Input Parameters:
 *   minor   - The input device minor number
 *
 * Returned Value:
 *   Zero is returned on success.  Otherwise, a negated errno value is
 *   returned to indicate the nature of the failure.
 *
 ****************************************************************************/

int sim_tsc_initialize(int minor)
{
  struct sim_dev_s *priv = (struct sim_dev_s *)&g_simtouchscreen;
  char devname[DEV_NAMELEN];
  int ret;

  iinfo("minor: %d\n", minor);

  /* Debug-only sanity checks */

  DEBUGASSERT(minor >= 0 && minor < 100);

  /* Initialize the touchscreen device driver instance */

  memset(priv, 0, sizeof(struct sim_dev_s));

  priv->minor = minor;
  priv->lower.maxpoint = 1;

  /* Register the device as an input device */

  snprintf(devname, sizeof(devname), DEV_FORMAT, minor);
  iinfo("Registering %s\n", devname);

  ret = touch_register(&priv->lower, devname, 1);
  if (ret < 0)
    {
      ierr("ERROR: touch_register() failed: %d\n", ret);
      return ret;
    }

  /* Enable X11 event processing from the IDLE loop */

  priv->eventloop = 1;

  /* And return success */

  return OK;
}

/****************************************************************************
 * Name: sim_tsc_uninitialize
 *
 * Description:
 *   Uninitialized the simulated touchscreen
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Return OK if success or negative value of the error.
 *
 ****************************************************************************/

int sim_tsc_uninitialize(void)
{
  struct sim_dev_s *priv = (struct sim_dev_s *)&g_simtouchscreen;
  char devname[DEV_NAMELEN];

  /* Stop the event loop (Hmm.. the caller must be sure that there are no
   * open references to the touchscreen driver.  This might better be
   * done in close() using a reference count).
   */

  priv->eventloop = 0;

  /* Un-register the device */

  snprintf(devname, sizeof(devname), DEV_FORMAT, priv->minor);
  iinfo("Un-registering %s\n", devname);

  touch_unregister(&priv->lower, devname);

  return OK;
}

/****************************************************************************
 * Name: sim_buttonevent
 ****************************************************************************/

void sim_buttonevent(int x, int y, int buttons)
{
  struct sim_dev_s  *priv = (struct sim_dev_s *)&g_simtouchscreen;
  struct touch_sample_s sample;   /* Sampled touch point data */
  bool                  pendown;  /* true: pen is down */

  if (priv->eventloop == 0)
    {
      return;
    }

  iinfo("x=%d y=%d buttons=%02x\n", x, y, buttons);

  /* Any button press will count as pendown. */

  pendown = (buttons != 0);

  /* Handle the change from pen down to pen up */

  if (!pendown)
    {
      /* Ignore the pend up if the pen was already up
       * (CONTACT_NONE == pen up and  already reported.
       *  CONTACT_UP == pen up, but not reported)
       */

      if (priv->contact == TOUCH_UP)
        {
          return;
        }

      /* Not yet reported */

      priv->contact = TOUCH_UP;
      sample.point[0].flags = TOUCH_UP | TOUCH_ID_VALID;
    }
  else
    {
      /* Save the measurements */

      sample.point[0].x = x;
      sample.point[0].y = y;

      /* Note the availability of new measurements:
       * If this is the first (acknowledged) pen down report, then report
       * this as the first contact.  If flags == TOUCH_DOWN, it will be
       * set to set to TOUCH_MOVE after the contact is first sampled.
       */

      if (priv->contact == TOUCH_UP)
        {
          /* First contact */

          priv->contact = TOUCH_DOWN;
          sample.point[0].flags = TOUCH_DOWN | TOUCH_ID_VALID |
                                  TOUCH_POS_VALID | TOUCH_PRESSURE_VALID;

          /* Indicate the availability of new sample data for this ID */

          priv->id++;
        }
      else
        {
           priv->contact = TOUCH_MOVE;
           sample.point[0].flags = TOUCH_MOVE | TOUCH_ID_VALID |
                                   TOUCH_POS_VALID | TOUCH_PRESSURE_VALID;
        }
    }

  sample.npoints            = 1;
  sample.point[0].h         = 1;
  sample.point[0].w         = 1;
  sample.point[0].pressure  = 42;
  sample.point[0].id        = priv->id;

  /* Report data changes */

  touch_event(priv->lower.priv, &sample);
}

#endif /* CONFIG_SIM_MULTI_SCREEN_SUPPORT */
