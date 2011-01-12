/*
 * Copyright 2010 <ole@monochrom.net>
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

 /*
 	This File provides all the mandatory functions prefixed with gui_
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <limits.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <windom.h>
#include <cflib.h>
#include <hubbub/hubbub.h>

#include "content/urldb.h"
#include "content/fetch.h"
#include "css/utils.h"
#include "desktop/gui.h"
#include "desktop/history_core.h"
#include "desktop/plotters.h"
#include "desktop/netsurf.h"
#include "desktop/401login.h"

#include "desktop/options.h"
#include "desktop/save_complete.h"
#include "desktop/selection.h"
#include "desktop/textinput.h"
#include "desktop/browser.h"
#include "desktop/mouse.h"
#include "render/html.h"
#include "render/font.h"
#include "utils/url.h"
#include "utils/log.h"
#include "utils/messages.h"
#include "utils/utils.h"

#include "atari/gui.h"
#include "atari/options.h"
#include "atari/misc.h"
#include "atari/findfile.h"
#include "atari/schedule.h"
#include "atari/browser_win.h"
#include "atari/browser.h"
#include "atari/statusbar.h"
#include "atari/toolbar.h"
#include "atari/verify_ssl.h"
#include "atari/hotlist.h"
#include "atari/login.h"
#include "atari/global_evnt.h"
#include "atari/font.h"
#include "atari/res/netsurf.rsh"
#include "atari/plot.h"
#include "atari/clipboard.h"

#define TODO() printf("%s Unimplemented!\n", __FUNCTION__)

char *default_stylesheet_url;
char *adblock_stylesheet_url;
char *quirks_stylesheet_url;
char *options_file_location;
char *tmp_clipboard;
struct gui_window *input_window = NULL;
struct gui_window *window_list = NULL;
void * h_gem_rsrc;
OBJECT * 	h_gem_menu;
OBJECT **rsc_trindex;
short vdih;
short rsc_ntree;
static clock_t last_multi_task;
int mouse_click_time[3];
int mouse_hold_start[3];
browser_mouse_state bmstate;
bool lck_multi = false;

/* Comandline / Options: */
int cfg_width;
int cfg_height;

const char * cfg_homepage_url;

extern GEM_PLOTTER plotter;

void gui_multitask(void)
{
	short winloc[4];
	int flags = MU_MESAG | MU_KEYBD | MU_BUTTON | MU_TIMER;
	if( ((clock() * 1000 / CLOCKS_PER_SEC) - last_multi_task ) < 50 || lck_multi == true   ) {
		return;
	}
	/* 	todo: instead of time, use dumm window message here, 
		timer takes at least 10ms, WMs take about 1ms!
	*/
	evnt.timer = 1;
	if(input_window) {
		graf_mkstate( &prev_inp_state.mx, &prev_inp_state.my,
				&prev_inp_state.mbut, &prev_inp_state.mkstat );
		wind_get(input_window->root->handle->handle, WF_WORKXYWH, &winloc[0],
			&winloc[1], &winloc[2], &winloc[3] );
		flags |= MU_M1;
		if( prev_inp_state.mx >= winloc[0] && prev_inp_state.mx <= winloc[0] + winloc[2] &&
				prev_inp_state.my >= winloc[1] && prev_inp_state.my <= winloc[1] + winloc[3] ){
			/* if the cursor is within workarea, capture an movement WITHIN: */
			evnt.m1_flag = MO_LEAVE;
			evnt.m1_w = 2;
			evnt.m1_h = 2;
			evnt.m1_x = prev_inp_state.mx;
			evnt.m1_y = prev_inp_state.my;
		} else {
			/* otherwise capture mouse move INTO the work area: */
			evnt.m1_flag = MO_ENTER;
			evnt.m1_w = winloc[2];
			evnt.m1_h = winloc[3];
			evnt.m1_x = winloc[0];
			evnt.m1_y = winloc[1];
		}
	}
	EvntWindom( flags );
	if( MOUSE_IS_DRAGGING() )
		global_track_mouse_state();
	last_multi_task = clock()*1000 / CLOCKS_PER_SEC;
}
void gui_poll(bool active)
{
	short winloc[4];
	int timeout = 50; /* timeout in milliseconds */
	int flags = MU_MESAG | MU_KEYBD | MU_BUTTON ;
	timeout = schedule_run();
	if ( active )
		timeout = 1;

	if(input_window) {
		flags |= MU_M1;
		graf_mkstate( &prev_inp_state.mx, &prev_inp_state.my,
				&prev_inp_state.mbut, &prev_inp_state.mkstat );
		wind_get(input_window->root->handle->handle, WF_WORKXYWH, &winloc[0],
			&winloc[1], &winloc[2], &winloc[3] );
		if( prev_inp_state.mx >= winloc[0] && prev_inp_state.mx <= winloc[0] + winloc[2] &&
				prev_inp_state.my >= winloc[1] && prev_inp_state.my <= winloc[1] + winloc[3] ){
			evnt.m1_flag = MO_LEAVE;
			evnt.m1_w = 2;
			evnt.m1_h = 2;
			evnt.m1_x = prev_inp_state.mx;
			evnt.m1_y = prev_inp_state.my;
		} else {
			evnt.m1_flag = MO_ENTER;
			evnt.m1_w = winloc[2];
			evnt.m1_h = winloc[3];
			evnt.m1_x = winloc[0];
			evnt.m1_y = winloc[1];
		}
		/* if we have some state that can't be recognized by evnt_multi, don't block
			so tracking can take place after timeout: */
		if( MOUSE_IS_DRAGGING() )
			timeout = 1;
	}

	if( timeout >= 0 ) {
		flags |= MU_TIMER;
		evnt.timer = timeout;
	}
	lck_multi = true;
	EvntWindom( flags );
	lck_multi = false;
	if( MOUSE_IS_DRAGGING() )
		global_track_mouse_state();
	last_multi_task = clock()*1000 / CLOCKS_PER_SEC;
	struct gui_window * g;
	for( g = window_list; g; g=g->next ) {
		if( browser_redraw_required( g ) )
			browser_redraw( g );
	}
}

struct gui_window *
gui_create_browser_window(struct browser_window *bw,
			  struct browser_window *clone,
			  bool new_tab)
{
	struct gui_window *gw=NULL;
	struct gui_window * gwroot ;
	short winloc[4];
	LOG(( "gw: %p, BW: %p, clone %p, tab: %d, type: %d\n" , gw,  bw, clone, 
		(int)new_tab, bw->browser_window_type 
	));

	gw = malloc( sizeof(struct gui_window) );
	if (gw == NULL)
		return NULL;
	memset( gw, 0, sizeof(struct gui_window) );

	switch(bw->browser_window_type) {
		case BROWSER_WINDOW_NORMAL:

			LOG(("normal browser window: %p\n", gw));
			window_create(gw, bw, WIDGET_STATUSBAR|WIDGET_TOOLBAR );
			if( gw->root->handle ) {
				window_open( gw );
				/* Recalculate windows browser area now */
				browser_update_rects( gw );
				tb_update_buttons( gw );
				input_window = gw;
				/* TODO:... this line: placeholder to create a local history widget ... */
			}
		break;

		case BROWSER_WINDOW_FRAME:
			gwroot = bw->parent->window;
			LOG(("create frame: %p, clone: %p\n", bw, clone));
			gw->parent = gwroot;
			gw->root = gwroot->root;
			gw->browser = browser_create( gw, bw, clone, BT_FRAME, CLT_VERTICAL, 1, 1);
			/*browser_attach_frame( gwroot, gw );*/
		break;

		case BROWSER_WINDOW_FRAMESET:
			LOG(("frameset: %p, clone: %p\n", bw, clone));
			gwroot = bw->parent->window;
			gw->parent = gwroot;
			gw->root = gwroot->root;
			gw->browser = browser_create( gw, bw, clone, BT_FRAME, CLT_VERTICAL, 1, 1);
			/*browser_attach_frame( gwroot, gw );*/
		break;

		case BROWSER_WINDOW_IFRAME:
			LOG(("iframe: %p, clone: %p\n", bw, clone));
			/* just dummy code here! */
			gwroot = bw->parent->window;
			gw->parent = gwroot;
			gw->root = bw->parent->window->root;
			gw->browser = browser_create( gw, bw, NULL, BT_FRAME, CLT_VERTICAL, 1, 1);
			/*browser_attach_frame( gwroot, gw );*/
		break;

		default:
			LOG(("unhandled type!"));
	}

	/* add the window to the window list: */
	if( window_list == NULL ) {
		window_list = gw;
		gw->next = NULL;
		gw->prev = NULL;
	} else {
		struct gui_window * tmp = window_list;
		while( tmp->next != NULL ) {
			tmp = tmp->next;
		}
		tmp->next = gw;
		gw->prev = tmp;
		gw->next = NULL;
	}

	return( gw );

}

void gui_window_destroy(struct gui_window *w)
{
	if (w == NULL)
		return;

	LOG(("%s\n", __FUNCTION__ ));

	input_window = NULL;

	LGRECT dbg;
	struct gui_window * root = browser_find_root( w );
	browser_get_rect( root, BR_CONTENT, &dbg );
	switch(w->browser->bw->browser_window_type) {
		case BROWSER_WINDOW_NORMAL:
			window_destroy( w );
			break;
		case BROWSER_WINDOW_FRAME:
			browser_destroy( w->browser );
			break;

		case BROWSER_WINDOW_FRAMESET:
			browser_destroy( w->browser );
			break;

		case BROWSER_WINDOW_IFRAME:
			/* just dummy code here: */
			window_destroy( w );
			break;

		default:
			LOG(("Unhandled type!"));
			assert( 1 == 0 );
			break;
	}
	/* unlink the window: */
	if(w->prev != NULL ) {
		w->prev->next = w->next;
	} else {
		window_list = w->next;
	}
	if( w->next != NULL ) {
		w->next->prev = w->prev;
	}
	if( input_window == w ) {
		input_window = window_list;
	}
	free(w);
	w = NULL;
}

void gui_window_get_dimensions(struct gui_window *w, int *width, int *height,
			       bool scaled)
{
	if (w == NULL)
		return;
	LGRECT rect;
	browser_get_rect( w, BR_CONTENT, &rect  );
	*width = rect.g_w;
	*height = rect.g_h;
}

void gui_window_set_title(struct gui_window *gw, const char *title)
{
	if (gw == NULL)
		return;
	WindSetStr( gw->root->handle, WF_NAME, (char *)title );
}

/**
 * set the status bar message
 */
void gui_window_set_status(struct gui_window *w, const char *text)
{
	if (w == NULL)
		return;
	sb_set_text( w , (char*)text );
}

void gui_window_redraw(struct gui_window *gw, int x0, int y0, int x1, int y1)
{
	if (gw == NULL)
		return;
	int w,h;
	w = x1 - x0;
	h = y1 - y0;
	browser_schedule_redraw( gw, x0, y0, x1, y1 );
}

void gui_window_redraw_window(struct gui_window *gw)
{
	CMP_BROWSER b;
	LGRECT rect;
	if (gw == NULL)
		return;
	b = gw->browser;
	browser_get_rect( gw, BR_CONTENT, &rect );
	browser_schedule_redraw( gw, 0, 0, rect.g_w, rect.g_h );
}

void gui_window_update_box(struct gui_window *gw,
			   const union content_msg_data *data)
{
	CMP_BROWSER b;
	LGRECT cmprect;
	if (gw == NULL)
		return;
	b = gw->browser;

	/* the box values are actually floats */
	int x0 = data->redraw.x - b->scroll.current.x;
	int y0 = data->redraw.y - b->scroll.current.y;
	int x1 = x0 + data->redraw.width;
	int y1 = y0 + data->redraw.height;

	browser_schedule_redraw( gw, x0, y0, x1, y1 );
}

bool gui_window_get_scroll(struct gui_window *w, int *sx, int *sy)
{
	if (w == NULL)
		return false;
	*sx = w->browser->scroll.current.x;
	*sy = w->browser->scroll.current.y;
	return( true );
}

void gui_window_set_scroll(struct gui_window *w, int sx, int sy)
{
	if ((w == NULL) ||
	    (w->browser->bw == NULL) ||
	    (w->browser->bw->current_content == NULL))
		return;

	if( sx != 0 ) {
		if( sx < 0 ) {
			browser_scroll(w, WA_LFLINE, abs(sx), true );
		} else {
			browser_scroll(w, WA_RTLINE, abs(sx), true );
		}
	}

	if( sy != 0 ) {
		if( sy < 0) {
			browser_scroll(w, WA_UPLINE, abs(sy), true );
		} else {
			browser_scroll(w, WA_DNLINE, abs(sy), true );
		}
	}
	return; 

}

void gui_window_scroll_visible(struct gui_window *w, int x0, int y0, int x1, int y1)
{
	LOG(("%s:(%p, %d, %d, %d, %d)", __func__, w, x0, y0, x1, y1));
}

void gui_window_position_frame(struct gui_window *gw, int x0, int y0, int x1, int y1)
{
	TODO();
	struct browser_window * bw = gw->browser->bw;
	LGRECT pardim;
	int width = x1 - x0 + 2, height = y1 - y0 + 2;
	/* get available width/height: */
	if( gw->parent ) {
		browser_get_rect( gw->parent, BR_CONTENT, &pardim );
		LOG(("posframe %s: x0,y0: %d/%d, x1,y1: %d/%d, w: %d, h: %d \n",gw->browser->bw->name, x0,y0, x1,y1, width, height));
	}
}


/* It seems this method is called when content size got adjusted,
	so that we can adjust scroll info. We also have to call it when tab
	change occurs.
*/
void gui_window_update_extent(struct gui_window *gw)
{
	int oldx, oldy;
	oldx = gw->browser->scroll.current.x;
	oldy = gw->browser->scroll.current.y;
	if( gw->browser->bw->current_content != NULL ) {
		/*printf("update_extent %p (\"%s\"), c_w: %d, c_h: %d, scale: %f\n",
			gw->browser->bw,gw->browser->bw->name,
			content_get_width(gw->browser->bw->current_content),
			content_get_height(gw->browser->bw->current_content),
			gw->browser->bw->scale
		);*/
		browser_set_content_size( gw, 
			content_get_width(gw->browser->bw->current_content), 
			content_get_height(gw->browser->bw->current_content) 
		);
	}
}


void gui_clear_selection(struct gui_window *g)
{
	
}



/**
 * set the pointer shape
 */
void gui_window_set_pointer(struct gui_window *w, gui_pointer_shape shape)
{
	if (w == NULL)
		return;
	switch (shape) {
	case GUI_POINTER_POINT: /* link */
		gem_set_cursor(&gem_cursors.hand);
		break;

	case GUI_POINTER_MENU:
		gem_set_cursor(&gem_cursors.menu);
		break;

	case GUI_POINTER_CARET: /* input */
		gem_set_cursor(&gem_cursors.ibeam);
		break;

	case GUI_POINTER_CROSS:
		gem_set_cursor(&gem_cursors.cross);
		break;

	case GUI_POINTER_MOVE:
		gem_set_cursor(&gem_cursors.sizeall);
		break;

	case GUI_POINTER_RIGHT:
	case GUI_POINTER_LEFT:
		gem_set_cursor(&gem_cursors.sizewe);
		break;

	case GUI_POINTER_UP:
	case GUI_POINTER_DOWN:
		gem_set_cursor(&gem_cursors.sizens);
		break;

	case GUI_POINTER_RU:
	case GUI_POINTER_LD:
		gem_set_cursor(&gem_cursors.sizenesw);
		break;

	case GUI_POINTER_RD:
	case GUI_POINTER_LU:
		gem_set_cursor(&gem_cursors.sizenwse);
		break;

	case GUI_POINTER_WAIT:
		gem_set_cursor(&gem_cursors.wait);
		break;

	case GUI_POINTER_PROGRESS:
		gem_set_cursor(&gem_cursors.appstarting);
		break;

	case GUI_POINTER_NO_DROP:
		gem_set_cursor(&gem_cursors.nodrop);
		break;

	case GUI_POINTER_NOT_ALLOWED:
		gem_set_cursor(&gem_cursors.deny);
		break;

	case GUI_POINTER_HELP:
		gem_set_cursor(&gem_cursors.help);
		break;

	default:
		gem_set_cursor(&gem_cursors.arrow);
		break;
	}
}

void gui_window_hide_pointer(struct gui_window *w)
{
	TODO();
}

void gui_window_set_url(struct gui_window *w, const char *url)
{
	if (w == NULL)
		return;
	tb_url_set(w, (char*)url );
}

static void throbber_advance( void * data )
{
	LGRECT work;
	struct gui_window * gw = (struct gui_window *)data;
	if( gw->root->toolbar->throbber.running == false )
		return;
	mt_CompGetLGrect(&app, gw->root->toolbar->throbber.comp,
						WF_WORKXYWH, &work);
	gw->root->toolbar->throbber.index++;
	if( gw->root->toolbar->throbber.index > gw->root->toolbar->throbber.max_index )
		gw->root->toolbar->throbber.index = 0;
	schedule(25, throbber_advance, gw );
	ApplWrite( _AESapid, WM_REDRAW,  gw->root->handle->handle,
		work.g_x, work.g_y, work.g_w, work.g_h );
}

void gui_window_start_throbber(struct gui_window *w)
{
	LGRECT work;
	if (w == NULL)
		return;
	mt_CompGetLGrect(&app, w->root->toolbar->throbber.comp,
						WF_WORKXYWH, &work);
	w->root->toolbar->throbber.running = true;
	w->root->toolbar->throbber.index = 0;
	schedule(25, throbber_advance, w );
	ApplWrite( _AESapid, WM_REDRAW,  w->root->handle->handle,
		work.g_x, work.g_y, work.g_w, work.g_h );
}

void gui_window_stop_throbber(struct gui_window *w)
{
	LGRECT work;
	if (w == NULL)
		return;
	mt_CompGetLGrect(&app, w->root->toolbar->throbber.comp,
						WF_WORKXYWH, &work);
	w->root->toolbar->throbber.running = false;
	ApplWrite( _AESapid, WM_REDRAW,  w->root->handle->handle,
		work.g_x, work.g_y, work.g_w, work.g_h );
}

/* Place caret in window */
void gui_window_place_caret(struct gui_window *w, int x, int y, int height)
{
	LGRECT work;
	if (w == NULL)
		return;
	if( w->browser->caret.current.g_w > 0 ) 
		gui_window_remove_caret( w );
	w->browser->caret.requested.g_x = x;
	w->browser->caret.requested.g_y = y;
	w->browser->caret.requested.g_w = 2;
	w->browser->caret.requested.g_h = height;
	w->browser->caret.redraw = true;
	browser_schedule_redraw( w, x, y, x+2, y + height );
	return;
}


/**
 * clear window caret
 */
void
gui_window_remove_caret(struct gui_window *w)
{
	if (w == NULL)
		return;
	w->browser->caret.requested.g_w = 0;
	w->browser->caret.redraw = true;
	browser_schedule_redraw( w,
		w->browser->caret.current.g_x,
		w->browser->caret.current.g_y,
		w->browser->caret.current.g_x + w->browser->caret.current.g_w + 1,
		w->browser->caret.current.g_y + w->browser->caret.current.g_h + 1 
	);
}

void
gui_window_set_icon(struct gui_window *g, hlcache_handle *icon)
{
	TODO();
}

void
gui_window_set_search_ico(hlcache_handle *ico)
{
	TODO();
}

bool
save_complete_gui_save(const char *path,
		       const char *filename,
		       size_t len,
		       const char *sourcedata,
		       content_type type)
{
	TODO();
	return false;
}

int
save_complete_htmlSaveFileFormat(const char *path,
				 const char *filename,
				 xmlDocPtr cur,
				 const char *encoding,
				 int format)
{
	TODO();
	return 0;
}


void gui_window_new_content(struct gui_window *w)
{
	w->browser->scroll.current.x = 0;
	w->browser->scroll.current.y = 0;
	/* update scrollers? */
}

bool gui_window_scroll_start(struct gui_window *w)
{
	TODO();
	return true;
}

bool gui_window_box_scroll_start(struct gui_window *w,
				 int x0, int y0, int x1, int y1)
{
	TODO();
	return true;
}

bool gui_window_frame_resize_start(struct gui_window *w)
{
	TODO();
	return true;
}

void gui_window_save_link(struct gui_window *g, const char *url,
		const char *title)
{
	TODO();
}

void gui_window_set_scale(struct gui_window *w, float scale)
{
	if (w == NULL)
		return;
	w->browser->bw->scale = scale;
	LOG(("%.2f\n", scale));
	/* TODO schedule redraw */
}

void gui_drag_save_object(gui_save_type type, hlcache_handle *c,
			  struct gui_window *w)
{
	TODO();
}

void gui_drag_save_selection(struct selection *s, struct gui_window *w)
{
	TODO();
}

void gui_start_selection(struct gui_window *w)
{
	gui_empty_clipboard();
}

void gui_paste_from_clipboard(struct gui_window *w, int x, int y)
{
	char * clip = scrap_txt_read( &app );
	if( clip == NULL )
		return;
	int clip_length = strlen( clip );
	if (clip_length > 0) {
		char *utf8;
		utf8_convert_ret ret;
		/* Clipboard is in local encoding so
		 * convert to UTF8 */
		ret = local_encoding_to_utf8(clip,
				clip_length, &utf8);
		if (ret == UTF8_CONVERT_OK) {
			browser_window_paste_text(w->browser->bw, utf8,
					strlen(utf8), true);
			free(utf8);
		}
		free( clip );
	}
}

bool gui_empty_clipboard(void)
{
	if( tmp_clipboard != NULL ){
		free( tmp_clipboard );
		tmp_clipboard = NULL;
	}
	return true;
}

bool gui_add_to_clipboard(const char *text_utf8, size_t length_utf8, bool space)
{
	LOG(("(%s): %s (%d)\n", (space)?"space":"", (char*)text_utf8, (int)length_utf8));
	char * oldptr = tmp_clipboard;
	size_t oldlen = 0;
	size_t newlen = 0;
	char * text = NULL;
	char * text2 = NULL;
	bool retval; 
	int length = 0;
	if( length_utf8 > 0 && text_utf8 != NULL ) {
		utf8_to_local_encoding(text_utf8,length_utf8,&text);
		if( text == NULL ) {
			LOG(("Conversion failed (%s)", text_utf8));
			goto error;
		} else {
			text2 = text;
		}
	} else {
		if( space == false ) {
			goto success;
		}
		text = malloc(length + 2);
		if( text == NULL ) {
			goto error;
		} 
		text2 = text;
		text[length+1] = 0;
		memset(text, ' ', length+1);
	}
	length = strlen(text);
	if( tmp_clipboard != NULL ) {
		oldlen = strlen( tmp_clipboard );
	} 
	newlen = oldlen + length + 1;
	if( tmp_clipboard == NULL){
		tmp_clipboard = malloc(newlen);
		if( tmp_clipboard == NULL ) {
			goto error;
		}
		strncpy(tmp_clipboard, text, newlen);
	} else {
		tmp_clipboard = realloc( tmp_clipboard, newlen);
		if( tmp_clipboard == NULL ) {
			goto error;
		}
		strncpy(tmp_clipboard, oldptr, newlen);
		strncat(tmp_clipboard, text, newlen-oldlen);
	}
	goto success;

error:
	retval = false;
	goto fin;

success:
	retval = true;

fin:
	if( text2 != NULL ) 
		free( text2 );
	return(retval);

}

bool gui_commit_clipboard(void)
{
	int r = scrap_txt_write(&app, tmp_clipboard);
	return( (r>0)?true:false );	
}


static bool
gui_selection_traverse_handler(const char *text,
			       size_t length,
			       struct box *box,
			       void *handle,
			       const char *space_text,
			       size_t space_length)
{

	if (space_text) {
		if (!gui_add_to_clipboard(space_text, space_length, false)) {
			return false;
		}
	}

	if (!gui_add_to_clipboard(text, length, box->space))
		return false;

	return true;
}

bool gui_copy_to_clipboard(struct selection *s)
{
	bool ret = false;
	if( (s->defined) && (s->bw != NULL) && (s->bw->window != NULL) &&
		(s->bw->window->root != NULL )) {
		gui_empty_clipboard();
		if(selection_traverse(s, gui_selection_traverse_handler, NULL)){
			ret = gui_commit_clipboard();
		}
	} 
	gui_empty_clipboard();		
	return ret;
}


void gui_create_form_select_menu(struct browser_window *bw,
				 struct form_control *control)
{
	TODO();
}

/**
 * Broadcast an URL that we can't handle.
 */
void gui_launch_url(const char *url)
{
	TODO();
	LOG(("launch file: %s\n", url));
}

void gui_401login_open(const char *url,	const char *realm,
		nserror (*cb)(bool proceed, void *pw), void *cbpw)
{
	bool bres;
	char * out = NULL;
	bres = login_form_do( (char*)url, (char*)realm, &out  );
	if( bres ) {
		LOG(("url: %s, realm: %s, auth: %s\n", url, realm, out ));
		urldb_set_auth_details(url, realm, out );
	}
	if( out != NULL ){
		free( out );
	}
	if( cb != NULL )
		cb(bres, cbpw);
}

void gui_cert_verify(const char *url, const struct ssl_cert_info *certs,
		unsigned long num,
		nserror (*cb)(bool proceed, void *pw), void *cbpw)
{
	LOG((""));
	
	bool bres;
	/*bres = verify_ssl_form_do(url, certs, num);
	if( bres )
		urldb_set_cert_permissions(url, true);
	*/
	int b = form_alert(1, "[2][SSL Verify failed, continue?][Continue|Abort]");
	bres = (b==1)? true : false;
	LOG(("Trust: %d", bres ));
	urldb_set_cert_permissions(url, bres);
	cb(bres, cbpw);
}


static void *myrealloc(void *ptr, size_t len, void *pw)
{
	return realloc(ptr, len);
}

void gui_quit(void)
{
	LOG((""));

	struct gui_window * gw = window_list;
	struct gui_window * tmp = window_list;
	
	while( gw ) {
		tmp = gw->next;
		if( gw->parent == NULL ) {
			browser_window_destroy(gw->browser->bw);
		}
		gw = tmp;
	}
	hotlist_destroy();
	/* send WM_DESTROY to windows purely managed by windom: */
	while( wglb.first ) {
		ApplWrite( _AESapid, WM_DESTROY, wglb.first->handle, 0, 0, 0, 0);
		EvntWindom( MU_MESAG );
	}

	RsrcXtype( 0, rsc_trindex, rsc_ntree);
	unbind_global_events();
	MenuBar( h_gem_menu , 0 );
	if( h_gem_rsrc == NULL ) {
		RsrcXfree(h_gem_rsrc );
	}
	LOG(("Shutting down plotter"));
	atari_plotter_finalise();
	LOG(("FrameExit"));
	mt_FrameExit( &app );
	if( tmp_clipboard != NULL ){
		free( tmp_clipboard );
		tmp_clipboard = NULL;
	}
	LOG(("done"));
}




static bool
process_cmdline(int argc, char** argv)
{
	int opt;

	LOG(("argc %d, argv %p", argc, argv));

	if ((option_window_width != 0) && (option_window_height != 0)) {
		cfg_width = option_window_width;
		cfg_height = option_window_height;
	} else {
		cfg_width = 800;
		cfg_height = 600;
	}

	if (option_homepage_url != NULL && option_homepage_url[0] != '\0')
		cfg_homepage_url = option_homepage_url;
	else
		cfg_homepage_url = NETSURF_HOMEPAGE;


	while((opt = getopt(argc, argv, "w:h:")) != -1) {
		switch (opt) {
		case 'w':
			cfg_width = atoi(optarg);
			break;

		case 'h':
			cfg_height = atoi(optarg);
			break;

		default:
			fprintf(stderr,
				"Usage: %s [w,h,v] url\n",
				argv[0]);
			return false;
		}
	}

	if (optind < argc) {
		cfg_homepage_url = argv[optind];
	}

	return true;
}

static inline void create_cursor(int flags, short mode, void * form, MFORM_EX * m)
{
	m->flags = flags;
	m->number = mode;
	if( flags & MFORM_EX_FLAG_USERFORM ) {
		m->number = mode;
		m->tree = (OBJECT*)form;
	}
}

static void gui_init(int argc, char** argv)
{
	char buf[PATH_MAX], sbuf[PATH_MAX];
	int len;
	OBJECT * cursors;

	atari_find_resource(buf, "netsurf.rsc", "./res/netsurf.rsc");
	LOG(("Load %s ", (char*)&buf));
	h_gem_rsrc = RsrcXload( (char*) &buf );
	if( !h_gem_rsrc )
		die("Uable to open GEM Resource file!");

	rsc_trindex = RsrcGhdr(h_gem_rsrc)->trindex;
	rsc_ntree   = RsrcGhdr(h_gem_rsrc)->ntree;

	RsrcGaddr( h_gem_rsrc, R_TREE, MAINMENU , &h_gem_menu );
	RsrcXtype( RSRC_XALL, rsc_trindex, rsc_ntree);

	create_cursor(0, POINT_HAND, NULL, &gem_cursors.hand );
	create_cursor(0, TEXT_CRSR,  NULL, &gem_cursors.ibeam );
	create_cursor(0, THIN_CROSS, NULL, &gem_cursors.cross);
 	create_cursor(0, BUSY_BEE, NULL, &gem_cursors.wait);
	create_cursor(0, ARROW, NULL, &gem_cursors.arrow);
	create_cursor(0, OUTLN_CROSS, NULL, &gem_cursors.sizeall);
	create_cursor(0, OUTLN_CROSS, NULL, &gem_cursors.sizenesw);
	create_cursor(0, OUTLN_CROSS, NULL, &gem_cursors.sizenwse);
	RsrcGaddr( h_gem_rsrc, R_TREE, CURSOR , &cursors );
	create_cursor(MFORM_EX_FLAG_USERFORM, CURSOR_APPSTART,
		cursors, &gem_cursors.appstarting);
	gem_set_cursor( &gem_cursors.appstarting );
	create_cursor(MFORM_EX_FLAG_USERFORM, CURSOR_SIZEWE,
		cursors, &gem_cursors.sizewe);
	create_cursor(MFORM_EX_FLAG_USERFORM, CURSOR_SIZENS,
		cursors, &gem_cursors.sizens);
	create_cursor(MFORM_EX_FLAG_USERFORM, CURSOR_NODROP,
		cursors, &gem_cursors.nodrop);
	create_cursor(MFORM_EX_FLAG_USERFORM, CURSOR_DENY,
		cursors, &gem_cursors.deny);
	create_cursor(MFORM_EX_FLAG_USERFORM, CURSOR_MENU,
		cursors, &gem_cursors.menu);
	create_cursor(MFORM_EX_FLAG_USERFORM, CURSOR_HELP,
		cursors, &gem_cursors.help);

	LOG(("Enabling core select menu"));
	option_core_select_menu = true;

	atari_find_resource(buf, "default.css", "./res/default.css");
	default_stylesheet_url = path_to_url(buf);
	LOG(("Using '%s' as Default CSS URL", default_stylesheet_url));

	atari_find_resource(buf, "quirks.css", "./res/quirks.css");
	quirks_stylesheet_url = path_to_url(buf);

	if (process_cmdline(argc,argv) != true)
		die("unable to process command line.\n");

	nkc_init();
	atari_plotter_init( option_atari_screen_driver, option_atari_font_driver );
}

static void gui_init2(int argc, char** argv)
{
	struct browser_window *bw;
	const char *addr = NETSURF_HOMEPAGE;
	MenuBar( h_gem_menu , 1 );
	bind_global_events();
	menu_register( _AESapid, (char*)"  NetSurf ");
}


/** Entry point from OS.
 *
 * /param argc The number of arguments in the string vector.
 * /param argv The argument string vector.
 * /return The return code to the OS
 */
int main(int argc, char** argv)
{
	char options[PATH_MAX];
	char messages[PATH_MAX];

	setbuf(stderr, NULL);

	ApplInit();

	graf_mouse(BUSY_BEE, NULL);

	atari_find_resource(messages, "messages", "./res/messages");
	atari_find_resource(options, "Choices", "./Choices");
	options_file_location = strdup(options);

	netsurf_init(&argc, &argv, options, messages);
	gui_init(argc, argv);
	gui_init2(argc, argv);
	browser_window_create(cfg_homepage_url, 0, 0, true, false);
	graf_mouse( ARROW , NULL);
	netsurf_main_loop();
	netsurf_exit();
	LOG(("ApplExit"));
	ApplExit();

	return 0;
}

