/* GdkMacosWindow.m
 *
 * Copyright © 2020 Red Hat, Inc.
 * Copyright © 2005-2007 Imendio AB
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <gdk/gdk.h>

#import "GdkMacosBaseView.h"
#import "GdkMacosCairoView.h"
#import "GdkMacosGLView.h"
#import "GdkMacosWindow.h"

#include "gdkmacosclipboard-private.h"
#include "gdkmacosdisplay-private.h"
#include "gdkmacosdrop-private.h"
#include "gdkmacosmonitor-private.h"
#include "gdkmacossurface-private.h"
#include "gdkmacospopupsurface-private.h"
#include "gdkmacostoplevelsurface-private.h"
#include "gdkmacosutils-private.h"

#include "gdkmonitorprivate.h"
#include "gdksurfaceprivate.h"

#ifndef AVAILABLE_MAC_OS_X_VERSION_10_15_AND_LATER
typedef NSString *CALayerContentsGravity;
#endif

@implementation GdkMacosWindow

-(BOOL)windowShouldClose:(id)sender
{
  GdkDisplay *display;
  GdkEvent *event;
  GList *node;

  display = gdk_surface_get_display (GDK_SURFACE (gdk_surface));
  event = gdk_delete_event_new (GDK_SURFACE (gdk_surface));
  node = _gdk_event_queue_append (display, event);
  _gdk_windowing_got_event (display, node, event,
                            _gdk_display_get_next_serial (display));

  return NO;
}

-(void)windowWillMiniaturize:(NSNotification *)aNotification
{
  if (GDK_IS_MACOS_TOPLEVEL_SURFACE (gdk_surface))
    _gdk_macos_toplevel_surface_detach_from_parent (GDK_MACOS_TOPLEVEL_SURFACE (gdk_surface));
  else if (GDK_IS_MACOS_POPUP_SURFACE (gdk_surface))
    _gdk_macos_popup_surface_detach_from_parent (GDK_MACOS_POPUP_SURFACE (gdk_surface));
}

-(void)windowDidMiniaturize:(NSNotification *)aNotification
{
  gdk_synthesize_surface_state (GDK_SURFACE (gdk_surface), 0, GDK_TOPLEVEL_STATE_MINIMIZED);
}

-(void)windowDidDeminiaturize:(NSNotification *)aNotification
{
  if (GDK_IS_MACOS_TOPLEVEL_SURFACE (gdk_surface))
    _gdk_macos_toplevel_surface_attach_to_parent (GDK_MACOS_TOPLEVEL_SURFACE (gdk_surface));
  else if (GDK_IS_MACOS_POPUP_SURFACE (gdk_surface))
    _gdk_macos_popup_surface_attach_to_parent (GDK_MACOS_POPUP_SURFACE (gdk_surface));

  gdk_synthesize_surface_state (GDK_SURFACE (gdk_surface), GDK_TOPLEVEL_STATE_MINIMIZED, 0);
}

-(void)windowDidBecomeKey:(NSNotification *)aNotification
{
  gdk_synthesize_surface_state (GDK_SURFACE (gdk_surface), 0, GDK_TOPLEVEL_STATE_FOCUSED);
  _gdk_macos_display_surface_became_key ([self gdkDisplay], gdk_surface);
}

-(void)windowDidResignKey:(NSNotification *)aNotification
{
  gdk_synthesize_surface_state (GDK_SURFACE (gdk_surface), GDK_TOPLEVEL_STATE_FOCUSED, 0);
  _gdk_macos_display_surface_resigned_key ([self gdkDisplay], gdk_surface);
}

-(void)windowDidBecomeMain:(NSNotification *)aNotification
{
  if (![self isVisible])
    {
      /* Note: This is a hack needed because for unknown reasons, hidden
       * windows get shown when clicking the dock icon when the application
       * is not already active.
       */
      [self orderOut:nil];
      return;
    }

  _gdk_macos_display_surface_became_main ([self gdkDisplay], gdk_surface);
}

-(void)windowDidResignMain:(NSNotification *)aNotification
{
  _gdk_macos_display_surface_resigned_main ([self gdkDisplay], gdk_surface);
}

/* Used in combination with NSLeftMouseUp in sendEvent to keep track
 * of when the window is being moved with the mouse.
 */
-(void)windowWillMove:(NSNotification *)aNotification
{
  inMove = YES;
}

-(void)sendEvent:(NSEvent *)event
{
  NSEventType event_type = [event type];

  switch ((int)event_type)
    {
    case NSEventTypeLeftMouseUp: {
      GdkDisplay *display = gdk_surface_get_display (GDK_SURFACE (gdk_surface));
      double time = ((double)[event timestamp]) * 1000.0;

      inManualMove = NO;
      inManualResize = NO;
      inMove = NO;

      /* We need to deliver the event to the proper drag gestures or we
       * will leave the window in inconsistent state that requires clicking
       * in the window to cancel the gesture.
       *
       * TODO: Can we improve grab breaking to fix this?
       */
      _gdk_macos_display_send_button_event ([self gdkDisplay], event);

      _gdk_macos_display_break_all_grabs (GDK_MACOS_DISPLAY (display), time);

      /* Reset gravity */
      if (GDK_IS_MACOS_GL_VIEW ([self contentView]))
        [[[self contentView] layer] setContentsGravity:kCAGravityBottomLeft];

      break;
    }

    case NSEventTypeLeftMouseDragged:
      if ([self trackManualMove] || [self trackManualResize])
        return;
      break;

    default:
      break;
    }

  [super sendEvent:event];
}

-(BOOL)isInMove
{
  return inMove;
}

-(void)checkSendEnterNotify
{
  /* When a new window has been created, and the mouse is in the window
   * area, we will not receive an NSEventTypeMouseEntered event.
   * Therefore, we synthesize an enter notify event manually.
   */
  if (!initialPositionKnown)
    {
      initialPositionKnown = YES;

      if (NSPointInRect ([NSEvent mouseLocation], [self frame]))
        {
          GdkMacosBaseView *view = (GdkMacosBaseView *)[self contentView];
          NSEvent *event;

          event = [NSEvent enterExitEventWithType: NSEventTypeMouseEntered
                                         location: [self mouseLocationOutsideOfEventStream]
                                    modifierFlags: 0
                                        timestamp: [[NSApp currentEvent] timestamp]
                                     windowNumber: [self windowNumber]
                                          context: NULL
                                      eventNumber: 0
                                   trackingNumber: (NSInteger)[view trackingArea]
                                         userData: nil];

          [NSApp postEvent:event atStart:NO];
        }
    }
}

-(void)windowDidUnmaximize
{
  NSWindowStyleMask style_mask = [self styleMask];

  gdk_synthesize_surface_state (GDK_SURFACE (gdk_surface), GDK_TOPLEVEL_STATE_MAXIMIZED, 0);

  /* If we are using CSD, then we transitioned to an opaque
   * window while we were maximized. Now we need to drop that
   * as we are leaving maximized state.
   */
  if ((style_mask & NSWindowStyleMaskTitled) == 0 && [self isOpaque])
    [self setOpaque:NO];
}

-(void)windowDidMove:(NSNotification *)aNotification
{
  GdkSurface *surface = GDK_SURFACE (gdk_surface);
  gboolean maximized = (surface->state & GDK_TOPLEVEL_STATE_MAXIMIZED) != 0;

  /* In case the window is changed when maximized remove the maximized state */
  if (maximized && !inMaximizeTransition && !NSEqualRects (lastMaximizedFrame, [self frame]))
    [self windowDidUnmaximize];

  _gdk_macos_surface_update_position (gdk_surface);
  _gdk_macos_surface_reposition_children (gdk_surface);

  [self checkSendEnterNotify];
}

-(void)windowDidResize:(NSNotification *)aNotification
{
  NSRect content_rect;
  GdkSurface *surface;
  GdkDisplay *display;
  gboolean maximized;

  surface = GDK_SURFACE (gdk_surface);
  display = gdk_surface_get_display (surface);

  content_rect = [self contentRectForFrameRect:[self frame]];
  maximized = (surface->state & GDK_TOPLEVEL_STATE_MAXIMIZED) != 0;

  /* see same in windowDidMove */
  if (maximized && !inMaximizeTransition && !NSEqualRects (lastMaximizedFrame, [self frame]))
    [self windowDidUnmaximize];

  surface->width = content_rect.size.width;
  surface->height = content_rect.size.height;

  /* Certain resize operations (e.g. going fullscreen), also move the
   * origin of the window.
   */
  _gdk_macos_surface_update_position (GDK_MACOS_SURFACE (surface));

  [[self contentView] setFrame:NSMakeRect (0, 0, surface->width, surface->height)];

  _gdk_surface_update_size (surface);

  gdk_surface_request_layout (surface);

  _gdk_macos_surface_reposition_children (gdk_surface);

  [self checkSendEnterNotify];
}

-(id)initWithContentRect:(NSRect)contentRect
               styleMask:(NSWindowStyleMask)styleMask
                 backing:(NSBackingStoreType)backingType
                   defer:(BOOL)flag
                  screen:(NSScreen *)screen
{
  GdkMacosCairoView *view;

  self = [super initWithContentRect:contentRect
	                        styleMask:styleMask
	                          backing:backingType
	                            defer:flag
                             screen:screen];

  [self setAcceptsMouseMovedEvents:YES];
  [self setDelegate:(id<NSWindowDelegate>)self];
  [self setReleasedWhenClosed:YES];

  view = [[GdkMacosCairoView alloc] initWithFrame:contentRect];
  [self setContentView:view];
  [view release];

  /* TODO: We might want to make this more extensible at some point */
  _gdk_macos_clipboard_register_drag_types (self);

  return self;
}

-(BOOL)canBecomeMainWindow
{
  return GDK_IS_TOPLEVEL (gdk_surface);
}

-(BOOL)canBecomeKeyWindow
{
  return GDK_IS_TOPLEVEL (gdk_surface);
}

-(void)showAndMakeKey:(BOOL)makeKey
{
  inShowOrHide = YES;

  if (makeKey && [self canBecomeKeyWindow])
    [self makeKeyAndOrderFront:nil];
  else
    [self orderFront:nil];

  inShowOrHide = NO;

  [self checkSendEnterNotify];
}

-(void)hide
{
  BOOL wasKey = [self isKeyWindow];
  BOOL wasMain = [self isMainWindow];

  inShowOrHide = YES;
  [self orderOut:nil];
  inShowOrHide = NO;

  initialPositionKnown = NO;

  if (wasMain)
    [self windowDidResignMain:nil];

  if (wasKey)
    [self windowDidResignKey:nil];
}

-(BOOL)trackManualMove
{
  NSRect windowFrame;
  NSPoint currentLocation;
  GdkMonitor *monitor;
  GdkRectangle geometry;
  GdkRectangle workarea;
  int shadow_top = 0;
  int shadow_left = 0;
  int shadow_right = 0;
  int shadow_bottom = 0;
  GdkRectangle window_gdk;
  GdkPoint pointer_position;
  GdkPoint new_origin;

  if (!inManualMove)
    return NO;

  /* Get our shadow so we can adjust the window position sans-shadow */
  _gdk_macos_surface_get_shadow (gdk_surface,
                                 &shadow_top,
                                 &shadow_right,
                                 &shadow_bottom,
                                 &shadow_left);

  windowFrame = [self frame];
  currentLocation = [NSEvent mouseLocation];

  /* Update the snapping geometry to match the current monitor */
  monitor = _gdk_macos_display_get_monitor_at_display_coords ([self gdkDisplay],
                                                              currentLocation.x,
                                                              currentLocation.y);
  gdk_monitor_get_geometry (monitor, &geometry);
  gdk_macos_monitor_get_workarea (monitor, &workarea);
  _edge_snapping_set_monitor (&self->snapping, &geometry, &workarea);

  /* Convert origins to GDK coordinates */
  _gdk_macos_display_from_display_coords ([self gdkDisplay],
                                          currentLocation.x,
                                          currentLocation.y,
                                          &pointer_position.x,
                                          &pointer_position.y);
  _gdk_macos_display_from_display_coords ([self gdkDisplay],
                                          windowFrame.origin.x,
                                          windowFrame.origin.y + windowFrame.size.height,
                                          &window_gdk.x,
                                          &window_gdk.y);
  window_gdk.width = windowFrame.size.width;
  window_gdk.height = windowFrame.size.height;

  /* Subtract our shadowin from the window */
  window_gdk.x += shadow_left;
  window_gdk.y += shadow_top;
  window_gdk.width = window_gdk.width - shadow_left - shadow_right;
  window_gdk.height = window_gdk.height - shadow_top - shadow_bottom;

  /* Now place things on the monitor */
  _edge_snapping_motion (&self->snapping, &pointer_position, &window_gdk);

  /* And add our shadow back to the frame */
  window_gdk.x -= shadow_left;
  window_gdk.y -= shadow_top;
  window_gdk.width += shadow_left + shadow_right;
  window_gdk.height += shadow_top + shadow_bottom;

  /* Convert to quartz coordinates */
  _gdk_macos_display_to_display_coords ([self gdkDisplay],
                                        window_gdk.x,
                                        window_gdk.y + window_gdk.height,
                                        &new_origin.x, &new_origin.y);
  windowFrame.origin.x = new_origin.x;
  windowFrame.origin.y = new_origin.y;

  /* And now apply the frame to the window */
  [self setFrameOrigin:NSMakePoint(new_origin.x, new_origin.y)];

  return YES;
}

/* Used by gdkmacosdisplay-translate.c to decide if our sendEvent() handler
 * above will see the event or if it will be subjected to standard processing
 * by GDK.
*/
-(BOOL)isInManualResizeOrMove
{
  return inManualResize || inManualMove;
}

-(void)beginManualMove
{
  NSPoint initialMoveLocation;
  GdkPoint point;
  GdkMonitor *monitor;
  GdkRectangle geometry;
  GdkRectangle area;
  GdkRectangle workarea;

  if (inMove || inManualMove || inManualResize)
    return;

  inManualMove = YES;

  monitor = _gdk_macos_surface_get_best_monitor ([self gdkSurface]);
  gdk_monitor_get_geometry (monitor, &geometry);
  gdk_macos_monitor_get_workarea (monitor, &workarea);

  initialMoveLocation = [NSEvent mouseLocation];

  _gdk_macos_display_from_display_coords ([self gdkDisplay],
                                          initialMoveLocation.x,
                                          initialMoveLocation.y,
                                          &point.x,
                                          &point.y);

  area.x = gdk_surface->root_x;
  area.y = gdk_surface->root_y;
  area.width = GDK_SURFACE (gdk_surface)->width;
  area.height = GDK_SURFACE (gdk_surface)->height;

  _edge_snapping_init (&self->snapping,
                       &geometry,
                       &workarea,
                       &point,
                       &area);
}

-(BOOL)trackManualResize
{
  NSPoint mouse_location;
  NSRect new_frame;
  float mdx, mdy, dw, dh, dx, dy;
  NSSize min_size;

  if (!inManualResize || inTrackManualResize)
    return NO;

  inTrackManualResize = YES;

  mouse_location = convert_nspoint_to_screen (self, [self mouseLocationOutsideOfEventStream]);
  mdx = initialResizeLocation.x - mouse_location.x;
  mdy = initialResizeLocation.y - mouse_location.y;

  /* Set how a mouse location delta translates to changes in width,
   * height and position.
   */
  dw = dh = dx = dy = 0.0;
  if (resizeEdge == GDK_SURFACE_EDGE_EAST ||
      resizeEdge == GDK_SURFACE_EDGE_NORTH_EAST ||
      resizeEdge == GDK_SURFACE_EDGE_SOUTH_EAST)
    {
      dw = -1.0;
    }
  if (resizeEdge == GDK_SURFACE_EDGE_NORTH ||
      resizeEdge == GDK_SURFACE_EDGE_NORTH_WEST ||
      resizeEdge == GDK_SURFACE_EDGE_NORTH_EAST)
    {
      dh = -1.0;
    }
  if (resizeEdge == GDK_SURFACE_EDGE_SOUTH ||
      resizeEdge == GDK_SURFACE_EDGE_SOUTH_WEST ||
      resizeEdge == GDK_SURFACE_EDGE_SOUTH_EAST)
    {
      dh = 1.0;
      dy = -1.0;
    }
  if (resizeEdge == GDK_SURFACE_EDGE_WEST ||
      resizeEdge == GDK_SURFACE_EDGE_NORTH_WEST ||
      resizeEdge == GDK_SURFACE_EDGE_SOUTH_WEST)
    {
      dw = 1.0;
      dx = -1.0;
    }

  /* Apply changes to the frame captured when we started resizing */
  new_frame = initialResizeFrame;
  new_frame.origin.x += mdx * dx;
  new_frame.origin.y += mdy * dy;
  new_frame.size.width += mdx * dw;
  new_frame.size.height += mdy * dh;

  /* In case the resulting window would be too small reduce the
   * change to both size and position.
   */
  min_size = [self contentMinSize];

  if (new_frame.size.width < min_size.width)
    {
      if (dx)
        new_frame.origin.x -= min_size.width - new_frame.size.width;
      new_frame.size.width = min_size.width;
    }

  if (new_frame.size.height < min_size.height)
    {
      if (dy)
        new_frame.origin.y -= min_size.height - new_frame.size.height;
      new_frame.size.height = min_size.height;
    }

  /* We could also apply aspect ratio:
     new_frame.size.height = new_frame.size.width / [self aspectRatio].width * [self aspectRatio].height;
  */

  [self setFrame:new_frame display:YES];

  /* Let the resizing be handled by GTK. */
  if (g_main_context_pending (NULL))
    g_main_context_iteration (NULL, FALSE);

  inTrackManualResize = NO;

  return YES;
}

-(void)beginManualResize:(GdkSurfaceEdge)edge
{
  if (inMove || inManualMove || inManualResize)
    return;

  inManualResize = YES;
  resizeEdge = edge;

  if (GDK_IS_MACOS_GL_VIEW ([self contentView]))
    {
      CALayerContentsGravity gravity = kCAGravityBottomLeft;

      switch (edge)
        {
        default:
        case GDK_SURFACE_EDGE_NORTH:
          gravity = kCAGravityTopLeft;
          break;

        case GDK_SURFACE_EDGE_NORTH_WEST:
          gravity = kCAGravityTopRight;
          break;

        case GDK_SURFACE_EDGE_SOUTH_WEST:
        case GDK_SURFACE_EDGE_WEST:
          gravity = kCAGravityBottomRight;
          break;

        case GDK_SURFACE_EDGE_SOUTH:
        case GDK_SURFACE_EDGE_SOUTH_EAST:
          gravity = kCAGravityBottomLeft;
          break;

        case GDK_SURFACE_EDGE_EAST:
          gravity = kCAGravityBottomLeft;
          break;

        case GDK_SURFACE_EDGE_NORTH_EAST:
          gravity = kCAGravityTopLeft;
          break;
        }

      [[[self contentView] layer] setContentsGravity:gravity];
    }

  initialResizeFrame = [self frame];
  initialResizeLocation = convert_nspoint_to_screen (self, [self mouseLocationOutsideOfEventStream]);
}

-(NSDragOperation)draggingEntered:(id <NSDraggingInfo>)sender
{
  NSPoint location = [sender draggingLocation];
  NSDragOperation ret;
  GdkMacosDrop *drop;

  if (!(drop = _gdk_macos_drop_new ([self gdkSurface], sender)))
    return NSDragOperationNone;

  _gdk_macos_display_set_drop ([self gdkDisplay],
                               [sender draggingSequenceNumber],
                               GDK_DROP (drop));

  gdk_drop_emit_enter_event (GDK_DROP (drop),
                             TRUE,
                             location.x,
                             GDK_SURFACE (gdk_surface)->height - location.y,
                             GDK_CURRENT_TIME);

  ret = _gdk_macos_drop_operation (drop);

  g_object_unref (drop);

  return ret;
}

-(void)draggingEnded:(id <NSDraggingInfo>)sender
{
  _gdk_macos_display_set_drop ([self gdkDisplay], [sender draggingSequenceNumber], NULL);
}

-(void)draggingExited:(id <NSDraggingInfo>)sender
{
  NSInteger sequence_number = [sender draggingSequenceNumber];
  GdkDrop *drop = _gdk_macos_display_find_drop ([self gdkDisplay], sequence_number);

  if (drop != NULL)
    gdk_drop_emit_leave_event (drop, TRUE, GDK_CURRENT_TIME);

  _gdk_macos_display_set_drop ([self gdkDisplay], sequence_number, NULL);
}

-(NSDragOperation)draggingUpdated:(id <NSDraggingInfo>)sender
{
  NSInteger sequence_number = [sender draggingSequenceNumber];
  GdkDisplay *display = gdk_surface_get_display (GDK_SURFACE (gdk_surface));
  GdkDrop *drop = _gdk_macos_display_find_drop (GDK_MACOS_DISPLAY (display), sequence_number);
  NSPoint location = [sender draggingLocation];

  if (drop == NULL)
    return NSDragOperationNone;

  _gdk_macos_drop_update_actions (GDK_MACOS_DROP (drop), sender);

  gdk_drop_emit_motion_event (drop,
                              TRUE,
                              location.x,
                              GDK_SURFACE (gdk_surface)->height - location.y,
                              GDK_CURRENT_TIME);

  return _gdk_macos_drop_operation (GDK_MACOS_DROP (drop));
}

-(BOOL)performDragOperation:(id <NSDraggingInfo>)sender
{
  NSInteger sequence_number = [sender draggingSequenceNumber];
  GdkDisplay *display = gdk_surface_get_display (GDK_SURFACE (gdk_surface));
  GdkDrop *drop = _gdk_macos_display_find_drop (GDK_MACOS_DISPLAY (display), sequence_number);
  NSPoint location = [sender draggingLocation];

  if (drop == NULL)
    return NO;

  gdk_drop_emit_drop_event (drop,
                            TRUE,
                            location.x,
                            GDK_SURFACE (gdk_surface)->height - location.y,
                            GDK_CURRENT_TIME);

  gdk_drop_emit_leave_event (drop, TRUE, GDK_CURRENT_TIME);

  return GDK_MACOS_DROP (drop)->finish_action != 0;
}

-(BOOL)wantsPeriodicDraggingUpdates
{
  return NO;
}

-(void)draggedImage:(NSImage *)anImage endedAt:(NSPoint)aPoint operation:(NSDragOperation)operation
{
}

-(void)setStyleMask:(NSWindowStyleMask)styleMask
{
  gboolean was_fullscreen;
  gboolean is_fullscreen;
  gboolean was_opaque;
  gboolean is_opaque;

  was_fullscreen = (([self styleMask] & NSWindowStyleMaskFullScreen) != 0);
  was_opaque = (([self styleMask] & NSWindowStyleMaskTitled) != 0);

  [super setStyleMask:styleMask];

  is_fullscreen = (([self styleMask] & NSWindowStyleMaskFullScreen) != 0);
  is_opaque = (([self styleMask] & NSWindowStyleMaskTitled) != 0);

  if (was_fullscreen != is_fullscreen)
    _gdk_macos_surface_update_fullscreen_state (gdk_surface);

  if (was_opaque != is_opaque)
    {
      [self setOpaque:is_opaque];

      if (!is_opaque)
        [self setBackgroundColor:[NSColor clearColor]];
    }
}

-(NSRect)constrainFrameRect:(NSRect)frameRect toScreen:(NSScreen *)screen
{
  GdkMacosSurface *surface = gdk_surface;
  NSRect rect;
  int shadow_top;

  /* Allow the window to move up "shadow_top" more than normally allowed
   * by the default impl. This makes it possible to move windows with
   * client side shadow right up to the screen's menu bar. */
  _gdk_macos_surface_get_shadow (surface, &shadow_top, NULL, NULL, NULL);
  rect = [super constrainFrameRect:frameRect toScreen:screen];
  if (frameRect.origin.y > rect.origin.y)
    rect.origin.y = MIN (frameRect.origin.y, rect.origin.y + shadow_top);

  return rect;
}

-(NSRect)windowWillUseStandardFrame:(NSWindow *)nsWindow
                       defaultFrame:(NSRect)newFrame
{
  NSRect screenFrame = [[self screen] visibleFrame];
  GdkMacosSurface *surface = gdk_surface;
  gboolean maximized = GDK_SURFACE (surface)->state & GDK_TOPLEVEL_STATE_MAXIMIZED;

  if (!maximized)
    return screenFrame;
  else
    return lastUnmaximizedFrame;
}

-(BOOL)windowShouldZoom:(NSWindow *)nsWindow
                toFrame:(NSRect)newFrame
{
  GdkMacosSurface *surface = gdk_surface;
  GdkToplevelState state = GDK_SURFACE (surface)->state;

  if (state & GDK_TOPLEVEL_STATE_MAXIMIZED)
    {
      lastMaximizedFrame = newFrame;
      [self windowDidUnmaximize];
    }
  else
    {
      lastUnmaximizedFrame = [nsWindow frame];
      gdk_synthesize_surface_state (GDK_SURFACE (gdk_surface), 0, GDK_TOPLEVEL_STATE_MAXIMIZED);
    }

  inMaximizeTransition = YES;

  return YES;
}

-(void)windowDidEndLiveResize:(NSNotification *)aNotification
{
  gboolean maximized = GDK_SURFACE (gdk_surface)->state & GDK_TOPLEVEL_STATE_MAXIMIZED;

  inMaximizeTransition = NO;

  /* Even if this is CSD, we want to be opaque while maximized
   * to speed up compositing by allowing the display server to
   * avoid costly blends.
   */
  if (maximized)
    [self setOpaque:YES];
}

-(NSSize)window:(NSWindow *)window willUseFullScreenContentSize:(NSSize)proposedSize
{
  return [[window screen] frame].size;
}

-(void)windowWillEnterFullScreen:(NSNotification *)aNotification
{
  lastUnfullscreenFrame = [self frame];
}

-(void)windowWillExitFullScreen:(NSNotification *)aNotification
{
  [self setFrame:lastUnfullscreenFrame display:YES];
}

-(void)windowDidExitFullScreen:(NSNotification *)aNotification
{
}

-(void)windowDidChangeScreen:(NSNotification *)aNotification
{
  _gdk_macos_surface_monitor_changed (gdk_surface);
}

-(void)setGdkSurface:(GdkMacosSurface *)surface
{
  self->gdk_surface = surface;
}

-(void)setDecorated:(BOOL)decorated
{
  NSWindowStyleMask style_mask = [self styleMask];

  [self setHasShadow:decorated];

  if (decorated)
    style_mask |= NSWindowStyleMaskTitled;
  else
    style_mask &= ~NSWindowStyleMaskTitled;

  [self setStyleMask:style_mask];
}

-(GdkMacosSurface *)gdkSurface
{
  return self->gdk_surface;
}

-(GdkMacosDisplay *)gdkDisplay
{
  return GDK_MACOS_DISPLAY (GDK_SURFACE (self->gdk_surface)->display);
}

-(BOOL)movableByWindowBackground
{
  return NO;
}

@end
