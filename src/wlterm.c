/* Communication module for terminals which understand the Wayland protocol.

Copyright (C) 1989, 1993-2014 Free Software Foundation, Inc.

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

#include <config.h>

#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <sys/types.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <sys/mman.h>

#include "lisp.h"
#include "blockinput.h"
#include "sysselect.h"
#include "wlterm.h"
#include "systime.h"
#include "character.h"
#include "fontset.h"
#include "composite.h"

#include "termhooks.h"
#include "termchar.h"
#include "menu.h"
#include "window.h"
#include "keyboard.h"
#include "buffer.h"
#include "font.h"

extern Lisp_Object Qwl;

struct ewl_display_info *x_display_list;

/* Unused dummy def needed for compatibility. */
Lisp_Object tip_frame;

static Lisp_Object Qalt, Qhyper, Qmeta, Qsuper, Qmodifier_value;

static void ewl_clear_frame (struct frame *f);
static void ewl_set_mouse_face_gc (struct glyph_string *s);
static void x_frame_rehighlight (struct ewl_display_info *dpyinfo);


void
frame_set_mouse_pixel_position (struct frame *f, int pix_x, int pix_y)
{
  /* FIXME: Not implemented.  */
}


void
x_focus_frame (struct frame *f)
{
  struct ewl_display_info *dpyinfo = FRAME_DISPLAY_INFO (f);
  dpyinfo->x_focus_frame = f;
  x_frame_rehighlight (dpyinfo);
}


void
x_set_offset (struct frame *f, int xoff, int yoff, int change_grav)
{
  /* FIXME: Not implemented.  */
}


/* Default implementation taken from xterm.c.  */

Lisp_Object
x_new_font (struct frame *f, Lisp_Object font_object, int fontset)
{
  struct font *font = XFONT_OBJECT (font_object);

  if (fontset < 0)
    fontset = fontset_from_font (font_object);
  FRAME_FONTSET (f) = fontset;

  if (FRAME_FONT (f) == font)
    /* This font is already set in frame F.  There's nothing more to
       do.  */
    return font_object;

  FRAME_FONT (f) = font;

  FRAME_BASELINE_OFFSET (f) = font->baseline_offset;
  FRAME_COLUMN_WIDTH (f) = font->average_width;
  FRAME_LINE_HEIGHT (f) = font->height;

  compute_fringe_widths (f, 1);

  /* Compute the scroll bar width in character columns.  */
  if (FRAME_CONFIG_SCROLL_BAR_WIDTH (f) > 0)
    {
      int wid = FRAME_COLUMN_WIDTH (f);
      FRAME_CONFIG_SCROLL_BAR_COLS (f)
	= (FRAME_CONFIG_SCROLL_BAR_WIDTH (f) + wid - 1) / wid;
    }
  else
    {
      int wid = FRAME_COLUMN_WIDTH (f);
      FRAME_CONFIG_SCROLL_BAR_COLS (f) = (14 + wid - 1) / wid;
    }

  /* Now make the frame display the given font.  */
  if (FRAME_WL_WINDOW (f) != 0)
    x_set_window_size (f, 0, FRAME_COLS (f) * FRAME_COLUMN_WIDTH (f),
		       FRAME_LINES (f) * FRAME_LINE_HEIGHT (f), 1);

  return font_object;
}


void
show_hourglass (struct atimer *timer)
{
  /* FIXME: Not implemented.  */
}


void
hide_hourglass (void)
{
  /* FIXME: Not implemented.  */
}


/* WL: Events.  */

static struct wl_display *default_display;

/* `ewl_select' is a `pselect' replacement.  To announce the intention
   to read from the fd, we need to call wl_display_prepare_read before
   pselect, and then actully read events with wl_display_read_events.

   Note that the fd should already be set in RFDS via
   add_keyboard_wait_descriptor.  */

int
ewl_select (int fds_lim, fd_set *rfds, fd_set *wfds, fd_set *efds,
	    struct timespec const *timeout, sigset_t const *sigmask)
{
  int retval;

  block_input ();

  if (default_display)
    {
      while (wl_display_prepare_read (default_display) != 0)
	wl_display_dispatch_pending (default_display);
      wl_display_flush (default_display);
    }

  retval = pselect (fds_lim, rfds, wfds, efds, timeout, sigmask);

  if (default_display)
    {
      wl_display_read_events (default_display);
      wl_display_flush (default_display);
    }

  unblock_input ();

  return retval;
}


/* WL: Redrawing.  */

static int
ewl_create_anonymous_file (off_t size)
{
  static char const nonce_base[] = "/emacs-sharedXXXXXX";
  char *nonce;
  const char *dir;
  int fd = -1;

  dir = getenv ("XDG_RUNTIME_DIR");
  if (dir == NULL)
    goto out;

  USE_SAFE_ALLOCA;
  nonce = SAFE_ALLOCA (strlen (dir) + sizeof nonce_base);
  strcpy (nonce, dir);
  strcat (nonce, nonce_base);

  fd = mkostemp (nonce, O_BINARY | O_CLOEXEC);
  if (fd < 0)
    goto out;

  if (ftruncate (fd, size) < 0)
    {
      close (fd);
      fd = -1;
      goto out;
    }

 out:
  SAFE_FREE ();
  return fd;
}


static void
ewl_prepare_cairo_surface (struct frame *f, int width, int height)
{
  struct ewl_display_info *dpyinfo = FRAME_DISPLAY_INFO (f);
  struct wl_shm_pool *pool;
  struct wl_buffer *buffer;
  int size, stride;
  int fd;
  void *p;

  stride = width * 4;
  size = stride * height;

  fd = ewl_create_anonymous_file (size);
  if (fd < 0)
    return;

  p = mmap (NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (p == MAP_FAILED)
    {
      close(fd);
      return;
    }

  if (f->output_data.wl->data)
    munmap (f->output_data.wl->data, f->output_data.wl->size);
  f->output_data.wl->size = size;
  f->output_data.wl->data = p;

  pool = wl_shm_create_pool (dpyinfo->shm, fd, size);
  if (f->output_data.wl->buffer)
    wl_buffer_destroy (f->output_data.wl->buffer);
  f->output_data.wl->buffer
    = wl_shm_pool_create_buffer (pool, 0,
				 width, height, stride,
				 WL_SHM_FORMAT_ARGB8888);
  wl_shm_pool_destroy (pool);

  if (f->output_data.wl->cairo_surface)
    cairo_surface_destroy (f->output_data.wl->cairo_surface);
  f->output_data.wl->cairo_surface
    = cairo_image_surface_create_for_data (p,
					   CAIRO_FORMAT_ARGB32,
					   width, height, stride);
  close (fd);
}


static void
ewl_redraw_frame (void *data, struct wl_callback *callback, uint32_t time);
static const struct wl_callback_listener ewl_frame_listener =
  {
    ewl_redraw_frame
  };


static void
ewl_redraw_frame (void *data, struct wl_callback *callback, uint32_t time)
{
  struct frame *f = data;

  block_input ();

  wl_surface_attach (f->output_data.wl->surface, f->output_data.wl->buffer,
		     0, 0);
  wl_surface_damage (f->output_data.wl->surface, 0, 0,
		     FRAME_PIXEL_WIDTH (f), FRAME_PIXEL_HEIGHT (f));
  if (callback)
    wl_callback_destroy (callback);

  f->output_data.wl->callback = wl_surface_frame (f->output_data.wl->surface);
  wl_callback_add_listener (f->output_data.wl->callback, &ewl_frame_listener,
			    f);
  if (f->output_data.wl->surface)
    wl_surface_commit (f->output_data.wl->surface);

  unblock_input ();
}


/* Resize the outer window of frame F after changing the height.
   COLUMNS/ROWS is the size the edit area shall have after the resize.  */

static void
ewl_frame_set_char_size (struct frame *f, int width, int height)
{
  int pixelwidth = FRAME_TEXT_TO_PIXEL_WIDTH (f, width);
  int pixelheight = FRAME_TEXT_TO_PIXEL_HEIGHT (f, height);

  if (FRAME_PIXEL_HEIGHT (f) == 0)
    return;

  if (f->output_data.wl->surface == NULL)
    return;

  ewl_prepare_cairo_surface (f, pixelwidth, pixelheight);

  if (f->output_data.wl->callback)
    wl_callback_destroy (f->output_data.wl->callback);

  ewl_clear_frame (f);

  SET_FRAME_GARBAGED (f);
  cancel_mouse_face (f);
}


void
x_set_window_size (struct frame *f,
                   int change_grav,
                   int width,
                   int height,
                   bool pixelwise)
{
  block_input ();

  check_frame_size (f, &width, &height, pixelwise);

  if (NILP (tip_frame) || XFRAME (tip_frame) != f)
    {
      int text_width, text_height;

      /* When the frame is maximized/fullscreen or running under for
         example Xmonad, x_set_window_size_1 will be a no-op.
         In that case, the right thing to do is extend rows/width to
         the current frame size.  We do that first if x_set_window_size_1
         turns out to not be a no-op (there is no way to know).
         The size will be adjusted again if the frame gets a
         ConfigureNotify event as a result of x_set_window_size.  */
      int pixelh = FRAME_PIXEL_HEIGHT (f);
      text_width = FRAME_PIXEL_TO_TEXT_WIDTH (f, FRAME_PIXEL_WIDTH (f));
      text_height = FRAME_PIXEL_TO_TEXT_HEIGHT (f, pixelh);

      change_frame_size (f, text_width, text_height, 0, 1, 0, 1);
    }

  if (! pixelwise)
    ewl_frame_set_char_size (f, width * FRAME_COLUMN_WIDTH (f),
			    height * FRAME_LINE_HEIGHT (f));
  else
    ewl_frame_set_char_size (f, width, height);

  /* If cursor was outside the new size, mark it as off.  */
  mark_window_cursors_off (XWINDOW (f->root_window));

  /* Clear out any recollection of where the mouse highlighting was,
     since it might be in a place that's outside the new frame size.
     Actually checking whether it is outside is a pain in the neck,
     so don't try--just let the highlighting be done afresh with new size.  */
  cancel_mouse_face (f);

  unblock_input ();
}


char *
x_get_keysym_name (int keysym)
{
  static char value[64];
  if (xkb_keysym_get_name (keysym, value, sizeof value) == -1)
    value[0] = '\0';
  return value;
}


/* Start update of window W.  */

static void
ewl_update_window_begin (struct window *w)
{
  struct frame *f = XFRAME (WINDOW_FRAME (w));
  Mouse_HLInfo *hlinfo = MOUSE_HL_INFO (f);

  w->output_cursor = w->cursor;

  block_input ();

  if (f == hlinfo->mouse_face_mouse_frame)
    {
      /* Don't do highlighting for mouse motion during the update.  */
      hlinfo->mouse_face_defer = 1;

      /* If F needs to be redrawn, simply forget about any prior mouse
	 highlighting.  */
      if (FRAME_GARBAGED_P (f))
	hlinfo->mouse_face_window = Qnil;
    }

  unblock_input ();
}


/* Draw a vertical window border from (x,y0) to (x,y1)  */

static void
ewl_draw_vertical_window_border (struct window *w, int x, int y0, int y1)
{
  struct frame *f = XFRAME (WINDOW_FRAME (w));
  struct face *face;
  cairo_t *cr;
  struct ewl_color fg;

  cr = cairo_create (f->output_data.wl->cairo_surface);
  face = FACE_FROM_ID (f, VERTICAL_BORDER_FACE_ID);
  if (face)
    {
      ewl_query_color (face->foreground, &fg);
      cairo_set_source_rgba (cr, fg.red, fg.green, fg.blue, fg.alpha);
    }
  cairo_move_to (cr, x, y0);
  cairo_line_to (cr, x, y1);
  cairo_stroke (cr);
}


/* Draw a window divider from (x0,y0) to (x1,y1)  */

static void
ewl_draw_window_divider (struct window *w, int x0, int x1, int y0, int y1)
{
  struct frame *f = XFRAME (WINDOW_FRAME (w));
  struct face *face = FACE_FROM_ID (f, WINDOW_DIVIDER_FACE_ID);
  struct face *face_first = FACE_FROM_ID (f, WINDOW_DIVIDER_FIRST_PIXEL_FACE_ID);
  struct face *face_last = FACE_FROM_ID (f, WINDOW_DIVIDER_LAST_PIXEL_FACE_ID);
  unsigned long color = face ? face->foreground : FRAME_FOREGROUND_PIXEL (f);
  unsigned long color_first = (face_first
			       ? face_first->foreground
			       : FRAME_FOREGROUND_PIXEL (f));
  unsigned long color_last = (face_last
			      ? face_last->foreground
			      : FRAME_FOREGROUND_PIXEL (f));
  cairo_t *cr;
  struct ewl_color fg;

  cr = cairo_create (f->output_data.wl->cairo_surface);
  if (y1 - y0 > x1 - x0 && x1 - x0 > 2)
    /* Vertical.  */
    {
      ewl_query_color (color_first, &fg);
      cairo_set_source_rgba (cr, fg.red, fg.green, fg.blue, fg.alpha);
      cairo_rectangle (cr, x0, y0, 1, y1 - y0);
      cairo_fill (cr);

      ewl_query_color (color, &fg);
      cairo_set_source_rgba (cr, fg.red, fg.green, fg.blue, fg.alpha);
      cairo_rectangle (cr, x0 + 1, y0, x1 - x0 - 2, y1 - y0);
      cairo_fill (cr);

      ewl_query_color (color_last, &fg);
      cairo_set_source_rgba (cr, fg.red, fg.green, fg.blue, fg.alpha);
      cairo_rectangle (cr, x1 - 1, y0, 1, y1 - y0);
      cairo_fill (cr);
    }
  else if (x1 - x0 > y1 - y0 && y1 - y0 > 3)
    /* Horizontal.  */
    {
      ewl_query_color (color_first, &fg);
      cairo_set_source_rgba (cr, fg.red, fg.green, fg.blue, fg.alpha);
      cairo_rectangle (cr, x0, y0, x1 - x0, 1);
      cairo_fill (cr);

      ewl_query_color (color, &fg);
      cairo_set_source_rgba (cr, fg.red, fg.green, fg.blue, fg.alpha);
      cairo_rectangle (cr, x0, y0 + 1, x1 - x0, y1 - y0 - 2);
      cairo_fill (cr);

      ewl_query_color (color_last, &fg);
      cairo_set_source_rgba (cr, fg.red, fg.green, fg.blue, fg.alpha);
      cairo_rectangle (cr, x0, y1 - 1, x1 - x0, 1);
      cairo_fill (cr);
    }
  else
    {
      ewl_query_color (color, &fg);
      cairo_set_source_rgba (cr, fg.red, fg.green, fg.blue, fg.alpha);
      cairo_rectangle (cr, x0, y0, x1 - x0, y1 - y0);
      cairo_fill (cr);
    }
}


/* End update of window W.

   Draw vertical borders between horizontally adjacent windows, and
   display W's cursor if CURSOR_ON_P is non-zero.

   MOUSE_FACE_OVERWRITTEN_P non-zero means that some row containing
   glyphs in mouse-face were overwritten.  In that case we have to
   make sure that the mouse-highlight is properly redrawn.

   W may be a menu bar pseudo-window in case we don't have X toolkit
   support.  Such windows don't have a cursor, so don't display it
   here.  */

static void
ewl_update_window_end (struct window *w, bool cursor_on_p,
		       bool mouse_face_overwritten_p)
{
  if (!w->pseudo_window_p)
    {
      block_input ();

      if (cursor_on_p)
	display_and_set_cursor (w, 1,
				w->output_cursor.hpos, w->output_cursor.vpos,
				w->output_cursor.x, w->output_cursor.y);

      if (draw_window_fringes (w, 1))
	{
	  if (WINDOW_RIGHT_DIVIDER_WIDTH (w))
	    x_draw_right_divider (w);
	  else
	    x_draw_vertical_border (w);
	}

      unblock_input ();
    }

  /* If a row with mouse-face was overwritten, arrange for
     XTframe_up_to_date to redisplay the mouse highlight.  */
  if (mouse_face_overwritten_p)
    {
      Mouse_HLInfo *hlinfo = MOUSE_HL_INFO (XFRAME (w->frame));

      hlinfo->mouse_face_beg_row = hlinfo->mouse_face_beg_col = -1;
      hlinfo->mouse_face_end_row = hlinfo->mouse_face_end_col = -1;
      hlinfo->mouse_face_window = Qnil;
    }
}


/* Scroll part of the display as described by RUN.  */

static void
ewl_scroll_run (struct window *w, struct run *run)
{
  struct frame *f = XFRAME (w->frame);
  int x, y, width, height, from_y, to_y, bottom_y;
  cairo_t *cr;

  /* Get frame-relative bounding box of the text display area of W,
     without mode lines.  Include in this box the left and right
     fringe of W.  */
  window_box (w, ANY_AREA, &x, &y, &width, &height);

  from_y = WINDOW_TO_FRAME_PIXEL_Y (w, run->current_y);
  to_y = WINDOW_TO_FRAME_PIXEL_Y (w, run->desired_y);
  bottom_y = y + height;

  if (to_y < from_y)
    {
      /* Scrolling up.  Make sure we don't copy part of the mode
	 line at the bottom.  */
      if (from_y + run->height > bottom_y)
	height = bottom_y - from_y;
      else
	height = run->height;
    }
  else
    {
      /* Scrolling down.  Make sure we don't copy over the mode line.
	 at the bottom.  */
      if (to_y + run->height > bottom_y)
	height = bottom_y - to_y;
      else
	height = run->height;
    }

  block_input ();

  /* Cursor off.  Will be switched on again in x_update_window_end.  */
  x_clear_cursor (w);

  cr = cairo_create (f->output_data.wl->cairo_surface);
  cairo_set_source_surface (cr, f->output_data.wl->cairo_surface,
			    0, to_y - from_y);
  cairo_rectangle (cr, x, to_y, width, height);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_fill (cr);
  cairo_destroy (cr);

  unblock_input ();
}


/* Draw truncation mark bitmaps, continuation mark bitmaps, overlay
   arrow bitmaps, or clear the fringes if no bitmaps are required
   before DESIRED_ROW is made current.  This function is called from
   update_window_line only if it is known that there are differences
   between bitmaps to be drawn between current row and DESIRED_ROW.  */

static void
ewl_after_update_window_line (struct window *w, struct glyph_row *desired_row)
{
  eassert (w);

  if (!desired_row->mode_line_p && !w->pseudo_window_p)
    desired_row->redraw_fringe_bitmaps_p = 1;
}


static void
ewl_clip_to_row (struct window *w, struct glyph_row *row,
		 enum glyph_row_area area, cairo_t *cr)
{
  NativeRectangle clip_rect;
  int window_x, window_y, window_width;

  window_box (w, area, &window_x, &window_y, &window_width, 0);

  clip_rect.x = window_x;
  clip_rect.y = WINDOW_TO_FRAME_PIXEL_Y (w, max (0, row->y));
  clip_rect.y = max (clip_rect.y, window_y);
  clip_rect.width = window_width;
  clip_rect.height = row->visible_height;

  cairo_rectangle (cr, clip_rect.x, clip_rect.y,
		   clip_rect.width, clip_rect.height);
  cairo_clip (cr);
}


static void
ewl_draw_fringe_bitmap (struct window *w, struct glyph_row *row, struct draw_fringe_bitmap_params *p)
{
  struct frame *f = XFRAME (WINDOW_FRAME (w));
  struct face *face = p->face;
  cairo_t *cr;

  cr = cairo_create (f->output_data.wl->cairo_surface);

  /* Must clip because of partially visible lines.  */
  ewl_clip_to_row (w, row, ANY_AREA, cr);

  if (p->bx >= 0 && !p->overlay_p)
    {
      struct ewl_color bg;

      ewl_query_color (face->background, &bg);
      cairo_set_source_rgba (cr, bg.red, bg.green, bg.blue, bg.alpha);
      cairo_rectangle (cr, p->bx, p->by, p->nx, p->ny);
      cairo_fill (cr);
    }

  if (p->which)
    /* FIXME: Not implemented.  */

  cairo_destroy (cr);
}


/* RIF: Clear area on frame F.  */

static void
ewl_clear_frame_area (struct frame *f, int x, int y, int width, int height)
{
  struct ewl_color bg;
  cairo_t *cr;

  ewl_query_color (FRAME_BACKGROUND_PIXEL (f), &bg);
  cr = cairo_create (f->output_data.wl->cairo_surface);
  cairo_set_source_rgba (cr, bg.red, bg.green, bg.blue, bg.alpha);
  cairo_rectangle (cr, x, y, width, height);
  cairo_fill (cr);
  cairo_destroy (cr);
}


/* Draw a hollow box cursor on window W in glyph row ROW.  */

static void
ewl_draw_hollow_cursor (struct window *w, struct glyph_row *row)
{
  struct frame *f = XFRAME (WINDOW_FRAME (w));
  struct ewl_display_info *dpyinfo = FRAME_DISPLAY_INFO (f);
  int x, y, wd, h;
  XGCValues xgcv;
  struct glyph *cursor_glyph;
  GC gc;
  cairo_t *cr = cairo_create (f->output_data.wl->cairo_surface);
  struct ewl_color fg;

  /* Get the glyph the cursor is on.  If we can't tell because
     the current matrix is invalid or such, give up.  */
  cursor_glyph = get_phys_cursor_glyph (w);
  if (cursor_glyph == NULL)
    return;

  /* Compute frame-relative coordinates for phys cursor.  */
  get_phys_cursor_geometry (w, row, cursor_glyph, &x, &y, &h);
  wd = w->phys_cursor_width;

  /* The foreground of cursor_gc is typically the same as the normal
     background color, which can cause the cursor box to be invisible.  */
  xgcv.foreground = f->output_data.wl->cursor_pixel;
  if (dpyinfo->scratch_cursor_gc == NULL)
    dpyinfo->scratch_cursor_gc = xmalloc (sizeof xgcv);
  *dpyinfo->scratch_cursor_gc = xgcv;
  gc = dpyinfo->scratch_cursor_gc;

  /* Set clipping, draw the rectangle, and reset clipping again.  */
  ewl_clip_to_row (w, row, TEXT_AREA, cr);
  ewl_query_color (gc->foreground, &fg);
  cairo_set_source_rgba (cr, fg.red, fg.green, fg.blue, fg.alpha);
  cairo_rectangle (cr, x, y, wd, h - 1);
  cairo_stroke (cr);
  cairo_destroy (cr);
}


/* RIF: Draw cursor on window W.  */

static void
ewl_draw_window_cursor (struct window *w, struct glyph_row *glyph_row, int x,
			int y, enum text_cursor_kinds cursor_type,
			int cursor_width, bool on_p, bool active_p)
{
  if (on_p)
    {
      w->phys_cursor_type = cursor_type;
      w->phys_cursor_on_p = 1;

      if (glyph_row->exact_window_width_line_p
	  && (glyph_row->reversed_p
	      ? (w->phys_cursor.hpos < 0)
	      : (w->phys_cursor.hpos >= glyph_row->used[TEXT_AREA])))
	{
	  glyph_row->cursor_in_fringe_p = 1;
	  draw_fringe_bitmap (w, glyph_row, glyph_row->reversed_p);
	}
      else
	{
	  switch (cursor_type)
	    {
	    case HOLLOW_BOX_CURSOR:
	      ewl_draw_hollow_cursor (w, glyph_row);
	      break;

	    case FILLED_BOX_CURSOR:
	      draw_phys_cursor_glyph (w, glyph_row, DRAW_CURSOR);
	      break;

	    case BAR_CURSOR:
	      /* FIXME: Not implemented.  */
	      break;

	    case HBAR_CURSOR:
	      /* FIXME: Not implemented.  */
	      break;

	    case NO_CURSOR:
	      w->phys_cursor_width = 0;
	      break;

	    default:
	      emacs_abort ();
	    }
	}
    }
}


/* Brightness beyond which a color won't have its highlight brightness
   boosted.

   Nominally, highlight colors for `3d' faces are calculated by
   brightening an object's color by a constant scale factor, but this
   doesn't yield good results for dark colors, so for colors who's
   brightness is less than this value (on a scale of 0-65535) have an
   use an additional additive factor.

   The value here is set so that the default menu-bar/mode-line color
   (grey75) will not have its highlights changed at all.  */
#define HIGHLIGHT_COLOR_DARK_BOOST_LIMIT 0.75


/* Allocate a color which is lighter or darker than *PIXEL by FACTOR
   or DELTA.  Try a color with RGB values multiplied by FACTOR first.
   If this produces the same color as PIXEL, try a color where all RGB
   values have DELTA added.  Return the allocated color in *PIXEL.
   DISPLAY is the X display, CMAP is the colormap to operate on.
   Value is non-zero if successful.  */

static bool
ewl_alloc_lighter_color (struct frame *f,
			 struct ewl_color *color, double factor, double delta)
{
  struct ewl_color new;
  double bright;

  /* Change RGB values by specified FACTOR.  Avoid overflow!  */
  eassert (factor >= 0);
  new.red = min (1.0, factor * color->red);
  new.green = min (1.0, factor * color->green);
  new.blue = min (1.0, factor * color->blue);

  /* Calculate brightness of COLOR.  */
  bright = (2 * color->red + 3 * color->green + color->blue) / 6;

  /* We only boost colors that are darker than
     HIGHLIGHT_COLOR_DARK_BOOST_LIMIT.  */
  if (bright < HIGHLIGHT_COLOR_DARK_BOOST_LIMIT)
    /* Make an additive adjustment to NEW, because it's dark enough so
       that scaling by FACTOR alone isn't enough.  */
    {
      /* How far below the limit this color is (0 - 1, 1 being darker).  */
      double dimness = 1 - bright / HIGHLIGHT_COLOR_DARK_BOOST_LIMIT;
      /* The additive adjustment.  */
      double min_delta = delta * dimness * factor / 2;

      if (factor < 1)
	{
	  new.red =   max (0, new.red -   min_delta);
	  new.green = max (0, new.green - min_delta);
	  new.blue =  max (0, new.blue -  min_delta);
	}
      else
	{
	  new.red =   min (1.0, min_delta + new.red);
	  new.green = min (1.0, min_delta + new.green);
	  new.blue =  min (1.0, min_delta + new.blue);
	}
    }

  *color = new;
  return 1;
}


/* Set up the foreground color for drawing relief lines of glyph
   string S.  RELIEF is a pointer to a struct relief containing the GC
   with which lines will be drawn.  Use a color that is FACTOR or
   DELTA lighter or darker than the relief's background which is found
   in S->f->output_data.x->relief_background.  If such a color cannot
   be allocated, use DEFAULT_PIXEL, instead.  */

static void
ewl_setup_relief_color (struct frame *f, struct relief *relief, double factor,
			double delta, unsigned long default_pixel)
{
  struct ewl_color color;
  struct ewl_output *di = f->output_data.wl;
  unsigned long background = di->relief_background;
  struct ewl_display_info *dpyinfo = FRAME_DISPLAY_INFO (f);

  /* Allocate new color.  */
  ewl_query_color (background, &color);
  relief->pixel = default_pixel;
  if (dpyinfo->n_planes != 1
      && ewl_alloc_lighter_color (f, &color, factor, delta))
    {
      relief->pixel = 0xFF000000
	| (int) (color.red * 255) << 16
	| (int) (color.green * 255) << 8
	| (int) (color.blue * 255);
    }

  if (!relief->gc)
    relief->gc = xmalloc (sizeof *relief->gc);
  relief->gc->background = relief->gc->foreground = relief->pixel;
}


/* Set up colors for the relief lines around glyph string S.  */

static void
ewl_setup_relief_colors (struct glyph_string *s)
{
  struct ewl_output *di = s->f->output_data.wl;
  unsigned long pixel;

  if (s->face->use_box_color_for_shadows_p)
    pixel = s->face->box_color;
  else
    pixel = s->gc->background;

  if (di->white_relief.gc == 0
      || pixel != di->relief_background)
    {
      di->relief_background = pixel;
      ewl_setup_relief_color (s->f, &di->white_relief, 1.2, 0.5,
			      WHITE_PIX_DEFAULT (s->f));
      ewl_setup_relief_color (s->f, &di->black_relief, 0.6, 0.25,
			      BLACK_PIX_DEFAULT (s->f));
    }
}


/* Draw a relief on frame F inside the rectangle given by LEFT_X,
   TOP_Y, RIGHT_X, and BOTTOM_Y.  WIDTH is the thickness of the relief
   to draw, it must be >= 0.  RAISED_P non-zero means draw a raised
   relief.  LEFT_P non-zero means draw a relief on the left side of
   the rectangle.  RIGHT_P non-zero means draw a relief on the right
   side of the rectangle.  CLIP_RECT is the clipping rectangle to use
   when drawing.  */

static void
ewl_draw_relief_rect (struct frame *f,
		      int left_x, int top_y, int right_x, int bottom_y, int width,
		      int raised_p, int top_p, int bot_p, int left_p, int right_p,
		      NativeRectangle *clip_rect,
		      cairo_t *cr)
{
  int i;
  GC gc;
  struct ewl_color fg, bg;

  if (raised_p)
    gc = f->output_data.wl->white_relief.gc;
  else
    gc = f->output_data.wl->black_relief.gc;

  ewl_query_color (gc->foreground, &fg);
  ewl_query_color (gc->background, &bg);

  cairo_rectangle (cr, clip_rect->x, clip_rect->y,
		   clip_rect->width, clip_rect->height);
  cairo_clip (cr);

  cairo_set_source_rgba (cr, fg.red, fg.green, fg.blue, fg.alpha);

  /* This code is more complicated than it has to be, because of two
     minor hacks to make the boxes look nicer: (i) if width > 1, draw
     the outermost line using the black relief.  (ii) Omit the four
     corner pixels.  */

  /* Top.  */
  if (top_p)
    {
      if (width == 1)
	{
	  cairo_move_to (cr, left_x  + (left_p  ? 1 : 0), top_y);
	  cairo_line_to (cr, right_x + (right_p ? 0 : 1), top_y);
	  cairo_stroke (cr);
	}

      for (i = 1; i < width; ++i)
	{
	  cairo_move_to (cr, left_x  + i * left_p, top_y + i);
	  cairo_line_to (cr, right_x + 1 - i * right_p, top_y + i);
	  cairo_stroke (cr);
	}
    }

  /* Left.  */
  if (left_p)
    {
      if (width == 1)
	{
	  cairo_move_to (cr, left_x, top_y + 1);
	  cairo_line_to (cr, left_x, bottom_y);
	  cairo_stroke (cr);
	}

      cairo_set_source_rgba (cr, bg.red, bg.green, bg.blue, bg.alpha);
      cairo_rectangle (cr, left_x, top_y, 1, 1);
      cairo_fill (cr);
      cairo_rectangle (cr, left_x, bottom_y, 1, 1);
      cairo_fill (cr);
      cairo_set_source_rgba (cr, fg.red, fg.green, fg.blue, fg.alpha);

      for (i = (width > 1 ? 1 : 0); i < width; ++i)
	{
	  cairo_move_to (cr, left_x + i, top_y + (i + 1) * top_p);
	  cairo_line_to (cr, left_x + i, bottom_y + 1 - (i + 1) * bot_p);
	  cairo_stroke (cr);
	}
    }

  cairo_reset_clip (cr);
  if (raised_p)
    gc = f->output_data.wl->black_relief.gc;
  else
    gc = f->output_data.wl->white_relief.gc;

  ewl_query_color (gc->foreground, &fg);
  ewl_query_color (gc->background, &bg);

  cairo_rectangle (cr, clip_rect->x, clip_rect->y,
		   clip_rect->width, clip_rect->height);
  cairo_clip (cr);

  cairo_set_source_rgba (cr, fg.red, fg.green, fg.blue, fg.alpha);

  if (width > 1)
    {
      /* Outermost top line.  */
      if (top_p)
	{
	  cairo_move_to (cr, left_x  + (left_p  ? 1 : 0), top_y);
	  cairo_line_to (cr, right_x + (right_p ? 0 : 1), top_y);
	  cairo_stroke (cr);
	}

      /* Outermost left line.  */
      if (left_p)
	{
	  cairo_move_to (cr, left_x, top_y + 1);
	  cairo_line_to (cr, left_x, bottom_y);
	  cairo_stroke (cr);
	}
    }

  /* Bottom.  */
  if (bot_p)
    {
      cairo_move_to (cr, left_x  + (left_p  ? 1 : 0), bottom_y);
      cairo_line_to (cr, right_x + (right_p ? 0 : 1), bottom_y);
      cairo_stroke (cr);

      for (i = 1; i < width; ++i)
	{
	  cairo_move_to (cr, left_x  + i * left_p, bottom_y - i);
	  cairo_line_to (cr, right_x + 1 - i * right_p, bottom_y - i);
	  cairo_stroke (cr);
	}
    }

  /* Right.  */
  if (right_p)
    {
      cairo_set_source_rgba (cr, bg.red, bg.green, bg.blue, bg.alpha);
      cairo_rectangle (cr, right_x, top_y, 1, 1);
      cairo_fill (cr);
      cairo_rectangle (cr, right_x, bottom_y, 1, 1);
      cairo_fill (cr);
      cairo_set_source_rgba (cr, fg.red, fg.green, fg.blue, fg.alpha);

      for (i = 0; i < width; ++i)
	{
	  cairo_move_to (cr, right_x - i, top_y + (i + 1) * top_p);
	  cairo_line_to (cr, right_x - i, bottom_y + 1 - (i + 1) * bot_p);
	  cairo_stroke (cr);
	}
    }

  cairo_reset_clip (cr);
}


/* Draw a box on frame F inside the rectangle given by LEFT_X, TOP_Y,
   RIGHT_X, and BOTTOM_Y.  WIDTH is the thickness of the lines to
   draw, it must be >= 0.  LEFT_P non-zero means draw a line on the
   left side of the rectangle.  RIGHT_P non-zero means draw a line
   on the right side of the rectangle.  CLIP_RECT is the clipping
   rectangle to use when drawing.  */

static void
ewl_draw_box_rect (struct glyph_string *s,
		   int left_x, int top_y, int right_x, int bottom_y, int width,
		   int left_p, int right_p, NativeRectangle *clip_rect,
		   cairo_t *cr)
{
  struct ewl_color fg;

  ewl_query_color (s->gc->foreground, &fg);
  cairo_set_source_rgba (cr, fg.red, fg.green, fg.blue, fg.alpha);

  cairo_rectangle (cr, clip_rect->x, clip_rect->y,
		   clip_rect->width, clip_rect->height);
  cairo_clip (cr);

  /* Top.  */
  cairo_rectangle (cr, left_x, top_y, right_x - left_x + 1, width);
  cairo_fill (cr);

  /* Left.  */
  if (left_p)
    {
      cairo_rectangle (cr, left_x, top_y, width, bottom_y - top_y + 1);
      cairo_fill (cr);
    }

  /* Bottom.  */
  cairo_rectangle (cr, left_x, bottom_y - width + 1,
		   right_x - left_x + 1, width);
  cairo_fill (cr);

  /* Right.  */
  if (right_p)
    {
      cairo_rectangle (cr, right_x - width + 1, top_y,
		       width, bottom_y - top_y + 1);
      cairo_fill (cr);
    }

  cairo_reset_clip (cr);
}


/* Draw a box around glyph string S.  */

static void
ewl_draw_glyph_string_box (struct glyph_string *s, cairo_t *cr)
{
  int width, left_x, right_x, top_y, bottom_y, last_x, raised_p;
  int left_p, right_p;
  struct glyph *last_glyph;
  NativeRectangle clip_rect;

  last_x = ((s->row->full_width_p && !s->w->pseudo_window_p)
	    ? WINDOW_RIGHT_EDGE_X (s->w)
	    : window_box_right (s->w, s->area));

  /* The glyph that may have a right box line.  */
  last_glyph = (s->cmp || s->img
		? s->first_glyph
		: s->first_glyph + s->nchars - 1);

  width = eabs (s->face->box_line_width);
  raised_p = s->face->box == FACE_RAISED_BOX;
  left_x = s->x;
  right_x = (s->row->full_width_p && s->extends_to_end_of_line_p
	     ? last_x - 1
	     : min (last_x, s->x + s->background_width) - 1);
  top_y = s->y;
  bottom_y = top_y + s->height - 1;

  left_p = (s->first_glyph->left_box_line_p
	    || (s->hl == DRAW_MOUSE_FACE
		&& (s->prev == NULL
		    || s->prev->hl != s->hl)));
  right_p = (last_glyph->right_box_line_p
	     || (s->hl == DRAW_MOUSE_FACE
		 && (s->next == NULL
		     || s->next->hl != s->hl)));

  get_glyph_string_clip_rect (s, &clip_rect);

  if (s->face->box == FACE_SIMPLE_BOX)
    ewl_draw_box_rect (s, left_x, top_y, right_x, bottom_y, width,
		       left_p, right_p, &clip_rect, cr);
  else
    {
      ewl_setup_relief_colors (s);
      ewl_draw_relief_rect (s->f, left_x, top_y, right_x, bottom_y,
			    width, raised_p, 1, 1, left_p, right_p, &clip_rect,
			    cr);
    }
}


/* Set clipping for output of glyph string S.  S may be part of a mode
   line or menu if we don't have X toolkit support.  */

static void
ewl_set_glyph_string_clipping (struct glyph_string *s, cairo_t *cr)
{
  NativeRectangle *r = s->clip;
  int n = get_glyph_string_clip_rects (s, r, 2);

  if (n > 0)
    {
      int i;

      for (i = 0; i < n; i++)
	{
	  cairo_rectangle (cr, r[i].x, r[i].y, r[i].width, r[i].height);
	  cairo_clip (cr);
	}
    }
  s->num_clips = n;
}


/* Set SRC's clipping for output of glyph string DST.  This is called
   when we are drawing DST's left_overhang or right_overhang only in
   the area of SRC.  */

static void
ewl_set_glyph_string_clipping_exactly (struct glyph_string *src, struct glyph_string *dst, cairo_t *cr)
{
  XRectangle r;

  r.x = src->x;
  r.width = src->width;
  r.y = src->y;
  r.height = src->height;
  CONVERT_TO_XRECT (dst->clip[0], r);
  dst->num_clips = 1;
  cairo_rectangle (cr, r.x, r.y, r.width, r.height);
  cairo_clip (cr);
}


/* RIF:
   Compute left and right overhang of glyph string S.  */

static void
ewl_compute_glyph_string_overhangs (struct glyph_string *s)
{
  if (s->cmp == NULL
      && (s->first_glyph->type == CHAR_GLYPH
	  || s->first_glyph->type == COMPOSITE_GLYPH))
    {
      struct font_metrics metrics;

      if (s->first_glyph->type == CHAR_GLYPH)
	{
	  unsigned *code = alloca (sizeof (unsigned) * s->nchars);
	  struct font *font = s->font;
	  int i;

	  for (i = 0; i < s->nchars; i++)
	    code[i] = ((XCHAR2B_BYTE1 (s->char2b + i) << 8)
		       | XCHAR2B_BYTE2 (s->char2b + i));
	  font->driver->text_extents (font, code, s->nchars, &metrics);
	}
      else
	{
	  Lisp_Object gstring = composition_gstring_from_id (s->cmp_id);

	  composition_gstring_width (gstring, s->cmp_from, s->cmp_to, &metrics);
	}
      s->right_overhang = (metrics.rbearing > metrics.width
			   ? metrics.rbearing - metrics.width : 0);
      s->left_overhang = metrics.lbearing < 0 ? - metrics.lbearing : 0;
    }
  else if (s->cmp)
    {
      s->right_overhang = s->cmp->rbearing - s->cmp->pixel_width;
      s->left_overhang = - s->cmp->lbearing;
    }
}


/* Fill rectangle X, Y, W, H with background color of glyph string S.  */

static void
ewl_clear_glyph_string_rect (struct glyph_string *s, int x, int y, int w, int h, cairo_t *cr)
{
  struct ewl_color bg;

  ewl_query_color (s->gc->background, &bg);
  cairo_set_source_rgba (cr, bg.red, bg.green, bg.blue, bg.alpha);
  cairo_rectangle (cr, x, y, w, h);
  cairo_fill (cr);
}


/* Draw stretch glyph string S.  */

static void
ewl_draw_stretch_glyph_string (struct glyph_string *s, cairo_t *cr)
{
  eassert (s->first_glyph->type == STRETCH_GLYPH);

  if (s->hl == DRAW_CURSOR
      && !x_stretch_cursor_p)
    {
      /* If `x-stretch-cursor' is nil, don't draw a block cursor as
	 wide as the stretch glyph.  */
      int width, background_width = s->background_width;
      int x = s->x;

      if (!s->row->reversed_p)
	{
	  int left_x = window_box_left_offset (s->w, TEXT_AREA);

	  if (x < left_x)
	    {
	      background_width -= left_x - x;
	      x = left_x;
	    }
	}
      else
	{
	  /* In R2L rows, draw the cursor on the right edge of the
	     stretch glyph.  */
	  int right_x = window_box_right_offset (s->w, TEXT_AREA);

	  if (x + background_width > right_x)
	    background_width -= x - right_x;
	  x += background_width;
	}
      width = min (FRAME_COLUMN_WIDTH (s->f), background_width);
      if (s->row->reversed_p)
	x -= width;

      /* Draw cursor.  */
      ewl_clear_glyph_string_rect (s, x, s->y, width, s->height, cr);

      /* Clear rest using the GC of the original non-cursor face.  */
      if (width < background_width)
	{
	  int y = s->y;
	  int w = background_width - width, h = s->height;
	  NativeRectangle r;
	  GC gc;
	  struct ewl_color fg;

	  if (!s->row->reversed_p)
	    x += width;
	  else
	    x = s->x;
	  if (s->row->mouse_face_p
	      && cursor_in_mouse_face_p (s->w))
	    {
	      ewl_set_mouse_face_gc (s);
	      gc = s->gc;
	    }
	  else
	    gc = s->face->gc;

	  get_glyph_string_clip_rect (s, &r);
	  cairo_rectangle (cr, r.x, r.y, r.width, r.height);
	  cairo_clip (cr);

	  ewl_query_color (gc->foreground, &fg);
	  cairo_set_source_rgba (cr, fg.red, fg.green, fg.blue, fg.alpha);
	  cairo_rectangle (cr, x, y, w, h);
	  cairo_fill (cr);
	}
    }
  else if (!s->background_filled_p)
    {
      int background_width = s->background_width;
      int x = s->x, left_x = window_box_left_offset (s->w, TEXT_AREA);

      /* Don't draw into left margin, fringe or scrollbar area
         except for header line and mode line.  */
      if (x < left_x && !s->row->mode_line_p)
	{
	  background_width -= left_x - x;
	  x = left_x;
	}
      if (background_width > 0)
	ewl_clear_glyph_string_rect (s, x, s->y, background_width, s->height,
				     cr);
    }

  s->background_filled_p = 1;
}


static void
ewl_draw_glyph_string_background (struct glyph_string *s, bool force_p,
				  cairo_t *cr)
{
  /* Nothing to do if background has already been drawn or if it
     shouldn't be drawn in the first place.  */
  if (!s->background_filled_p)
    {
      int box_line_width = max (s->face->box_line_width, 0);

      if (s->stippled_p)
	{
	  /* Not implemented.  */
	}
      else if (FONT_HEIGHT (s->font) < s->height - 2 * box_line_width
	  || s->font_not_found_p
	  || s->extends_to_end_of_line_p
	  || force_p)
	{
	  ewl_clear_glyph_string_rect (s, s->x, s->y + box_line_width,
				       s->background_width,
				       s->height - 2 * box_line_width,
				       cr);
	  s->background_filled_p = 1;
	}
    }
}


static void
ewl_draw_glyph_string_foreground (struct glyph_string *s, cairo_t *cr)
{
  int i, x;
  struct ewl_color fg;

  ewl_query_color (s->gc->foreground, &fg);
  cairo_set_source_rgba (cr, fg.red, fg.green, fg.blue, fg.alpha);

  /* If first glyph of S has a left box line, start drawing the text
     of S to the right of that box line.  */
  if (s->face->box != FACE_NO_BOX
      && s->first_glyph->left_box_line_p)
    x = s->x + eabs (s->face->box_line_width);
  else
    x = s->x;

  /* Draw characters of S as rectangles if S's font could not be
     loaded.  */
  if (s->font_not_found_p)
    {
      for (i = 0; i < s->nchars; ++i)
	{
	  struct glyph *g = s->first_glyph + i;
	  cairo_rectangle (cr, x, s->y, g->pixel_width - 1,
			   s->height - 1);
	  cairo_stroke (cr);
	  x += g->pixel_width;
	}
    }
  else
    {
      struct font *font = s->font;
      int boff = font->baseline_offset;
      int y;

      if (font->vertical_centering)
	boff = VCENTER_BASELINE_OFFSET (font, s->f) - boff;

      y = s->ybase - boff;
      if (s->for_overlaps
	  || (s->background_filled_p && s->hl != DRAW_CURSOR))
	font->driver->draw (s, 0, s->nchars, x, y, 0);
      else
	font->driver->draw (s, 0, s->nchars, x, y, 1);
      if (s->face->overstrike)
	font->driver->draw (s, 0, s->nchars, x + 1, y, 0);
    }
}


/* Set S->gc to a suitable GC for drawing glyph string S in cursor
   face.  */

static void
ewl_set_cursor_gc (struct glyph_string *s)
{
  if (s->font == FRAME_FONT (s->f)
      && s->face->background == FRAME_BACKGROUND_PIXEL (s->f)
      && s->face->foreground == FRAME_FOREGROUND_PIXEL (s->f)
      && !s->cmp)
    s->gc = s->f->output_data.wl->cursor_gc;
  else
    {
      /* Cursor on non-default face: must merge.  */
      XGCValues xgcv;

      xgcv.background = s->f->output_data.wl->cursor_pixel;
      xgcv.foreground = s->face->background;

      /* If the glyph would be invisible, try a different foreground.  */
      if (xgcv.foreground == xgcv.background)
	xgcv.foreground = s->face->foreground;
      if (xgcv.foreground == xgcv.background)
	xgcv.foreground = s->f->output_data.wl->cursor_foreground_pixel;
      if (xgcv.foreground == xgcv.background)
	xgcv.foreground = s->face->foreground;

      /* Make sure the cursor is distinct from text in this face.  */
      if (xgcv.background == s->face->background
	  && xgcv.foreground == s->face->foreground)
	{
	  xgcv.background = s->face->foreground;
	  xgcv.foreground = s->face->background;
	}

      if (!FRAME_DISPLAY_INFO (s->f)->scratch_cursor_gc)
	{
	  FRAME_DISPLAY_INFO (s->f)->scratch_cursor_gc
	    = xmalloc (sizeof xgcv);
	}
      *FRAME_DISPLAY_INFO (s->f)->scratch_cursor_gc = xgcv;

      s->gc = FRAME_DISPLAY_INFO (s->f)->scratch_cursor_gc;
    }
}


/* Set up S->gc of glyph string S for drawing text in mouse face.  */

static void
ewl_set_mouse_face_gc (struct glyph_string *s)
{
  int face_id;
  struct face *face;

  /* What face has to be used last for the mouse face?  */
  face_id = MOUSE_HL_INFO (s->f)->mouse_face_face_id;
  face = FACE_FROM_ID (s->f, face_id);
  if (face == NULL)
    face = FACE_FROM_ID (s->f, MOUSE_FACE_ID);

  if (s->first_glyph->type == CHAR_GLYPH)
    face_id = FACE_FOR_CHAR (s->f, face, s->first_glyph->u.ch, -1, Qnil);
  else
    face_id = FACE_FOR_CHAR (s->f, face, 0, -1, Qnil);
  s->face = FACE_FROM_ID (s->f, face_id);
  prepare_face_for_display (s->f, s->face);

  if (s->font == s->face->font)
    s->gc = s->face->gc;
  else
    {
      /* Otherwise construct scratch_cursor_gc with values from FACE
	 except for FONT.  */
      XGCValues xgcv;

      xgcv.background = s->face->background;
      xgcv.foreground = s->face->foreground;

      if (!FRAME_DISPLAY_INFO (s->f)->scratch_cursor_gc)
	FRAME_DISPLAY_INFO (s->f)->scratch_cursor_gc
	  = xmalloc (sizeof xgcv);
      *FRAME_DISPLAY_INFO (s->f)->scratch_cursor_gc = xgcv;
      s->gc = FRAME_DISPLAY_INFO (s->f)->scratch_cursor_gc;
    }
  eassert (s->gc != 0);
}


/* Set S->gc of glyph string S to a GC suitable for drawing a mode line.
   Faces to use in the mode line have already been computed when the
   matrix was built, so there isn't much to do, here.  */

static void
ewl_set_mode_line_face_gc (struct glyph_string *s)
{
  s->gc = s->face->gc;
}

/* Set S->gc of glyph string S for drawing that glyph string.  Set
   S->stippled_p to a non-zero value if the face of S has a stipple
   pattern.  */

static void
ewl_set_glyph_string_gc (struct glyph_string *s)
{
  prepare_face_for_display (s->f, s->face);

  if (s->hl == DRAW_NORMAL_TEXT)
    {
      s->gc = s->face->gc;
      s->stippled_p = s->face->stipple != 0;
    }
  else if (s->hl == DRAW_INVERSE_VIDEO)
    {
      ewl_set_mode_line_face_gc (s);
      s->stippled_p = s->face->stipple != 0;
    }
  else if (s->hl == DRAW_CURSOR)
    {
      ewl_set_cursor_gc (s);
      s->stippled_p = 0;
    }
  else if (s->hl == DRAW_MOUSE_FACE)
    {
      ewl_set_mouse_face_gc (s);
      s->stippled_p = s->face->stipple != 0;
    }
  else if (s->hl == DRAW_IMAGE_RAISED
	   || s->hl == DRAW_IMAGE_SUNKEN)
    {
      s->gc = s->face->gc;
      s->stippled_p = s->face->stipple != 0;
    }
  else
    emacs_abort ();

  /* GC must have been set.  */
  eassert (s->gc != 0);
}


/* Draw the foreground of composite glyph string S.  */

static void
ewl_draw_composite_glyph_string_foreground (struct glyph_string *s, cairo_t *cr)
{
  int i, j, x;
  struct font *font = s->font;

  /* If first glyph of S has a left box line, start drawing the text
     of S to the right of that box line.  */
  if (s->face && s->face->box != FACE_NO_BOX
      && s->first_glyph->left_box_line_p)
    x = s->x + eabs (s->face->box_line_width);
  else
    x = s->x;

  /* S is a glyph string for a composition.  S->cmp_from is the index
     of the first character drawn for glyphs of this composition.
     S->cmp_from == 0 means we are drawing the very first character of
     this composition.  */

  /* Draw a rectangle for the composition if the font for the very
     first character of the composition could not be loaded.  */
  if (s->font_not_found_p)
    {
      if (s->cmp_from == 0)
	{
	  cairo_rectangle (cr, x, s->y, s->width - 1, s->height - 1);
	  cairo_stroke (cr);
	}
    }
  else if (! s->first_glyph->u.cmp.automatic)
    {
      int y = s->ybase;

      for (i = 0, j = s->cmp_from; i < s->nchars; i++, j++)
	/* TAB in a composition means display glyphs with padding
	   space on the left or right.  */
	if (COMPOSITION_GLYPH (s->cmp, j) != '\t')
	  {
	    int xx = x + s->cmp->offsets[j * 2];
	    int yy = y - s->cmp->offsets[j * 2 + 1];

	    font->driver->draw (s, j, j + 1, xx, yy, 0);
	    if (s->face->overstrike)
	      font->driver->draw (s, j, j + 1, xx + 1, yy, 0);
	  }
    }
  else
    {
      Lisp_Object gstring = composition_gstring_from_id (s->cmp_id);
      Lisp_Object glyph;
      int y = s->ybase;
      int width = 0;

      for (i = j = s->cmp_from; i < s->cmp_to; i++)
	{
	  glyph = LGSTRING_GLYPH (gstring, i);
	  if (NILP (LGLYPH_ADJUSTMENT (glyph)))
	    width += LGLYPH_WIDTH (glyph);
	  else
	    {
	      int xoff, yoff, wadjust;

	      if (j < i)
		{
		  font->driver->draw (s, j, i, x, y, 0);
		  if (s->face->overstrike)
		    font->driver->draw (s, j, i, x + 1, y, 0);
		  x += width;
		}
	      xoff = LGLYPH_XOFF (glyph);
	      yoff = LGLYPH_YOFF (glyph);
	      wadjust = LGLYPH_WADJUST (glyph);
	      font->driver->draw (s, i, i + 1, x + xoff, y + yoff, 0);
	      if (s->face->overstrike)
		font->driver->draw (s, i, i + 1, x + xoff + 1, y + yoff, 0);
	      x += wadjust;
	      j = i + 1;
	      width = 0;
	    }
	}
      if (j < i)
	{
	  font->driver->draw (s, j, i, x, y, 0);
	  if (s->face->overstrike)
	    font->driver->draw (s, j, i, x + 1, y, 0);
	}
    }
}


/* Draw the foreground of glyph string S for glyphless characters.  */

static void
ewl_draw_glyphless_glyph_string_foreground (struct glyph_string *s, cairo_t *cr)
{
  struct glyph *glyph = s->first_glyph;
  XChar2b char2b[8];
  int x, i, j;

  /* If first glyph of S has a left box line, start drawing the text
     of S to the right of that box line.  */
  if (s->face && s->face->box != FACE_NO_BOX
      && s->first_glyph->left_box_line_p)
    x = s->x + eabs (s->face->box_line_width);
  else
    x = s->x;

  s->char2b = char2b;

  for (i = 0; i < s->nchars; i++, glyph++)
    {
      char buf[7], *str = NULL;
      int len = glyph->u.glyphless.len;

      if (glyph->u.glyphless.method == GLYPHLESS_DISPLAY_ACRONYM)
	{
	  if (len > 0
	      && CHAR_TABLE_P (Vglyphless_char_display)
	      && (CHAR_TABLE_EXTRA_SLOTS (XCHAR_TABLE (Vglyphless_char_display))
		  >= 1))
	    {
	      Lisp_Object acronym
		= (! glyph->u.glyphless.for_no_font
		   ? CHAR_TABLE_REF (Vglyphless_char_display,
				     glyph->u.glyphless.ch)
		   : XCHAR_TABLE (Vglyphless_char_display)->extras[0]);
	      if (STRINGP (acronym))
		str = SSDATA (acronym);
	    }
	}
      else if (glyph->u.glyphless.method == GLYPHLESS_DISPLAY_HEX_CODE)
	{
	  sprintf (buf, "%0*X",
		   glyph->u.glyphless.ch < 0x10000 ? 4 : 6,
		   glyph->u.glyphless.ch);
	  str = buf;
	}

      if (str)
	{
	  int upper_len = (len + 1) / 2;
	  unsigned code;

	  /* It is assured that all LEN characters in STR is ASCII.  */
	  for (j = 0; j < len; j++)
	    {
	      code = s->font->driver->encode_char (s->font, str[j]);
	      STORE_XCHAR2B (char2b + j, code >> 8, code & 0xFF);
	    }
	  s->font->driver->draw (s, 0, upper_len,
				 x + glyph->slice.glyphless.upper_xoff,
				 s->ybase + glyph->slice.glyphless.upper_yoff,
				 0);
	  s->font->driver->draw (s, upper_len, len,
				 x + glyph->slice.glyphless.lower_xoff,
				 s->ybase + glyph->slice.glyphless.lower_yoff,
				 0);
	}
      if (glyph->u.glyphless.method != GLYPHLESS_DISPLAY_THIN_SPACE)
	{
	  cairo_rectangle (cr,
			   x, s->ybase - glyph->ascent,
			   glyph->pixel_width - 1,
			   glyph->ascent + glyph->descent - 1);
	  cairo_stroke (cr);
	}
      x += glyph->pixel_width;
   }
}


/* RIF: Draw glyph string S.  */

static void
ewl_draw_glyph_string (struct glyph_string *s)
{
  bool relief_drawn_p = 0;
  cairo_t *cr = cairo_create (s->f->output_data.wl->cairo_surface);

  /* If S draws into the background of its successors, draw the
     background of the successors first so that S can draw into it.
     This makes S->next use XDrawString instead of XDrawImageString.  */
  if (s->next && s->right_overhang && !s->for_overlaps)
    {
      int width;
      struct glyph_string *next;

      for (width = 0, next = s->next;
	   next && width < s->right_overhang;
	   width += next->width, next = next->next)
	if (next->first_glyph->type != IMAGE_GLYPH)
	  {
	    ewl_set_glyph_string_gc (next);
	    ewl_set_glyph_string_clipping (next, cr);
	    if (next->first_glyph->type == STRETCH_GLYPH)
	      ewl_draw_stretch_glyph_string (next, cr);
	    else
	      ewl_draw_glyph_string_background (next, 1, cr);
	    next->num_clips = 0;
	  }
    }

  /* Set up S->gc, set clipping and draw S.  */
  ewl_set_glyph_string_gc (s);

  /* Draw relief (if any) in advance for char/composition so that the
     glyph string can be drawn over it.  */
  if (!s->for_overlaps
      && s->face->box != FACE_NO_BOX
      && (s->first_glyph->type == CHAR_GLYPH
	  || s->first_glyph->type == COMPOSITE_GLYPH))

    {
      ewl_set_glyph_string_clipping (s, cr);
      ewl_draw_glyph_string_background (s, 1, cr);
      ewl_draw_glyph_string_box (s, cr);
      ewl_set_glyph_string_clipping (s, cr);
      relief_drawn_p = 1;
    }
  else if (!s->clip_head /* draw_glyphs didn't specify a clip mask. */
	   && !s->clip_tail
	   && ((s->prev && s->prev->hl != s->hl && s->left_overhang)
	       || (s->next && s->next->hl != s->hl && s->right_overhang)))
    /* We must clip just this glyph.  left_overhang part has already
       drawn when s->prev was drawn, and right_overhang part will be
       drawn later when s->next is drawn. */
    ewl_set_glyph_string_clipping_exactly (s, s, cr);
  else
    ewl_set_glyph_string_clipping (s, cr);

  switch (s->first_glyph->type)
    {
    case IMAGE_GLYPH:
      /* FIXME: Not implemented.  */
      break;

    case STRETCH_GLYPH:
      ewl_draw_stretch_glyph_string (s, cr);
      break;

    case CHAR_GLYPH:
      if (s->for_overlaps)
	s->background_filled_p = 1;
      else
	ewl_draw_glyph_string_background (s, 0, cr);
      ewl_draw_glyph_string_foreground (s, cr);
      break;

    case COMPOSITE_GLYPH:
      if (s->for_overlaps || (s->cmp_from > 0
			      && ! s->first_glyph->u.cmp.automatic))
	s->background_filled_p = 1;
      else
	ewl_draw_glyph_string_background (s, 1, cr);
      ewl_draw_composite_glyph_string_foreground (s, cr);
      break;

    case GLYPHLESS_GLYPH:
      if (s->for_overlaps)
	s->background_filled_p = 1;
      else
	ewl_draw_glyph_string_background (s, 1, cr);
      ewl_draw_glyphless_glyph_string_foreground (s, cr);
      break;

    default:
      emacs_abort ();
    }
  if (!s->for_overlaps)
    {
      /* Draw underline.  */
      if (s->face->underline_p)
	/* FIXME: Not implemented.  */

      /* Draw overline.  */
      if (s->face->overline_p)
	/* FIXME: Not implemented.  */

      /* Draw strike-through.  */
      if (s->face->strike_through_p)
	/* FIXME: Not implemented.  */

      /* Draw relief if not yet drawn.  */
      if (!relief_drawn_p && s->face->box != FACE_NO_BOX)
	ewl_draw_glyph_string_box (s, cr);

      if (s->prev)
	{
	  struct glyph_string *prev;

	  for (prev = s->prev; prev; prev = prev->prev)
	    if (prev->hl != s->hl
		&& prev->x + prev->width + prev->right_overhang > s->x)
	      {
		/* As prev was drawn while clipped to its own area, we
		   must draw the right_overhang part using s->hl now.  */
		enum draw_glyphs_face save = prev->hl;

		prev->hl = s->hl;
		ewl_set_glyph_string_gc (prev);
		ewl_set_glyph_string_clipping_exactly (s, prev, cr);
		if (prev->first_glyph->type == CHAR_GLYPH)
		  ewl_draw_glyph_string_foreground (prev, cr);
		else
		  ewl_draw_composite_glyph_string_foreground (prev, cr);
		cairo_reset_clip (cr);
		prev->hl = save;
		prev->num_clips = 0;
	      }
	}

      if (s->next)
	{
	  struct glyph_string *next;

	  for (next = s->next; next; next = next->next)
	    if (next->hl != s->hl
		&& next->x - next->left_overhang < s->x + s->width)
	      {
		/* As next will be drawn while clipped to its own area,
		   we must draw the left_overhang part using s->hl now.  */
		enum draw_glyphs_face save = next->hl;

		next->hl = s->hl;
		ewl_set_glyph_string_gc (next);
		ewl_set_glyph_string_clipping_exactly (s, next, cr);
		if (next->first_glyph->type == CHAR_GLYPH)
		  ewl_draw_glyph_string_foreground (next, cr);
		else
		  ewl_draw_composite_glyph_string_foreground (next, cr);
		next->hl = save;
		next->num_clips = 0;
		next->clip_head = s->next;
		cairo_reset_clip (cr);
	      }
	}
    }

  /* Reset clipping.  */
  s->num_clips = 0;
  cairo_destroy (cr);
}


/* RIF: Shift display to make room for inserted glyphs.   */

static void
ewl_shift_glyphs_for_insert (struct frame *f, int x, int y, int width, int height, int shift_by)
{
  cairo_t *cr;

  cr = cairo_create (f->output_data.wl->cairo_surface);
  cairo_set_source_surface (cr, f->output_data.wl->cairo_surface,
			    shift_by, 0);
  cairo_rectangle (cr, x + shift_by, y, width, height);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_fill (cr);
  cairo_destroy (cr);
}


/* Get rid of display DPYINFO, deleting all frames on it,
   and without sending any more commands to the X server.  */

void
x_delete_display (struct ewl_display_info *dpyinfo)
{
  struct terminal *t;

  /* Close all frames and delete the generic struct terminal for this
     X display.  */
  for (t = terminal_list; t; t = t->next_terminal)
    if (t->type == output_x_window && t->display_info.wl == dpyinfo)
      {
        delete_terminal (t);
        break;
      }

  /* WL: currently we don't support multiple displays.  */
  if (x_display_list == dpyinfo)
    x_display_list = dpyinfo->next;

  if (dpyinfo->registry)
    wl_registry_destroy (dpyinfo->registry);
  if (dpyinfo->compositor)
    wl_compositor_destroy (dpyinfo->compositor);
  if (dpyinfo->shell)
    wl_shell_destroy (dpyinfo->shell);
  if (dpyinfo->shm)
    wl_shm_destroy (dpyinfo->shm);
  if (dpyinfo->seat)
    wl_seat_destroy (dpyinfo->seat);
  if (dpyinfo->keyboard)
    wl_keyboard_destroy (dpyinfo->keyboard);
  if (dpyinfo->display)
    wl_display_disconnect (dpyinfo->display);

  xfree (dpyinfo);
}


extern frame_parm_handler ewl_frame_parm_handlers[];
static struct redisplay_interface ewl_redisplay_interface =
{
  ewl_frame_parm_handlers,
  x_produce_glyphs,
  x_write_glyphs,
  x_insert_glyphs,
  x_clear_end_of_line,
  ewl_scroll_run,
  ewl_after_update_window_line,
  ewl_update_window_begin,
  ewl_update_window_end,
  0, /* flush_display */
  x_clear_window_mouse_face,
  x_get_glyph_overhangs,
  x_fix_overlapping_area,
  ewl_draw_fringe_bitmap,
  0, /* define_fringe_bitmap */ /* FIXME: simplify ewl_draw_fringe_bitmap */
  0, /* destroy_fringe_bitmap */
  ewl_compute_glyph_string_overhangs,
  ewl_draw_glyph_string,
  0, /* ewl_define_frame_cursor */
  ewl_clear_frame_area,
  ewl_draw_window_cursor,
  ewl_draw_vertical_window_border,
  ewl_draw_window_divider,
  ewl_shift_glyphs_for_insert
};


/* Clear an entire frame.  */

static void
ewl_clear_frame (struct frame *f)
{
  cairo_t *cr;
  struct ewl_color bg;

  cr = cairo_create (f->output_data.wl->cairo_surface);
  ewl_query_color (FRAME_BACKGROUND_PIXEL (f), &bg);
  cairo_set_source_rgba (cr, bg.red, bg.green, bg.blue, bg.alpha);
  cairo_paint (cr);
  cairo_destroy (cr);
  ewl_redraw_frame (f, NULL, 0);
}


static void
x_frame_rehighlight (struct ewl_display_info *dpyinfo)
{
  struct frame *old_highlight = dpyinfo->x_highlight_frame;

  if (dpyinfo->x_focus_frame)
    {
      dpyinfo->x_highlight_frame
	= ((FRAMEP (FRAME_FOCUS_FRAME (dpyinfo->x_focus_frame)))
	   ? XFRAME (FRAME_FOCUS_FRAME (dpyinfo->x_focus_frame))
	   : dpyinfo->x_focus_frame);
      if (! FRAME_LIVE_P (dpyinfo->x_highlight_frame))
	{
	  fset_focus_frame (dpyinfo->x_focus_frame, Qnil);
	  dpyinfo->x_highlight_frame = dpyinfo->x_focus_frame;
	}
    }
  else
    dpyinfo->x_highlight_frame = 0;

  if (dpyinfo->x_highlight_frame != old_highlight)
    {
      if (old_highlight)
	x_update_cursor (old_highlight, 1);
      if (dpyinfo->x_highlight_frame)
	x_update_cursor (dpyinfo->x_highlight_frame, 1);
    }
}

/* The focus has changed, or we have redirected a frame's focus to
   another frame (this happens when a frame uses a surrogate
   mini-buffer frame).  Shift the highlight as appropriate.

   The FRAME argument doesn't necessarily have anything to do with which
   frame is being highlighted or un-highlighted; we only use it to find
   the appropriate X display info.  */

static void
ewl_frame_rehighlight (struct frame *frame)
{
  x_frame_rehighlight (FRAME_DISPLAY_INFO (frame));
}

/* Destroy the X window of frame F.  */

static void
ewl_destroy_window (struct frame *f)
{
  struct ewl_display_info *dpyinfo = FRAME_DISPLAY_INFO (f);

  /* If a display connection is dead, don't try sending more
     commands to the X server.  */
  if (dpyinfo->display != 0)
    x_free_frame_resources (f);
}

static struct terminal *
ewl_create_terminal (struct ewl_display_info *dpyinfo)
{
  struct terminal *terminal;

  terminal = create_terminal (output_wl, &ewl_redisplay_interface);

  terminal->display_info.wl = dpyinfo;
  dpyinfo->terminal = terminal;

#if 0
  terminal->read_socket_hook = ewl_read_socket;
  terminal->clear_frame_hook = ewl_clear_frame;
  terminal->ring_bell_hook = ewl_ring_bell;
  terminal->update_begin_hook = ewl_update_begin;
  terminal->update_end_hook = ewl_update_end;
  terminal->frame_up_to_date_hook = ewl_frame_up_to_date;
  terminal->mouse_position_hook = ewl_mouse_position;
#endif
  terminal->frame_rehighlight_hook = ewl_frame_rehighlight;
#if 0
  terminal->frame_raise_lower_hook = ewl_frame_raise_lower;
  terminal->fullscreen_hook = ewl_fullscreen_hook;
  terminal->menu_show_hook = ewl_menu_show;
  terminal->popup_dialog_hook = ewl_popup_dialog;
  terminal->set_vertical_scroll_bar_hook = ewl_set_vertical_scroll_bar;
  terminal->condemn_scroll_bars_hook = ewl_condemn_scroll_bars;
  terminal->redeem_scroll_bar_hook = ewl_redeem_scroll_bar;
  terminal->judge_scroll_bars_hook = ewl_judge_scroll_bars;
  terminal->delete_terminal_hook = ewl_delete_terminal;
#endif
  terminal->delete_frame_hook = ewl_destroy_window;
  /* Other hooks are NULL by default.  */

  return terminal;
}


/* WL: Protocol stuff.  */

static void
ewl_shm_format (void *data, struct wl_shm *wl_shm, uint32_t format)
{
  struct ewl_display_info *dpyinfo = data;

  dpyinfo->formats |= (1 << format);
}


struct wl_shm_listener ewl_shm_listener =
  {
    ewl_shm_format
  };


static void
ewl_keyboard_handle_keymap (void *data, struct wl_keyboard *wl_keyboard,
			      uint32_t format, int32_t fd, uint32_t size)
{
  struct ewl_display_info *dpyinfo = data;
  char *p;

  if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1)
    {
      close (fd);
      return;
    }

  p = mmap (NULL, size, PROT_READ, MAP_SHARED, fd, 0);
  if (p == MAP_FAILED)
    {
      close (fd);
      return;
    }

  if (dpyinfo->xkb_keymap)
    xkb_keymap_unref (dpyinfo->xkb_keymap);
  dpyinfo->xkb_keymap
    = xkb_map_new_from_string (dpyinfo->xkb_context,
			       p,
			       XKB_KEYMAP_FORMAT_TEXT_V1,
			       0);
  munmap (p, size);
  close (fd);
  eassert (dpyinfo->xkb_keymap);

  if (dpyinfo->xkb_state)
    xkb_state_unref (dpyinfo->xkb_state);
  dpyinfo->xkb_state = xkb_state_new (dpyinfo->xkb_keymap);
  eassert (dpyinfo->xkb_state);

  dpyinfo->control_mod_mask =
    1 << xkb_map_mod_get_index (dpyinfo->xkb_keymap, "Control");
  dpyinfo->alt_mod_mask =
    1 << xkb_map_mod_get_index (dpyinfo->xkb_keymap, "Mod1");
  dpyinfo->shift_mod_mask =
    1 << xkb_map_mod_get_index (dpyinfo->xkb_keymap, "Shift");
  dpyinfo->shift_lock_mask =
    1 << xkb_map_mod_get_index (dpyinfo->xkb_keymap, "Lock");
  dpyinfo->super_mod_mask =
    1 << xkb_map_mod_get_index (dpyinfo->xkb_keymap, "Super");
  dpyinfo->hyper_mod_mask =
    1 << xkb_map_mod_get_index (dpyinfo->xkb_keymap, "Hyper");
}


/* Convert between the modifier bits X uses and the modifier bits
   Emacs uses.  */

int
ewl_wl_to_emacs_modifiers (struct ewl_display_info *dpyinfo, int state)
{
  int mod_meta = meta_modifier;
  int mod_alt  = alt_modifier;
  int mod_hyper = hyper_modifier;
  int mod_super = super_modifier;
  Lisp_Object tem;

  tem = Fget (Vx_alt_keysym, Qmodifier_value);
  if (INTEGERP (tem)) mod_alt = XINT (tem) & INT_MAX;
  tem = Fget (Vx_meta_keysym, Qmodifier_value);
  if (INTEGERP (tem)) mod_meta = XINT (tem) & INT_MAX;
  tem = Fget (Vx_hyper_keysym, Qmodifier_value);
  if (INTEGERP (tem)) mod_hyper = XINT (tem) & INT_MAX;
  tem = Fget (Vx_super_keysym, Qmodifier_value);
  if (INTEGERP (tem)) mod_super = XINT (tem) & INT_MAX;

  return (  ((state & (dpyinfo->shift_mod_mask | dpyinfo->shift_lock_mask)) ? shift_modifier : 0)
            | ((state & dpyinfo->control_mod_mask)	? ctrl_modifier	: 0)
            | ((state & dpyinfo->meta_mod_mask)		? mod_meta	: 0)
            | ((state & dpyinfo->alt_mod_mask)		? mod_alt	: 0)
            | ((state & dpyinfo->super_mod_mask)	? mod_super	: 0)
            | ((state & dpyinfo->hyper_mod_mask)	? mod_hyper	: 0));
}


static int
ewl_emacs_to_wl_modifiers (struct ewl_display_info *dpyinfo, EMACS_INT state)
{
  EMACS_INT mod_meta = meta_modifier;
  EMACS_INT mod_alt  = alt_modifier;
  EMACS_INT mod_hyper = hyper_modifier;
  EMACS_INT mod_super = super_modifier;

  Lisp_Object tem;

  tem = Fget (Vx_alt_keysym, Qmodifier_value);
  if (INTEGERP (tem)) mod_alt = XINT (tem);
  tem = Fget (Vx_meta_keysym, Qmodifier_value);
  if (INTEGERP (tem)) mod_meta = XINT (tem);
  tem = Fget (Vx_hyper_keysym, Qmodifier_value);
  if (INTEGERP (tem)) mod_hyper = XINT (tem);
  tem = Fget (Vx_super_keysym, Qmodifier_value);
  if (INTEGERP (tem)) mod_super = XINT (tem);


  return (  ((state & mod_alt)		? dpyinfo->alt_mod_mask   : 0)
            | ((state & mod_super)	? dpyinfo->super_mod_mask : 0)
            | ((state & mod_hyper)	? dpyinfo->hyper_mod_mask : 0)
            | ((state & shift_modifier)	? dpyinfo->shift_mod_mask : 0)
            | ((state & ctrl_modifier)	? dpyinfo->control_mod_mask : 0)
            | ((state & mod_meta)	? dpyinfo->meta_mod_mask  : 0));
}


static void
ewl_keyboard_handle_key (void *data, struct wl_keyboard *wl_keyboard,
			 uint32_t serial, uint32_t time, uint32_t key,
			 uint32_t state)
{
  struct ewl_display_info *dpyinfo = data;
  struct input_event ie;
  uint32_t code = key + 8;
  USE_SAFE_ALLOCA;

  xkb_state_update_key (dpyinfo->xkb_state,
			code,
			state == WL_KEYBOARD_KEY_STATE_RELEASED
			? XKB_KEY_UP : XKB_KEY_DOWN);

  if (state == WL_KEYBOARD_KEY_STATE_RELEASED)
    {
      SAFE_FREE ();
      return;
    }

#if 0
  /* Dispatch KeyPress events when in menu.  */
  if (popup_activated ())
    goto OTHER;
#endif

  ie.kind = NO_EVENT;

  if (dpyinfo->x_focus_frame != 0)
    {
      uint32_t num_keysyms;
      const xkb_keysym_t *keysyms;
      xkb_keysym_t keysym, orig_keysym;
      uint32_t modifiers;

#if 0
      /* al%imercury@uunet.uu.net says that making this 81
	 instead of 80 fixed a bug whereby meta chars made
	 his Emacs hang.

	 It seems that some version of XmbLookupString has
	 a bug of not returning XBufferOverflow in
	 status_return even if the input is too long to
	 fit in 81 bytes.  So, we must prepare sufficient
	 bytes for copy_buffer.  513 bytes (256 chars for
	 two-byte character set) seems to be a fairly good
	 approximation.  -- 2000.8.10 handa@etl.go.jp  */
      unsigned char copy_buffer[513];
      unsigned char *copy_bufptr = copy_buffer;
      int copy_bufsiz = sizeof (copy_buffer);
      Lisp_Object coding_system = Qlatin_1;
      Lisp_Object c;
      ptrdiff_t nbytes = 0;
      uint32_t orig_modifiers;
#endif

      num_keysyms = xkb_key_get_syms (dpyinfo->xkb_state, code, &keysyms);

      keysym = XKB_KEY_NoSymbol;
      if (num_keysyms == 1)
	keysym = keysyms[0];

      modifiers = xkb_state_serialize_mods (dpyinfo->xkb_state,
					    XKB_STATE_DEPRESSED
					    | XKB_STATE_LATCHED);
      modifiers
	|= ewl_emacs_to_wl_modifiers (dpyinfo, extra_keyboard_modifiers);
#if 0
      orig_modifiers = modifiers;

      /* This will have to go some day...  */

      /* make_lispy_event turns chars into control chars.
	 Don't do it here because XLookupString is too eager.  */
      modifiers &= ~(dpyinfo->control_mod_mask
		     | dpyinfo->meta_mod_mask
		     | dpyinfo->super_mod_mask
		     | dpyinfo->hyper_mod_mask
		     | dpyinfo->alt_mod_mask);

      /* In case Meta is ComposeCharacter,
	 clear its status.  According to Markus Ehrnsperger
	 Markus.Ehrnsperger@lehrstuhl-bross.physik.uni-muenchen.de
	 this enables ComposeCharacter to work whether or
	 not it is combined with Meta.  */
      if (modifiers & dpyinfo->meta_mod_mask)
	memset (&compose_status, 0, sizeof (compose_status));

      nbytes = XLookupString (&xkey, (char *) copy_bufptr,
			      copy_bufsiz, &keysym,
			      &compose_status);

      /* If not using XIM/XIC, and a compose sequence is in progress,
	 we break here.  Otherwise, chars_matched is always 0.  */
      if (compose_status.chars_matched > 0 && nbytes == 0)
	break;

      memset (&compose_status, 0, sizeof (compose_status));
      modifiers = orig_modifiers;
#endif
      orig_keysym = keysym;

      /* Common for all keysym input events.  */
      XSETFRAME (ie.frame_or_window, dpyinfo->x_focus_frame);
      ie.modifiers = ewl_wl_to_emacs_modifiers (dpyinfo, modifiers);
      ie.timestamp = time;

      /* First deal with keysyms which have defined
	 translations to characters.  */
      if (keysym >= 32 && keysym < 128)
	/* Avoid explicitly decoding each ASCII character.  */
	{
	  ie.kind = ASCII_KEYSTROKE_EVENT;
	  ie.code = keysym;
	  goto done;
	}

      /* Keysyms directly mapped to Unicode characters.  */
      if (keysym >= 0x01000000 && keysym <= 0x0110FFFF)
	{
	  if (keysym < 0x01000080)
	    ie.kind = ASCII_KEYSTROKE_EVENT;
	  else
	    ie.kind = MULTIBYTE_CHAR_KEYSTROKE_EVENT;
	  ie.code = keysym & 0xFFFFFF;
	  goto done;
	}

      /* Random non-modifier sorts of keysyms.  */
      if (((keysym >= XKB_KEY_BackSpace && keysym <= XKB_KEY_Escape)
	   || keysym == XKB_KEY_Delete
	   || (keysym >= XKB_KEY_ISO_Left_Tab
	       && keysym <= XKB_KEY_ISO_Enter)
	   || (0xff50 <= keysym && keysym < 0xff60)
#if 0
	   || 0xff60 <= keysym && keysym < VARIES
#endif
	   || orig_keysym == XKB_KEY_dead_circumflex
	   || orig_keysym == XKB_KEY_dead_grave
	   || orig_keysym == XKB_KEY_dead_tilde
	   || orig_keysym == XKB_KEY_dead_diaeresis
	   || orig_keysym == XKB_KEY_dead_macron
	   || orig_keysym == XKB_KEY_dead_acute
	   || orig_keysym == XKB_KEY_dead_cedilla
	   || orig_keysym == XKB_KEY_dead_breve
	   || orig_keysym == XKB_KEY_dead_ogonek
	   || orig_keysym == XKB_KEY_dead_caron
	   || orig_keysym == XKB_KEY_dead_doubleacute
	   || orig_keysym == XKB_KEY_dead_abovedot
	   || (0xff80 <= keysym && keysym < 0xffbe)
	   || (0xffbe <= keysym && keysym < 0xffe1)
	   /* Any "vendor-specific" key is ok.  */
	   || (orig_keysym & (1 << 28)))
#if 0
	   || (keysym != XKB_KEY_NoSymbol && nbytes == 0))
	  && ! (IsModifierKey (orig_keysym)
		/* The symbols from XKB_KEY_ISO_Lock
		   to XKB_KEY_ISO_Last_Group_Lock
		   don't have real modifiers but
		   should be treated similarly to
		   Mode_switch by Emacs. */
		|| (XKB_KEY_ISO_Lock <= orig_keysym
		    && orig_keysym <= XKB_KEY_ISO_Last_Group_Lock)
		)
#endif
	  )
	{
	  /* make_lispy_event will convert this to a symbolic
	     key.  */
	  ie.kind = NON_ASCII_KEYSTROKE_EVENT;
	  ie.code = keysym;
	  goto done;
	}

#if 0
      {	/* Raw bytes, not keysym.  */
	ptrdiff_t i;
	int nchars, len;

	for (i = 0, nchars = 0; i < nbytes; i++)
	  {
	    if (ASCII_CHAR_P (copy_bufptr[i]))
	      nchars++;
	    STORE_KEYSYM_FOR_DEBUG (copy_bufptr[i]);
	  }

	if (nchars < nbytes)
	  {
	    /* Decode the input data.  */

	    /* The input should be decoded with `coding_system'
	       which depends on which X*LookupString function
	       we used just above and the locale.  */
	    setup_coding_system (coding_system, &coding);
	    coding.src_multibyte = 0;
	    coding.dst_multibyte = 1;
	    /* The input is converted to events, thus we can't
	       handle composition.  Anyway, there's no XIM that
	       gives us composition information.  */
	    coding.common_flags &= ~CODING_ANNOTATION_MASK;

	    SAFE_NALLOCA (coding.destination, MAX_MULTIBYTE_LENGTH,
			  nbytes);
	    coding.dst_bytes = MAX_MULTIBYTE_LENGTH * nbytes;
	    coding.mode |= CODING_MODE_LAST_BLOCK;
	    decode_coding_c_string (&coding, copy_bufptr, nbytes, Qnil);
	    nbytes = coding.produced;
	    nchars = coding.produced_char;
	    copy_bufptr = coding.destination;
	  }

	/* Convert the input data to a sequence of
	   character events.  */
	for (i = 0; i < nbytes; i += len)
	  {
	    int ch;
	    if (nchars == nbytes)
	      ch = copy_bufptr[i], len = 1;
	    else
	      ch = STRING_CHAR_AND_LENGTH (copy_bufptr + i, len);
	    ie.kind = (SINGLE_BYTE_CHAR_P (ch)
		       ? ASCII_KEYSTROKE_EVENT
		       : MULTIBYTE_CHAR_KEYSTROKE_EVENT);
	    ie.code = ch;
	    kbd_buffer_store_event_hold (&inev.ie, hold_quit);
	  }

	ie.kind = NO_EVENT;  /* Already stored above.  */

	if (keysym == XKB_KEY_NoSymbol)
	  break;
      }
#endif
  }

 done:
  if (ie.kind != NO_EVENT)
    kbd_buffer_store_event (&ie);

  SAFE_FREE ();
}


static void
ewl_keyboard_handle_modifiers (void *data, struct wl_keyboard *wl_keyboard,
				 uint32_t serial, uint32_t mods_depressed,
				 uint32_t mods_latched, uint32_t mods_locked,
				 uint32_t group)
{
  struct ewl_display_info *dpyinfo = data;

  xkb_state_update_mask (dpyinfo->xkb_state, mods_depressed, mods_latched,
                         mods_locked, 0, 0, group);
}


static void
ewl_keyboard_handle_enter (void *data,
			   struct wl_keyboard *wl_keyboard,
			   uint32_t serial,
			   struct wl_surface *surface,
			   struct wl_array *keys)
{
  struct ewl_display_info *dpyinfo = data;
  struct input_event ie;

  /* FIXME: Is it really okay to consider keyboard focus as a focus change?  */

  ie.arg = Qt;
  ie.kind = FOCUS_IN_EVENT;
  XSETFRAME (ie.frame_or_window, dpyinfo->x_focus_frame);
  kbd_buffer_store_event (&ie);
}


static void
ewl_keyboard_handle_leave (void *data,
			   struct wl_keyboard *wl_keyboard,
			   uint32_t serial,
			   struct wl_surface *surface)
{
  struct ewl_display_info *dpyinfo = data;
  struct input_event ie;

  /* FIXME: Is it really okay to consider keyboard focus as a focus change?  */

  ie.arg = Qt;
  ie.kind = FOCUS_OUT_EVENT;
  XSETFRAME (ie.frame_or_window, dpyinfo->x_focus_frame);
  kbd_buffer_store_event (&ie);
}


static const struct wl_keyboard_listener
ewl_keyboard_listener =
  {
    ewl_keyboard_handle_keymap,
    ewl_keyboard_handle_enter,
    ewl_keyboard_handle_leave,
    ewl_keyboard_handle_key,
    ewl_keyboard_handle_modifiers
  };


static void
ewl_seat_handle_capabilities (void *data, struct wl_seat *seat,
			      enum wl_seat_capability caps)
{
  struct ewl_display_info *dpyinfo = data;

  if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !dpyinfo->keyboard)
    {
      dpyinfo->keyboard = wl_seat_get_keyboard (seat);
      dpyinfo->xkb_context = xkb_context_new (0);
      wl_keyboard_add_listener (dpyinfo->keyboard, &ewl_keyboard_listener,
				dpyinfo);
    }
  else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && dpyinfo->keyboard)
    {
      wl_keyboard_destroy (dpyinfo->keyboard);
      dpyinfo->keyboard = NULL;
      xkb_context_unref (dpyinfo->xkb_context);
    }
}


static const struct wl_seat_listener ewl_seat_listener =
  {
    ewl_seat_handle_capabilities,
  };


static void
ewl_registry_handle_global (void *data, struct wl_registry *registry,
			    uint32_t id, const char *interface,
			    uint32_t version)
{
  struct ewl_display_info *dpyinfo = data;

  if (strcmp (interface, "wl_compositor") == 0)
    {
      dpyinfo->compositor
	= wl_registry_bind (registry, id, &wl_compositor_interface, 1);
    }
  else if (strcmp (interface, "wl_shell") == 0)
    {
      dpyinfo->shell
	= wl_registry_bind (registry, id, &wl_shell_interface, 1);
    }
  else if (strcmp (interface, "wl_shm") == 0)
    {
      dpyinfo->shm = wl_registry_bind (registry, id, &wl_shm_interface, 1);
      wl_shm_add_listener (dpyinfo->shm, &ewl_shm_listener, dpyinfo);
    }
  else if (strcmp (interface, "wl_seat") == 0)
    {
      dpyinfo->seat = wl_registry_bind (registry, id, &wl_seat_interface, 1);
      wl_seat_add_listener (dpyinfo->seat, &ewl_seat_listener, dpyinfo);
    }
}


static void
ewl_registry_handle_global_remove (void *data, struct wl_registry *registry,
				   uint32_t name)
{
}


static const struct wl_registry_listener ewl_registry_listener =
  {
    ewl_registry_handle_global,
    ewl_registry_handle_global_remove
  };


/* Open a connection to Wayland display DISPLAY_NAME, and return
   the structure that describes the open display.
   If we cannot contact the display, return null.  */

struct ewl_display_info *
ewl_term_init (Lisp_Object display_name)
{
  struct terminal *terminal;
  struct ewl_display_info *dpyinfo;
  static int ewl_initialized = 0;

  if (ewl_initialized)
    return x_display_list;
  ewl_initialized = 1;

  block_input ();

  dpyinfo = xzalloc (sizeof *dpyinfo);
  terminal = ewl_create_terminal (dpyinfo);
  dpyinfo->display = default_display
    = wl_display_connect (SSDATA (display_name));

  dpyinfo->registry = wl_display_get_registry (dpyinfo->display);
  wl_registry_add_listener (dpyinfo->registry, &ewl_registry_listener,
			    dpyinfo);
  wl_display_roundtrip (dpyinfo->display);
  wl_display_dispatch (dpyinfo->display);
  eassert (dpyinfo->compositor && dpyinfo->shell && dpyinfo->shm
	   && dpyinfo->seat);

  terminal->kboard = allocate_kboard (Qwl);
  /* Don't let the initial kboard remain current longer than necessary.
     That would cause problems if a file loaded on startup tries to
     prompt in the mini-buffer.  */
  if (current_kboard == initial_kboard)
    current_kboard = terminal->kboard;
  terminal->kboard->reference_count++;

  /* Put this display on the chain.  */
  dpyinfo->next = x_display_list;
  x_display_list = dpyinfo;

  dpyinfo->name_list_element = Fcons (display_name, Qnil);
  dpyinfo->root_window = 42;	/* a placeholder.. */
  dpyinfo->x_highlight_frame = dpyinfo->x_focus_frame = NULL;
  dpyinfo->n_fonts = 0;
  dpyinfo->smallest_font_height = 1;
  dpyinfo->smallest_char_width = 1;
  dpyinfo->resx = dpyinfo->resy = 96; /* FIXME */
  dpyinfo->n_planes = 16;	      /* FIXME */

  reset_mouse_highlight (&dpyinfo->mouse_highlight);

  add_keyboard_wait_descriptor (wl_display_get_fd (dpyinfo->display));

  /* Set the name of the terminal. */
  terminal->name = xstrdup (SSDATA (display_name));

  unblock_input ();

  return dpyinfo;
}


void
syms_of_wlterm (void)
{
  DEFSYM (Qmodifier_value, "modifier-value");
  DEFSYM (Qalt, "alt");
  Fput (Qalt, Qmodifier_value, make_number (alt_modifier));
  DEFSYM (Qhyper, "hyper");
  Fput (Qhyper, Qmodifier_value, make_number (hyper_modifier));
  DEFSYM (Qmeta, "meta");
  Fput (Qmeta, Qmodifier_value, make_number (meta_modifier));
  DEFSYM (Qsuper, "super");
  Fput (Qsuper, Qmodifier_value, make_number (super_modifier));

  DEFVAR_LISP ("x-alt-keysym", Vx_alt_keysym,
    doc: /* Which keys Emacs uses for the alt modifier.
This should be one of the symbols `alt', `hyper', `meta', `super'.
For example, `alt' means use the Alt_L and Alt_R keysyms.  The default
is nil, which is the same as `alt'.  */);
  Vx_alt_keysym = Qnil;

  DEFVAR_LISP ("x-hyper-keysym", Vx_hyper_keysym,
    doc: /* Which keys Emacs uses for the hyper modifier.
This should be one of the symbols `alt', `hyper', `meta', `super'.
For example, `hyper' means use the Hyper_L and Hyper_R keysyms.  The
default is nil, which is the same as `hyper'.  */);
  Vx_hyper_keysym = Qnil;

  DEFVAR_LISP ("x-meta-keysym", Vx_meta_keysym,
    doc: /* Which keys Emacs uses for the meta modifier.
This should be one of the symbols `alt', `hyper', `meta', `super'.
For example, `meta' means use the Meta_L and Meta_R keysyms.  The
default is nil, which is the same as `meta'.  */);
  Vx_meta_keysym = Qnil;

  DEFVAR_LISP ("x-super-keysym", Vx_super_keysym,
    doc: /* Which keys Emacs uses for the super modifier.
This should be one of the symbols `alt', `hyper', `meta', `super'.
For example, `super' means use the Super_L and Super_R keysyms.  The
default is nil, which is the same as `super'.  */);
  Vx_super_keysym = Qnil;

  /* Tell Emacs about this window system.  */
  Fprovide (Qwl, Qnil);
}
