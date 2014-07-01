/* Definitions and headers for communication with Wayland protocol.
   Copyright (C) 1989, 1993-1994, 1998-2014 Free Software Foundation,
   Inc.

This file is part of GNU Emacs.

GNU Emacs is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

GNU Emacs is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU Emacs.  If not, see <http://www.gnu.org/licenses/>.  */

#ifndef WLTERM_H
#define WLTERM_H

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include "dispextern.h"
#include "frame.h"
#include "character.h"
#include "font.h"
#include "sysselect.h"

/* Structure recording X pixmap and reference count.
   If REFCOUNT is 0 then this record is free to be reused.  */

struct ewl_bitmap_record
{
  char *file;
  int refcount;
  /* Record some info about this pixmap.  */
  int height, width, depth;
};

/* For each X display, we have a structure that records
   information about it.  */

struct ewl_color_table;

struct ewl_display_info
{
  /* Chain of all ewl_display_info structures.  */
  struct ewl_display_info *next;

  /* The generic display parameters corresponding to this X display. */
  struct terminal *terminal;

  /* WL: Global objects in Wayland.  */
  struct wl_display *display;
  struct wl_registry *registry;
  struct wl_compositor *compositor;
  struct wl_shell *shell;
  struct wl_shm *shm;
  struct wl_seat *seat;
  struct wl_keyboard *keyboard;

  /* WL: Format masks for SHM.  */
  uint32_t formats;

  /* This is a cons cell of the form (NAME . FONT-LIST-CACHE).  */
  Lisp_Object name_list_element;

  /* Number of frames that are on this display.  */
  int reference_count;

  /* WL: 'screen', 'visual', and 'cmap' are omitted */

  /* Dots per inch of the screen.  */
  double resx, resy;

  /* Number of planes on this screen.  */
  int n_planes;

  /* Mask of things that cause the mouse to be grabbed.  */
  int grabbed;

  /* Emacs bitmap-id of the default icon bitmap for this frame.
     Or -1 if none has been allocated yet.  */
  ptrdiff_t icon_bitmap_id;

  /* The root window of this screen.  */
  Window root_window;

  /* Client leader window.  */
  Window client_leader_window;

  /* The cursor to use for vertical scroll bars.  */
  Cursor vertical_scroll_bar_cursor;

  /* The invisible cursor used for pointer blanking.
     Unused if this display supports Xfixes extension.  */
  Cursor invisible_cursor;

  /* Function used to toggle pointer visibility on this display.  */
  void (*toggle_visible_pointer) (struct frame *, bool);

  /* X Resource data base */
  XrmDatabase xrdb;

  /* Minimum width over all characters in all fonts in font_table.  */
  int smallest_char_width;

  /* Minimum font height over all fonts in font_table.  */
  int smallest_font_height;

  /* Reusable Graphics Context for drawing a cursor in a non-default face. */
  GC scratch_cursor_gc;

  /* Information about the range of text currently shown in
     mouse-face.  */
  Mouse_HLInfo mouse_highlight;

  /* Logical identifier of this display.  */
  unsigned x_id;

  /* Default name for all frames on this display.  */
  char *x_id_name;

  /* The number of fonts opened for this display.  */
  int n_fonts;

  /* Pointer to bitmap records.  */
  struct ewl_bitmap_record *bitmaps;

  /* Allocated size of bitmaps field.  */
  ptrdiff_t bitmaps_size;

  /* Last used bitmap index.  */
  ptrdiff_t bitmaps_last;

  /* WL: Keyboard handling through libxkbcommon.  */
  struct xkb_keymap *xkb_keymap;
  struct xkb_context *xkb_context;
  struct xkb_state *xkb_state;
  xkb_mod_mask_t control_mod_mask, shift_mod_mask;
  xkb_mod_mask_t meta_mod_mask, shift_lock_mask;
  xkb_mod_mask_t alt_mod_mask, super_mod_mask, hyper_mod_mask;

  /* The frame (if any) which has the X window that has keyboard focus.
     Zero if none.  This is examined by Ffocus_frame in xfns.c.  Note
     that a mere EnterNotify event can set this; if you need to know the
     last frame specified in a FocusIn or FocusOut event, use
     x_focus_event_frame.  */
  struct frame *x_focus_frame;

  /* The last frame mentioned in a FocusIn or FocusOut event.  This is
     separate from x_focus_frame, because whether or not LeaveNotify
     events cause us to lose focus depends on whether or not we have
     received a FocusIn event for it.  */
  struct frame *x_focus_event_frame;

  /* The frame which currently has the visual highlight, and should get
     keyboard input (other sorts of input have the frame encoded in the
     event).  It points to the X focus frame's selected window's
     frame.  It differs from x_focus_frame when we're using a global
     minibuffer.  */
  struct frame *x_highlight_frame;

  /* The frame where the mouse was last time we reported a ButtonPress event.  */
  struct frame *last_mouse_frame;

  /* The frame where the mouse was last time we reported a mouse motion.  */
  struct frame *last_mouse_motion_frame;

  /* Position where the mouse was last time we reported a motion.
     This is a position on last_mouse_motion_frame.  */
  int last_mouse_motion_x;
  int last_mouse_motion_y;

  /* Time of last mouse movement on this display.  This is a hack because
     we would really prefer that XTmouse_position would return the time
     associated with the position it returns, but there doesn't seem to be
     any way to wrest the time-stamp from the server along with the position
     query.  So, we just keep track of the time of the last movement we
     received, and return that in hopes that it's somewhat accurate.  */
  Time last_mouse_movement_time;

  struct ewl_color_table *color_table;
};

#define BLACK_PIX_DEFAULT(f) 0xFF000000
#define WHITE_PIX_DEFAULT(f) 0xFFFFFFFF

#define ALPHA_FROM_ULONG(color) ((color) >> 24)
#define RED_FROM_ULONG(color) (((color) >> 16) & 0xff)
#define GREEN_FROM_ULONG(color) (((color) >> 8) & 0xff)
#define BLUE_FROM_ULONG(color) ((color) & 0xff)

/* Do not change `* 0x101' in the following lines to `<< 8'.  If
   changed, image masks in 1-bit depth will not work. */
#define RED16_FROM_ULONG(color) (RED_FROM_ULONG(color) * 0x101)
#define GREEN16_FROM_ULONG(color) (GREEN_FROM_ULONG(color) * 0x101)
#define BLUE16_FROM_ULONG(color) (BLUE_FROM_ULONG(color) * 0x101)

struct ewl_output
{
  /* Default ASCII font of this frame.  */
  struct font *font;

  struct wl_surface *surface;
  struct wl_shell_surface *shell_surface;
  struct wl_buffer *buffer;
  struct wl_callback *callback;

  void *data;
  size_t size;
  cairo_surface_t *cairo_surface;

  /* The baseline offset of the default ASCII font.  */
  int baseline_offset;

  unsigned long cursor_pixel;
  unsigned long border_pixel;
  unsigned long mouse_pixel;
  unsigned long cursor_foreground_pixel;

  Cursor text_cursor;
  Cursor nontext_cursor;
  Cursor modeline_cursor;
  Cursor hand_cursor;
  Cursor hourglass_cursor;
  Cursor horizontal_drag_cursor;
  Cursor vertical_drag_cursor;

  /* Here are the Graphics Contexts for the default font.  */
  GC normal_gc;				/* Normal video */
  GC reverse_gc;			/* Reverse video */
  GC cursor_gc;				/* cursor drawing */

  /* The X window used for this frame.
     May be zero while the frame object is being created
     and the X window has not yet been created.  */
  Window window_desc;

  /* The X window used for the bitmap icon;
     or 0 if we don't have a bitmap icon.  */
  Window icon_desc;

  /* The X window that is the parent of this X window.
     Usually this is a window that was made by the window manager,
     but it can be the root window, and it can be explicitly specified
     (see the explicit_parent field, below).  */
  Window parent_desc;

  char explicit_parent;

  /* Relief GCs, colors etc.  */
  struct relief
  {
    GC gc;
    unsigned long pixel;
  }
  black_relief, white_relief;

  /* The background for which the above relief GCs were set up.
     They are changed only when a different background is involved.  */
  unsigned long relief_background;

  /* If a fontset is specified for this frame instead of font, this
     value contains an ID of the fontset, else -1.  */
  int fontset; /* only used with font_backend */

  Lisp_Object icon_top;
  Lisp_Object icon_left;

  /* The size of the extra width currently allotted for vertical
     scroll bars, in pixels.  */
  int vertical_scroll_bar_extra;

  /* The height of the titlebar decoration (included in NSWindow's frame). */
  int titlebar_height;

  /* The height of the toolbar if displayed, else 0. */
  int toolbar_height;

  /* This is the Emacs structure for the NS display this frame is on.  */
  struct ewl_display_info *display_info;

  /* Non-zero if we are zooming (maximizing) the frame.  */
  int zooming;
};

/* this dummy decl needed to support TTYs */
struct x_output
{
  int unused;
};


/* This is a chain of structures for all the NS displays currently in use.  */
extern struct ewl_display_info *x_display_list;

/* This gives the ns_display_info structure for the display F is on.  */
#define FRAME_DISPLAY_INFO(f) ((f)->output_data.wl->display_info)
#define FRAME_X_OUTPUT(f) ((f)->output_data.wl)
#define FRAME_X_WINDOW(f) ((f)->output_data.wl->window_desc)
#define FRAME_WL_WINDOW(f) ((f)->output_data.wl->window_desc)

#define FRAME_X_DISPLAY(f) (0)
#define FRAME_X_SCREEN(f) (0)
#define FRAME_X_VISUAL(f) FRAME_DISPLAY_INFO(f)->visual

#define FRAME_FONT(f) ((f)->output_data.wl->font)
#define FRAME_FONTSET(f) ((f)->output_data.wl->fontset)

#define FRAME_BASELINE_OFFSET(f) ((f)->output_data.wl->baseline_offset)

extern bool ewl_defined_color (struct frame *, const char *, XColor *, bool);
extern void ewl_query_color (unsigned long pixel, struct ewl_color *color);
extern struct ewl_display_info *ewl_term_init (Lisp_Object display_name);
extern int ewl_select (int fds_lim, fd_set *rfds, fd_set *wfds, fd_set *efds,
		       struct timespec const *timeout, sigset_t const *sigmask);
extern void x_delete_display (struct ewl_display_info *dpyinfo);
extern void x_free_frame_resources (struct frame *f);

extern void syms_of_wlterm (void);
extern void syms_of_wlfns (void);

#endif /* WLTERM_H */
