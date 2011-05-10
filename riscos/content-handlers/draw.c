/*
 * Copyright 2003 John M Bell <jmb202@ecs.soton.ac.uk>
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/** \file
 * Content for image/x-drawfile (RISC OS implementation).
 *
 * The DrawFile module is used to plot the DrawFile.
 */

#include "utils/config.h"
#ifdef WITH_DRAW

#include <string.h>
#include <stdlib.h>
#include "oslib/drawfile.h"
#include "utils/config.h"
#include "content/content_protected.h"
#include "desktop/plotters.h"
#include "riscos/content-handlers/draw.h"
#include "riscos/gui.h"
#include "utils/log.h"
#include "utils/messages.h"
#include "utils/talloc.h"
#include "utils/utils.h"

typedef struct draw_content {
	struct content base;

	int x0, y0;
} draw_content;

static nserror draw_create(const content_handler *handler,
		lwc_string *imime_type, const http_parameter *params,
		llcache_handle *llcache, const char *fallback_charset,
		bool quirks, struct content **c);
static bool draw_convert(struct content *c);
static void draw_destroy(struct content *c);
static bool draw_redraw(struct content *c, int x, int y,
		int width, int height, const struct rect *clip,
		float scale, colour background_colour,
		bool repeat_x, bool repeat_y);
static nserror draw_clone(const struct content *old, struct content **newc);
static content_type draw_content_type(lwc_string *mime_type);

static const content_handler draw_content_handler = {
	.create = draw_create,
	.data_complete = draw_convert,
	.destroy = draw_destroy,
	.redraw = draw_redraw,
	.clone = draw_clone,
	.type = draw_content_type,
	.no_share = false,
};

static const char *draw_types[] = {
	"application/drawfile",
	"application/x-drawfile",
	"image/drawfile",
	"image/x-drawfile"
};

static lwc_string *draw_mime_types[NOF_ELEMENTS(draw_types)];

nserror draw_init(void)
{
	uint32_t i;
	lwc_error lerror;
	nserror error;

	for (i = 0; i < NOF_ELEMENTS(draw_mime_types); i++) {
		lerror = lwc_intern_string(draw_types[i],
				strlen(draw_types[i]),
				&draw_mime_types[i]);
		if (lerror != lwc_error_ok) {
			error = NSERROR_NOMEM;
			goto error;
		}

		error = content_factory_register_handler(draw_mime_types[i],
				&draw_content_handler);
		if (error != NSERROR_OK)
			goto error;
	}

	return NSERROR_OK;

error:
	draw_fini();

	return error;
}

void draw_fini(void)
{
	uint32_t i;

	for (i = 0; i < NOF_ELEMENTS(draw_mime_types); i++) {
		if (draw_mime_types[i] != NULL)
			lwc_string_unref(draw_mime_types[i]);
	}
}

nserror draw_create(const content_handler *handler,
		lwc_string *imime_type, const http_parameter *params,
		llcache_handle *llcache, const char *fallback_charset,
		bool quirks, struct content **c)
{
	draw_content *draw;
	nserror error;

	draw = talloc_zero(0, draw_content);
	if (draw == NULL)
		return NSERROR_NOMEM;

	error = content__init(&draw->base, handler, imime_type, params,
			llcache, fallback_charset, quirks);
	if (error != NSERROR_OK) {
		talloc_free(draw);
		return error;
	}

	*c = (struct content *) draw;

	return NSERROR_OK;
}

/**
 * Convert a CONTENT_DRAW for display.
 *
 * No conversion is necessary. We merely read the DrawFile dimensions and
 * bounding box bottom-left.
 */

bool draw_convert(struct content *c)
{
	draw_content *draw = (draw_content *) c;
	union content_msg_data msg_data;
	const char *source_data;
	unsigned long source_size;
	const void *data;
	os_box bbox;
	os_error *error;
	char title[100];

	source_data = content__get_source_data(c, &source_size);
	data = source_data;

	/* BBox contents in Draw units (256*OS unit) */
	error = xdrawfile_bbox(0, (drawfile_diagram *) data,
			(int) source_size, 0, &bbox);
	if (error) {
		LOG(("xdrawfile_bbox: 0x%x: %s",
				error->errnum, error->errmess));
		msg_data.error = error->errmess;
		content_broadcast(c, CONTENT_MSG_ERROR, msg_data);
		return false;
	}

	if (bbox.x1 > bbox.x0 && bbox.y1 > bbox.y0) {
		/* c->width & c->height stored as (OS units/2)
		   => divide by 512 to convert from draw units */
		c->width = ((bbox.x1 - bbox.x0) / 512);
		c->height = ((bbox.y1 - bbox.y0) / 512);
	}
	else
		/* invalid/undefined bounding box */
		c->height = c->width = 0;

	draw->x0 = bbox.x0;
	draw->y0 = bbox.y0;
	snprintf(title, sizeof(title), messages_get("DrawTitle"), c->width,
			c->height, source_size);
	content__set_title(c, title);

	content_set_ready(c);
	content_set_done(c);
	/* Done: update status bar */
	content_set_status(c, "");
	return true;
}


/**
 * Destroy a CONTENT_DRAW and free all resources it owns.
 */

void draw_destroy(struct content *c)
{
}


/**
 * Redraw a CONTENT_DRAW.
 */

bool draw_redraw(struct content *c, int x, int y,
		int width, int height, const struct rect *clip,
		float scale, colour background_colour,
		bool repeat_x, bool repeat_y)
{
	draw_content *draw = (draw_content *) c;
	os_trfm matrix;
	const char *source_data;
	unsigned long source_size;
	const void *data;
	os_error *error;

	if (plot.flush && !plot.flush())
		return false;

	if (!c->width || !c->height)
		return false;

	source_data = content__get_source_data(c, &source_size);
	data = source_data;

	/* Scaled image. Transform units (65536*OS units) */
	matrix.entries[0][0] = width * 65536 / c->width;
	matrix.entries[0][1] = 0;
	matrix.entries[1][0] = 0;
	matrix.entries[1][1] = height * 65536 / c->height;
	/* Draw units. (x,y) = bottom left */
	matrix.entries[2][0] = ro_plot_origin_x * 256 + x * 512 -
			draw->x0 * width / c->width;
	matrix.entries[2][1] = ro_plot_origin_y * 256 - (y + height) * 512 -
			draw->y0 * height / c->height;

	error = xdrawfile_render(0, (drawfile_diagram *) data,
			(int) source_size, &matrix, 0, 0);
	if (error) {
		LOG(("xdrawfile_render: 0x%x: %s",
				error->errnum, error->errmess));
		return false;
	}

	return true;
}

/**
 * Clone a CONTENT_DRAW
 */

nserror draw_clone(const struct content *old, struct content **newc)
{
	draw_content *draw;
	nserror error;

	draw = talloc_zero(0, draw_content);
	if (draw == NULL)
		return NSERROR_NOMEM;

	error = content__clone(old, &draw->base);
	if (error != NSERROR_OK) {
		content_destroy(&draw->base);
		return error;
	}

	/* Simply rerun convert */
	if (old->status == CONTENT_STATUS_READY ||
			old->status == CONTENT_STATUS_DONE) {
		if (draw_convert(&draw->base) == false) {
			content_destroy(&draw->base);
			return NSERROR_CLONE_FAILED;
		}
	}

	*newc = (struct content *) draw;

	return NSERROR_OK;
}

content_type draw_content_type(lwc_string *mime_type)
{
	return CONTENT_IMAGE;
}

#endif