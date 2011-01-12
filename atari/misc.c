/*
 * Copyright 2010 Ole Loots <ole@monochrom.net>
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

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <windom.h>

#include "desktop/cookies.h"
#include "desktop/mouse.h"
#include "utils/messages.h"
#include "utils/utils.h"
#include "utils/url.h"
#include "utils/log.h"
#include "content/fetch.h"
#include "atari/gui.h"
#include "atari/toolbar.h"
#include "atari/misc.h"

extern void * h_gem_rsrc;

void warn_user(const char *warning, const char *detail)
{
	size_t len = 1 + ((warning != NULL) ? strlen(messages_get(warning)) :
			0) + ((detail != 0) ? strlen(detail) : 0);
	char message[len];
	snprintf(message, len, messages_get(warning), detail);
	printf("%s\n", message);
}

void die(const char *error)
{
	printf("%s\n", error);
	exit(1);
}

bool cookies_update(const char *domain, const struct cookie_data *data)
{
    return true;
}

/**
 * Return the filename part of a full path
 *
 * \param path full path and filename
 * \return filename (will be freed with free())
 */

char *filename_from_path(char *path)
{
	char *leafname;

	leafname = strrchr(path, '\\');
	if (!leafname)
		leafname = path;
	else
		leafname += 1;

	return strdup(leafname);
}

/**
 * Add a path component/filename to an existing path
 *
 * \param path buffer containing path + free space
 * \param length length of buffer "path"
 * \param newpart string containing path component to add to path
 * \return true on success
 */

bool path_add_part(char *path, int length, const char *newpart)
{
	if(path[strlen(path) - 1] != '/')
		strncat(path, "/", length);

	strncat(path, newpart, length);

	return true;
}

#define IS_TOPLEVEL_BROWSER_WIN( gw ) (gw->root->handle == win && gw->parent == NULL )
struct gui_window * find_root_gui_window( WINDOW * win )
{

	int i=0;
	struct gui_window * gw;
	gw = window_list;
	while( gw != NULL ) {
		if( IS_TOPLEVEL_BROWSER_WIN( gw )  ) {
			return( gw );
		}
		else
			gw = gw->next;
		i++;
		assert( i < 1000);
	}
	return( NULL );
}


struct gui_window * find_cmp_window( COMPONENT * c )
{
	struct gui_window * gw;
	int i=0;
	gw = window_list;
	while( gw != NULL ) {
		assert( gw->browser != NULL );
		if( gw->browser->comp == c ) {
			return( gw );
		}
		else
			gw = gw->next;
		i++;
		assert( i < 1000);
	}
	return( NULL );
}


/* -------------------------------------------------------------------------- */
/* GEM Utillity functions:                                                    */
/* -------------------------------------------------------------------------- */

/* Return a string from resource file */
char *get_rsc_string( int idx) {
	char *txt;
	RsrcGaddr( h_gem_rsrc, R_STRING, idx,  &txt );
	return txt;
}

OBJECT *get_tree( int idx) {
  OBJECT *tree;
  RsrcGaddr( h_gem_rsrc, R_TREE, idx, &tree);
  return tree;
}

void gem_set_cursor( MFORM_EX * cursor )
{
	static unsigned char flags = 255;
	static int number = 255; 
	if( flags == cursor->flags && number == cursor->number )
		return;
	if( cursor->flags & MFORM_EX_FLAG_USERFORM ) {
		MouseSprite( cursor->tree, cursor->number);
	} else {
		graf_mouse(cursor->number, NULL );
	}
	number = cursor->number;
	flags = cursor->flags;
}

void dbg_lgrect( char * str, LGRECT * r )
{
	printf("%s: x: %d, y: %d, w: %d, h: %d\n", str, 
		r->g_x, r->g_y, r->g_w, r->g_h );
}

void dbg_grect( char * str, GRECT * r )
{
	printf("%s: x: %d, y: %d, w: %d, h: %d\n", str, 
		r->g_x, r->g_y, r->g_w, r->g_h );
} 

void dbg_pxy( char * str, short * pxy )
{
	printf("%s: x: %d, y: %d, w: %d, h: %d\n", str, 
		pxy[0], pxy[1], pxy[2], pxy[3] );
} 