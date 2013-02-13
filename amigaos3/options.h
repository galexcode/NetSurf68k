/*
 * Copyright 2008, 2010 Daniel Silverstone <dsilvers@netsurf-browser.org>
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

#ifndef _NETSURF_DESKTOP_OPTIONS_INCLUDING_
#error "Frontend options header cannot be included directly"
#endif

#ifndef _NETSURF_FRAMEBUFFER_OPTIONS_H_
#define _NETSURF_FRAMEBUFFER_OPTIONS_H_

#define NSOPTION_EXTRA_DEFINE						\
	/* surface options */						\
	int fb_depth;							\
	int fb_refresh;							\
	char *fb_device;						\
	char *fb_input_devpath;						\
	char *fb_input_glob;						\
									\
	/* toolkit options */						\
	int fb_furniture_size; /**< toolkit furniture size */		\
	int fb_toolbar_size; /**< toolbar furniture size */		\
	char *fb_toolbar_layout; /**< toolbar layout */			\
	bool fb_osk; /**< enable on screen keyboard */			\
									\
	/* font options */						\
	bool fb_font_monochrome; /**< render font monochrome */		\
	int fb_font_cachesize; /**< size of font glyph cache in kilobytes. */ \
									\
	char *fb_face_sans_serif; /**< default sans face */		\
	char *fb_face_sans_serif_bold; /**< bold sans face */		\
	char *fb_face_sans_serif_italic; /**< bold sans face */		\
	char *fb_face_sans_serif_italic_bold; /**< bold sans face */	\
									\
	char *fb_face_serif; /**< serif face */				\
	char *fb_face_serif_bold; /**< bold serif face */		\
									\
	char *fb_face_monospace; /**< monospace face */			\
	char *fb_face_monospace_bold; /**< bold monospace face */	\
									\
	char *fb_face_cursive; /**< cursive face */			\
	char *fb_face_fantasy; /**< fantasy face */		\
	char *lastpage_url;							\
	char *favourite_1_url;						\
	char *favourite_2_url;						 \
	char *favourite_3_url;    \
	char *favourite_4_url;    \
	char *favourite_5_url;    \
	char *favourite_6_url;    \
	char *favourite_7_url;    \
	char *favourite_8_url;    \
	char *favourite_9_url;    \
	char *favourite_10_url;    \
	char *favourite_11_url;    \
	char *favourite_12_url;    \
	char *favourite_1_label;    \
	char *favourite_2_label;    \
	char *favourite_3_label;    \
	char *favourite_4_label;    \
	char *favourite_5_label;    \
	char *favourite_6_label;    \
	char *favourite_7_label;    \
	char *favourite_8_label;    \
	char *favourite_9_label;    \
	char *favourite_10_label;    \
	char *favourite_11_label;    \
	char *favourite_12_label;    \
	char *download_path;    \
	char *def_search_bar;    \
	char *theme;		\
	char *text_editor;		\
	int fullscreen;		\
	int gv_action
	
#define NSOPTION_EXTRA_DEFAULTS				\
	.fb_depth = 32,					\
	.fb_refresh = 70,				\
	.fb_device = NULL,				\
	.fb_input_devpath = NULL,			\
	.fb_input_glob = NULL,				\
	.fb_furniture_size = 18,			\
	.fb_toolbar_size = 58,				\
	.fb_toolbar_layout = NULL,				\
	.fb_osk = false,				\
	.fb_font_monochrome = false,			\
	.fb_font_cachesize = 2048,			\
	.fb_face_sans_serif = NULL,			\
	.fb_face_sans_serif_bold = NULL,		\
	.fb_face_sans_serif_italic = NULL,		\
	.fb_face_sans_serif_italic_bold = NULL,		\
	.fb_face_serif = NULL,				\
	.fb_face_serif_bold = NULL,			\
	.fb_face_monospace = NULL,			\
	.fb_face_monospace_bold = NULL,			\
	.fb_face_cursive = NULL,			\
	.fb_face_fantasy = NULL,		\
	.theme = NULL,		\
	.text_editor = NULL,		\
	.fullscreen = 0,	\
	.gv_action = 1

	
#define NSOPTION_EXTRA_TABLE						\
	{ "fb_depth", OPTION_INTEGER, &nsoptions.fb_depth },		\
	{ "fb_refresh", OPTION_INTEGER, &nsoptions.fb_refresh },	\
	{ "fb_device", OPTION_STRING, &nsoptions.fb_device },		\
	{ "fb_input_devpath", OPTION_STRING, &nsoptions.fb_input_devpath }, \
	{ "fb_input_glob", OPTION_STRING, &nsoptions.fb_input_glob },	\
	{ "fb_furniture_size", OPTION_INTEGER, &nsoptions.fb_furniture_size }, \
	{ "fb_toolbar_size", OPTION_INTEGER, &nsoptions.fb_toolbar_size }, \
	{ "fb_toolbar_layout", OPTION_STRING, &nsoptions.fb_toolbar_layout }, \
	{ "fb_osk", OPTION_BOOL, &nsoptions.fb_osk },			\
	{ "fb_font_monochrome", OPTION_BOOL, &nsoptions.fb_font_monochrome }, \
	{ "fb_font_cachesize", OPTION_INTEGER, &nsoptions.fb_font_cachesize }, \
	{ "fb_face_sans_serif", OPTION_STRING, &nsoptions.fb_face_sans_serif }, \
	{ "fb_face_sans_serif_bold", OPTION_STRING, &nsoptions.fb_face_sans_serif_bold }, \
	{ "fb_face_sans_serif_italic", OPTION_STRING, &nsoptions.fb_face_sans_serif_italic }, \
	{ "fb_face_sans_serif_italic_bold", OPTION_STRING, &nsoptions.fb_face_sans_serif_italic_bold }, \
	{ "fb_face_serif", OPTION_STRING, &nsoptions.fb_face_serif },	\
	{ "fb_serif_bold", OPTION_STRING, &nsoptions.fb_face_serif_bold }, \
	{ "fb_face_monospace", OPTION_STRING, &nsoptions.fb_face_monospace }, \
	{ "fb_face_monospace_bold", OPTION_STRING, &nsoptions.fb_face_monospace_bold }, \
	{ "fb_face_cursive", OPTION_STRING, &nsoptions.fb_face_cursive }, \
	{ "fb_face_fantasy", OPTION_STRING, &nsoptions.fb_face_fantasy }, \
	{ "lastpage_url",	OPTION_STRING,	&nsoptions.lastpage_url }, \
	{ "favourite_1_url",	OPTION_STRING,	&nsoptions.favourite_1_url }, \
	{ "favourite_1_label",	OPTION_STRING,	&nsoptions.favourite_1_label }, \
	{ "favourite_2_url",	OPTION_STRING,	&nsoptions.favourite_2_url }, \
	{ "favourite_2_label",	OPTION_STRING,	&nsoptions.favourite_2_label }, \
	{ "favourite_3_url",	OPTION_STRING,	&nsoptions.favourite_3_url }, \
	{ "favourite_3_label",	OPTION_STRING,	&nsoptions.favourite_3_label }, \
	{ "favourite_4_url",	OPTION_STRING,	&nsoptions.favourite_4_url }, \
	{ "favourite_4_label",	OPTION_STRING,	&nsoptions.favourite_4_label }, \
	{ "favourite_5_url",	OPTION_STRING,	&nsoptions.favourite_5_url }, \
	{ "favourite_5_label",	OPTION_STRING,	&nsoptions.favourite_5_label }, \
	{ "favourite_6_url",	OPTION_STRING,	&nsoptions.favourite_6_url }, \
	{ "favourite_6_label",	OPTION_STRING,	&nsoptions.favourite_6_label }, \
	{ "favourite_7_url",	OPTION_STRING,	&nsoptions.favourite_7_url }, \
	{ "favourite_7_label",	OPTION_STRING,	&nsoptions.favourite_7_label }, \
	{ "favourite_8_url",	OPTION_STRING,	&nsoptions.favourite_8_url }, \
	{ "favourite_8_label",	OPTION_STRING,	&nsoptions.favourite_8_label }, \
	{ "favourite_9_url",	OPTION_STRING,	&nsoptions.favourite_9_url }, \
	{ "favourite_9_label",	OPTION_STRING,	&nsoptions.favourite_9_label }, \
	{ "favourite_10_url",	OPTION_STRING,	&nsoptions.favourite_10_url }, \
	{ "favourite_10_label",	OPTION_STRING,	&nsoptions.favourite_10_label }, \
	{ "favourite_11_url",	OPTION_STRING,	&nsoptions.favourite_11_url }, \
	{ "favourite_11_label",	OPTION_STRING,	&nsoptions.favourite_11_label }, \
	{ "favourite_12_url",	OPTION_STRING,	&nsoptions.favourite_12_url }, \
	{ "favourite_12_label",	OPTION_STRING,	&nsoptions.favourite_12_label }, \
	{ "download_path",	OPTION_STRING,	&nsoptions.download_path }, \
	{ "def_search_bar",	OPTION_STRING, &nsoptions.def_search_bar }, \
	{ "text_editor", 	OPTION_STRING, &nsoptions.text_editor }, \
	{ "theme", 			OPTION_STRING, &nsoptions.theme },\
	{ "fullscreen",	OPTION_INTEGER, &nsoptions.fullscreen}, \
	{ "gv_action",	OPTION_INTEGER, &nsoptions.gv_action }
#endif

/*
 * Local Variables:
 * c-basic-offset:8
 * End:
 */
