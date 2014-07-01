/* Font back-end driver for Wayland, based on XFT font driver.
   Copyright (C) 2006-2014 Free Software Foundation, Inc.
   Copyright (C) 2006, 2007, 2008, 2009, 2010, 2011
     National Institute of Advanced Industrial Science and Technology (AIST)
     Registration Number H13PRO009

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
#include <stdio.h>
#include <cairo-ft.h>

#include "lisp.h"
#include "dispextern.h"
#include "wlterm.h"
#include "frame.h"
#include "blockinput.h"
#include "character.h"
#include "charset.h"
#include "fontset.h"
#include "font.h"
#include "ftfont.h"

/* WL font driver.  */

static Lisp_Object QChinting, QChintstyle;

/* The actual structure for WL font that can be cast to struct
   font.  */

struct wlfont_info
{
  struct font font;
  cairo_scaled_font_t *scaled_font;
  cairo_surface_t *surface;
};

/* Structure pointed by (struct face *)->extra  */

struct wlface_info
{
  struct ewl_color fg;
  struct ewl_color bg;
};

/* Setup foreground and background colors of GC into FG and BG.  If
   XFTFACE_INFO is not NULL, reuse the colors in it if possible.  BG
   may be NULL.  */

static void
wlfont_get_colors (struct frame *f, struct face *face, GC gc,
		   struct wlface_info *wlface_info,
		   struct ewl_color *fg, struct ewl_color *bg)
{
  if (wlface_info && face->gc == gc)
    {
      *fg = wlface_info->fg;
      if (bg)
	*bg = wlface_info->bg;
    }
  else
    {
      bool fg_done = 0, bg_done = 0;

      if (wlface_info)
	{
	  if (gc->foreground == face->foreground)
	    *fg = wlface_info->fg, fg_done = 1;
	  else if (gc->foreground == face->background)
	    *fg = wlface_info->bg, fg_done = 1;
	  if (! bg)
	    bg_done = 1;
	  else if (gc->background == face->background)
	    *bg = wlface_info->bg, bg_done = 1;
	  else if (gc->background == face->foreground)
	    *bg = wlface_info->fg, bg_done = 1;
	}

      if (! (fg_done & bg_done))
	{
	  ewl_query_color (gc->foreground, fg);
	  if (bg)
	    ewl_query_color (gc->background, bg);
	}
    }
}


struct font_driver wlfont_driver;

static Lisp_Object
wlfont_list (struct frame *f, Lisp_Object spec)
{
  Lisp_Object list = ftfont_driver.list (f, spec), tail;

  for (tail = list; CONSP (tail); tail = XCDR (tail))
    ASET (XCAR (tail), FONT_TYPE_INDEX, Qwl);
  return list;
}

static Lisp_Object
wlfont_match (struct frame *f, Lisp_Object spec)
{
  Lisp_Object entity = ftfont_driver.match (f, spec);

  if (VECTORP (entity))
    ASET (entity, FONT_TYPE_INDEX, Qwl);
  return entity;
}

static char ascii_printable[96];


static void
wlfont_add_rendering_parameters (cairo_font_options_t *options,
				 Lisp_Object entity)
{
  Lisp_Object tail;
  int ival;

  for (tail = AREF (entity, FONT_EXTRA_INDEX); CONSP (tail); tail = XCDR (tail))
    {
      Lisp_Object key = XCAR (XCAR (tail));
      Lisp_Object val = XCDR (XCAR (tail));

      if (EQ (key, QCantialias))
	cairo_font_options_set_antialias (options, NILP (val) ? CAIRO_ANTIALIAS_NONE : CAIRO_ANTIALIAS_GRAY);
      else if (EQ (key, QChinting))
	cairo_font_options_set_hint_metrics (options, NILP (val) ? CAIRO_HINT_METRICS_OFF : CAIRO_HINT_METRICS_ON);
      /* TODO: hintstyle?  */
    }
}

static Lisp_Object
wlfont_open (struct frame *f, Lisp_Object entity, int pixel_size)
{
  Lisp_Object val, filename, idx, font_object;
  FcPattern *pat = NULL;
  struct wlfont_info *wlfont_info = NULL;
  struct font *font;
  double size = 0;
  int spacing;
  char name[256];
  int len, i;
  cairo_font_face_t *font_face;
  cairo_scaled_font_t *scaled_font;
  cairo_matrix_t font_matrix;
  cairo_matrix_t matrix;
  cairo_font_options_t *options;
  cairo_font_extents_t fe;
  cairo_text_extents_t te;
  cairo_t *cr;

  val = assq_no_quit (QCfont_entity, AREF (entity, FONT_EXTRA_INDEX));
  if (! CONSP (val))
    return Qnil;
  val = XCDR (val);
  filename = XCAR (val);
  idx = XCDR (val);
  size = XINT (AREF (entity, FONT_SIZE_INDEX));
  if (size == 0)
    size = pixel_size;
  pat = FcPatternCreate ();
  FcPatternAddInteger (pat, FC_WEIGHT, FONT_WEIGHT_NUMERIC (entity));
  i = FONT_SLANT_NUMERIC (entity) - 100;
  if (i < 0) i = 0;
  FcPatternAddInteger (pat, FC_SLANT, i);
  FcPatternAddInteger (pat, FC_WIDTH, FONT_WIDTH_NUMERIC (entity));
  FcPatternAddDouble (pat, FC_PIXEL_SIZE, pixel_size);
  val = AREF (entity, FONT_FAMILY_INDEX);
  if (! NILP (val))
    FcPatternAddString (pat, FC_FAMILY, (FcChar8 *) SDATA (SYMBOL_NAME (val)));
  val = AREF (entity, FONT_FOUNDRY_INDEX);
  if (! NILP (val))
    FcPatternAddString (pat, FC_FOUNDRY, (FcChar8 *) SDATA (SYMBOL_NAME (val)));
  val = AREF (entity, FONT_SPACING_INDEX);
  if (! NILP (val))
    FcPatternAddInteger (pat, FC_SPACING, XINT (val));
  val = AREF (entity, FONT_DPI_INDEX);
  if (! NILP (val))
    {
      double dbl = XINT (val);

      FcPatternAddDouble (pat, FC_DPI, dbl);
    }
  val = AREF (entity, FONT_AVGWIDTH_INDEX);
  if (INTEGERP (val) && XINT (val) == 0)
    FcPatternAddBool (pat, FC_SCALABLE, FcTrue);
  /* This is necessary to identify the exact font (e.g. 10x20.pcf.gz
     over 10x20-ISO8859-1.pcf.gz).  */
  FcPatternAddCharSet (pat, FC_CHARSET, ftfont_get_fc_charset (entity));

  FcPatternAddString (pat, FC_FILE, (FcChar8 *) SDATA (filename));
  FcPatternAddInteger (pat, FC_INDEX, XINT (idx));

  FcDefaultSubstitute (pat);
  font_face = cairo_ft_font_face_create_for_pattern (pat);
  cairo_matrix_init_scale (&font_matrix, pixel_size, pixel_size);
  cairo_matrix_init_identity (&matrix);
  options = cairo_font_options_create ();
  wlfont_add_rendering_parameters (options, entity);
  scaled_font = cairo_scaled_font_create (font_face, &font_matrix, &matrix,
					  options);

  /* We should not destroy PAT here because it is kept in XFTFONT and
     destroyed automatically when XFTFONT is closed.  */
  font_object = font_build_object (VECSIZE (struct wlfont_info),
				   Qwl, entity, size);
  ASET (font_object, FONT_FILE_INDEX, filename);
  font = XFONT_OBJECT (font_object);
  font->pixel_size = size;
  font->driver = &wlfont_driver;
  font->encoding_charset = font->repertory_charset = -1;

  wlfont_info = (struct wlfont_info *) font;
  wlfont_info->scaled_font = scaled_font;
  wlfont_info->surface
    = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
				  FRAME_PIXEL_WIDTH (f),
				  FRAME_PIXEL_HEIGHT (f));
  if (INTEGERP (AREF (entity, FONT_SPACING_INDEX)))
    spacing = XINT (AREF (entity, FONT_SPACING_INDEX));
  else
    spacing = FC_PROPORTIONAL;
  if (! ascii_printable[0])
    {
      int ch;
      for (ch = 0; ch < 95; ch++)
	ascii_printable[ch] = ' ' + ch;
    }

  block_input ();

  cr = cairo_create (wlfont_info->surface);
  cairo_set_scaled_font (cr, wlfont_info->scaled_font);
  cairo_font_extents (cr, &fe);

  /* Unfortunately Xft doesn't provide a way to get minimum char
     width.  So, we set min_width to space_width.  */

  if (spacing != FC_PROPORTIONAL
#ifdef FC_DUAL
      && spacing != FC_DUAL
#endif	/* FC_DUAL */
      )
    {
      font->min_width = font->max_width = font->average_width
	= font->space_width = fe.max_x_advance;
      cairo_text_extents (cr, ascii_printable + 1, &te);
    }
  else
    {
      cairo_text_extents (cr, " ", &te);
      font->min_width = font->max_width = font->space_width
	= te.x_advance;
      if (font->space_width <= 0)
	/* dirty workaround */
	font->space_width = pixel_size;
      cairo_text_extents (cr, ascii_printable + 1, &te);
      font->average_width = (font->space_width + te.x_advance) / 95;
    }

  cairo_destroy (cr);

  unblock_input ();

  font->ascent = fe.ascent;
  font->descent = fe.descent;
  if (pixel_size >= 5)
    {
      /* The above condition is a dirty workaround because
	 XftTextExtents8 behaves strangely for some fonts
	 (e.g. "Dejavu Sans Mono") when pixel_size is less than 5. */
      /* Perhaps not needed for WL?  */
      if (font->ascent < - te.y_bearing)
	font->ascent = - te.y_bearing;
      if (font->descent < te.height + te.y_bearing)
	font->descent = te.height + te.y_bearing;
    }
  font->height = fe.height + fe.max_y_advance;

  if (XINT (AREF (entity, FONT_SIZE_INDEX)) == 0)
    {
      FT_Face ft_face;
      int upEM;

      ft_face = cairo_ft_scaled_font_lock_face (wlfont_info->scaled_font);
      upEM = ft_face->units_per_EM;
      font->underline_position = -ft_face->underline_position * size / upEM;
      font->underline_thickness = ft_face->underline_thickness * size / upEM;
      if (font->underline_thickness > 2)
	font->underline_position -= font->underline_thickness / 2;
      cairo_ft_scaled_font_unlock_face (wlfont_info->scaled_font);
    }
  else
    {
      font->underline_position = -1;
      font->underline_thickness = 0;
    }

  font->baseline_offset = 0;
  font->relative_compose = 0;
  font->default_ascent = 0;
  font->vertical_centering = 0;

  return font_object;
}

static void
wlfont_close (struct font *font)
{
  struct wlfont_info *wlfont_info = (struct wlfont_info *) XFONT_OBJECT (font);

  if (wlfont_info->surface)
    {
      cairo_surface_destroy (wlfont_info->surface);
      wlfont_info->surface = NULL;
    }
  if (wlfont_info->scaled_font)
    {
      cairo_scaled_font_destroy (wlfont_info->scaled_font);
      wlfont_info->scaled_font = NULL;
    }
}

static void
wlfont_prepare_face (struct frame *f, struct face *face)
{
  struct wlface_info *wlface_info;

#if 0
  /* This doesn't work if face->ascii_face doesn't use an Xft font. */
  if (face != face->ascii_face)
    {
      face->extra = face->ascii_face->extra;
      return;
    }
#endif

  wlface_info = xmalloc (sizeof *wlface_info);
  wlfont_get_colors (f, face, face->gc, NULL,
		     &wlface_info->fg, &wlface_info->bg);
  face->extra = wlface_info;
}

static void
wlfont_done_face (struct frame *f, struct face *face)
{
  struct wlface_info *wlface_info;

#if 0
  /* This doesn't work if face->ascii_face doesn't use an Xft font. */
  if (face != face->ascii_face
      || ! face->extra)
    return;
#endif

  wlface_info = (struct wlface_info *) face->extra;
  if (wlface_info)
    {
      xfree (wlface_info);
      face->extra = NULL;
    }
}

static int
wlfont_has_char (Lisp_Object font, int c)
{
  struct charset *cs = NULL;

  if (EQ (AREF (font, FONT_ADSTYLE_INDEX), Qja)
      && charset_jisx0208 >= 0)
    cs = CHARSET_FROM_ID (charset_jisx0208);
  else if (EQ (AREF (font, FONT_ADSTYLE_INDEX), Qko)
      && charset_ksc5601 >= 0)
    cs = CHARSET_FROM_ID (charset_ksc5601);
  if (cs)
    return (ENCODE_CHAR (cs, c) != CHARSET_INVALID_CODE (cs));

  if (FONT_ENTITY_P (font))
    return ftfont_driver.has_char (font, c);
  else
    {
      struct wlfont_info *wlfont_info
	 = (struct wlfont_info *) XFONT_OBJECT (font);
      FT_Face ft_face;
      FT_UInt code;

      ft_face = cairo_ft_scaled_font_lock_face (wlfont_info->scaled_font);
      code = FT_Get_Char_Index (ft_face, (FT_ULong) c);
      cairo_ft_scaled_font_unlock_face (wlfont_info->scaled_font);

      return code != 0;
    }
}

static unsigned
wlfont_encode_char (struct font *font, int c)
{
  struct wlfont_info *wlfont_info = (struct wlfont_info *) font;
  FT_Face ft_face;
  FT_ULong charcode = c;
  FT_UInt code;

  ft_face = cairo_ft_scaled_font_lock_face (wlfont_info->scaled_font);
  code = FT_Get_Char_Index (ft_face, charcode);
  cairo_ft_scaled_font_unlock_face (wlfont_info->scaled_font);

  return (code > 0 ? code : FONT_INVALID_CODE);
}

static int
wlfont_text_extents (struct font *font, unsigned int *code, int nglyphs, struct font_metrics *metrics)
{
  struct wlfont_info *wlfont_info = (struct wlfont_info *) font;
  cairo_text_extents_t te;
  cairo_t *cr;
  cairo_glyph_t *glyphs;
  int i;

  block_input ();

  cr = cairo_create (wlfont_info->surface);
  cairo_set_scaled_font (cr, wlfont_info->scaled_font);
  glyphs = alloca (sizeof (cairo_glyph_t) * nglyphs);
  for (i = 0; i < nglyphs; i++)
    {
      glyphs[i].x = 0;
      glyphs[i].y = 0;
      glyphs[i].index = code[i];
    }
  cairo_glyph_extents (cr, glyphs, nglyphs, &te);
  cairo_destroy (cr);

  if (metrics)
    {
      metrics->lbearing = te.x_bearing;
      metrics->rbearing = te.x_bearing + te.width;
      metrics->width = te.x_advance;
      metrics->ascent = - te.y_bearing;
      metrics->descent = te.height + te.y_bearing;
    }

  unblock_input ();

  return te.x_advance;
}

static cairo_t *
wlfont_cairo_create (struct frame *f)
{
  cairo_t *cr = font_get_frame_data (f, Qwl);

  if (! cr)
    {
      block_input ();
      cr = cairo_create (f->output_data.wl->cairo_surface);
      unblock_input ();
      eassert (cr != NULL);
      font_put_frame_data (f, Qwl, cr);
    }
  return cr;
}

static int
wlfont_draw (struct glyph_string *s, int from, int to, int x, int y,
	     bool with_background)
{
  struct frame *f = s->f;
  struct face *face = s->face;
  struct wlfont_info *wlfont = (struct wlfont_info *) s->font;
  struct wlface_info *wlface_info = NULL;
  cairo_t *cr;
  cairo_glyph_t *glyphs;
  struct ewl_color fg, bg;
  int len = to - from;
  int i;
  double x_offset;

  block_input ();

  if (s->font == face->font)
    wlface_info = (struct wlface_info *) face->extra;
  wlfont_get_colors (f, face, s->gc, wlface_info,
		     &fg, with_background ? &bg : NULL);

  cr = wlfont_cairo_create (s->f);
  cairo_reset_clip (cr);
  if (s->num_clips > 0)
    for (i = 0; i < s->num_clips; i++)
      {
	cairo_rectangle (cr, s->clip[i].x, s->clip[i].y, s->clip[i].width,
			 s->clip[i].height);
	cairo_clip (cr);
      }

  if (with_background)
    {
      cairo_set_source_rgba (cr, bg.red, bg.green, bg.blue, 1.0);
      cairo_rectangle (cr, x, y - s->font->ascent, s->width, s->font->height);
      cairo_fill (cr);
    }

  cairo_set_source_rgba (cr, fg.red, fg.green, fg.blue, fg.alpha);
  cairo_set_scaled_font (cr, wlfont->scaled_font);
  glyphs = alloca (sizeof (cairo_glyph_t) * len);
  for (i = 0, x_offset = x; i < len; i++)
    {
      cairo_text_extents_t te;

      glyphs[i].x = 0;
      glyphs[i].y = 0;
      glyphs[i].index = ((XCHAR2B_BYTE1 (s->char2b + from + i) << 8)
			 | XCHAR2B_BYTE2 (s->char2b + from + i));
      cairo_glyph_extents (cr, &glyphs[i], 1, &te);

      glyphs[i].x = x_offset + (s->padding_p ? 1 : 0);
      glyphs[i].y = y;

      x_offset += te.x_advance;
    }
  cairo_show_glyphs (cr, glyphs, len);

  unblock_input ();

  return len;
}

static int
wlfont_end_for_frame (struct frame *f)
{
  cairo_t *cr;

  cr = font_get_frame_data (f, Qwl);

  if (cr)
    {
      block_input ();
      cairo_destroy (cr);
      unblock_input ();
      font_put_frame_data (f, Qwl, NULL);
    }
  return 0;
}

void
syms_of_wlfont (void)
{
  DEFSYM (QChinting, ":hinting");
  DEFSYM (QChintstyle, ":hintstyle");

  ascii_printable[0] = 0;

  wlfont_driver = ftfont_driver;
  wlfont_driver.type = Qwl;
  wlfont_driver.list = wlfont_list;
  wlfont_driver.match = wlfont_match;
  wlfont_driver.list_family = NULL;
  wlfont_driver.open = wlfont_open;
  wlfont_driver.close = wlfont_close;
  wlfont_driver.prepare_face = wlfont_prepare_face;
  wlfont_driver.done_face = wlfont_done_face;
  wlfont_driver.has_char = wlfont_has_char;
  wlfont_driver.encode_char = wlfont_encode_char;
  wlfont_driver.text_extents = wlfont_text_extents;
  wlfont_driver.draw = wlfont_draw;
  wlfont_driver.end_for_frame = wlfont_end_for_frame;
  wlfont_driver.cached_font_ok = NULL;
  register_font_driver (&wlfont_driver, NULL);
}
