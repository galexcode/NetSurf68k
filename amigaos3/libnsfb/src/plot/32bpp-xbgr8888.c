/*
 * Copyright 2009 Vincent Sanders <vince@simtec.co.uk>
 * Copyright 2010 Michael Drake <tlsa@netsurf-browser.org>
 *
 * This file is part of libnsfb, http://www.netsurf-browser.org/
 * Licenced under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 */

#include <stdbool.h>
#include <endian.h>
#include <stdlib.h>

#include "libnsfb.h"
#include "libnsfb_plot.h"
#include "libnsfb_plot_util.h"

#include "nsfb.h"
#include "plot.h"

#define BPP 32

#define UNUSED __attribute__((unused)) 

static inline uint32_t *get_xy_loc(nsfb_t *nsfb, int x, int y)
{
        return (void *)(nsfb->ptr + (y * nsfb->linelen) + (x << 2));
}

#if __BYTE_ORDER == __BIG_ENDIAN
static inline nsfb_colour_t pixel_to_colour(UNUSED nsfb_t *nsfb, uint32_t pixel)
{
	/* TODO: FIX */
        return (pixel >> 8) & ~0xFF000000U;
}

/* convert a colour value to a 32bpp pixel value ready for screen output */
static inline uint32_t colour_to_pixel(UNUSED nsfb_t *nsfb, nsfb_colour_t c)
{
	/* TODO: FIX */
        return (c << 8);
}
#ifdef __BIG_ENDIAN_BGRA__
// this funcs are need because bitmaps have diffrent format (rgba)when get from netsurf.
//if your SDL surface work in rgba mode(seem in SDL_SWSURFACE done), you need diffrent funcs.
static inline nsfb_colour_t nsfb_plot_ablend_rgba(nsfb_colour_t pixel,nsfb_colour_t  scrpixel)
{
	  int opacity = pixel & 0xFF;
	  int transp = 0x100 - opacity;
          uint32_t rb, g; 
	  pixel >>= 8;
	  scrpixel >>= 8;
	  rb = ((pixel & 0xFF00FF) * opacity +
          (scrpixel & 0xFF00FF) * transp) >> 8;
          g  = ((pixel & 0x00FF00) * opacity +
          (scrpixel & 0x00FF00) * transp) >> 8;

    return ((rb & 0xFF00FF) | (g & 0xFF00)) << 8; 
}

static inline uint32_t colour_rgba_to_pixel_bgra(UNUSED nsfb_t *nsfb, nsfb_colour_t c)
{
	   
		return (((c & 0xFF00) << 16 ) |
                ((c & 0xFF0000) ) |
                ((c & 0xFF000000) >> 16) );
}
static inline nsfb_colour_t pixel_bgra_to_colour_rgba(UNUSED nsfb_t *nsfb, uint32_t pixel)
{

	return (((pixel & 0xFF000000) >> 16  ) |
                ((pixel & 0xFF0000) >> 0) |
                ((pixel & 0xFF00) << 16));
} 

#endif /*__BIG_ENDIAN_BGRA__*/
#else /* __BYTE_ORDER == __BIG_ENDIAN */
static inline nsfb_colour_t pixel_to_colour(UNUSED nsfb_t *nsfb, uint32_t pixel)
{
        return pixel | 0xFF000000U;
}

/* convert a colour value to a 32bpp pixel value ready for screen output */
static inline uint32_t colour_to_pixel(UNUSED nsfb_t *nsfb, nsfb_colour_t c)
{
        return c;
}
#endif

#define PLOT_TYPE uint32_t
#define PLOT_LINELEN(ll) ((ll) >> 2)

#include "32bpp-common.c"

const nsfb_plotter_fns_t _nsfb_32bpp_xbgr8888_plotters = {
        .line = line,
        .fill = fill,
        .point = point,
        .bitmap = bitmap,
        .glyph8 = glyph8,
        .glyph1 = glyph1,
        .readrect = readrect,
};

/*
 * Local Variables:
 * c-basic-offset:8
 * End:
 */
