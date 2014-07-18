/* Functions for Wayland.

Copyright (C) 1989, 1992-2014 Free Software Foundation, Inc.

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
#include <errno.h>
#include <c-ctype.h>
#include <sys/mman.h>

#include "lisp.h"
#include "blockinput.h"
#include "wlterm.h"
#include "window.h"
#include "character.h"
#include "buffer.h"
#include "keyboard.h"
#include "termhooks.h"
#include "fontset.h"
#include "font.h"
#include "wlcolor.h"

static struct ewl_display_info *check_ewl_display_info (Lisp_Object object);


static bool
ewl_color_map_lookup (const char *colorname, unsigned long *pixel)
{
  int i;

  for (i = 0; i < ARRAYELTS (ewl_color_map); i++)
    {
      if (strcasecmp (colorname, ewl_color_map[i].name) == 0)
	{
	  *pixel = ewl_color_map[i].pixel;
	  return 1;
	}
    }

  return 0;
}


static bool
ewl_color_parse (const char *colorname, unsigned long *pixel)
{
  *pixel = 0;

  if (colorname[0] == '#')
    {
      /* Could be an old-style RGB Device specification.  */
      int size = strlen (colorname + 1);
      char *color = alloca (size + 1);

      strcpy (color, colorname + 1);
      if (size == 3 || size == 6 || size == 9 || size == 12)
	{
	  int i, pos;
	  pos = 0;
	  size /= 3;

	  for (i = 0; i < 3; i++)
	    {
	      char *end;
	      char t;
	      unsigned long value;

	      /* The check for 'x' in the following conditional takes into
		 account the fact that strtol allows a "0x" in front of
		 our numbers, and we don't.  */
	      if (!c_isxdigit (color[0]) || color[1] == 'x')
		break;
	      t = color[size];
	      color[size] = '\0';
	      value = strtoul (color, &end, 16);
	      color[size] = t;
	      if (errno == ERANGE || end - color != size)
		break;
	      switch (size)
		{
		case 1:
		  value = value * 0x10;
		  break;
		case 2:
		  break;
		case 3:
		  value /= 0x10;
		  break;
		case 4:
		  value /= 0x100;
		  break;
		}
	      *pixel |= (value << pos);
	      pos += 0x8;
	      if (i == 2)
		return 1;
	      color = end;
	    }
	}
    }
  else if (strncasecmp (colorname, "rgb:", 4) == 0)
    {
      const char *color;
      int i, pos;
      pos = 0;

      color = colorname + 4;
      for (i = 0; i < 3; i++)
	{
	  char *end;
	  unsigned long value;

	  /* The check for 'x' in the following conditional takes into
	     account the fact that strtol allows a "0x" in front of
	     our numbers, and we don't.  */
	  if (!c_isxdigit (color[0]) || color[1] == 'x')
	    break;
	  value = strtoul (color, &end, 16);
	  if (errno == ERANGE)
	    break;
	  switch (end - color)
	    {
	    case 1:
	      value = value * 0x10 + value;
	      break;
	    case 2:
	      break;
	    case 3:
	      value /= 0x10;
	      break;
	    case 4:
	      value /= 0x100;
	      break;
	    default:
	      value = ULONG_MAX;
	    }
	  if (value == ULONG_MAX)
	    break;
	  *pixel |= (value << pos);
	  pos += 0x8;
	  if (i == 2)
	    {
	      if (*end != '\0')
		break;
	      return 1;
	    }
	  if (*end != '/')
	    break;
	  color = end + 1;
	}
    }
  else if (strncasecmp (colorname, "rgbi:", 5) == 0)
    {
      /* This is an RGB Intensity specification.  */
      const char *color;
      int i, pos;
      pos = 0;

      color = colorname + 5;
      for (i = 0; i < 3; i++)
	{
	  char *end;
	  double value;
	  unsigned int val;

	  value = strtod (color, &end);
	  if (errno == ERANGE)
	    break;
	  if (value < 0.0 || value > 1.0)
	    break;
	  val = 0x100 * value;
	  /* We used 0x100 instead of 0xFF to give a continuous
             range between 0.0 and 1.0 inclusive.  The next statement
             fixes the 1.0 case.  */
	  if (val == 0x100)
	    val = 0xFF;
	  *pixel |= (val << pos);
	  pos += 0x8;
	  if (i == 2)
	    {
	      if (*end != '\0')
		break;
	      return 1;
	    }
	  if (*end != '/')
	    break;
	  color = end + 1;
	}
    }

  /* If we fail to lookup the color name in ewl_color_map, then check the
     colorname to see if it can be crudely approximated: If the X color
     ends in a number (e.g., "darkseagreen2"), strip the number and
     return the result of looking up the base color name.  */
  if (ewl_color_map_lookup (colorname, pixel))
    return 1;
  else
    {
      int len = strlen (colorname);

      if (c_isdigit (colorname[len - 1]))
	{
	  char *ptr, *approx = alloca (len + 1);

	  strcpy (approx, colorname);
	  ptr = &approx[len - 1];
	  while (ptr > approx && c_isdigit (*ptr))
	      *ptr-- = '\0';

	  if (ewl_color_map_lookup (colorname, pixel))
	    return 1;
	}
    }

  return 0;
}


bool
ewl_defined_color (struct frame *f, const char *name, XColor *color_def,
		   bool alloc)
{
  unsigned long pixel;

  if (ewl_color_parse (name, &pixel))
    {
      struct ewl_color color;

      color_def->pixel = pixel;
      ewl_query_color (pixel, &color);

      color_def->red = (unsigned short) (color.red * 65535);
      color_def->green = (unsigned short) (color.green * 65535);
      color_def->blue = (unsigned short) (color.blue * 65535);

      return 1;
    }

  return 0;
}


void
ewl_query_color (unsigned long pixel, struct ewl_color *color)
{
  color->red = RED_FROM_ULONG (pixel) / 255.0;
  color->green = GREEN_FROM_ULONG (pixel) / 255.0;
  color->blue = BLUE_FROM_ULONG (pixel) / 255.0;
  color->alpha = ALPHA_FROM_ULONG (pixel) / 255.0;
}


/* Called from frame.c.  */
struct ewl_display_info *
check_x_display_info (Lisp_Object frame)
{
  return check_ewl_display_info (frame);
}


void
x_implicitly_set_name (struct frame *f, Lisp_Object arg, Lisp_Object oldval)
{
  /* FIXME: Not implemented.  */
}


void
x_set_tool_bar_lines (struct frame *f, Lisp_Object value, Lisp_Object oldval)
{
  /* FIXME: Not implemented.  */
}


void
x_set_scroll_bar_default_width (struct frame *f)
{
  /* FIXME: Not implemented.  */
}


char *
x_get_string_resource (XrmDatabase rdb, const char *name, const char *class)
{
  /* FIXME: Not implemented.  */
  return NULL;
}


Lisp_Object
x_get_focus_frame (struct frame *frame)
{
  struct ewl_display_info *dpyinfo = FRAME_DISPLAY_INFO (frame);
  Lisp_Object xfocus;
  if (! dpyinfo->x_focus_frame)
    return Qnil;

  XSETFRAME (xfocus, dpyinfo->x_focus_frame);
  return xfocus;
}


void
x_make_frame_visible (struct frame *f)
{
  if (! FRAME_VISIBLE_P (f))
    {
      SET_FRAME_VISIBLE (f, 1);
    }
  x_focus_frame (f);
}


void
x_make_frame_invisible (struct frame *f)
{
  /* FIXME: Not implemented.  */
}


void
x_iconify_frame (struct frame *f)
{
  /* FIXME: Not implemented.  */
}


void
x_set_menu_bar_lines (struct frame *f, Lisp_Object value, Lisp_Object oldval)
{
  /* FIXME: Not implemented.  */
}


DEFUN ("xw-display-color-p", Fxw_display_color_p, Sxw_display_color_p, 0, 1, 0,
       doc: /* Internal function called by `display-color-p', which see.  */)
     (Lisp_Object terminal)
{
  return Qt;
}


DEFUN ("x-display-grayscale-p", Fx_display_grayscale_p, Sx_display_grayscale_p,
       0, 1, 0,
       doc: /* Return t if the Wayland display supports shades of gray.
Note that color displays do support shades of gray.
The optional argument TERMINAL specifies which display to ask about.
TERMINAL should be a terminal object, a frame or a display name (a string).
If omitted or nil, that stands for the selected frame's display.  */)
  (Lisp_Object terminal)
{
  return Qnil;
}


/* Return the pixel color value for color COLOR_NAME on frame F.  If F
   is a monochrome frame, return MONO_COLOR regardless of what ARG says.
   Signal an error if color can't be allocated.  */

static int
x_decode_color (struct frame *f, Lisp_Object color_name, int mono_color)
{
  XColor cdef;

  CHECK_STRING (color_name);

#if 0 /* Don't do this.  It's wrong when we're not using the default
	 colormap, it makes freeing difficult, and it's probably not
	 an important optimization.  */
  if (strcmp (SDATA (color_name), "black") == 0)
    return BLACK_PIX_DEFAULT (f);
  else if (strcmp (SDATA (color_name), "white") == 0)
    return WHITE_PIX_DEFAULT (f);
#endif

  /* Return MONO_COLOR for monochrome frames.  */
  if (FRAME_DISPLAY_INFO (f)->n_planes == 1)
    return mono_color;

  /* ewl_defined_color is responsible for coping with failures
     by looking for a near-miss.  */
  if (ewl_defined_color (f, SSDATA (color_name), &cdef, 1))
    return cdef.pixel;

  signal_error ("Undefined color", color_name);
}


static void
x_set_foreground_color (struct frame *f, Lisp_Object arg, Lisp_Object oldval)
{
  FRAME_FOREGROUND_PIXEL (f) = x_decode_color (f, arg, BLACK_PIX_DEFAULT (f));

  if (FRAME_X_WINDOW (f) != 0)
    {
      update_face_from_frame_parameter (f, Qforeground_color, arg);

      if (FRAME_VISIBLE_P (f))
        redraw_frame (f);
    }
}


static void
x_set_background_color (struct frame *f, Lisp_Object arg, Lisp_Object oldval)
{
  FRAME_BACKGROUND_PIXEL (f) = x_decode_color (f, arg, WHITE_PIX_DEFAULT (f));

  if (FRAME_X_WINDOW (f) != 0)
    {
      update_face_from_frame_parameter (f, Qbackground_color, arg);

      if (FRAME_VISIBLE_P (f))
        redraw_frame (f);
    }
}


static void
x_set_mouse_color (struct frame *f, Lisp_Object arg, Lisp_Object oldval)
{
  struct ewl_output *wl = f->output_data.wl;
  unsigned long pixel = x_decode_color (f, arg, BLACK_PIX_DEFAULT (f));
  unsigned long mask_color = FRAME_BACKGROUND_PIXEL (f);

  /* Don't let pointers be invisible.  */
  if (mask_color == pixel)
    pixel = FRAME_FOREGROUND_PIXEL (f);

  wl->mouse_pixel = pixel;

  update_face_from_frame_parameter (f, Qmouse_color, arg);
}


static void
x_set_cursor_color (struct frame *f, Lisp_Object arg, Lisp_Object oldval)
{
  unsigned long fore_pixel, pixel;
  struct ewl_output *wl = f->output_data.wl;

  if (!NILP (Vx_cursor_fore_pixel))
    fore_pixel = x_decode_color (f, Vx_cursor_fore_pixel,
				 WHITE_PIX_DEFAULT (f));
  else
    fore_pixel = FRAME_BACKGROUND_PIXEL (f);

  pixel = x_decode_color (f, arg, BLACK_PIX_DEFAULT (f));

  /* Make sure that the cursor color differs from the background color.  */
  if (pixel == FRAME_BACKGROUND_PIXEL (f))
    {
      pixel = wl->mouse_pixel;
      if (pixel == fore_pixel)
	fore_pixel = FRAME_BACKGROUND_PIXEL (f);
    }

  wl->cursor_foreground_pixel = fore_pixel;
  wl->cursor_pixel = pixel;

  if (FRAME_X_WINDOW (f) != 0)
    {
      if (wl->cursor_gc == NULL)
	wl->cursor_gc = xmalloc (sizeof *wl->cursor_gc);
      wl->cursor_gc->foreground = wl->cursor_gc->background = wl->cursor_pixel;
    }

  update_face_from_frame_parameter (f, Qcursor_color, arg);
}


static void
x_set_cursor_type (struct frame *f, Lisp_Object arg, Lisp_Object oldval)
{
  set_frame_cursor_types (f, arg);
}


/* Note: see frame.c for template, also where generic functions are impl */
frame_parm_handler ewl_frame_parm_handlers[] =
  {
    x_set_autoraise, /* generic OK */
    x_set_autolower, /* generic OK */
    x_set_background_color,
    0, /* x_set_border_color */
    0, /* x_set_border_width */
    x_set_cursor_color,
    x_set_cursor_type,
    x_set_font, /* generic OK */
    x_set_foreground_color,
    0,				/* x_set_icon_name */
    0,				/* x_set_icon_type */
    x_set_internal_border_width, /* generic OK */
    0, /* x_set_right_divider_width */
    0, /* x_set_bottom_divider_width */
    0, /* x_set_menu_bar_lines */
    x_set_mouse_color,
    0,				/* x_explicitly_set_name */
    x_set_scroll_bar_width, /* generic OK */
    0,			  /* x_set_title */
    x_set_unsplittable, /* generic OK */
    x_set_vertical_scroll_bars, /* generic OK */
    x_set_visibility, /* generic OK */
    0,		    /* x_set_tool_bar_lines */
    0, /* x_set_scroll_bar_foreground, will ignore (not possible on NS) */
    0, /* x_set_scroll_bar_background,  will ignore (not possible on NS) */
    x_set_screen_gamma, /* generic OK */
    x_set_line_spacing, /* generic OK, sets f->extra_line_spacing to int */
    x_set_fringe_width, /* generic OK */
    x_set_fringe_width, /* generic OK */
    0, /* x_set_wait_for_wm, will ignore */
    x_set_fullscreen, /* generic OK */
    x_set_font_backend, /* generic OK */
    0,		      /* x_set_alpha */
    0, /* x_set_sticky */
    0, /* x_set_tool_bar_position */
  };


/* Return the X display structure for the display named NAME.
   Open a new connection if necessary.  */
struct ewl_display_info *
ewl_display_info_for_name (Lisp_Object name)
{
  struct ewl_display_info *dpyinfo;

  CHECK_STRING (name);

  for (dpyinfo = x_display_list; dpyinfo; dpyinfo = dpyinfo->next)
    if (!NILP (Fstring_equal (XCAR (dpyinfo->name_list_element), name)))
      return dpyinfo;

  error ("Emacs for Wayland does not yet support multi-display");

  Fx_open_connection (name, Qnil, Qnil);
  dpyinfo = x_display_list;

  if (dpyinfo == 0)
    error ("Display on %s not responding.\n", SDATA (name));

  return dpyinfo;
}


static struct ewl_display_info *
check_ewl_display_info (Lisp_Object object)
{
  struct ewl_display_info *dpyinfo = NULL;

  if (NILP (object))
    {
      struct frame *sf = XFRAME (selected_frame);

      if (FRAME_NS_P (sf) && FRAME_LIVE_P (sf))
	dpyinfo = FRAME_DISPLAY_INFO (sf);
      else if (x_display_list != 0)
	dpyinfo = x_display_list;
      else
        error ("Wayland windows are not in use or not initialized");
    }
  else if (TERMINALP (object))
    {
      struct terminal *t = get_terminal (object, 1);

      if (t->type != output_ns)
        error ("Terminal %d is not a Wayland display", t->id);

      dpyinfo = t->display_info.wl;
    }
  else if (STRINGP (object))
    dpyinfo = ewl_display_info_for_name (object);
  else
    {
      struct frame *f = decode_window_system_frame (object);
      dpyinfo = FRAME_DISPLAY_INFO (f);
    }

  return dpyinfo;
}


void
x_free_frame_resources (struct frame *f)
{
  block_input ();

  if (f->output_data.wl->shell_surface)
    wl_shell_surface_destroy (f->output_data.wl->shell_surface);
  if (f->output_data.wl->surface)
    wl_surface_destroy (f->output_data.wl->surface);
  if (f->output_data.wl->buffer)
    wl_buffer_destroy (f->output_data.wl->buffer);
  if (f->output_data.wl->callback)
    wl_callback_destroy (f->output_data.wl->callback);
  if (f->output_data.wl->cairo_surface)
    cairo_surface_destroy (f->output_data.wl->cairo_surface);
  if (f->output_data.wl->data)
    munmap (f->output_data.wl->data, f->output_data.wl->size);
  xfree (f->output_data.wl);

  unblock_input ();
}


/* Handler for signals raised during x_create_frame.
   FRAME is the frame which is partially constructed.  */

static void
unwind_create_frame (Lisp_Object frame)
{
  struct frame *f = XFRAME (frame);

  /* If frame is already dead, nothing to do.  This can happen if the
     display is disconnected after the frame has become official, but
     before x_create_frame removes the unwind protect.  */
  if (!FRAME_LIVE_P (f))
    return;

  /* If frame is ``official'', nothing to do.  */
  if (NILP (Fmemq (frame, Vframe_list)))
    {
#if defined GLYPH_DEBUG && defined ENABLE_CHECKING
      struct ns_display_info *dpyinfo = FRAME_DISPLAY_INFO (f);
#endif

      x_free_frame_resources (f);
      free_glyphs (f);

#ifdef GLYPH_DEBUG
      /* Check that reference counts are indeed correct.  */
      eassert (dpyinfo->terminal->image_cache->refcount == image_cache_refcount);
#endif
    }
}


static void
ewl_default_font_parameter (struct frame *f, Lisp_Object parms)
{
  Lisp_Object font;

  font = font_open_by_name (f, build_unibyte_string ("Monospace 11"));
  if (NILP (font))
    error ("No suitable font was found");

  /* This call will make X resources override any system font setting.  */
  x_default_parameter (f, parms, Qfont, font, "font", "Font", RES_TYPE_STRING);
}


static void
ewl_shell_surface_handle_ping (void *data,
			       struct wl_shell_surface *shell_surface,
			       uint32_t serial)
{
  wl_shell_surface_pong (shell_surface, serial);
}

static void
ewl_shell_surface_handle_configure (void *data,
				    struct wl_shell_surface *wl_shell_surface,
				    uint32_t edges,
				    int32_t width,
				    int32_t height)
{
}

static void
ewl_shell_surface_popup_done (void *data,
			      struct wl_shell_surface *wl_shell_surface)
{
}

static const struct wl_shell_surface_listener ewl_shell_surface_listener =
  {
    ewl_shell_surface_handle_ping,
    ewl_shell_surface_handle_configure,
    ewl_shell_surface_popup_done
  };


DEFUN ("x-create-frame", Fx_create_frame, Sx_create_frame,
       1, 1, 0,
       doc: /* Make a new client window, which is called a "frame" in Emacs terms.
	       Return an Emacs frame object.
	       PARMS is an alist of frame parameters.
	       If the parameters specify that the frame should not have a minibuffer,
	       and do not specify a specific minibuffer window to use,
	       then `default-minibuffer-frame' must be a frame whose minibuffer can
	       be shared by the new frame.

	       This function is an internal primitive--use `make-frame' instead.  */)
  (Lisp_Object parms)
{
  struct frame *f;
  Lisp_Object frame, tem;
  Lisp_Object name;
  int minibuffer_only = 0;
  long window_prompting = 0;
  int width, height;
  ptrdiff_t count = SPECPDL_INDEX ();
  struct gcpro gcpro1, gcpro2, gcpro3, gcpro4;
  Lisp_Object display;
  struct ewl_display_info *dpyinfo = NULL;
  Lisp_Object parent;
  struct kboard *kb;
  static int desc_ctr = 1;

  parms = Fcopy_alist (parms);

  /* Use this general default value to start with
     until we know if this frame has a specified name.  */
  Vx_resource_name = Vinvocation_name;

  display = x_get_arg (dpyinfo, parms, Qdisplay, 0, 0, RES_TYPE_STRING);
  if (EQ (display, Qunbound))
    display = Qnil;
  dpyinfo = check_ewl_display_info (display);
  kb = dpyinfo->terminal->kboard;

  if (!dpyinfo->terminal->name)
    error ("Terminal is not live, can't create new frames on it");

  name = x_get_arg (dpyinfo, parms, Qname, "name", "Name", RES_TYPE_STRING);
  if (!STRINGP (name)
      && ! EQ (name, Qunbound)
      && ! NILP (name))
    error ("Invalid frame name--not a string or nil");

  if (STRINGP (name))
    Vx_resource_name = name;

  /* See if parent window is specified.  */
  parent = x_get_arg (dpyinfo, parms, Qparent_id, NULL, NULL, RES_TYPE_NUMBER);
  if (EQ (parent, Qunbound))
    parent = Qnil;
  if (! NILP (parent))
    CHECK_NUMBER (parent);

  /* make_frame_without_minibuffer can run Lisp code and garbage collect.  */
  /* No need to protect DISPLAY because that's not used after passing
     it to make_frame_without_minibuffer.  */
  frame = Qnil;
  GCPRO4 (parms, parent, name, frame);
  tem = x_get_arg (dpyinfo, parms, Qminibuffer, "minibuffer", "Minibuffer",
		   RES_TYPE_SYMBOL);
  if (EQ (tem, Qnone) || NILP (tem))
    f = make_frame_without_minibuffer (Qnil, kb, display);
  else if (EQ (tem, Qonly))
    {
      f = make_minibuffer_frame ();
      minibuffer_only = 1;
    }
  else if (WINDOWP (tem))
    f = make_frame_without_minibuffer (tem, kb, display);
  else
    f = make_frame (1);

  XSETFRAME (frame, f);

  f->terminal = dpyinfo->terminal;

  f->output_method = output_wl;
  f->output_data.wl = xzalloc (sizeof *f->output_data.wl);

  FRAME_FONTSET (f) = -1;

  fset_icon_name (f,
		  x_get_arg (dpyinfo, parms, Qicon_name, "iconName", "Title",
			     RES_TYPE_STRING));
  if (! STRINGP (f->icon_name))
    fset_icon_name (f, Qnil);

  FRAME_DISPLAY_INFO (f) = dpyinfo;

  /* With FRAME_DISPLAY_INFO set up, this unwind-protect is safe.  */
  record_unwind_protect (unwind_create_frame, frame);

  f->output_data.wl->window_desc = desc_ctr++;

  /* Specify the parent under which to make this X window.  */

  if (!NILP (parent))
    {
      f->output_data.wl->parent_desc = (Window) XFASTINT (parent);
      f->output_data.wl->explicit_parent = 1;
    }
  else
    {
      f->output_data.wl->parent_desc = FRAME_DISPLAY_INFO (f)->root_window;
      f->output_data.wl->explicit_parent = 0;
    }

  /* Set the name; the functions to which we pass f expect the name to
     be set.  */
  if (EQ (name, Qunbound) || NILP (name))
    {
      fset_name (f, build_string ("emacs"));
      f->explicit_name = 0;
    }
  else
    {
      fset_name (f, name);
      f->explicit_name = 1;
      /* use the frame's title when getting resources for this frame.  */
      specbind (Qx_resource_name, name);
    }

  register_font_driver (&wlfont_driver, f);

  x_default_parameter (f, parms, Qfont_backend, Qnil,
		       "fontBackend", "FontBackend", RES_TYPE_STRING);

  /* Extract the window parameters from the supplied values
     that are needed to determine window geometry.  */
  ewl_default_font_parameter (f, parms);

  x_default_parameter (f, parms, Qborder_width, make_number (0),
		       "borderWidth", "BorderWidth", RES_TYPE_NUMBER);
  x_default_parameter (f, parms, Qinternal_border_width,
		       make_number (0),
		       "internalBorderWidth", "internalBorderWidth",
		       RES_TYPE_NUMBER);
  x_default_parameter (f, parms, Qvertical_scroll_bars,
		       Qright,
		       "verticalScrollBars", "ScrollBars",
		       RES_TYPE_SYMBOL);

  /* Also do the stuff which must be set before the window exists.  */
  x_default_parameter (f, parms, Qforeground_color, build_string ("black"),
		       "foreground", "Foreground", RES_TYPE_STRING);
  x_default_parameter (f, parms, Qbackground_color, build_string ("white"),
		       "background", "Background", RES_TYPE_STRING);
  x_default_parameter (f, parms, Qmouse_color, build_string ("black"),
		       "pointerColor", "Foreground", RES_TYPE_STRING);
  x_default_parameter (f, parms, Qborder_color, build_string ("black"),
		       "borderColor", "BorderColor", RES_TYPE_STRING);
  x_default_parameter (f, parms, Qscreen_gamma, Qnil,
		       "screenGamma", "ScreenGamma", RES_TYPE_FLOAT);
  x_default_parameter (f, parms, Qline_spacing, Qnil,
		       "lineSpacing", "LineSpacing", RES_TYPE_NUMBER);
  x_default_parameter (f, parms, Qleft_fringe, Qnil,
		       "leftFringe", "LeftFringe", RES_TYPE_NUMBER);
  x_default_parameter (f, parms, Qright_fringe, Qnil,
		       "rightFringe", "RightFringe", RES_TYPE_NUMBER);

#ifdef GLYPH_DEBUG
  image_cache_refcount =
    FRAME_IMAGE_CACHE (f) ? FRAME_IMAGE_CACHE (f)->refcount : 0;
#endif /* GLYPH_DEBUG */

  /* Init faces before x_default_parameter is called for scroll-bar
     parameters because that function calls x_set_scroll_bar_width,
     which calls change_frame_size, which calls Fset_window_buffer,
     which runs hooks, which call Fvertical_motion.  At the end, we
     end up in init_iterator with a null face cache, which should not
     happen.  */
  init_frame_faces (f);

  /* PXW: This is a duplicate from below.  We have to do it here since
     otherwise x_set_tool_bar_lines will work with the character sizes
     installed by init_frame_faces while the frame's pixel size is still
     calculated from a character size of 1 and we subsequently hit the
     eassert (height >= 0) assertion in window_box_height.  The
     non-pixelwise code apparently worked around this because it had one
     frame line vs one toolbar line which left us with a zero root
     window height which was obviously wrong as well ...  */
  change_frame_size (f, FRAME_COLS (f) * FRAME_COLUMN_WIDTH (f),
		     FRAME_LINES (f) * FRAME_LINE_HEIGHT (f), 1, 0, 0, 1);

  /* The resources controlling the menu-bar and tool-bar are
     processed specially at startup, and reflected in the mode
     variables; ignore them here.  */
  x_default_parameter (f, parms, Qmenu_bar_lines,
		       NILP (Vmenu_bar_mode)
		       ? make_number (0) : make_number (1),
		       NULL, NULL, RES_TYPE_NUMBER);
  x_default_parameter (f, parms, Qtool_bar_lines,
		       NILP (Vtool_bar_mode)
		       ? make_number (0) : make_number (1),
		       NULL, NULL, RES_TYPE_NUMBER);

  x_default_parameter (f, parms, Qbuffer_predicate, Qnil,
		       "bufferPredicate", "BufferPredicate",
		       RES_TYPE_SYMBOL);
  x_default_parameter (f, parms, Qtitle, Qnil,
		       "title", "Title", RES_TYPE_STRING);
  x_default_parameter (f, parms, Qwait_for_wm, Qt,
		       "waitForWM", "WaitForWM", RES_TYPE_BOOLEAN);
  x_default_parameter (f, parms, Qfullscreen, Qnil,
                       "fullscreen", "Fullscreen", RES_TYPE_SYMBOL);
  x_default_parameter (f, parms, Qtool_bar_position,
                       f->tool_bar_position, 0, 0, RES_TYPE_SYMBOL);
  /* Compute the size of the X window.  */
  window_prompting = x_figure_window_size (f, parms, 1);

  tem = x_get_arg (dpyinfo, parms, Qunsplittable, 0, 0, RES_TYPE_BOOLEAN);
  f->no_split = minibuffer_only || EQ (tem, Qt);

#if 0
  x_icon_verify (f, parms);
#endif

  f->output_data.wl->surface
    = wl_compositor_create_surface (dpyinfo->compositor);
  f->output_data.wl->shell_surface
    = wl_shell_get_shell_surface (dpyinfo->shell, f->output_data.wl->surface);
  if (f->output_data.wl->shell_surface)
    {
      wl_shell_surface_add_listener (f->output_data.wl->shell_surface,
				     &ewl_shell_surface_listener,
				     NULL);
      wl_shell_surface_set_toplevel (f->output_data.wl->shell_surface);
    }

#if 0
  x_icon (f, parms);
#endif

  /* Now consider the frame official.  */
  f->terminal->reference_count++;
  Vframe_list = Fcons (frame, Vframe_list);

  /* We need to do this after creating the X window, so that the
     icon-creation functions can say whose icon they're describing.  */
  x_default_parameter (f, parms, Qicon_type, Qt,
		       "bitmapIcon", "BitmapIcon", RES_TYPE_BOOLEAN);

  x_default_parameter (f, parms, Qauto_raise, Qnil,
		       "autoRaise", "AutoRaiseLower", RES_TYPE_BOOLEAN);
  x_default_parameter (f, parms, Qauto_lower, Qnil,
		       "autoLower", "AutoRaiseLower", RES_TYPE_BOOLEAN);
  x_default_parameter (f, parms, Qcursor_type, Qbox,
		       "cursorType", "CursorType", RES_TYPE_SYMBOL);
  x_default_parameter (f, parms, Qscroll_bar_width, Qnil,
		       "scrollBarWidth", "ScrollBarWidth",
		       RES_TYPE_NUMBER);
  x_default_parameter (f, parms, Qalpha, Qnil,
		       "alpha", "Alpha", RES_TYPE_NUMBER);

  /* Dimensions, especially FRAME_LINES (f), must be done via change_frame_size.
     Change will not be effected unless different from the current
     FRAME_LINES (f).  */
  width = FRAME_TEXT_WIDTH (f);
  height = FRAME_TEXT_HEIGHT (f);
  FRAME_TEXT_HEIGHT (f) = 0;
  SET_FRAME_WIDTH (f, 0);
  change_frame_size (f, width, height, 1, 0, 0, 1);

  /* Create the menu bar.  */
#if 0				/* FIXME */
  if (!minibuffer_only && FRAME_EXTERNAL_MENU_BAR (f))
    {
      /* If this signals an error, we haven't set size hints for the
	 frame and we didn't make it visible.  */
      initialize_frame_menubar (f);
    }
#endif

  /* Make the window appear on the frame and enable display, unless
     the caller says not to.  However, with explicit parent, Emacs
     cannot control visibility, so don't try.  */
  if (! f->output_data.wl->explicit_parent)
    {
      Lisp_Object visibility;

      visibility = x_get_arg (dpyinfo, parms, Qvisibility, 0, 0,
			      RES_TYPE_SYMBOL);
      if (EQ (visibility, Qunbound))
	visibility = Qt;

      if (EQ (visibility, Qicon))
	x_iconify_frame (f);
      else if (! NILP (visibility))
	x_make_frame_visible (f);
      else
	{
	  /* Must have been Qnil.  */
	}
    }

  /* Initialize `default-minibuffer-frame' in case this is the first
     frame on this terminal.  */
  if (FRAME_HAS_MINIBUF_P (f)
      && (!FRAMEP (KVAR (kb, Vdefault_minibuffer_frame))
          || !FRAME_LIVE_P (XFRAME (KVAR (kb, Vdefault_minibuffer_frame)))))
    kset_default_minibuffer_frame (kb, frame);

  /* All remaining specified parameters, which have not been "used"
     by x_get_arg and friends, now go in the misc. alist of the frame.  */
  for (tem = parms; CONSP (tem); tem = XCDR (tem))
    if (CONSP (XCAR (tem)) && !NILP (XCAR (XCAR (tem))))
      fset_param_alist (f, Fcons (XCAR (tem), f->param_alist));

  UNGCPRO;

  /* Make sure windows on this frame appear in calls to next-window
     and similar functions.  */
  Vwindow_list = Qnil;

  return unbind_to (count, frame);
}


DEFUN ("x-open-connection", Fx_open_connection, Sx_open_connection,
       1, 3, 0,
       doc: /* Open a connection to a display server.
DISPLAY is the name of the display to connect to.
Optional second arg XRM-STRING is a string of resources in xrdb format.
If the optional third arg MUST-SUCCEED is non-nil,
terminate Emacs if we can't open the connection.
\(In the Wayland version, the last two arguments are currently ignored.)  */)
     (Lisp_Object display, Lisp_Object resource_string, Lisp_Object must_succeed)
{
  struct ewl_display_info *dpyinfo;

  CHECK_STRING (display);

  dpyinfo = ewl_term_init (display);
  if (dpyinfo == 0)
    {
      if (!NILP (must_succeed))
        fatal ("Display on %s not responding.\n",
               SSDATA (display));
      else
        error ("Display on %s not responding.\n",
               SSDATA (display));
    }

  return Qnil;
}


DEFUN ("x-close-connection", Fx_close_connection, Sx_close_connection,
       1, 1, 0,
       doc: /* Close the connection to TERMINAL's Wayland display server.
For TERMINAL, specify a terminal object, a frame or a display name (a
string).  If TERMINAL is nil, that stands for the selected frame's
terminal.  */)
     (Lisp_Object terminal)
{
  struct ewl_display_info *dpyinfo = check_x_display_info (terminal);

  block_input ();

  x_delete_display (dpyinfo);

  unblock_input ();

  return Qnil;
}

DEFUN ("x-hide-tip", Fx_hide_tip, Sx_hide_tip, 0, 0, 0,
       doc: /* Hide the current tooltip window, if there is any.
Value is t if tooltip was open, nil otherwise.  */)
     (void)
{
  return Qt;
}


DEFUN ("x-display-planes", Fx_display_planes, Sx_display_planes,
       0, 1, 0,
       doc: /* Return the number of bitplanes of the X display TERMINAL.
The optional argument TERMINAL specifies which display to ask about.
TERMINAL should be a terminal object, a frame or a display name (a string).
If omitted or nil, that stands for the selected frame's display.  */)
  (Lisp_Object terminal)
{
  struct ewl_display_info *dpyinfo = check_ewl_display_info (terminal);

  return make_number (dpyinfo->n_planes);
}


DEFUN ("x-display-color-cells", Fx_display_color_cells, Sx_display_color_cells,
       0, 1, 0,
       doc: /* Return the number of color cells of the X display TERMINAL.
The optional argument TERMINAL specifies which display to ask about.
TERMINAL should be a terminal object, a frame or a display name (a string).
If omitted or nil, that stands for the selected frame's display.  */)
  (Lisp_Object terminal)
{
  struct ewl_display_info *dpyinfo = check_ewl_display_info (terminal);

  return make_number (1 << dpyinfo->n_planes);
}


DEFUN ("xw-color-defined-p", Fxw_color_defined_p, Sxw_color_defined_p, 1, 2, 0,
       doc: /* Internal function called by `color-defined-p', which see
.\(Note that the Wayland version of this function ignores FRAME.)  */)
  (Lisp_Object color, Lisp_Object frame)
{
  XColor foo;
  struct frame *f = decode_window_system_frame (frame);

  CHECK_STRING (color);

  if (ewl_defined_color (f, SSDATA (color), &foo, 0))
    return Qt;
  else
    return Qnil;
}


DEFUN ("xw-color-values", Fxw_color_values, Sxw_color_values, 1, 2, 0,
       doc: /* Internal function called by `color-values', which see.  */)
  (Lisp_Object color, Lisp_Object frame)
{
  XColor foo;
  struct frame *f = decode_window_system_frame (frame);

  CHECK_STRING (color);

  if (ewl_defined_color (f, SSDATA (color), &foo, 0))
    return list3i (foo.red, foo.green, foo.blue);
  else
    return Qnil;
}


void
syms_of_wlfns (void)
{
  DEFVAR_LISP ("x-cursor-fore-pixel", Vx_cursor_fore_pixel,
    doc: /* A string indicating the foreground color of the cursor box.  */);
  Vx_cursor_fore_pixel = Qnil;

  defsubr (&Sx_create_frame);
  defsubr (&Sx_open_connection);
  defsubr (&Sx_close_connection);
  defsubr (&Sx_hide_tip);
  defsubr (&Sx_display_grayscale_p);
  defsubr (&Sx_display_planes);
  defsubr (&Sx_display_color_cells);
  defsubr (&Sxw_color_defined_p);
  defsubr (&Sxw_color_values);
  defsubr (&Sxw_display_color_p);
}
