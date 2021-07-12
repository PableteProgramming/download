/* GDK - The GIMP Drawing Kit
 *
 * gdkglcontext-glx.c: GLX specific wrappers
 *
 * SPDX-FileCopyrightText: 2014  Emmanuele Bassi
 * SPDX-FileCopyrightText: 2021  GNOME Foundation
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "gdkglcontext-x11.h"
#include "gdkdisplay-x11.h"
#include "gdkprivate-x11.h"
#include "gdkscreen-x11.h"

#include "gdkx11display.h"
#include "gdkx11glcontext.h"
#include "gdkx11screen.h"
#include "gdkx11surface.h"
#include "gdkvisual-x11.h"
#include "gdkx11property.h"
#include <X11/Xatom.h>

#include "gdkinternals.h"
#include "gdkprofilerprivate.h"
#include "gdkintl.h"

#include <cairo-xlib.h>

#include <epoxy/glx.h>

struct _GdkX11GLContextGLX
{
  GdkX11GLContext parent_instance;

  GLXContext glx_context;
  GLXFBConfig glx_config;
  GLXDrawable attached_drawable;
  GLXDrawable unattached_drawable;

#ifdef HAVE_XDAMAGE
  GLsync frame_fence;
  Damage xdamage;
#endif

  guint is_direct : 1;
};

typedef struct _GdkX11GLContextClass    GdkX11GLContextGLXClass;

typedef struct {
  GdkDisplay *display;

  GLXDrawable glx_drawable;

  Window dummy_xwin;
  GLXWindow dummy_glx;

  guint32 last_frame_counter;
} DrawableInfo;

G_DEFINE_TYPE (GdkX11GLContextGLX, gdk_x11_gl_context_glx, GDK_TYPE_X11_GL_CONTEXT)

static void
drawable_info_free (gpointer data_)
{
  DrawableInfo *data = data_;
  Display *dpy;

  gdk_x11_display_error_trap_push (data->display);

  dpy = gdk_x11_display_get_xdisplay (data->display);

  if (data->glx_drawable)
    glXDestroyWindow (dpy, data->glx_drawable);

  if (data->dummy_glx)
    glXDestroyWindow (dpy, data->dummy_glx);

  if (data->dummy_xwin)
    XDestroyWindow (dpy, data->dummy_xwin);

  gdk_x11_display_error_trap_pop_ignored (data->display);

  g_slice_free (DrawableInfo, data);
}

static DrawableInfo *
get_glx_drawable_info (GdkSurface *surface)
{
  return g_object_get_data (G_OBJECT (surface), "-gdk-x11-surface-glx-info");
}

static void
set_glx_drawable_info (GdkSurface    *surface,
                       DrawableInfo *info)
{
  g_object_set_data_full (G_OBJECT (surface), "-gdk-x11-surface-glx-info",
                          info,
                          drawable_info_free);
}

static void
maybe_wait_for_vblank (GdkDisplay  *display,
                       GLXDrawable  drawable)
{
  GdkX11Display *display_x11 = GDK_X11_DISPLAY (display);
  Display *dpy = gdk_x11_display_get_xdisplay (display);

  if (display_x11->has_glx_sync_control)
    {
      gint64 ust, msc, sbc;

      glXGetSyncValuesOML (dpy, drawable, &ust, &msc, &sbc);
      glXWaitForMscOML (dpy, drawable,
                        0, 2, (msc + 1) % 2,
                        &ust, &msc, &sbc);
    }
  else if (display_x11->has_glx_video_sync)
    {
      guint32 current_count;

      glXGetVideoSyncSGI (&current_count);
      glXWaitVideoSyncSGI (2, (current_count + 1) % 2, &current_count);
    }
}

static void
gdk_x11_gl_context_glx_end_frame (GdkDrawContext *draw_context,
                                  cairo_region_t *painted)
{
  GdkGLContext *context = GDK_GL_CONTEXT (draw_context);
  GdkX11GLContext *context_x11 = GDK_X11_GL_CONTEXT (context);
  GdkX11GLContextGLX *context_glx = GDK_X11_GL_CONTEXT_GLX (context);
  GdkSurface *surface = gdk_gl_context_get_surface (context);
  GdkDisplay *display = gdk_gl_context_get_display (context);
  Display *dpy = gdk_x11_display_get_xdisplay (display);
  GdkX11Display *display_x11 = GDK_X11_DISPLAY (display);
  DrawableInfo *info;
  GLXDrawable drawable;

  GDK_DRAW_CONTEXT_CLASS (gdk_x11_gl_context_glx_parent_class)->end_frame (draw_context, painted);
  if (gdk_gl_context_get_shared_context (context) != NULL)
    return;

  gdk_gl_context_make_current (context);

  info = get_glx_drawable_info (surface);

  drawable = context_glx->attached_drawable;

  GDK_DISPLAY_NOTE (display, OPENGL,
            g_message ("Flushing GLX buffers for drawable %lu (window: %lu), frame sync: %s",
                       (unsigned long) drawable,
                       (unsigned long) gdk_x11_surface_get_xid (surface),
                       context_x11->do_frame_sync ? "yes" : "no"));

  gdk_profiler_add_mark (GDK_PROFILER_CURRENT_TIME, 0, "x11", "swap buffers");

  /* if we are going to wait for the vertical refresh manually
   * we need to flush pending redraws, and we also need to wait
   * for that to finish, otherwise we are going to tear.
   *
   * obviously, this condition should not be hit if we have
   * GLX_SGI_swap_control, and we ask the driver to do the right
   * thing.
   */
  if (context_x11->do_frame_sync)
    {
      guint32 end_frame_counter = 0;
      gboolean has_counter = display_x11->has_glx_video_sync;
      gboolean can_wait = display_x11->has_glx_video_sync || display_x11->has_glx_sync_control;

      if (display_x11->has_glx_video_sync)
        glXGetVideoSyncSGI (&end_frame_counter);

      if (context_x11->do_frame_sync && !display_x11->has_glx_swap_interval)
        {
          glFinish ();

          if (has_counter && can_wait)
            {
              guint32 last_counter = info != NULL ? info->last_frame_counter : 0;

              if (last_counter == end_frame_counter)
                maybe_wait_for_vblank (display, drawable);
            }
          else if (can_wait)
            maybe_wait_for_vblank (display, drawable);
        }
    }

  gdk_x11_surface_pre_damage (surface);

#ifdef HAVE_XDAMAGE
  if (context_glx->xdamage != 0 && _gdk_x11_surface_syncs_frames (surface))
    {
      g_assert (context_glx->frame_fence == 0);

      context_glx->frame_fence = glFenceSync (GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

      /* We consider the frame still getting painted until the GL operation is
       * finished, and the window gets damage reported from the X server.
       * It's only at this point the compositor can be sure it has full
       * access to the new updates.
       */
      _gdk_x11_surface_set_frame_still_painting (surface, TRUE);
    }
#endif

  glXSwapBuffers (dpy, drawable);

  if (context_x11->do_frame_sync && info != NULL && display_x11->has_glx_video_sync)
    glXGetVideoSyncSGI (&info->last_frame_counter);
}

static cairo_region_t *
gdk_x11_gl_context_glx_get_damage (GdkGLContext *context)
{
  GdkDisplay *display = gdk_draw_context_get_display (GDK_DRAW_CONTEXT (context));
  GdkX11Display *display_x11 = GDK_X11_DISPLAY (display);
  Display *dpy = gdk_x11_display_get_xdisplay (display);
  unsigned int buffer_age = 0;

  if (display_x11->has_glx_buffer_age)
    {
      GdkGLContext *shared;
      GdkX11GLContextGLX *shared_glx;

      shared = gdk_gl_context_get_shared_context (context);
      if (shared == NULL)
        shared = context;

      shared_glx = GDK_X11_GL_CONTEXT_GLX (shared);

      gdk_gl_context_make_current (shared);
      glXQueryDrawable (dpy, shared_glx->attached_drawable,
                        GLX_BACK_BUFFER_AGE_EXT, &buffer_age);

      switch (buffer_age)
        {
          case 1:
            return cairo_region_create ();
            break;

          case 2:
            if (context->old_updated_area[0])
              return cairo_region_copy (context->old_updated_area[0]);
            break;

          case 3:
            if (context->old_updated_area[0] &&
                context->old_updated_area[1])
              {
                cairo_region_t *damage = cairo_region_copy (context->old_updated_area[0]);
                cairo_region_union (damage, context->old_updated_area[1]);
                return damage;
              }
            break;

          default:
            ;
        }
    }

  return GDK_GL_CONTEXT_CLASS (gdk_x11_gl_context_glx_parent_class)->get_damage (context);
}

static XVisualInfo *
find_xvisinfo_for_fbconfig (GdkDisplay  *display,
                            GLXFBConfig  config)
{
  Display *dpy = gdk_x11_display_get_xdisplay (display);

  return glXGetVisualFromFBConfig (dpy, config);
}

static GLXContext
create_gl3_context (GdkDisplay   *display,
                    GLXFBConfig   config,
                    GdkGLContext *share,
                    int           profile,
                    int           flags,
                    int           major,
                    int           minor)
{
  int attrib_list[] = {
    GLX_CONTEXT_PROFILE_MASK_ARB, profile,
    GLX_CONTEXT_MAJOR_VERSION_ARB, major,
    GLX_CONTEXT_MINOR_VERSION_ARB, minor,
    GLX_CONTEXT_FLAGS_ARB, flags,
    None,
  };
  GLXContext res;

  GdkX11GLContextGLX *share_glx = NULL;

  if (share != NULL)
    share_glx = GDK_X11_GL_CONTEXT_GLX (share);

  gdk_x11_display_error_trap_push (display);

  res = glXCreateContextAttribsARB (gdk_x11_display_get_xdisplay (display),
                                    config,
                                    share_glx != NULL ? share_glx->glx_context : NULL,
                                    True,
                                    attrib_list);

  if (gdk_x11_display_error_trap_pop (display))
    return NULL;

  return res;
}

static GLXContext
create_legacy_context (GdkDisplay   *display,
                       GLXFBConfig   config,
                       GdkGLContext *share)
{
  GdkX11GLContextGLX *share_glx = NULL;
  GLXContext res;

  if (share != NULL)
    share_glx = GDK_X11_GL_CONTEXT_GLX (share);

  gdk_x11_display_error_trap_push (display);

  res = glXCreateNewContext (gdk_x11_display_get_xdisplay (display),
                             config,
                             GLX_RGBA_TYPE,
                             share_glx != NULL ? share_glx->glx_context : NULL,
                             TRUE);

  if (gdk_x11_display_error_trap_pop (display))
    return NULL;

  return res;
}

#ifdef HAVE_XDAMAGE
static void
bind_context_for_frame_fence (GdkX11GLContextGLX *self)
{
  GdkX11GLContextGLX *current_context_glx;
  GLXContext current_glx_context = NULL;
  GdkGLContext *current_context;
  gboolean needs_binding = TRUE;

  /* We don't care if the passed context is the current context,
   * necessarily, but we do care that *some* context that can
   * see the sync object is bound.
   *
   * If no context is bound at all, the GL dispatch layer will
   * make glClientWaitSync() silently return 0.
   */
  current_glx_context = glXGetCurrentContext ();

  if (current_glx_context == NULL)
    goto out;

  current_context = gdk_gl_context_get_current ();

  if (current_context == NULL)
    goto out;

  current_context_glx = GDK_X11_GL_CONTEXT_GLX (current_context);

  /* If the GLX context was changed out from under GDK, then
   * that context may not be one that is able to see the
   * created fence object.
   */
  if (current_context_glx->glx_context != current_glx_context)
    goto out;

  needs_binding = FALSE;

out:
  if (needs_binding)
    gdk_gl_context_make_current (GDK_GL_CONTEXT (self));
}

static void
finish_frame (GdkGLContext *context)
{
  GdkX11GLContextGLX *context_glx = GDK_X11_GL_CONTEXT_GLX (context);
  GdkSurface *surface = gdk_gl_context_get_surface (context);

  if (context_glx->xdamage == 0)
    return;

  if (context_glx->frame_fence == 0)
    return;

  glDeleteSync (context_glx->frame_fence);
  context_glx->frame_fence = 0;

  _gdk_x11_surface_set_frame_still_painting (surface, FALSE);
}

static gboolean
on_gl_surface_xevent (GdkGLContext   *context,
                      XEvent         *xevent,
                      GdkX11Display  *display_x11)
{
  GdkX11GLContextGLX *context_glx = GDK_X11_GL_CONTEXT_GLX (context);
  GdkX11GLContext *context_x11 = GDK_X11_GL_CONTEXT (context);
  XDamageNotifyEvent *damage_xevent;

  if (!context_x11->is_attached)
    return FALSE;

  if (xevent->type != (display_x11->damage_event_base + XDamageNotify))
    return FALSE;

  damage_xevent = (XDamageNotifyEvent *) xevent;

  if (damage_xevent->damage != context_glx->xdamage)
    return FALSE;

  if (context_glx->frame_fence)
    {
      GLenum wait_result;

      bind_context_for_frame_fence (context_glx);

      wait_result = glClientWaitSync (context_glx->frame_fence, 0, 0);

      switch (wait_result)
        {
          /* We assume that if the fence has been signaled, that this damage
           * event is the damage event that was triggered by the GL drawing
           * associated with the fence. That's, technically, not necessarly
           * always true. The X server could have generated damage for
           * an unrelated event (say the size of the window changing), at
           * just the right moment such that we're picking it up instead.
           *
           * We're choosing not to handle this edge case, but if it does ever
           * happen in the wild, it could lead to slight underdrawing by
           * the compositor for one frame. In the future, if we find out
           * this edge case is noticeable, we can compensate by copying the
           * painted region from gdk_x11_gl_context_end_frame and subtracting
           * damaged areas from the copy as they come in. Once the copied
           * region goes empty, we know that there won't be any underdraw,
           * and can mark painting has finished. It's not worth the added
           * complexity and resource usage to do this bookkeeping, however,
           * unless the problem is practically visible.
           */
          case GL_ALREADY_SIGNALED:
          case GL_CONDITION_SATISFIED:
          case GL_WAIT_FAILED:
            if (wait_result == GL_WAIT_FAILED)
              g_warning ("failed to wait on GL fence associated with last swap buffers call");
            finish_frame (context);
            break;

          /* We assume that if the fence hasn't been signaled, that this
           * damage event is not the damage event that was triggered by the
           * GL drawing associated with the fence. That's only true for
           * the Nvidia vendor driver. When using open source drivers, damage
           * is emitted immediately on swap buffers, before the fence ever
           * has a chance to signal.
           */
          case GL_TIMEOUT_EXPIRED:
            break;
          default:
            g_error ("glClientWaitSync returned unexpected result: %x", (guint) wait_result);
        }
    }

  return FALSE;
}

static void
on_surface_state_changed (GdkGLContext *context)
{
  GdkSurface *surface = gdk_gl_context_get_surface (context);

  if (GDK_SURFACE_IS_MAPPED (surface))
    return;

  /* If we're about to withdraw the surface, then we don't care if the frame is
   * still getting rendered by the GPU. The compositor is going to remove the surface
   * from the scene anyway, so wrap up the frame.
   */
  finish_frame (context);
}
#endif

static gboolean
gdk_x11_gl_context_glx_realize (GdkGLContext  *context,
                                GError       **error)
{
  GdkX11Display *display_x11;
  GdkDisplay *display;
  GdkX11GLContextGLX *context_glx;
  XVisualInfo *xvisinfo;
  Display *dpy;
  DrawableInfo *info;
  GdkGLContext *share;
  GdkGLContext *shared_data_context;
  GdkSurface *surface;
  gboolean debug_bit, compat_bit, legacy_bit, es_bit;
  int major, minor, flags;

  surface = gdk_gl_context_get_surface (context);
  display = gdk_surface_get_display (surface);
  dpy = gdk_x11_display_get_xdisplay (display);
  context_glx = GDK_X11_GL_CONTEXT_GLX (context);
  display_x11 = GDK_X11_DISPLAY (display);
  share = gdk_gl_context_get_shared_context (context);
  shared_data_context = gdk_surface_get_shared_data_gl_context (surface);

  gdk_gl_context_get_required_version (context, &major, &minor);
  debug_bit = gdk_gl_context_get_debug_enabled (context);
  compat_bit = gdk_gl_context_get_forward_compatible (context);

  /* If there is no glXCreateContextAttribsARB() then we default to legacy */
  legacy_bit = !display_x11->has_glx_create_context || GDK_DISPLAY_DEBUG_CHECK (display, GL_LEGACY);

  es_bit = (GDK_DISPLAY_DEBUG_CHECK (display, GL_GLES) || (share != NULL && gdk_gl_context_get_use_es (share))) &&
           (display_x11->has_glx_create_context && display_x11->has_glx_create_es2_context);

  /* We cannot share legacy contexts with core profile ones, so the
   * shared context is the one that decides if we're going to create
   * a legacy context or not.
   */
  if (share != NULL && gdk_gl_context_is_legacy (share))
    legacy_bit = TRUE;

  flags = 0;
  if (debug_bit)
    flags |= GLX_CONTEXT_DEBUG_BIT_ARB;
  if (compat_bit)
    flags |= GLX_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB;

  GDK_DISPLAY_NOTE (display, OPENGL,
            g_message ("Creating GLX context (GL version:%d.%d, debug:%s, forward:%s, legacy:%s, es:%s)",
                       major, minor,
                       debug_bit ? "yes" : "no",
                       compat_bit ? "yes" : "no",
                       legacy_bit ? "yes" : "no",
                       es_bit ? "yes" : "no"));

  /* If we have access to GLX_ARB_create_context_profile then we can ask for
   * a compatibility profile; if we don't, then we have to fall back to the
   * old GLX 1.3 API.
   */
  if (legacy_bit && !GDK_X11_DISPLAY (display)->has_glx_create_context)
    {
      GDK_DISPLAY_NOTE (display, OPENGL, g_message ("Creating legacy GL context on request"));
      context_glx->glx_context = create_legacy_context (display, context_glx->glx_config, share ? share : shared_data_context);
    }
  else
    {
      int profile;

      if (es_bit)
        profile = GLX_CONTEXT_ES2_PROFILE_BIT_EXT;
      else
        profile = legacy_bit ? GLX_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB
                             : GLX_CONTEXT_CORE_PROFILE_BIT_ARB;

      /* We need to tweak the version, otherwise we may end up requesting
       * a compatibility context with a minimum version of 3.2, which is
       * an error
       */
      if (legacy_bit)
        {
          major = 3;
          minor = 0;
        }

      GDK_DISPLAY_NOTE (display, OPENGL, g_message ("Creating GL3 context"));
      context_glx->glx_context = create_gl3_context (display,
                                                     context_glx->glx_config,
                                                     share ? share : shared_data_context,
                                                     profile, flags, major, minor);

      /* Fall back to legacy in case the GL3 context creation failed */
      if (context_glx->glx_context == NULL)
        {
          GDK_DISPLAY_NOTE (display, OPENGL, g_message ("Creating fallback legacy context"));
          context_glx->glx_context = create_legacy_context (display, context_glx->glx_config, share ? share : shared_data_context);
          legacy_bit = TRUE;
          es_bit = FALSE;
        }
    }

  if (context_glx->glx_context == NULL)
    {
      g_set_error_literal (error, GDK_GL_ERROR,
                           GDK_GL_ERROR_NOT_AVAILABLE,
                           _("Unable to create a GL context"));
      return FALSE;
    }

  /* Ensure that any other context is created with a legacy bit set */
  gdk_gl_context_set_is_legacy (context, legacy_bit);

  /* Ensure that any other context is created with an ES bit set */
  gdk_gl_context_set_use_es (context, es_bit);

  xvisinfo = find_xvisinfo_for_fbconfig (display, context_glx->glx_config);

  info = get_glx_drawable_info (surface);
  if (info == NULL)
    {
      XSetWindowAttributes attrs;
      unsigned long mask;

      gdk_x11_display_error_trap_push (display);

      info = g_slice_new0 (DrawableInfo);
      info->display = display;
      info->last_frame_counter = 0;

      attrs.override_redirect = True;
      attrs.colormap = XCreateColormap (dpy, DefaultRootWindow (dpy), xvisinfo->visual, AllocNone);
      attrs.border_pixel = 0;
      mask = CWOverrideRedirect | CWColormap | CWBorderPixel;
      info->dummy_xwin = XCreateWindow (dpy, DefaultRootWindow (dpy),
                                        -100, -100, 1, 1,
                                        0,
                                        xvisinfo->depth,
                                        CopyFromParent,
                                        xvisinfo->visual,
                                        mask,
                                        &attrs);
      XMapWindow(dpy, info->dummy_xwin);

      if (display_x11->glx_version >= 13)
        {
          info->glx_drawable = glXCreateWindow (dpy, context_glx->glx_config,
                                                gdk_x11_surface_get_xid (surface),
                                                NULL);
          info->dummy_glx = glXCreateWindow (dpy, context_glx->glx_config, info->dummy_xwin, NULL);
        }

      if (gdk_x11_display_error_trap_pop (display))
        {
          g_set_error_literal (error, GDK_GL_ERROR,
                               GDK_GL_ERROR_NOT_AVAILABLE,
                               _("Unable to create a GL context"));

          XFree (xvisinfo);
          drawable_info_free (info);
          glXDestroyContext (dpy, context_glx->glx_context);
          context_glx->glx_context = NULL;

          return FALSE;
        }

      set_glx_drawable_info (surface, info);
    }

  XFree (xvisinfo);

  context_glx->attached_drawable = info->glx_drawable ? info->glx_drawable : gdk_x11_surface_get_xid (surface);
  context_glx->unattached_drawable = info->dummy_glx ? info->dummy_glx : info->dummy_xwin;

  context_glx->is_direct = glXIsDirect (dpy, context_glx->glx_context);

  GDK_DISPLAY_NOTE (display, OPENGL,
            g_message ("Realized GLX context[%p], %s, version: %d.%d",
                       context_glx->glx_context,
                       context_glx->is_direct ? "direct" : "indirect",
                       display_x11->glx_version / 10,
                       display_x11->glx_version % 10));

#ifdef HAVE_XDAMAGE
  if (display_x11->have_damage &&
      display_x11->has_async_glx_swap_buffers)
    {
      gdk_x11_display_error_trap_push (display);
      context_glx->xdamage = XDamageCreate (dpy,
                                            gdk_x11_surface_get_xid (surface),
                                            XDamageReportRawRectangles);
      if (gdk_x11_display_error_trap_pop (display))
        {
          context_glx->xdamage = 0;
        }
      else
        {
          g_signal_connect_object (G_OBJECT (display),
                                   "xevent",
                                   G_CALLBACK (on_gl_surface_xevent),
                                   context,
                                   G_CONNECT_SWAPPED);
          g_signal_connect_object (G_OBJECT (surface),
                                   "notify::state",
                                   G_CALLBACK (on_surface_state_changed),
                                   context,
                                   G_CONNECT_SWAPPED);

        }
    }
#endif

  return TRUE;
}

static void
gdk_x11_gl_context_glx_dispose (GObject *gobject)
{
  GdkX11GLContextGLX *context_glx = GDK_X11_GL_CONTEXT_GLX (gobject);

#ifdef HAVE_XDAMAGE
  context_glx->xdamage = 0;
#endif

  if (context_glx->glx_context != NULL)
    {
      GdkGLContext *context = GDK_GL_CONTEXT (gobject);
      GdkDisplay *display = gdk_gl_context_get_display (context);
      Display *dpy = gdk_x11_display_get_xdisplay (display);

      if (glXGetCurrentContext () == context_glx->glx_context)
        glXMakeContextCurrent (dpy, None, None, NULL);

      GDK_DISPLAY_NOTE (display, OPENGL, g_message ("Destroying GLX context"));
      glXDestroyContext (dpy, context_glx->glx_context);
      context_glx->glx_context = NULL;
    }

  G_OBJECT_CLASS (gdk_x11_gl_context_glx_parent_class)->dispose (gobject);
}

static void
gdk_x11_gl_context_glx_class_init (GdkX11GLContextGLXClass *klass)
{
  GdkGLContextClass *context_class = GDK_GL_CONTEXT_CLASS (klass);
  GdkDrawContextClass *draw_context_class = GDK_DRAW_CONTEXT_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  context_class->realize = gdk_x11_gl_context_glx_realize;
  context_class->get_damage = gdk_x11_gl_context_glx_get_damage;

  draw_context_class->end_frame = gdk_x11_gl_context_glx_end_frame;

  gobject_class->dispose = gdk_x11_gl_context_glx_dispose;
}

static void
gdk_x11_gl_context_glx_init (GdkX11GLContextGLX *self)
{
}

#define MAX_GLX_ATTRS   30

static gboolean
find_fbconfig (GdkDisplay   *display,
               GLXFBConfig  *fb_config_out,
               GError      **error)
{
  static int attrs[MAX_GLX_ATTRS];
  Display *dpy = gdk_x11_display_get_xdisplay (display);
  GLXFBConfig *configs;
  int n_configs, i;
  gboolean retval = FALSE;
  VisualID xvisual_id = XVisualIDFromVisual (gdk_x11_display_get_window_visual (GDK_X11_DISPLAY (display)));

  i = 0;
  attrs[i++] = GLX_DRAWABLE_TYPE;
  attrs[i++] = GLX_WINDOW_BIT;

  attrs[i++] = GLX_RENDER_TYPE;
  attrs[i++] = GLX_RGBA_BIT;

  attrs[i++] = GLX_DOUBLEBUFFER;
  attrs[i++] = GL_TRUE;

  attrs[i++] = GLX_RED_SIZE;
  attrs[i++] = 1;
  attrs[i++] = GLX_GREEN_SIZE;
  attrs[i++] = 1;
  attrs[i++] = GLX_BLUE_SIZE;
  attrs[i++] = 1;

  if (gdk_display_is_rgba (display))
    {
      attrs[i++] = GLX_ALPHA_SIZE;
      attrs[i++] = 1;
    }
  else
    {
      attrs[i++] = GLX_ALPHA_SIZE;
      attrs[i++] = GLX_DONT_CARE;
    }

  attrs[i++] = None;

  g_assert (i < MAX_GLX_ATTRS);

  configs = glXChooseFBConfig (dpy, DefaultScreen (dpy), attrs, &n_configs);
  if (configs == NULL || n_configs == 0)
    {
      g_set_error_literal (error, GDK_GL_ERROR,
                           GDK_GL_ERROR_UNSUPPORTED_FORMAT,
                           _("No available configurations for the given pixel format"));
      return FALSE;
    }

  for (i = 0; i < n_configs; i++)
    {
      XVisualInfo *visinfo;

      visinfo = glXGetVisualFromFBConfig (dpy, configs[i]);
      if (visinfo == NULL)
        continue;

      if (visinfo->visualid != xvisual_id)
        {
          XFree (visinfo);
          continue;
        }

      if (fb_config_out != NULL)
        *fb_config_out = configs[i];

      XFree (visinfo);
      retval = TRUE;
      goto out;
    }

  g_set_error (error, GDK_GL_ERROR,
               GDK_GL_ERROR_UNSUPPORTED_FORMAT,
               _("No available configurations for the given RGBA pixel format"));

out:
  XFree (configs);

  return retval;
}

#undef MAX_GLX_ATTRS

struct glvisualinfo {
  int supports_gl;
  int double_buffer;
  int stereo;
  int alpha_size;
  int depth_size;
  int stencil_size;
  int num_multisample;
  int visual_caveat;
};

static gboolean
visual_compatible (const GdkX11Visual *a, const GdkX11Visual *b)
{
  return a->type == b->type &&
    a->depth == b->depth &&
    a->red_mask == b->red_mask &&
    a->green_mask == b->green_mask &&
    a->blue_mask == b->blue_mask &&
    a->colormap_size == b->colormap_size &&
    a->bits_per_rgb == b->bits_per_rgb;
}

static gboolean
visual_is_rgba (const GdkX11Visual *visual)
{
  return
    visual->depth == 32 &&
    visual->red_mask   == 0xff0000 &&
    visual->green_mask == 0x00ff00 &&
    visual->blue_mask  == 0x0000ff;
}

/* This picks a compatible (as in has the same X visual details) visual
   that has "better" characteristics on the GL side */
static GdkX11Visual *
pick_better_visual_for_gl (GdkX11Screen *x11_screen,
                           struct glvisualinfo *gl_info,
                           GdkX11Visual *compatible)
{
  GdkX11Visual *visual;
  int i;
  gboolean want_alpha = visual_is_rgba (compatible);

  /* First look for "perfect match", i.e:
   * supports gl
   * double buffer
   * alpha iff visual is an rgba visual
   * no unnecessary stuff
   */
  for (i = 0; i < x11_screen->nvisuals; i++)
    {
      visual = x11_screen->visuals[i];
      if (visual_compatible (visual, compatible) &&
          gl_info[i].supports_gl &&
          gl_info[i].double_buffer &&
          !gl_info[i].stereo &&
          (want_alpha ? (gl_info[i].alpha_size > 0) : (gl_info[i].alpha_size == 0)) &&
          (gl_info[i].depth_size == 0) &&
          (gl_info[i].stencil_size == 0) &&
          (gl_info[i].num_multisample == 0) &&
          (gl_info[i].visual_caveat == GLX_NONE_EXT))
        return visual;
    }

  if (!want_alpha)
    {
      /* Next, allow alpha even if we don't want it: */
      for (i = 0; i < x11_screen->nvisuals; i++)
        {
          visual = x11_screen->visuals[i];
          if (visual_compatible (visual, compatible) &&
              gl_info[i].supports_gl &&
              gl_info[i].double_buffer &&
              !gl_info[i].stereo &&
              (gl_info[i].depth_size == 0) &&
              (gl_info[i].stencil_size == 0) &&
              (gl_info[i].num_multisample == 0) &&
              (gl_info[i].visual_caveat == GLX_NONE_EXT))
            return visual;
        }
    }

  /* Next, allow depth and stencil buffers: */
  for (i = 0; i < x11_screen->nvisuals; i++)
    {
      visual = x11_screen->visuals[i];
      if (visual_compatible (visual, compatible) &&
          gl_info[i].supports_gl &&
          gl_info[i].double_buffer &&
          !gl_info[i].stereo &&
          (gl_info[i].num_multisample == 0) &&
          (gl_info[i].visual_caveat == GLX_NONE_EXT))
        return visual;
    }

  /* Next, allow multisample: */
  for (i = 0; i < x11_screen->nvisuals; i++)
    {
      visual = x11_screen->visuals[i];
      if (visual_compatible (visual, compatible) &&
          gl_info[i].supports_gl &&
          gl_info[i].double_buffer &&
          !gl_info[i].stereo &&
          (gl_info[i].visual_caveat == GLX_NONE_EXT))
        return visual;
    }

  return compatible;
}

static gboolean
get_cached_gl_visuals (GdkDisplay *display, int *system, int *rgba)
{
  gboolean found;
  Atom type_return;
  int format_return;
  gulong nitems_return;
  gulong bytes_after_return;
  guchar *data = NULL;
  Display *dpy;

  dpy = gdk_x11_display_get_xdisplay (display);

  found = FALSE;

  gdk_x11_display_error_trap_push (display);
  if (XGetWindowProperty (dpy, DefaultRootWindow (dpy),
                          gdk_x11_get_xatom_by_name_for_display (display, "GDK_VISUALS"),
                          0, 2, False, XA_INTEGER, &type_return,
                          &format_return, &nitems_return,
                          &bytes_after_return, &data) == Success)
    {
      if (type_return == XA_INTEGER &&
          format_return == 32 &&
          nitems_return == 2 &&
          data != NULL)
        {
          long *visuals = (long *) data;

          *system = (int)visuals[0];
          *rgba = (int)visuals[1];
          found = TRUE;
        }
    }
  gdk_x11_display_error_trap_pop_ignored (display);

  if (data)
    XFree (data);

  return found;
}

static void
save_cached_gl_visuals (GdkDisplay *display, int system, int rgba)
{
  long visualdata[2];
  Display *dpy;

  dpy = gdk_x11_display_get_xdisplay (display);

  visualdata[0] = system;
  visualdata[1] = rgba;

  gdk_x11_display_error_trap_push (display);
  XChangeProperty (dpy, DefaultRootWindow (dpy),
                   gdk_x11_get_xatom_by_name_for_display (display, "GDK_VISUALS"),
                   XA_INTEGER, 32, PropModeReplace,
                   (unsigned char *)visualdata, 2);
  gdk_x11_display_error_trap_pop_ignored (display);
}

void
gdk_x11_screen_update_visuals_for_glx (GdkX11Screen *x11_screen)
{
  GdkDisplay *display;
  GdkX11Display *display_x11;
  Display *dpy;
  struct glvisualinfo *gl_info;
  int i;
  int system_visual_id, rgba_visual_id;

  display = x11_screen->display;
  display_x11 = GDK_X11_DISPLAY (display);
  dpy = gdk_x11_display_get_xdisplay (display);

  if (display_x11->have_egl)
    return;

  /* We save the default visuals as a property on the root window to avoid
     having to initialize GL each time, as it may not be used later. */
  if (get_cached_gl_visuals (display, &system_visual_id, &rgba_visual_id))
    {
      for (i = 0; i < x11_screen->nvisuals; i++)
        {
          GdkX11Visual *visual = x11_screen->visuals[i];
          int visual_id = gdk_x11_visual_get_xvisual (visual)->visualid;

          if (visual_id == system_visual_id)
            x11_screen->system_visual = visual;
          if (visual_id == rgba_visual_id)
            x11_screen->rgba_visual = visual;
        }

      return;
    }

  if (!gdk_x11_screen_init_glx (x11_screen))
    return;

  gl_info = g_new0 (struct glvisualinfo, x11_screen->nvisuals);

  for (i = 0; i < x11_screen->nvisuals; i++)
    {
      XVisualInfo *visual_list;
      XVisualInfo visual_template;
      int nxvisuals;

      visual_template.screen = x11_screen->screen_num;
      visual_template.visualid = gdk_x11_visual_get_xvisual (x11_screen->visuals[i])->visualid;
      visual_list = XGetVisualInfo (x11_screen->xdisplay, VisualIDMask| VisualScreenMask, &visual_template, &nxvisuals);

      if (visual_list == NULL)
        continue;

      glXGetConfig (dpy, &visual_list[0], GLX_USE_GL, &gl_info[i].supports_gl);
      glXGetConfig (dpy, &visual_list[0], GLX_DOUBLEBUFFER, &gl_info[i].double_buffer);
      glXGetConfig (dpy, &visual_list[0], GLX_STEREO, &gl_info[i].stereo);
      glXGetConfig (dpy, &visual_list[0], GLX_ALPHA_SIZE, &gl_info[i].alpha_size);
      glXGetConfig (dpy, &visual_list[0], GLX_DEPTH_SIZE, &gl_info[i].depth_size);
      glXGetConfig (dpy, &visual_list[0], GLX_STENCIL_SIZE, &gl_info[i].stencil_size);

      if (display_x11->has_glx_multisample)
        glXGetConfig(dpy, &visual_list[0], GLX_SAMPLE_BUFFERS_ARB, &gl_info[i].num_multisample);

      if (display_x11->has_glx_visual_rating)
        glXGetConfig(dpy, &visual_list[0], GLX_VISUAL_CAVEAT_EXT, &gl_info[i].visual_caveat);
      else
        gl_info[i].visual_caveat = GLX_NONE_EXT;

      XFree (visual_list);
    }

  x11_screen->system_visual = pick_better_visual_for_gl (x11_screen, gl_info, x11_screen->system_visual);
  if (x11_screen->rgba_visual)
    x11_screen->rgba_visual = pick_better_visual_for_gl (x11_screen, gl_info, x11_screen->rgba_visual);

  g_free (gl_info);

  save_cached_gl_visuals (display,
                          gdk_x11_visual_get_xvisual (x11_screen->system_visual)->visualid,
                          x11_screen->rgba_visual
                            ? gdk_x11_visual_get_xvisual (x11_screen->rgba_visual)->visualid
                            : 0);
}

GdkX11GLContext *
gdk_x11_gl_context_glx_new (GdkSurface    *surface,
                            gboolean       attached,
                            GdkGLContext  *share,
                            GError       **error)
{
  GdkDisplay *display;
  GdkX11GLContextGLX *context;
  GLXFBConfig config;

  display = gdk_surface_get_display (surface);
  if (!find_fbconfig (display, &config, error))
    return NULL;

  context = g_object_new (GDK_TYPE_X11_GL_CONTEXT_GLX,
                          "surface", surface,
                          "shared-context", share,
                          NULL);

  context->glx_config = config;

  return GDK_X11_GL_CONTEXT (context);
}

gboolean
gdk_x11_gl_context_glx_make_current (GdkDisplay   *display,
                                     GdkGLContext *context)
{
  GdkX11GLContextGLX *context_glx;
  GdkX11GLContext *context_x11;
  Display *dpy = gdk_x11_display_get_xdisplay (display);
  gboolean do_frame_sync = FALSE;
  GLXWindow drawable;

  if (context == NULL)
    {
      glXMakeContextCurrent (dpy, None, None, NULL);
      return TRUE;
    }

  context_glx = GDK_X11_GL_CONTEXT_GLX (context);
  if (context_glx->glx_context == NULL)
    {
      g_critical ("No GLX context associated to the GdkGLContext; you must "
                  "call gdk_gl_context_realize() first.");
      return FALSE;
    }

  context_x11 = GDK_X11_GL_CONTEXT (context);
  if (context_x11->is_attached || gdk_draw_context_is_in_frame (GDK_DRAW_CONTEXT (context)))
    drawable = context_glx->attached_drawable;
  else
    drawable = context_glx->unattached_drawable;

  GDK_DISPLAY_NOTE (display, OPENGL,
                    g_message ("Making GLX context %p current to drawable %lu",
                               context, (unsigned long) drawable));

  if (!glXMakeContextCurrent (dpy, drawable, drawable, context_glx->glx_context))
    {
      GDK_DISPLAY_NOTE (display, OPENGL,
                        g_message ("Making GLX context current failed"));
      return FALSE;
    }

  if (context_x11->is_attached && GDK_X11_DISPLAY (display)->has_glx_swap_interval)
    {
      /* If the WM is compositing there is no particular need to delay
       * the swap when drawing on the offscreen, rendering to the screen
       * happens later anyway, and its up to the compositor to sync that
       * to the vblank. */
      do_frame_sync = ! gdk_display_is_composited (display);

      if (do_frame_sync != context_x11->do_frame_sync)
        {
          context_x11->do_frame_sync = do_frame_sync;

          if (do_frame_sync)
            glXSwapIntervalSGI (1);
          else
            glXSwapIntervalSGI (0);
        }
    }

  return TRUE;
}

/**
 * gdk_x11_display_get_glx_version:
 * @display: (type GdkX11Display): a `GdkDisplay`
 * @major: (out): return location for the GLX major version
 * @minor: (out): return location for the GLX minor version
 *
 * Retrieves the version of the GLX implementation.
 *
 * Returns: %TRUE if GLX is available
 */
gboolean
gdk_x11_display_get_glx_version (GdkDisplay *display,
                                 int        *major,
                                 int        *minor)
{
  g_return_val_if_fail (GDK_IS_DISPLAY (display), FALSE);

  if (!GDK_IS_X11_DISPLAY (display))
    return FALSE;

  GdkX11Display *display_x11 = GDK_X11_DISPLAY (display);

  if (!gdk_x11_screen_init_glx (display_x11->screen))
    return FALSE;

  if (major != NULL)
    *major = display_x11->glx_version / 10;
  if (minor != NULL)
    *minor = display_x11->glx_version % 10;

  return TRUE;
}

/*< private >
 * gdk_x11_screen_init_glx:
 * @screen: an X11 screen
 *
 * Initializes the cached GLX state for the given @screen.
 *
 * It's safe to call this function multiple times.
 *
 * Returns: %TRUE if GLX was initialized
 */
gboolean
gdk_x11_screen_init_glx (GdkX11Screen *screen)
{
  GdkDisplay *display = GDK_SCREEN_DISPLAY (screen);
  GdkX11Display *display_x11 = GDK_X11_DISPLAY (display);
  Display *dpy;
  int error_base, event_base;
  int screen_num;

  if (display_x11->have_glx)
    return TRUE;

  dpy = gdk_x11_display_get_xdisplay (display);

  if (!epoxy_has_glx (dpy))
    return FALSE;

  if (!glXQueryExtension (dpy, &error_base, &event_base))
    return FALSE;

  screen_num = screen->screen_num;

  display_x11->have_glx = TRUE;

  display_x11->glx_version = epoxy_glx_version (dpy, screen_num);
  display_x11->glx_error_base = error_base;
  display_x11->glx_event_base = event_base;

  display_x11->has_glx_create_context =
    epoxy_has_glx_extension (dpy, screen_num, "GLX_ARB_create_context_profile");
  display_x11->has_glx_create_es2_context =
    epoxy_has_glx_extension (dpy, screen_num, "GLX_EXT_create_context_es2_profile");
  display_x11->has_glx_swap_interval =
    epoxy_has_glx_extension (dpy, screen_num, "GLX_SGI_swap_control");
  display_x11->has_glx_texture_from_pixmap =
    epoxy_has_glx_extension (dpy, screen_num, "GLX_EXT_texture_from_pixmap");
  display_x11->has_glx_video_sync =
    epoxy_has_glx_extension (dpy, screen_num, "GLX_SGI_video_sync");
  display_x11->has_glx_buffer_age =
    epoxy_has_glx_extension (dpy, screen_num, "GLX_EXT_buffer_age");
  display_x11->has_glx_sync_control =
    epoxy_has_glx_extension (dpy, screen_num, "GLX_OML_sync_control");
  display_x11->has_glx_multisample =
    epoxy_has_glx_extension (dpy, screen_num, "GLX_ARB_multisample");
  display_x11->has_glx_visual_rating =
    epoxy_has_glx_extension (dpy, screen_num, "GLX_EXT_visual_rating");

  if (g_strcmp0 (glXGetClientString (dpy, GLX_VENDOR), "NVIDIA Corporation") == 0)
    {
      Atom type;
      int format;
      gulong nitems;
      gulong bytes_after;
      guchar *data = NULL;

      /* With the mesa based drivers, we can safely assume the compositor can
       * access the updated surface texture immediately after glXSwapBuffers is
       * run, because the kernel ensures there is an implicit synchronization
       * operation upon texture access. This is not true with the Nvidia vendor
       * driver. There is a window of time after glXSwapBuffers before other
       * processes can see the updated drawing. We need to take special care,
       * in that case, to defer telling the compositor our latest frame is
       * ready until after the GPU has completed all issued commands related
       * to the frame, and that the X server says the frame has been drawn.
       *
       * As this can cause deadlocks, we want to make sure to only enable it for Xorg,
       * but not for XWayland, Xnest or whatever other X servers exist.
       */

      gdk_x11_display_error_trap_push (display);
      if (XGetWindowProperty (dpy, DefaultRootWindow (dpy),
                              gdk_x11_get_xatom_by_name_for_display (display, "XFree86_VT"),
                              0, 1, False, AnyPropertyType,
                              &type, &format, &nitems, &bytes_after, &data) == Success)
        {
          if (type != None)
            display_x11->has_async_glx_swap_buffers = TRUE;
        }
      gdk_x11_display_error_trap_pop_ignored (display);

      if (data)
        XFree (data);
    }

  GDK_DISPLAY_NOTE (display, OPENGL,
            g_message ("GLX version %d.%d found\n"
                       " - Vendor: %s\n"
                       " - Checked extensions:\n"
                       "\t* GLX_ARB_create_context_profile: %s\n"
                       "\t* GLX_EXT_create_context_es2_profile: %s\n"
                       "\t* GLX_SGI_swap_control: %s\n"
                       "\t* GLX_EXT_texture_from_pixmap: %s\n"
                       "\t* GLX_SGI_video_sync: %s\n"
                       "\t* GLX_EXT_buffer_age: %s\n"
                       "\t* GLX_OML_sync_control: %s"
                       "\t* GLX_ARB_multisample: %s"
                       "\t* GLX_EXT_visual_rating: %s",
                     display_x11->glx_version / 10,
                     display_x11->glx_version % 10,
                     glXGetClientString (dpy, GLX_VENDOR),
                     display_x11->has_glx_create_context ? "yes" : "no",
                     display_x11->has_glx_create_es2_context ? "yes" : "no",
                     display_x11->has_glx_swap_interval ? "yes" : "no",
                     display_x11->has_glx_texture_from_pixmap ? "yes" : "no",
                     display_x11->has_glx_video_sync ? "yes" : "no",
                     display_x11->has_glx_buffer_age ? "yes" : "no",
                     display_x11->has_glx_sync_control ? "yes" : "no",
                     display_x11->has_glx_multisample ? "yes" : "no",
                     display_x11->has_glx_visual_rating ? "yes" : "no"));

  return TRUE;
}
