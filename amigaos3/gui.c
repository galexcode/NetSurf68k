/*
 * Copyright 2008 Vincent Sanders <vince@simtec.co.uk>
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
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <limits.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

#include <libnsfb.h>
#include <libnsfb_plot.h>
#include <libnsfb_event.h>

#include <SDL/SDL.h>

#include "desktop/browser_private.h"
#include "desktop/gui.h"
#include "desktop/mouse.h"
#include "desktop/plotters.h"
#include "desktop/netsurf.h"
#include "desktop/options.h"

#include "utils/filepath.h"
#include "utils/log.h"

#include "utils/messages.h"

#include "utils/schedule.h"
#include "utils/types.h"
#include "utils/url.h"
#include "utils/utils.h"


#include "desktop/textinput.h"
#include "render/form.h"

#include "framebuffer/gui.h"
#include "framebuffer/fbtk.h"
#include "framebuffer/framebuffer.h"
#include "framebuffer/schedule.h"
#include "framebuffer/findfile.h"

#include "framebuffer/image_data.h"
#include "framebuffer/font.h"


#include "content/urldb.h"
#include "desktop/history_core.h"
#include "content/fetch.h"

#define NSFB_TOOLBAR_DEFAULT_LAYOUT "blfrhuvaqetk123456789xgdnmyop"

/* __AMIGA__ */
#include <exec/exec.h>
#include <libraries/asl.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dostags.h>
#include <proto/asl.h>
#include <proto/openurl.h>

#include "amigaos3/gui_os3.h"

int window_depth;
int mouse_2_click;
struct browser_window *bw_url;
struct gui_window *g_ui;

char theme_path[128] = "PROGDIR:Resources/theme/";

struct Library *AslBase;
struct Library *OpenURLBase;
/*__AMIGA__*/

fbtk_widget_t *fbtk;

struct gui_window *input_window = NULL;
struct gui_window *search_current_window;
struct gui_window *window_list = NULL;

/* private data for browser user widget */
struct browser_widget_s {
	struct browser_window *bw; /**< The browser window connected to this gui window */
	int scrollx, scrolly; /**< scroll offsets. */

	/* Pending window redraw state. */
	bool redraw_required; /**< flag indicating the foreground loop
			       * needs to redraw the browser widget.
			       */
	bbox_t redraw_box; /**< Area requiring redraw. */
	bool pan_required; /**< flag indicating the foreground loop
			    * needs to pan the window.
			    */
	int panx, pany; /**< Panning required. */
};

static struct gui_drag {
	enum state {
		GUI_DRAG_NONE,
		GUI_DRAG_PRESSED,
		GUI_DRAG_DRAG
	} state;
	int button;
	int x;
	int y;
	bool grabbed_pointer;
} gui_drag;


/* queue a redraw operation, co-ordinates are relative to the window */
static void
fb_queue_redraw(struct fbtk_widget_s *widget, int x0, int y0, int x1, int y1)
{
	struct browser_widget_s *bwidget = fbtk_get_userpw(widget);

	bwidget->redraw_box.x0 = min(bwidget->redraw_box.x0, x0);
	bwidget->redraw_box.y0 = min(bwidget->redraw_box.y0, y0);
	bwidget->redraw_box.x1 = max(bwidget->redraw_box.x1, x1);
	bwidget->redraw_box.y1 = max(bwidget->redraw_box.y1, y1);

	if (fbtk_clip_to_widget(widget, &bwidget->redraw_box)) {
		bwidget->redraw_required = true;
		fbtk_request_redraw(widget);
	} else {
		bwidget->redraw_box.y0 = bwidget->redraw_box.x0 = INT_MAX;
		bwidget->redraw_box.y1 = bwidget->redraw_box.x1 = -(INT_MAX);
		bwidget->redraw_required = false;
	}
}

/* queue a window scroll */
static void
widget_scroll_y(struct gui_window *gw, int y, bool abs)
{
	struct browser_widget_s *bwidget = fbtk_get_userpw(gw->browser);
	int content_height;
	int height;
	float scale = gw->bw->scale;

	LOG(("window scroll"));
	if (abs) {
		bwidget->pany = y - bwidget->scrolly;
	} else {
		bwidget->pany += y;
	}
	

	content_height = content_get_height(gw->bw->current_content) * scale;

	height = fbtk_get_height(gw->browser);

	/* dont pan off the top */
	if ((bwidget->scrolly + bwidget->pany) < 0)
		bwidget->pany = -bwidget->scrolly;

	/* do not pan off the bottom of the content */
	if ((bwidget->scrolly + bwidget->pany) > (content_height - height))
		bwidget->pany = (content_height - height) - bwidget->scrolly;

	if (bwidget->pany == 0)
		return;

	bwidget->pan_required = true;

	fbtk_request_redraw(gw->browser);

	fbtk_set_scroll_position(gw->vscroll, bwidget->scrolly + bwidget->pany);
}

/* queue a window scroll */
static void
widget_scroll_x(struct gui_window *gw, int x, bool abs)
{
	struct browser_widget_s *bwidget = fbtk_get_userpw(gw->browser);
	int content_width;
	int width;
	float scale = gw->bw->scale;

	if (abs) {
		bwidget->panx = x - bwidget->scrollx;
	} else {
		bwidget->panx += x;
	}

	content_width = content_get_width(gw->bw->current_content) * scale;

	width = fbtk_get_width(gw->browser);

	/* dont pan off the left */
	if ((bwidget->scrollx + bwidget->panx) < 0)
		bwidget->panx = - bwidget->scrollx;

	/* do not pan off the right of the content */
	if ((bwidget->scrollx + bwidget->panx) > (content_width - width))
		bwidget->panx = (content_width - width) - bwidget->scrollx;

	if (bwidget->panx == 0)
		return;

	bwidget->pan_required = true;

	fbtk_request_redraw(gw->browser);

	fbtk_set_scroll_position(gw->hscroll, bwidget->scrollx + bwidget->panx);
}

static void
fb_pan(fbtk_widget_t *widget,
       struct browser_widget_s *bwidget,
       struct browser_window *bw)
{
	int x;
	int y;
	int width;
	int height;
	nsfb_bbox_t srcbox;
	nsfb_bbox_t dstbox;

	nsfb_t *nsfb = fbtk_get_nsfb(widget);

	height = fbtk_get_height(widget);
	width = fbtk_get_width(widget);

	LOG(("panning %d, %d", bwidget->panx, bwidget->pany));

	x = fbtk_get_absx(widget);
	y = fbtk_get_absy(widget);

	/* if the pan exceeds the viewport size just redraw the whole area */
	if (bwidget->pany >= height || bwidget->pany <= -height ||
	    bwidget->panx >= width || bwidget->panx <= -width) {

		bwidget->scrolly += bwidget->pany;
		bwidget->scrollx += bwidget->panx;
		fb_queue_redraw(widget, 0, 0, width, height);

		/* ensure we don't try to scroll again */
		bwidget->panx = 0;
		bwidget->pany = 0;
		bwidget->pan_required = false;
		return;
	}

	if (bwidget->pany < 0) {
		/* pan up by less then viewport height */
		srcbox.x0 = x;
		srcbox.y0 = y;
		srcbox.x1 = srcbox.x0 + width;
		srcbox.y1 = srcbox.y0 + height + bwidget->pany;

		dstbox.x0 = x;
		dstbox.y0 = y - bwidget->pany;
		dstbox.x1 = dstbox.x0 + width;
		dstbox.y1 = dstbox.y0 + height + bwidget->pany;

		/* move part that remains visible up */
		nsfb_plot_copy(nsfb, &srcbox, nsfb, &dstbox);

		/* redraw newly exposed area */
		bwidget->scrolly += bwidget->pany;
		fb_queue_redraw(widget, 0, 0, width, - bwidget->pany);


	} else if (bwidget->pany > 0) {
		/* pan down by less then viewport height */
		srcbox.x0 = x;
		srcbox.y0 = y + bwidget->pany;
		srcbox.x1 = srcbox.x0 + width;
		srcbox.y1 = srcbox.y0 + height - bwidget->pany;

		dstbox.x0 = x;
		dstbox.y0 = y;
		dstbox.x1 = dstbox.x0 + width;
		dstbox.y1 = dstbox.y0 + height - bwidget->pany;

		/* move part that remains visible down */
		nsfb_plot_copy(nsfb, &srcbox, nsfb, &dstbox);

		/* redraw newly exposed area */
		bwidget->scrolly += bwidget->pany;
		fb_queue_redraw(widget, 0, height - bwidget->pany,
				width, height);
	}

	if (bwidget->panx < 0) {
		/* pan left by less then viewport width */
		srcbox.x0 = x;
		srcbox.y0 = y;
		srcbox.x1 = srcbox.x0 + width + bwidget->panx;
		srcbox.y1 = srcbox.y0 + height;

		dstbox.x0 = x - bwidget->panx;
		dstbox.y0 = y;
		dstbox.x1 = dstbox.x0 + width + bwidget->panx;
		dstbox.y1 = dstbox.y0 + height;

		/* move part that remains visible left */
		nsfb_plot_copy(nsfb, &srcbox, nsfb, &dstbox);

		/* redraw newly exposed area */
		bwidget->scrollx += bwidget->panx;
		fb_queue_redraw(widget, 0, 0, -bwidget->panx, height);
	

	} else if (bwidget->panx > 0) {
		/* pan right by less then viewport width */
		srcbox.x0 = x + bwidget->panx;
		srcbox.y0 = y;
		srcbox.x1 = srcbox.x0 + width - bwidget->panx;
		srcbox.y1 = srcbox.y0 + height;

		dstbox.x0 = x;
		dstbox.y0 = y;
		dstbox.x1 = dstbox.x0 + width - bwidget->panx;
		dstbox.y1 = dstbox.y0 + height;

		/* move part that remains visible right */
		nsfb_plot_copy(nsfb, &srcbox, nsfb, &dstbox);

		/* redraw newly exposed area */
		bwidget->scrollx += bwidget->panx;
		fb_queue_redraw(widget, width - bwidget->panx, 0, 
				width, height);
	}

	bwidget->pan_required = false;
	bwidget->panx = 0;
	bwidget->pany = 0;
}

static void
fb_redraw(fbtk_widget_t *widget,
	  struct browser_widget_s *bwidget,
	  struct browser_window *bw)
{
	int x;
	int y;
	int caret_x, caret_y, caret_h;
	struct rect clip;
	struct redraw_context ctx = {
		.interactive = true,
		.background_images = true,
		.plot = &fb_plotters
	};
	nsfb_t *nsfb = fbtk_get_nsfb(widget);
	
	LOG(("%d,%d to %d,%d",
	     bwidget->redraw_box.x0,
	     bwidget->redraw_box.y0,
	     bwidget->redraw_box.x1,
	     bwidget->redraw_box.y1));

	x = fbtk_get_absx(widget);
	y = fbtk_get_absy(widget);

	/* adjust clipping co-ordinates according to window location */
	bwidget->redraw_box.y0 += y;
	bwidget->redraw_box.y1 += y;
	bwidget->redraw_box.x0 += x;
	bwidget->redraw_box.x1 += x;

	nsfb_claim(nsfb, &bwidget->redraw_box);

	/* redraw bounding box is relative to window */
	clip.x0 = bwidget->redraw_box.x0;
	clip.y0 = bwidget->redraw_box.y0;
	clip.x1 = bwidget->redraw_box.x1;
	clip.y1 = bwidget->redraw_box.y1;

	browser_window_redraw(bw,
			(x - bwidget->scrollx) / bw->scale,
			(y - bwidget->scrolly) / bw->scale,
			&clip, &ctx);

	if (fbtk_get_caret(widget, &caret_x, &caret_y, &caret_h)) {
		/* This widget has caret, so render it */
		nsfb_bbox_t line;
		nsfb_plot_pen_t pen;

		line.x0 = x - bwidget->scrollx + caret_x;
		line.y0 = y - bwidget->scrolly + caret_y;
		line.x1 = x - bwidget->scrollx + caret_x;
		line.y1 = y - bwidget->scrolly + caret_y + caret_h;

		pen.stroke_type = NFSB_PLOT_OPTYPE_SOLID;
		pen.stroke_width = 1;
		pen.stroke_colour = 0xFF0000FF;

		nsfb_plot_line(nsfb, &line, &pen);
	}
	nsfb_update(fbtk_get_nsfb(widget), &bwidget->redraw_box);

	bwidget->redraw_box.y0 = bwidget->redraw_box.x0 = INT_MAX;
	bwidget->redraw_box.y1 = bwidget->redraw_box.x1 = INT_MIN;
	bwidget->redraw_required = false;
}

static int
fb_browser_window_redraw(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct gui_window *gw = cbi->context;
	struct browser_widget_s *bwidget;

	bwidget = fbtk_get_userpw(widget);
	if (bwidget == NULL) {
		LOG(("browser widget from widget %p was null", widget));
		return -1;
	}

	if (bwidget->pan_required) {
		fb_pan(widget, bwidget, gw->bw);
	}

	if (bwidget->redraw_required) {
		fb_redraw(widget, bwidget, gw->bw);
	} else {
		bwidget->redraw_box.x0 = 0;
		bwidget->redraw_box.y0 = 0;
		bwidget->redraw_box.x1 = fbtk_get_width(widget);
		bwidget->redraw_box.y1 = fbtk_get_height(widget);
		fb_redraw(widget, bwidget, gw->bw);
	}
	return 0;
}


static const char *fename;
static int febpp;
static int fewidth;
static int feheight;
static const char *feurl;

static bool
process_cmdline(int argc, char** argv)
{
	int opt;

	LOG(("argc %d, argv %p", argc, argv));

	fename = "sdl";
	febpp = 32;


	if ((nsoption_int(window_width) != 0) && 
	   (nsoption_int(window_width) <= 1920) &&
		(nsoption_int(window_height) != 0) &&
		(nsoption_int(window_height) <= 1080)) {
		
		fewidth = nsoption_int(window_width);
		feheight = nsoption_int(window_height);
	} else {
		fewidth = 800;
		feheight = 600;
	}

	if (nsoption_charp(homepage_url) != NULL && 
		nsoption_charp(homepage_url[0])!= '\0') {
		if (nsoption_charp(lastpage_url) == NULL)
			feurl = nsoption_charp(homepage_url); 
		else {
			feurl = nsoption_charp(lastpage_url);
			nsoption_charp(lastpage_url) = NULL;
			nsoption_write("PROGDIR:Resources/Options");
			}	
	}		


	while((opt = getopt(argc, argv, "f:b:w:h:")) != -1) {
		switch (opt) {
		case 'f':
			fename = optarg;
			break;

		case 'b':
			febpp = atoi(optarg);
			break;

		case 'w':
			fewidth = atoi(optarg);
			break;

		case 'h':
			feheight = atoi(optarg);
			break;

		default:
			fprintf(stderr,
				"Usage: %s [-f frontend] [-b bpp] url\n",
				argv[0]);
			return false;
		}
	}

	if (optind < argc) {
		feurl = argv[optind];
	}

	return true;
}

void redraw_gui()
{
	fbtk_request_redraw(toolbar);
	fbtk_request_redraw(url);
	if (label1)
		fbtk_request_redraw(label1);
	if (label2)
		fbtk_request_redraw(label2);
	if (label3)
		fbtk_request_redraw(label3);
	if (label4)
		fbtk_request_redraw(label4);
	if (label5)
		fbtk_request_redraw(label5);
	if (label6)
		fbtk_request_redraw(label6);
	if (label7)
		fbtk_request_redraw(label7);
	if (label8)
		fbtk_request_redraw(label8);
	if (label9)
		fbtk_request_redraw(label9);
	if (label10)		
		fbtk_request_redraw(label10);
	if (label11)		
		fbtk_request_redraw(label11);
	if (label12)		
		fbtk_request_redraw(label12);	

	gui_window_redraw_window(g_ui);
}


static void
gui_init(int argc, char** argv)
{
	nsfb_t *nsfb;

	nsoption_bool(core_select_menu) = true;
	
	window_depth = nsoption_int(fb_depth);

	if (nsoption_charp(cookie_file) == NULL) {
		nsoption_charp(cookie_file) = strdup("PROGDIR:Resources/Cookies");
		LOG(("Using '%s' as Cookies file", nsoption_charp(cookie_file)));
	}

	if (nsoption_charp(cookie_jar) == NULL) {
		nsoption_charp(cookie_jar) = strdup("PROGDIR:Resources/Cookies");
		LOG(("Using '%s' as Cookie Jar file", nsoption_charp(cookie_jar)));
	}

	if (nsoption_charp(cookie_file) == NULL || nsoption_charp(cookie_jar) == NULL)
		die("Failed initialising cookie options");

	if (nsoption_charp(theme) == NULL) {
		nsoption_charp(theme) = strdup("default");
	}
	
	if (nsoption_charp(accept_language) == NULL) {
		nsoption_charp(accept_language) = strdup("en,pl");
	}
	
	if (nsoption_charp(accept_charset) == NULL) {
		nsoption_charp(accept_charset) = strdup("ISO-8859-2");
	}
	
	if (process_cmdline(argc,argv) != true)
		die("unable to process command line.\n");

	nsfb = framebuffer_initialise(fename, fewidth, feheight, febpp);
	if (nsfb == NULL)
		die("Unable to initialise framebuffer");

    framebuffer_set_cursor(NULL); //pointer /*check it */

	if (fb_font_init() == false)
		die("Unable to initialise the font system");

	fbtk = fbtk_init(nsfb);

	/*fbtk_enable_oskb(fbtk);*/

	urldb_load_cookies(nsoption_charp(cookie_file));

}


/** Entry point from OS.
 *
 * /param argc The number of arguments in the string vector.
 * /param argv The argument string vector.
 * /return The return code to the OS
 */
nsurl *html_default_stylesheet_url;
int
main(int argc, char** argv)
{
	struct browser_window *bw;
	char *options;
	char *messages;

	setbuf(stderr, NULL);
	
	respaths = fb_init_resource("PROGDIR:Resources/");

	messages = strdup("PROGDIR:Resources/messages");
	options = strdup("PROGDIR:Resources/Options");

	netsurf_init(&argc, &argv, options, messages);

	free(messages);
	free(options);
	
	gui_init(argc, argv);
	
	LOG(("calling browser_window_create"));
	bw = browser_window_create(feurl, 0, 0, true, false);	

	netsurf_main_loop();

	browser_window_destroy(bw);

	netsurf_exit();

	return 0;
}


void
gui_poll(bool active)
{
	nsfb_event_t event;
	int timeout; /* timeout in miliseconds */

	/* run the scheduler and discover how long to wait for the next event */
	timeout = schedule_run();	
	
	/* if active do not wait for event, return immediately */
	if (active)
		timeout = 0;
		
	/* if redraws are pending do not wait for event, return immediately */
	if (fbtk_get_redraw_pending(fbtk))
		timeout = 0;
		
	if (fbtk_event(fbtk, &event, timeout)) {
		if ((event.type == NSFB_EVENT_CONTROL) &&
		    (event.value.controlcode ==  NSFB_CONTROL_QUIT))
			{
			netsurf_quit = true;
			}
	}
	
	fbtk_redraw(fbtk);

}

void
gui_quit(void)
{
	LOG(("gui_quit"));
	
	urldb_save_cookies(nsoption_charp(cookie_jar));

	framebuffer_finalise();
}

/* called back when click in browser window */
static int
fb_browser_window_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct gui_window *gw = cbi->context;
	struct browser_widget_s *bwidget = fbtk_get_userpw(widget);
	float scale = gw->bw->scale;
	int x = (cbi->x + bwidget->scrollx) / scale;
	int y = (cbi->y + bwidget->scrolly) / scale;

	
	if (cbi->event->type != NSFB_EVENT_KEY_DOWN &&
	    cbi->event->type != NSFB_EVENT_KEY_UP)
		return 0;

	LOG(("browser window clicked at %d,%d", cbi->x, cbi->y));

	switch (cbi->event->type) {
	case NSFB_EVENT_KEY_DOWN:
		switch (cbi->event->value.keycode) {
		case NSFB_KEY_MOUSE_1:
			browser_window_mouse_click(gw->bw,
					BROWSER_MOUSE_PRESS_1, x, y);
			gui_drag.state = GUI_DRAG_PRESSED;
			gui_drag.button = 1;
			gui_drag.x = x;
			gui_drag.y = y;
			break;	

		case NSFB_KEY_MOUSE_3:
			browser_window_mouse_click(gw->bw,
					BROWSER_MOUSE_PRESS_2, x, y);
			gui_drag.state = GUI_DRAG_PRESSED;
			gui_drag.button = 2;
			gui_drag.x = x;
			gui_drag.y = y;
			mouse_2_click = 1;	
			break;

		case NSFB_KEY_MOUSE_4:
			/* scroll up */
			if (browser_window_scroll_at_point(gw->bw, x, y,
					0, -100) == false)
				widget_scroll_y(gw, -100, false);
			break;

		case NSFB_KEY_MOUSE_5:
			/* scroll down */
			if (browser_window_scroll_at_point(gw->bw, x, y,
					0, 100) == false)
				widget_scroll_y(gw, 100, false);
			break;

		default:
			break;

		}

		break;		
	case NSFB_EVENT_KEY_UP:
		switch (cbi->event->value.keycode) {
		case NSFB_KEY_MOUSE_1:
			if (gui_drag.state == GUI_DRAG_DRAG) {
				/* End of a drag, rather than click */

				if (gui_drag.grabbed_pointer) {
					/* need to ungrab pointer */
					fbtk_tgrab_pointer(widget);
					gui_drag.grabbed_pointer = false;
				}

				gui_drag.state = GUI_DRAG_NONE;

				/* Tell core */
				browser_window_mouse_track(gw->bw, 0, x, y);
				break;
			}
			/* This is a click;
			 * clear PRESSED state and pass to core */
			gui_drag.state = GUI_DRAG_NONE;
			browser_window_mouse_click(gw->bw,
					BROWSER_MOUSE_CLICK_1, x, y);
			break;
			
		case NSFB_KEY_MOUSE_3:
			if (gui_drag.state == GUI_DRAG_DRAG) {
				/* End of a drag, rather than click */
				gui_drag.state = GUI_DRAG_NONE;

				if (gui_drag.grabbed_pointer) {
					/* need to ungrab pointer */
					fbtk_tgrab_pointer(widget);
					gui_drag.grabbed_pointer = false;
				}

				/* Tell core */
				browser_window_mouse_track(gw->bw, 0, x, y);
				break;
			}
			/* This is a click;
			 * clear PRESSED state and pass to core */
			gui_drag.state = GUI_DRAG_NONE;
			browser_window_mouse_click(gw->bw,
					BROWSER_MOUSE_CLICK_2, x, y);
			break;

		default:
			break;

		}

		break;
	default:
		break;

	}
	return 1;
}

/* called back when movement in browser window */
static int
fb_browser_window_move(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	browser_mouse_state mouse = 0;
	struct gui_window *gw = cbi->context;
	struct browser_widget_s *bwidget = fbtk_get_userpw(widget);
	int x = (cbi->x + bwidget->scrollx) / gw->bw->scale;
	int y = (cbi->y + bwidget->scrolly) / gw->bw->scale;

	if (gui_drag.state == GUI_DRAG_PRESSED &&
			(abs(x - gui_drag.x) > 5 ||
			 abs(y - gui_drag.y) > 5)) {
		/* Drag started */
		if (gui_drag.button == 1) {
			browser_window_mouse_click(gw->bw,
					BROWSER_MOUSE_DRAG_1,
					gui_drag.x, gui_drag.y);
		} else {
			browser_window_mouse_click(gw->bw,
					BROWSER_MOUSE_DRAG_2,
					gui_drag.x, gui_drag.y);
		}
		gui_drag.grabbed_pointer = fbtk_tgrab_pointer(widget);
		gui_drag.state = GUI_DRAG_DRAG;
	}

	if (gui_drag.state == GUI_DRAG_DRAG) {
		/* set up mouse state */
		mouse |= BROWSER_MOUSE_DRAG_ON;

		if (gui_drag.button == 1)
			mouse |= BROWSER_MOUSE_HOLDING_1;
		else
			mouse |= BROWSER_MOUSE_HOLDING_2;
	}

	browser_window_mouse_track(gw->bw, mouse, x, y);

	return 0;
}


int ucs4_pop = 0;
	
void rerun_netsurf()
{
	char curDir[1024];
	char run[1028];
	
	GetCurrentDirName(curDir,1024);
	strcpy(run, "run ");
	strcat(run, curDir);
	strcat(run, "/netsurf");
	
	Execute(run, 0, 0);
	
	netsurf_quit = true;	
}

static int
fb_browser_window_input(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct gui_window *gw = cbi->context;
	static fbtk_modifier_type modifier = FBTK_MOD_CLEAR;

	int ucs4 = -1;

	
	LOG(("got value %d", cbi->event->value.keycode));

	switch (cbi->event->type) {
	case NSFB_EVENT_KEY_DOWN:


		switch (cbi->event->value.keycode) {
		case NSFB_KEY_PAGEUP:
			if (browser_window_key_press(gw->bw,
					KEY_PAGE_UP) == false)
				widget_scroll_y(gw, -fbtk_get_height(
						gw->browser), false);
			break;

		case NSFB_KEY_PAGEDOWN:
			if (browser_window_key_press(gw->bw, 
					KEY_PAGE_DOWN) == false)
				widget_scroll_y(gw, fbtk_get_height(
						gw->browser), false);
			break;

		case NSFB_KEY_RIGHT:
			if (modifier & FBTK_MOD_RCTRL ||
					modifier & FBTK_MOD_LCTRL) {
				/* CTRL held */
				if (browser_window_key_press(gw->bw,
						KEY_LINE_END) == false)
					widget_scroll_x(gw, INT_MAX, true);

			} else if (modifier & FBTK_MOD_RSHIFT ||
					modifier & FBTK_MOD_LSHIFT) {
				/* SHIFT held */
				if (browser_window_key_press(gw->bw,
						KEY_WORD_RIGHT) == false)
					widget_scroll_x(gw, fbtk_get_width(
						gw->browser), false);
			} else {
				/* no modifier */
				if (browser_window_key_press(gw->bw,
						KEY_RIGHT) == false)
					widget_scroll_x(gw, 100, false);
			}
			break;
			 	
					
		case NSFB_KEY_LEFT:
			if (modifier & FBTK_MOD_RCTRL ||
					modifier & FBTK_MOD_LCTRL) {
				/* CTRL held */
				if (browser_window_key_press(gw->bw,
						KEY_LINE_START) == false)
					widget_scroll_x(gw, 0, true);

			} else if (modifier & FBTK_MOD_RSHIFT ||
					modifier & FBTK_MOD_LSHIFT) {
				/* SHIFT held */
				if (browser_window_key_press(gw->bw,
						KEY_WORD_LEFT) == false)
					widget_scroll_x(gw, -fbtk_get_width(
						gw->browser), false);

			} else {
				/* no modifier */
				if (browser_window_key_press(gw->bw,
						KEY_LEFT) == false)
					widget_scroll_x(gw, -100, false);
			}
			break;

		case NSFB_KEY_UP:
			if (browser_window_key_press(gw->bw,
					KEY_UP) == false)
				widget_scroll_y(gw, -100, false);
			break;

		case NSFB_KEY_DOWN:
			if (browser_window_key_press(gw->bw,
					KEY_DOWN) == false)
				widget_scroll_y(gw, 100, false);
			break;

		case NSFB_KEY_RSHIFT:
			modifier |= FBTK_MOD_RSHIFT;
			break;

		case NSFB_KEY_LSHIFT:
			modifier |= FBTK_MOD_LSHIFT;
			break;

		case NSFB_KEY_RCTRL:
			SDL_EnableUNICODE(false);
			modifier |= FBTK_MOD_RCTRL;
			break;

		case NSFB_KEY_LCTRL:
			SDL_EnableUNICODE(false);
			modifier |= FBTK_MOD_LCTRL;
			break;
			
		case NSFB_KEY_F4:         			
			get_video(gw->bw);
			break;
			
		case NSFB_KEY_F5:
			browser_window_reload(gw->bw, true);     
			break;

		case NSFB_KEY_F6:
			nsoption_charp(lastpage_url) = strdup(nsurl_access(hlcache_handle_get_url(gw->bw->current_content)));
			rerun_netsurf();  
			break;
			
		case NSFB_KEY_F7:
			nsoption_charp(lastpage_url) = strdup(nsurl_access(hlcache_handle_get_url(gw->bw->current_content)));
			nsoption_write("PROGDIR:Resources/Options");
			rerun_netsurf(); 
			break;
            			
		case NSFB_KEY_DELETE:
			{
			char *se = strndup(status_txt, 8);
			//se[2] = '\0';
			if ( (strcmp(nsurl_access(hlcache_handle_get_url(gw->bw->current_content)),
                    "file:///PROGDIR:Resources/Bookmarks.htm") == 0) && (strcmp(se, "Document") != 0) )  
				{
				char *p, cmd[200] = "null";
				
				p = strtok(status_txt, "/"); 
				p = strtok(NULL, "/"); 
				
				strcpy(cmd, "sed -e /");
				strcat(cmd, p);  
				strcat(cmd, "/d resources/bookmarks.htm > resources/bookmarks2.htm");       				    				

				Execute(cmd, 0, 0);
						
				Execute("delete resources/bookmarks.htm", 0, 0);
				Execute("rename resources/bookmarks2.htm resources/bookmarks.htm", 0, 0);
						
				browser_window_reload(gw->bw, true);	
				}	
			}
			break; 			
		
		case NSFB_KEY_ESCAPE:
			netsurf_quit = true;
			break;				
			
		default:
			SDL_EnableUNICODE(true);
			if (strcmp(nsoption_charp(accept_charset), "AmigaPL") == 0) 	{
				switch (cbi->event->value.keycode) { 
				case 172: cbi->event->value.keycode = 175;			
				break;		
				case 177: cbi->event->value.keycode = 191;			
				break;	
				case 202: cbi->event->value.keycode = 198;
				break;
				case 203: cbi->event->value.keycode = 202;			
				break;
				case 234: cbi->event->value.keycode = 230;
				break;
				case 235: cbi->event->value.keycode = 234;			
				break;	
				}
			}		
			ucs4 = fbtk_keycode_to_ucs4(cbi->event->value.keycode, 
						    modifier);					

			if (ucs4 != -1)
				browser_window_key_press(gw->bw, ucs4);					
			break;
		}
		break;

		
	case NSFB_EVENT_KEY_UP:
		switch (cbi->event->value.keycode) {
		case NSFB_KEY_RSHIFT:
			modifier &= ~FBTK_MOD_RSHIFT;
			shift = 0;
			break;

		case NSFB_KEY_LSHIFT:
			modifier &= ~FBTK_MOD_LSHIFT;
			shift = 0;
			break;
		
		case NSFB_KEY_LALT:
			alt = 0;
			break;      
							  		
		case NSFB_KEY_RALT:
			alt = 0;
			break;      


		case NSFB_KEY_RCTRL:
			SDL_EnableUNICODE(true);
			modifier &= ~FBTK_MOD_RCTRL;
			break;

		case NSFB_KEY_LCTRL:
			SDL_EnableUNICODE(true);
			modifier &= ~FBTK_MOD_LCTRL;
			break;

		default:		
			break;
		}
		
		break;		

	default:
		break;
	}

	return 0;
}

char *
add_theme_path(char* icon) 
{
static char path[128];

strcpy(path, "PROGDIR:Resources/theme/");
strcat(path, nsoption_charp(theme));
strcat(path, icon);

return path;
}

static void
fb_update_back_forward(struct gui_window *gw)
{
	struct browser_window *bw = gw->bw;

	fbtk_set_bitmap(gw->back,
			(browser_window_back_available(bw)) ?			
			load_bitmap(add_theme_path("/back.png")) :  load_bitmap(add_theme_path("/back_g.png")));
		
	fbtk_set_bitmap(gw->forward,
			(browser_window_forward_available(bw)) ?			
			load_bitmap(add_theme_path("/forward.png")) :  load_bitmap(add_theme_path("/forward_g.png")));

}

/* left icon click routine */

int
fb_leftarrow_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct gui_window *gw = cbi->context;
	struct browser_window *bw = gw->bw;

	
 	if (cbi->event->type != NSFB_EVENT_KEY_UP)	
		return 0;

	
	if (history_back_available(bw->history)) {
		fbtk_set_bitmap(widget, load_bitmap(icon_file));		
		history_back(bw, bw->history);

		}
		
	fb_update_back_forward(gw);	

	
	return 1;		
}

/* right arrow icon click routine */

int
fb_rightarrow_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct gui_window *gw = cbi->context;
	struct browser_window *bw = gw->bw;

	
	if (cbi->event->type == NSFB_EVENT_KEY_UP) 
		return 0;	
	
	if (history_forward_available(bw->history)) 
		{
		history_forward(bw, bw->history);
		fbtk_set_bitmap(widget, load_bitmap(icon_file));
		}
			
	fb_update_back_forward(gw);
		
	return 1;		

}

/* reload icon click routine */

int
fb_reload_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;

	
	if (cbi->event->type == NSFB_EVENT_KEY_UP) 
		return 0;	
		
	fbtk_set_bitmap(widget, load_bitmap(icon_file));

	if (cbi->event->value.keycode == NSFB_KEY_MOUSE_3) {
		nsoption_read("PROGDIR:Resources/Options");
		SDL_Delay(500);
		nsoption_charp(lastpage_url) = strdup(nsurl_access(hlcache_handle_get_url(bw->current_content)));
		nsoption_write("PROGDIR:Resources/Options");
		rerun_netsurf();  	
	}
	
	browser_window_reload(bw, true);

	return 1;
}

/* stop icon click routine */

int
fb_stop_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;

	
	if (cbi->event->type == NSFB_EVENT_KEY_UP) 
		return 0;	

		
	fbtk_set_bitmap(widget, load_bitmap(icon_file));
	
	browser_window_stop(bw);

	return 1;
}

static int
fb_osk_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{

	if (cbi->event->type != NSFB_EVENT_KEY_UP)
		return 0;

	map_osk();

	return 0;
}

/* close browser window icon click routine */
static int
fb_close_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;	
	
	if (cbi->event->type != NSFB_EVENT_KEY_UP)
		return 0;

		
		netsurf_quit = true;
		return 0;

}

static int
fb_scroll_callback(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct gui_window *gw = cbi->context;

	switch (cbi->type) {
	case FBTK_CBT_SCROLLY:
		widget_scroll_y(gw, cbi->y, true);
		break;

	case FBTK_CBT_SCROLLX:
		widget_scroll_x(gw, cbi->x, true);
		break;

	default:
		break;
	}
	return 0;
}

int addURLtofile(const char *tekst)
{
	if (tekst) {
		char *nowywiersz = AllocVec(strlen(tekst), MEMF_CLEAR | MEMF_ANY);
		strcpy(nowywiersz,tekst);
		
		FILE *fp;
		char starywiersz[500];
		int i =0;
		
		if ((fp=fopen("ProgDir:Resources/URLhistory", "r+"))==NULL) {
			 printf ("Can't open URLhistory for writing! \n");
			 return 1;
			}
		
		while (fgets(starywiersz, 500, fp) != NULL) {	
			char *p; p = strchr(starywiersz, '\n'); if(p != NULL) *p = '\0';
			if(strcmp(tekst, starywiersz) == 0)
				i++;
			}
		strcat(nowywiersz,"\n");	
		if (i==0)
			fprintf (fp, "%s", nowywiersz); /* zapisz nasz ³añcuch w pliku */
						
		FreeVec(nowywiersz);
		fclose (fp); /* zamknij plik */
   }
   return 0;
}

void strip_newline( char *str, int size )
{
    int i;

    /* remove the null terminator */
    for (  i = 0; i < size; ++i )
    {
        if ( str[i] == '\n' )
        {
            str[i] = '\0';

            /* we're done, so just exit the function by returning */
            return;   
        }
    }
    /* if we get all the way to here, there must not have been a newline! */
}

int readURLfromfile()
 {
	FILE *fp;
	char wiersz[500];
	char *wsk;
	
	if ((fp=fopen("ProgDir:Resources/URLhistory", "r"))==NULL) {
		 printf ("Can't open URLhistory! \n");
		 return 1;
	 }

	while (fgets(wiersz, 500, fp) != NULL) {	
		wsk=strchr(wiersz, '\n');
		*wsk='\0';
		//AG_TlistAdd(comUrl->list, NULL, wiersz);
	}

	 fclose(fp);
	 return 0;
 }
 
	
static int
fb_url_enter(void *pw, char *text)
{
    struct browser_window *bw = pw;
	
	browser_window_go(bw, text, 0, true);

	return 0;
}

int
fb_url_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct gui_window *gw = cbi->context;
	struct browser_window *bw = cbi->context;

	framebuffer_set_cursor(NULL);	
	SDL_ShowCursor(SDL_ENABLE);		
	
	return 0;
}

#ifdef __AMIGA__

/* home icon click routine */
int
fb_home_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;
	
	if (cbi->event->type == NSFB_EVENT_KEY_UP)	
		return 0;
		
	fbtk_set_bitmap(widget, load_bitmap(icon_file));			
			
	browser_window_go(bw, nsoption_charp(homepage_url), 0, true);
			
	return 1;
}

/* paste from clipboard click routine */
int
fb_paste_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;
	
	if (cbi->event->type == NSFB_EVENT_KEY_UP) 
		return 0;	
		
	fbtk_set_bitmap(widget, load_bitmap(icon_file));
		
	if (strcmp(ReadClip(),"Null") != 0)
		browser_window_go(bw, strdup(ReadClip()), 0, true);
	
	return 1;
}

/* write to clipboard icon click routine */
int
fb_copy_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;
	struct gui_window *gw = cbi->context;
	
	if (cbi->event->type == NSFB_EVENT_KEY_UP) 
		return 0;
		
	char *clip = NULL;
		
	fbtk_set_bitmap(widget, load_bitmap(icon_file));			
			
	clip = strdup(nsurl_access(hlcache_handle_get_url(bw->current_content)));
	
	WriteClip(clip);
		
	return 1;
}

char* usunhttp(char* s)
{
     int p = 0, q = 0, i = 7;
     while (s[++p] != '\0') if (p >= i) s[q++] = s[p];
     s[q] = '\0';

    return s;
}

char* tylko_domena(char* s)
{
     int p = 6, q = 0, i = 0;
     while (s[++p] != '/') if (p >= i) s[q++] = s[p];
     s[q] = '\0';

    return s;
}
 
char* tytul(char* s)
{
	s = strndup(s, 10);
	
     /*int p = 0, q = -1;
	 /*
	 while (q < 10) {
	  if ((s[++p] != ' ') || (s[++p] != ':'))
		s[q++] = s[p];
	  else
		{
		WriteClip(s);

		
		}
	}	
	*/
	return s;
}
 
void 
get_video(struct browser_window *bw) 
{   	
	int res = 0;
	BPTR fh;	
	char *cmd = AllocVec(1000, MEMF_CLEAR | MEMF_ANY);
	char *data = "empty";
	
	char *url = strdup(nsurl_access(hlcache_handle_get_url(bw->current_content)));	
				
	if (nsoption_int(gv_action) == 0) 
		{
			Execute("RequestChoice >ENV:NStemp TITLE=\"Netsurf\" BODY=\" Please choose preffered action\" GADGETS=\"Play|SavePlay|Save|Cancel\"", 0, 0);	
	
			fh = Open("ENV:NStemp",MODE_OLDFILE);
			Read(fh, strdup(data), 2);	
			res = atoi(data);	
			Execute("delete ENV:NStemp",0,0); 					
			Close(fh); 	
		}
	else
		res = 1;
	
	if (res == 1)	{
		strcpy(cmd, "run Sys:Rexxc/rx Rexx:getvideo.rexx "); 
		}
	else if ((res == 2) || (res == 3))					
		strcpy(cmd, "echo \" Sys:Rexxc/rx Rexx:getvideo.rexx ");
		
	strcat(cmd,"\"");					 
	strcat(cmd,url);
	strcat(cmd,"\"");
	
	if (res == 1)		
		strcat(cmd, " play ");
	else if (res == 2)
		{
		strcat(cmd, " saveplay\" > T:script ");	
		}
	else if (res == 3)
		{
		strcat(cmd, " save\" > T:script ");
		}	
			
	Execute(cmd, 0, 0);	

	if ((res == 2) || (res == 3))
		{	
		Execute("echo \"endcli\" >>T:script",0,0);
		Execute("execute T:script",0,0);
		Execute("delete T:script",0,0); 				
		}
			
	FreeVec(cmd);
	
}

/* get video icon click routine */
int
fb_getvideo_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;

	if (cbi->event->type == NSFB_EVENT_KEY_UP)
		return 0;
		
    get_video(bw);
    
	fbtk_set_bitmap(widget, load_bitmap(icon_file));
	
	return 1;
}


/* set current url as homepage icon click routine */
int
fb_sethome_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
 	struct browser_window *bw = cbi->context;
	
	if (cbi->event->type == NSFB_EVENT_KEY_UP) 
		return 0;	
		
	fbtk_set_bitmap(widget, load_bitmap(icon_file));
	
	nsoption_charp(homepage_url) = strdup(nsurl_access(hlcache_handle_get_url(bw->current_content)));
	nsoption_write("PROGDIR:Resources/Options");
	free(nsoption_charp(homepage_url));
	
	return 1;
}

char *
fav_num(int num, char *type)
{
	char *fav = strdup("favourite_01_label");
	
	strcpy(fav, "favourite_");
	strcat(fav, (char *) num);
	strcat(fav, type);
	
	return fav;
}

/* add favourites icon click routine */
int
fb_add_fav_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;

	fbtk_widget_t *fav = NULL;
	fbtk_widget_t *label = NULL;
	char *bitmap = NULL;	
	
	if (cbi->event->type == NSFB_EVENT_KEY_UP) 
		return 0;	
		
	fbtk_set_bitmap(widget, load_bitmap(icon_file));
				
	BPTR fh;
	char *cmd = "nic";
	char *get_url;
	int inum = 0;	
	
	/* Show select window */
	
	cmd = strdup("RequestChoice > ENV:NSfav TITLE=\"Select favourite\" BODY=\"Choose which favourite slot you would like to use\" GADGETS=\"___1___|___2___|___3___|___4___|___5___|___6___|___7___|___8___|___9___|___10___|___11___|___12___|Cancel\"");

	Execute(cmd, 0, 0);
	fh = Open("ENV:NSfav",MODE_OLDFILE);

	char snum[3];
	
	Read(fh,snum,2);
	inum = atoi(snum);

	Close(fh);
	
	Execute("delete ENV:NSfav", 0, 0);
	
	/* Download favicon */		
	
	char *wget = AllocVec(1000,MEMF_CLEAR | MEMF_ANY);
	char *opt = strdup("?&format=png&width=16&height=16 -OResources/Icons/favicon");


	strcpy(wget, "echo \" wget ");
	strcat(wget, "-PPROGDIR:Resources/Icons/ ");
	/* strcat(wget, " 	http://www.google.com/s2/favicons?domain="); */ 
	strcat(wget, " 	http://fvicon.com/");
	
	
	if (inum != 0) {
		get_url = strdup(nsurl_access(hlcache_handle_get_url(bw->current_content)));	
		get_url = strdup(tylko_domena(get_url));		
		}
	
		
	if (inum == 1 ) {	
		   nsoption_charp(favourite_1_url) = strdup(get_url);
		   nsoption_charp(favourite_1_label) = strdup(stitle);	
		   fav = fav1;
		   label = label1;
		   }
	else if (inum == 2 ) {
		   nsoption_charp(favourite_2_url) = strdup(get_url);
		   nsoption_charp(favourite_2_label) = strdup(stitle);		   	   
		   fav = fav2;
		   label = label2;		   
		   } 
	else if (inum == 3 ) {
		   nsoption_charp(favourite_3_url) = strdup(get_url);
		   nsoption_charp(favourite_3_label) = strdup(stitle);		   
		   fav = fav3;
		   label = label3;	   
		   } 
	else if (inum == 4 ) {
		   nsoption_charp(favourite_4_url) = strdup(get_url);
		   nsoption_charp(favourite_4_label) = strdup(stitle);		   	   
		   fav = fav4;
		   label = label4;		   
		   } 	
	else if (inum == 5 )  {
		   nsoption_charp(favourite_5_url) = strdup(get_url);
		   nsoption_charp(favourite_5_label) = strdup(stitle);		   
		   fav = fav5;
		   label = label5;		   
		   } 
	else if (inum == 6  ) {
		   nsoption_charp(favourite_6_url) = strdup(get_url);
		   nsoption_charp(favourite_6_label) = strdup(stitle);		   
		   fav = fav6;
		   label = label6;		   
		   } 
	else if (inum == 7  ) {
		   nsoption_charp(favourite_7_url) = strdup(get_url);
		   nsoption_charp(favourite_7_label) = strdup(stitle);		   	   
		   fav = fav7;
		   label = label7;		   
		   } 	
	else if (inum == 8  ) {
		   nsoption_charp(favourite_8_url) = strdup(get_url);
		   strcat(wget, get_url);
		   nsoption_charp(favourite_8_label) = strdup(stitle);	   
		   strcat(wget, opt);
		   fav = fav8;		
		   label = label8;		   
		   }	 
	else if (inum == 9 ) {
		   nsoption_charp(favourite_9_url) = strdup(get_url);
		   nsoption_charp(favourite_9_label) = strdup(stitle);		      
		   fav = fav9;
		   label = label9;		   
		   } 
	else if (inum == 10 ) {
		   nsoption_charp(favourite_10_url) = strdup(get_url);
		   nsoption_charp(favourite_10_label) = strdup(stitle);		   	   		   
		   fav = fav10;
		   label = label10;		   
		   } 	
	else if (inum == 11 ) {
		   nsoption_charp(favourite_11_url) = strdup(get_url);
		   nsoption_charp(favourite_11_label) = strdup(stitle);		   
		   fav = fav11;
		   label = label11;		   
		   } 
	else if (inum == 12 ) {
		   nsoption_charp(favourite_12_url) = strdup(get_url);
		   nsoption_charp(favourite_12_label) = strdup(stitle);			   
		   fav = fav12;
		   label = label12;		   
		} 				
		
	if (inum != 0 ) 
	{
		sprintf(snum, "%d", inum);
		
		strcat(wget, get_url);
		strcat(wget, opt);		
		strcat(wget, snum);	
		strcat(wget, ".png ");	
		WriteClip(wget);
		strcat(wget, " \" >script");
			
		Execute(wget, 0, 0);
		Execute("echo \"endcli\" >>script", 0, 0);
		Execute("execute script", 0, 0);
		Execute("delete script", 0, 0);
	
		nsoption_write("PROGDIR:Resources/Options");	
				
		if (fav != NULL)
			{	
			bitmap = strdup("PROGDIR:Resources/Icons/favicon");
			strcat(bitmap, snum);	
			strcat(bitmap, ".png");			
			fbtk_set_bitmap(fav, load_bitmap(bitmap));	
			fbtk_set_text(label, stitle);
			}
			
		free(get_url);	
	}

	FreeVec(wget);			

	return 1;
}

/* add bookmark icon click routine */
int
fb_add_bookmark_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;
	
	if (cbi->event->type == NSFB_EVENT_KEY_UP) 
		return 0;		
			
	fbtk_set_bitmap(widget, load_bitmap(icon_file));
	
	char *cmd = AllocVec(strlen("echo <li><a href=\"") + strlen(nsurl_access(hlcache_handle_get_url(bw->current_content))) + strlen(stitle) + strlen("</a></li >> Resources/Bookmarks.htm") + 10, MEMF_CLEAR | MEMF_ANY);

	strcpy(cmd, "echo \"<li><a href=");
	strcat(cmd,nsurl_access(hlcache_handle_get_url(bw->current_content)));
	strcat(cmd, ">");
	strcat(cmd, stitle );
	strcat(cmd, "</a></li>\" >> /Netsurf/Resources/Bookmarks.htm");

	Execute(cmd, 0, 0);

	FreeVec(cmd);	
	
	return 1;
}

/* search icon click routine */
int
fb_search_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{

	if (cbi->event->type == NSFB_EVENT_KEY_UP) {		
      	BPTR fh;
      	char *cmd = "nic";
      	char *buffer = AllocVec(1, MEMF_CLEAR | MEMF_ANY);		
		int buff = 0;
		
      	cmd = strdup("RequestChoice > ENV:NSsrcheng TITLE=\"Select search engine\" BODY=\"Select search engine\" GADGETS=\"Google|Yahoo|Bing|DuckDuckGo|YouTube|Ebay|Allegro|Aminet|Wikipedia|Cancel\"");

      	Execute(cmd, 0, 0);
      	fh = Open("ENV:NSsrcheng",MODE_OLDFILE);

      	Read(fh,buffer,1);
		buff = atoi(buffer);
		
		FreeVec(buffer);
      	Close(fh);

      	Execute("delete ENV:NSsrcheng", 0, 0);

      	  if (buff == 1) {
      		   nsoption_charp(def_search_bar) = strdup("http://www.google.com/search?q=");
      		   fbtk_set_bitmap(widget, load_bitmap("PROGDIR:Resources/Icons/google.png"));
				Execute("copy Resources/Icons/google.png Resources/Icons/search.png", 0, 0);
				}
      	  else if (buff == 2) {
      		   nsoption_charp(def_search_bar) = strdup("http://search.yahoo.com/search?p=");
      		   fbtk_set_bitmap(widget, load_bitmap("PROGDIR:Resources/Icons/yahoo.png"));     		   
				Execute("copy Resources/Icons/yahoo.png Resources/Icons/search.png", 0, 0);
			   }
      	  else if (buff == 3) {
      		   nsoption_charp(def_search_bar) = strdup("http://www.bing.com/search?q=");
      		   fbtk_set_bitmap(widget, load_bitmap("PROGDIR:Resources/Icons/bing.png"));     		   
				Execute("copy Resources/Icons/bing.png Resources/Icons/search.png", 0, 0);
			   }
      	  else if (buff == 4) {
      		   nsoption_charp(def_search_bar) = strdup("http://www.duckduckgo.com/html/?q=");
      		   fbtk_set_bitmap(widget, load_bitmap("PROGDIR:Resources/Icons/duckduckgo.png"));     		   
				Execute("copy Resources/Icons/duckduckgo.png Resources/Icons/search.png", 0, 0);
			   }			
      	  else if (buff == 5) {
      		   nsoption_charp(def_search_bar) = strdup("http://www.youtube.com/results?search_query=");
      		   fbtk_set_bitmap(widget, load_bitmap("PROGDIR:Resources/Icons/youtube.png"));
				Execute("copy Resources/Icons/youtube.png Resources/Icons/search.png", 0, 0);
			   }
      	  else if (buff == 6) {
      		   nsoption_charp(def_search_bar) = strdup("http://shop.ebay.com/items/");
      		   fbtk_set_bitmap(widget, load_bitmap("PROGDIR:Resources/Icons/ebay.png"));     		   
				Execute("copy Resources/Icons/ebay.png Resources/Icons/search.png", 0, 0);
			   }			
      	  else if (buff == 7) {
      		   nsoption_charp(def_search_bar) = strdup("http://allegro.pl/listing.php/search?string=");
      		   fbtk_set_bitmap(widget, load_bitmap("PROGDIR:Resources/Icons/allegro.png"));
				Execute("copy Resources/Icons/allegro.png Resources/Icons/search.png", 0, 0);
			   }			
      	  else if (buff == 8) {
      		   nsoption_charp(def_search_bar) = strdup("http://aminet.net/search?query=");
      		   fbtk_set_bitmap(widget,  load_bitmap("PROGDIR:Resources/Icons/aminet.png"));
				Execute("copy Resources/Icons/aminet.png Resources/Icons/search.png", 0, 0);
			   }
      	  else if (buff == 9) {
      		   nsoption_charp(def_search_bar) = strdup("http://en.wikipedia.org/w/index.php?title=Special:Search&search=");
      		   fbtk_set_bitmap(widget, load_bitmap("PROGDIR:Resources/Icons/wiki.png"));
				Execute("copy Resources/Icons/wiki.png Resources/Icons/search.png", 0, 0);
			   }

      	nsoption_write("PROGDIR:Resources/Options");
		free(cmd);
		
		return 0;
		}
	return 1;
}

int
fb_searchbar_enter(void *pw, char *text)
{
    struct browser_window *bw = pw;

	if (text)
		{
		char addr[500];

		strcpy(addr, nsoption_charp(def_search_bar));
		strcat(addr, text);
		browser_window_go(bw, addr, 0, true);	
		
		}
	return 0;
}

int
fb_bookmarks_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;
	
	if (cbi->event->type == NSFB_EVENT_KEY_UP) 
		return 0;		

	fbtk_set_bitmap(widget, load_bitmap(icon_file));
	
	char *cmd = strdup("file:///PROGDIR:Resources/Bookmarks.htm");
	
	browser_window_go(bw, cmd, 0, true);
	free(cmd);
	
	return 1;
}

int
fb_prefs_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;
	
	if (cbi->event->type == NSFB_EVENT_KEY_UP) 
		return 0;	
		
	fbtk_set_bitmap(widget, load_bitmap(icon_file));
	
	char text_editor[200];

	if (strcmp(nsoption_charp(text_editor),"") == 0)
		{
		AslBase = OpenLibrary("asl.library", 0);
		struct FileRequester *savereq;

		savereq = (struct FileRequester *)AllocAslRequestTags(ASL_FileRequest,
							ASLFR_DoSaveMode,TRUE,
							ASLFR_RejectIcons,TRUE,
							ASLFR_InitialDrawer,0,
							TAG_DONE);
		AslRequest(savereq,NULL);
		
		strcpy(text_editor, savereq->fr_Drawer);
		strcat(text_editor, savereq->fr_File);
		nsoption_charp(text_editor) = strdup(text_editor);

		nsoption_write("PROGDIR:Resources/Options");
		FreeAslRequest(savereq);
		CloseLibrary(AslBase);
		}
		

	strcpy(text_editor, nsoption_charp(text_editor));
	char curDir[1024];
	GetCurrentDirName(curDir,1024);
	strcat(text_editor, " "); 	
	strcat(text_editor,curDir);
	strcat(text_editor, "/"); 	
	strcat(text_editor, "Resources/Options"); 
	
	Execute(text_editor,0,0);

	return 1;
} 
 
int
fb_getpage_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;
	
	if (cbi->event->type == NSFB_EVENT_KEY_UP) 
		return 0;			

	fbtk_set_bitmap(widget, load_bitmap(icon_file));  

	BPTR fh;
	UBYTE *result = "0";
	int resint;

	char *cmd = strdup("RequestChoice > env:nstemp TITLE=\"Download\" BODY=\"Download \" GADGETS=\"current page|as PDF|as PNG|Cancel\"");
			   
	Execute(cmd, 0, 0);
	free(cmd);
	fh = Open("ENV:NStemp",MODE_OLDFILE);    
	Read(fh, result, 1);	
	resint = atoi(result);
	Close(fh); 
			
	if (resint > 0)  
		{
		if (strcmp(nsoption_charp(download_path),"") == 0)
			{
			AslBase = OpenLibrary("asl.library", 0);
			struct FileRequester *savereq;

			savereq = (struct FileRequester *)AllocAslRequestTags(ASL_FileRequest,
								ASLFR_DoSaveMode,TRUE,
								ASLFR_RejectIcons,TRUE,
								ASLFR_InitialDrawer,0,
								TAG_DONE);
			AslRequest(savereq,NULL);
			nsoption_charp(download_path) = strdup(savereq->fr_Drawer);
			nsoption_write("PROGDIR:Resources/Options");

			FreeAslRequest(savereq);
			CloseLibrary(AslBase);
			}
		 
		char *wget = AllocVec(2000,MEMF_CLEAR | MEMF_ANY);
		char *url = strdup(nsurl_access(hlcache_handle_get_url(bw->current_content)));
		usunhttp(url);
		char *wsk;
		wsk=strchr(url,'/');
		*wsk='-';
		strlcpy(url,url,strlen(url));
		
		strcpy(wget, "echo \" run wget ");
		strcat(wget, "-P");
		strcat(wget, nsoption_charp(download_path));
		strcat(wget, " ");
		if ( resint >= 2 )	
			strcat(wget, "http://pdfmyurl.com?url=");	
		strcat(wget,nsurl_access(hlcache_handle_get_url(bw->current_content)));
		if ( resint == 3  ) 			
			strcat(wget, "&--png");		
		
		strcat(wget, " -x -O ");		
		strcat(wget, nsoption_charp(download_path));	
		strcat(wget, "/");
		strcat(wget, url);
		if ( resint == 1 ) 			
			strcat(wget, ".html");			
		if ( resint == 2 ) 			
			strcat(wget, ".pdf");
		if ( resint == 3 )	 
			strcat(wget, ".png");
			
		strcat(wget, " \" >T:nsscript");

		fh = Open("CON:", MODE_NEWFILE);

		Execute(wget, 0, 0);
		Execute("echo \"endcli\" >>T:nsscript", 0, 0);
		Execute("execute T:nsscript", 0, 0);
		Execute("delete >nil: T:nsscript", 0, 0);

		Close(fh);
		free(url);
		FreeVec(wget);			
		}  

		Execute("delete >nil: ENV:NStemp", 0, 0); 		
			
	return 1;
}

int
fb_openfile_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;
	
	if (cbi->event->type == NSFB_EVENT_KEY_UP)
		return 0;
		
	fbtk_set_bitmap(widget, load_bitmap(icon_file));
	
	char *file = AllocVec(1024,MEMF_CLEAR | MEMF_ANY);

	AslBase = OpenLibrary("asl.library", 0);
	struct FileRequester *savereq;

	savereq = (struct FileRequester *)AllocAslRequestTags(ASL_FileRequest,
						ASLFR_DoSaveMode,TRUE,
						ASLFR_RejectIcons,TRUE,
						ASLFR_InitialDrawer,0,
						TAG_DONE);
						
	AslRequest(savereq,NULL);
	
	strcpy(file,"file:///");
	strcat(file, savereq->fr_Drawer);
	if (strcmp(savereq->fr_Drawer,"") == 0)
		strcat(file, "ProgDir:");	
	else
		strcat(file, "/");		
	strcat(file, savereq->fr_File);

	FreeAslRequest(savereq);
	CloseLibrary(AslBase);

	if (strcmp(file, "file:///") != 0 )
		browser_window_go(bw, file, 0, true);

	FreeVec(file);
	
	return 1;
}
/* fav1 icon click routine */
int
fb_fav1_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;	
	
	if (cbi->event->value.keycode == NSFB_KEY_MOUSE_3)	{
		fbtk_set_bitmap(widget, NULL);
		nsoption_charp(favourite_1_url) = NULL;
		nsoption_charp(favourite_1_label) = NULL;
		fbtk_set_text(label1, NULL);
	    nsoption_write("PROGDIR:Resources/Options");
		Execute("delete /Netsurf/Resources/Icons/favicon1.png", 0, 0);
		return 0;
	}	
	
  	fbtk_set_bitmap(widget,  load_bitmap("PROGDIR:Resources/Icons/favicon1_h.png"));     	

	if (nsoption_charp(favourite_1_url) != NULL && nsoption_charp(favourite_1_url[0]) != '\0')					
		browser_window_go(bw, nsoption_charp(favourite_1_url), 0, true);	
	
	if (cbi->event->type == NSFB_EVENT_KEY_UP) {
		fbtk_set_bitmap(widget,  load_bitmap("PROGDIR:Resources/Icons/favicon1.png"));
		
		return 0;	
	}	
	return 1;
}
/* fav2 icon click routine */
int
fb_fav2_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;	
	
	if (cbi->event->value.keycode == NSFB_KEY_MOUSE_3)	{
		fbtk_set_bitmap(widget, NULL);
		nsoption_charp(favourite_2_url) = NULL;
		nsoption_charp(favourite_2_label) = NULL;
		fbtk_set_text(label2, NULL);
	    nsoption_write("PROGDIR:Resources/Options");
		Execute("delete /Netsurf/Resources/Icons/favicon2.png", 0, 0);
		return 0;
	}		
	
	fbtk_set_bitmap(widget,  load_bitmap("PROGDIR:Resources/Icons/favicon2_h.png")); 

	if (nsoption_charp(favourite_2_url) != NULL && nsoption_charp(favourite_2_url[0]) != '\0')		
		browser_window_go(bw, nsoption_charp(favourite_2_url), 0, true);

	if (cbi->event->type == NSFB_EVENT_KEY_UP) {
		fbtk_set_bitmap(widget,  load_bitmap("PROGDIR:Resources/Icons/favicon2.png"));			
		return 0;	
	}	
	return 1;
}
/* fav3 icon click routine */
int
fb_fav3_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;	
	
	if (cbi->event->value.keycode == NSFB_KEY_MOUSE_3)	{
		fbtk_set_bitmap(widget, NULL);
		nsoption_charp(favourite_3_url) = NULL;
		nsoption_charp(favourite_3_label) = NULL;
		fbtk_set_text(label3, NULL);	
	    nsoption_write("PROGDIR:Resources/Options");
		Execute("delete /Netsurf/Resources/Icons/favicon3.png", 0, 0);
		return 0;
	}	
	
	fbtk_set_bitmap(widget,  load_bitmap("PROGDIR:Resources/Icons/favicon3_h.png")); 

	if (nsoption_charp(favourite_3_url) != NULL && nsoption_charp(favourite_3_url[0])!= '\0')		
		browser_window_go(bw, nsoption_charp(favourite_3_url), 0, true);

	if (cbi->event->type == NSFB_EVENT_KEY_UP) {
		fbtk_set_bitmap(widget,  load_bitmap("PROGDIR:Resources/Icons/favicon3.png"));			
		return 0;	
	}	
	return 1;
}
/* fav4 icon click routine */
int
fb_fav4_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;	
	
	if (cbi->event->value.keycode == NSFB_KEY_MOUSE_3)	{
		fbtk_set_bitmap(widget, NULL);
		nsoption_charp(favourite_4_url) = NULL;
		nsoption_charp(favourite_4_label) = NULL;
		fbtk_set_text(label4, NULL);
	    nsoption_write("PROGDIR:Resources/Options");
		Execute("delete /Netsurf/Resources/Icons/favicon4.png", 0, 0);
		return 0;
	}	
	
	fbtk_set_bitmap(widget,  load_bitmap("PROGDIR:Resources/Icons/favicon4_h.png")); 

	if (nsoption_charp(favourite_4_url) != NULL && nsoption_charp(favourite_4_url[0])!= '\0')		
		browser_window_go(bw, nsoption_charp(favourite_4_url), 0, true);

	if (cbi->event->type == NSFB_EVENT_KEY_UP) {
		fbtk_set_bitmap(widget,  load_bitmap("PROGDIR:Resources/Icons/favicon4.png"));			
		return 0;	
	}	
	return 1;
}
/* fav5 icon click routine */
int
fb_fav5_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;	
	
	if (cbi->event->value.keycode == NSFB_KEY_MOUSE_3)	{
		fbtk_set_bitmap(widget, NULL);
		nsoption_charp(favourite_5_url) = NULL;
		nsoption_charp(favourite_5_label) = NULL;
		fbtk_set_text(label5, NULL);	
	    nsoption_write("PROGDIR:Resources/Options");
		Execute("delete /Netsurf/Resources/Icons/favicon5.png", 0, 0);
		return 0;
	}	
	
	fbtk_set_bitmap(widget,  load_bitmap("PROGDIR:Resources/Icons/favicon5_h.png")); 

	if (nsoption_charp(favourite_5_url) != NULL && nsoption_charp(favourite_5_url[0])!= '\0')		
		browser_window_go(bw, nsoption_charp(favourite_5_url), 0, true);

	if (cbi->event->type == NSFB_EVENT_KEY_UP) {
		fbtk_set_bitmap(widget,  load_bitmap("PROGDIR:Resources/Icons/favicon5.png"));			
		return 0;	
	}	
	return 1;
}
/* fav6 icon click routine */
int
fb_fav6_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;	
	
	if (cbi->event->value.keycode == NSFB_KEY_MOUSE_3)	{
		fbtk_set_bitmap(widget, NULL);
		nsoption_charp(favourite_6_url) = NULL;
		nsoption_charp(favourite_6_label) = NULL;
		fbtk_set_text(label6, NULL);	
	    nsoption_write("PROGDIR:Resources/Options");
		Execute("delete /Netsurf/Resources/Icons/favicon6.png", 0, 0);
		return 0;
	}	
		
	fbtk_set_bitmap(widget,  load_bitmap("PROGDIR:Resources/Icons/favicon6_h.png")); 

	if (nsoption_charp(favourite_6_url) != NULL && nsoption_charp(favourite_6_url[0])!= '\0')		
		browser_window_go(bw, nsoption_charp(favourite_6_url), 0, true);

	if (cbi->event->type == NSFB_EVENT_KEY_UP) {
		fbtk_set_bitmap(widget,  load_bitmap("PROGDIR:Resources/Icons/favicon6.png"));			
		return 0;	
	}	
	return 1;
}
/* fav7 icon click routine */
int
fb_fav7_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;	
	
	if (cbi->event->value.keycode == NSFB_KEY_MOUSE_3)	{
		fbtk_set_bitmap(widget, NULL);
		nsoption_charp(favourite_7_url) = NULL;
		nsoption_charp(favourite_7_label) = NULL;
		fbtk_set_text(label7, NULL);		
	    nsoption_write("PROGDIR:Resources/Options");
		Execute("delete /Netsurf/Resources/Icons/favicon7.png", 0, 0);
		return 0;
	}	
	
	fbtk_set_bitmap(widget,  load_bitmap("PROGDIR:Resources/Icons/favicon7_h.png")); 

	if (nsoption_charp(favourite_7_url) != NULL && nsoption_charp(favourite_7_url[0])!= '\0')		
		browser_window_go(bw, nsoption_charp(favourite_7_url), 0, true);

	if (cbi->event->type == NSFB_EVENT_KEY_UP) {
		fbtk_set_bitmap(widget,  load_bitmap("PROGDIR:Resources/Icons/favicon7.png"));			
		return 0;	
	}	
	return 1;
}
/* fav8 icon click routine */
int
fb_fav8_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;	
	
	if (cbi->event->value.keycode == NSFB_KEY_MOUSE_3)	{
		fbtk_set_bitmap(widget, NULL);
		nsoption_charp(favourite_8_url) = NULL;
		nsoption_charp(favourite_8_label) = NULL;
		fbtk_set_text(label8, NULL);	
	    nsoption_write("PROGDIR:Resources/Options");
		Execute("delete /Netsurf/Resources/Icons/favicon8.png", 0, 0);
		return 0;
	}	
	
	fbtk_set_bitmap(widget,  load_bitmap("PROGDIR:Resources/Icons/favicon8_h.png")); 

	if (nsoption_charp(favourite_8_url) != NULL && nsoption_charp(favourite_8_url[0])!= '\0')		
		browser_window_go(bw, nsoption_charp(favourite_8_url), 0, true);

	if (cbi->event->type == NSFB_EVENT_KEY_UP) {
		fbtk_set_bitmap(widget,  load_bitmap("PROGDIR:Resources/Icons/favicon8.png"));			
		return 0;	
	}	
	return 1;
}
/* fav9 icon click routine */
int
fb_fav9_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;	
	
	if (cbi->event->value.keycode == NSFB_KEY_MOUSE_3)	{
		fbtk_set_bitmap(widget, NULL);
		nsoption_charp(favourite_9_url) = NULL;
		nsoption_charp(favourite_9_label) = NULL;
		fbtk_set_text(label9, NULL);	
	    nsoption_write("PROGDIR:Resources/Options");
		Execute("delete /Netsurf/Resources/Icons/favicon9.png", 0, 0);
		return 0;
	}	
	
	fbtk_set_bitmap(widget,  load_bitmap("PROGDIR:Resources/Icons/favicon9_h.png")); 

	if (nsoption_charp(favourite_9_url) != NULL && nsoption_charp(favourite_9_url[0])!= '\0')		
		browser_window_go(bw, nsoption_charp(favourite_9_url), 0, true);

	if (cbi->event->type == NSFB_EVENT_KEY_UP) {
		fbtk_set_bitmap(widget,  load_bitmap("PROGDIR:Resources/Icons/favicon9.png"));			
		return 0;	
	}	
	return 1;
}
/* fav10 icon click routine */
int
fb_fav10_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;	
	
	if (cbi->event->value.keycode == NSFB_KEY_MOUSE_3)	{
		fbtk_set_bitmap(widget, NULL);
		nsoption_charp(favourite_10_url) = NULL;
		nsoption_charp(favourite_10_label) = NULL;
		fbtk_set_text(label10, NULL);	
	    nsoption_write("PROGDIR:Resources/Options");
		Execute("delete /Netsurf/Resources/Icons/favicon10.png", 0, 0);
		return 0;
	}	
	
	fbtk_set_bitmap(widget,  load_bitmap("PROGDIR:Resources/Icons/favicon10_h.png")); 

	if (nsoption_charp(favourite_10_url) != NULL && nsoption_charp(favourite_10_url[0])!= '\0')		
		browser_window_go(bw, nsoption_charp(favourite_10_url), 0, true);

	if (cbi->event->type == NSFB_EVENT_KEY_UP) {
		fbtk_set_bitmap(widget,  load_bitmap("PROGDIR:Resources/Icons/favicon10.png"));			
		return 0;	
	}	
	return 1;
}
/* fav11 icon click routine */
int
fb_fav11_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;	
	
	if (cbi->event->value.keycode == NSFB_KEY_MOUSE_3)	{
		fbtk_set_bitmap(widget, NULL);
		nsoption_charp(favourite_11_url) = NULL;
		nsoption_charp(favourite_11_label) = NULL;
		fbtk_set_text(label11, NULL);	
	    nsoption_write("PROGDIR:Resources/Options");
		Execute("delete /Netsurf/Resources/Icons/favicon11.png", 0, 0);
		return 0;
	}	
	
	
	fbtk_set_bitmap(widget,  load_bitmap("PROGDIR:Resources/Icons/favicon11_h.png")); 

	if (nsoption_charp(favourite_11_url) != NULL && nsoption_charp(favourite_11_url[0])!= '\0')		
		browser_window_go(bw, nsoption_charp(favourite_11_url), 0, true);

	if (cbi->event->type == NSFB_EVENT_KEY_UP) {
		fbtk_set_bitmap(widget,  load_bitmap("PROGDIR:Resources/Icons/favicon11.png"));			
		return 0;	
	}	
	return 1;
}
/* fav12 icon click routine */
int
fb_fav12_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;	
	
	if (cbi->event->value.keycode == NSFB_KEY_MOUSE_3)	{
		fbtk_set_bitmap(widget, NULL);
		nsoption_charp(favourite_12_url) = NULL;
		nsoption_charp(favourite_12_label) = NULL;
		fbtk_set_text(label12, NULL);	
	    nsoption_write("PROGDIR:Resources/Options");
		Execute("delete /Netsurf/Resources/Icons/favicon12.png", 0, 0);
		return 0;
	}	
	
	fbtk_set_bitmap(widget,  load_bitmap("PROGDIR:Resources/Icons/favicon12_h.png")); 

	if (nsoption_charp(favourite_12_url) != NULL && nsoption_charp(favourite_12_url[0])!= '\0')		
		browser_window_go(bw, nsoption_charp(favourite_12_url), 0, true);

	if (cbi->event->type == NSFB_EVENT_KEY_UP) {
		fbtk_set_bitmap(widget,  load_bitmap("PROGDIR:Resources/Icons/favicon12.png"));			
		return 0;	
	}	
	return 1;
}




#include <proto/intuition.h>
#include <intuition/pointerclass.h>

static int
fb_url_move(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct gui_window *g = cbi->context;
    	bw_url =  cbi->context;
	
	framebuffer_set_cursor(&caret_image);	
	SDL_ShowCursor(SDL_DISABLE);
	
	return 0;
}

static int
set_ptr_default_move(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	framebuffer_set_cursor(NULL); //pointer
	if (icon_file)
	   fbtk_set_bitmap(button,  load_bitmap(icon_file));

	SDL_ShowCursor(SDL_ENABLE);
	
	return 0;
}

static int
fb_localhistory_btn_clik(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct gui_window *gw = cbi->context;

	
	fbtk_set_bitmap(widget,  load_bitmap(add_theme_path("/history_h.png")));	
	
	if (cbi->event->type == NSFB_EVENT_KEY_UP) {
		fbtk_set_bitmap(widget,  load_bitmap(add_theme_path("/history.png")));		
		
		fb_localhistory_map(gw->localhistory);
			
		return 0;
	}
	return 1;
}


static int
set_back_status(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;	
	
	if (history_back_available(bw->history)) {
		button = widget;
		icon_file = strdup(add_theme_path("/back.png"));
		fbtk_set_bitmap(widget, load_bitmap(add_theme_path("/back_h.png")));
	}
	gui_window_set_status(bw->window, "Go to previous page");

	return 0;
}

static int
set_forward_status(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;
	
	if (history_forward_available(bw->history)) {
		fbtk_set_bitmap(widget, load_bitmap(add_theme_path("/forward_h.png")));
		button = widget;
		icon_file = strdup(add_theme_path("/forward.png"));	
	}	
	gui_window_set_status(bw->window, "Go to next page");

	return 0;
}

static int
set_close_status(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;

	gui_window_set_status(bw->window, "Close or restart(RMB) program");

	return 0;
}

static int
set_stop_status(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;
	
	button = widget;
	icon_file = strdup(add_theme_path("/stop.png"));	
	
	fbtk_set_bitmap(widget, load_bitmap(add_theme_path("/stop_h.png")));
	
	gui_window_set_status(bw->window, "Stop loading of current page");

	return 0;
}


static int
set_local_status(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;

	button = widget;
	icon_file = strdup(add_theme_path("/history.png"));	
	fbtk_set_bitmap(widget, load_bitmap(add_theme_path("/history_h.png")));
	
	gui_window_set_status(bw->window, "Show local history treeview");

	return 0;
}

static int
set_reload_status(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;
	
	button = widget;
	icon_file = strdup(add_theme_path("/reload.png"));	
	fbtk_set_bitmap(widget, load_bitmap(add_theme_path("/reload_h.png")));
	
	gui_window_set_status(bw->window, "Reload");

	return 0;
}

static int
set_home_status(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;
	
	button = widget;
	icon_file = strdup(add_theme_path("/home.png"));	
	fbtk_set_bitmap(widget, load_bitmap(add_theme_path("/home_h.png")));
	
	gui_window_set_status(bw->window, "Go to homepage");

	return 0;
}


static int
set_add_fav_status(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;
	
	button = widget;
	icon_file = strdup(add_theme_path("/add_fav.png"));	
	fbtk_set_bitmap(widget, load_bitmap(add_theme_path("/add_fav_h.png")));
	
	gui_window_set_status(bw->window, "Add current page to favourites");

	return 0;
}

static int
set_add_bookmark_status(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;
	
	button = widget;
	icon_file = strdup(add_theme_path("/add_bookmark.png"));	
	fbtk_set_bitmap(widget, load_bitmap(add_theme_path("/add_bookmark_h.png")));
	
	gui_window_set_status(bw->window, "Add current page to bookmarks");

	return 0;
}

static int
set_bookmarks_status(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;
	
	button = widget;
	icon_file = strdup(add_theme_path("/bookmarks.png"));	
	fbtk_set_bitmap(widget, load_bitmap(add_theme_path("/bookmarks_h.png")));
	
	gui_window_set_status(bw->window, "Go to bookmarks");

	return 0;
}

static int
set_search_status(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;

	gui_window_set_status(bw->window, "Select search engine");

	return 0;
}

static int
set_prefs_status(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;
	
	button = widget;
	icon_file = strdup(add_theme_path("/prefs.png"));	
	fbtk_set_bitmap(widget, load_bitmap(add_theme_path("/prefs_h.png")));
	
	gui_window_set_status(bw->window, "Open preferences file in editor");

	return 0;
}

static int
set_getpage_status(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;
	
	button = widget;
	icon_file = strdup(add_theme_path("/getpage.png"));	
	fbtk_set_bitmap(widget, load_bitmap(add_theme_path("/getpage_h.png")));
	
	gui_window_set_status(bw->window, "Save current page or whole website to disk");

	return 0;
}

static int
set_openfile_status(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;
	
	button = widget;
	icon_file = strdup(add_theme_path("/openfile.png"));	
	fbtk_set_bitmap(widget, load_bitmap(add_theme_path("/openfile_h.png")));
	
	gui_window_set_status(bw->window, "Open file from disk");

	return 0;
}

static int
set_sethome_status(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;
	
	button = widget;
	icon_file = strdup(add_theme_path("/sethome.png"));	
	fbtk_set_bitmap(widget, load_bitmap(add_theme_path("/sethome_h.png")));
	
	gui_window_set_status(bw->window, "Set current site as homepage");

	return 0;
}

static int
set_getvideo_status(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;
	
	button = widget;
	icon_file = strdup(add_theme_path("/getvideo.png"));	
	fbtk_set_bitmap(widget, load_bitmap(add_theme_path("/getvideo_h.png")));
	
	gui_window_set_status(bw->window, "Play or save multimedia to disk using GetVideo plugin");

	return 0;
}

static int
set_paste_status(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;
	
	button = widget;
	icon_file = strdup(add_theme_path("/paste.png"));	
	fbtk_set_bitmap(widget, load_bitmap(add_theme_path("/paste_h.png")));
	
	gui_window_set_status(bw->window, "Paste and open URL from clipboard");

	return 0;
}

static int
set_copy_status(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;
	
	button = widget;
	icon_file = strdup(add_theme_path("/copy.png"));	
	fbtk_set_bitmap(widget, load_bitmap(add_theme_path("/copy_h.png")));
	
	gui_window_set_status(bw->window, "Copy current URL to clipboard");

	return 0;
}

static int
set_fav1_status(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;	
	gui_window_set_status(bw->window, "Favourite #01");		
	return 0;
}
static int
set_fav2_status(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;	
	gui_window_set_status(bw->window, "Favourite #02");			
	return 0;
}
static int
set_fav3_status(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;	
	gui_window_set_status(bw->window, "Favourite #03");			
	return 0;
}
static int
set_fav4_status(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;	
	gui_window_set_status(bw->window, "Favourite #04");			
	return 0;
}
static int
set_fav5_status(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;	
	gui_window_set_status(bw->window, "Favourite #05");			
	return 0;
}
static int
set_fav6_status(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;	
	gui_window_set_status(bw->window, "Favourite #06");			
	return 0;
}
static int
set_fav7_status(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;	
	gui_window_set_status(bw->window, "Favourite #07");			
	return 0;
}
static int
set_fav8_status(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;	
	gui_window_set_status(bw->window, "Favourite #08");			
	return 0;
}
static int
set_fav9_status(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;	
	gui_window_set_status(bw->window, "Favourite #09");			
	return 0;
}
static int
set_fav10_status(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;	
	gui_window_set_status(bw->window, "Favourite #10");			
	return 0;
}
static int
set_fav11_status(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;	
	gui_window_set_status(bw->window, "Favourite #11");			
	return 0;
}
static int
set_fav12_status(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	struct browser_window *bw = cbi->context;	
	gui_window_set_status(bw->window, "Favourite #12");			
	return 0;
}

#endif /*__AMIGA__*/


/** Create a toolbar window and populate it with buttons. 
 *
 * The toolbar layout uses a character to define buttons type and position:
 * b - back
 * l - local history
 * f - forward
 * s - stop 
 * r - refresh
 * u - url bar expands to fit remaining space
 * t - throbber/activity indicator
 * c - close the current window
 *
 * The default layout is "blfsrut" there should be no more than a
 * single url bar entry or behaviour will be undefined.
 *
 * @param gw Parent window 
 * @param toolbar_height The height in pixels of the toolbar
 * @param padding The padding in pixels round each element of the toolbar
 * @param frame_col Frame colour.
 * @param toolbar_layout A string defining which buttons and controls
 *                       should be added to the toolbar. May be empty
 *                       string to disable the bar..
 * 
 */
#undef FB_FRAME_COLOUR
#define FB_FRAME_COLOUR 0xFFd6f1f4

static fbtk_widget_t *
create_toolbar(struct gui_window *gw, 
	       int toolbar_height, 
	       int padding, 
	       colour frame_col,
	       const char *toolbar_layout)
{
	fbtk_widget_t *toolbar;

	fbtk_widget_t *widget;

	int xpos; /* The position of the next widget. */
	int xlhs = 0; /* extent of the left hand side widgets */
	int xdir = 1; /* the direction of movement + or - 1 */
	int width = 0; /* width of widget */
	int text_w = 0;
	const char *itmtype; /* type of the next item */

	char *label = strdup("12345678901234567890");
	
	if (toolbar_layout == NULL) {
		toolbar_layout = NSFB_TOOLBAR_DEFAULT_LAYOUT;
	}

	LOG(("Using toolbar layout %s", toolbar_layout));

	itmtype = toolbar_layout;

	if (*itmtype == 0) {
		return NULL;
	}

	toolbar = fbtk_create_window(gw->window, 0, 0, 0, 
				     toolbar_height, 
				     frame_col);

	if (toolbar == NULL) {
		return NULL;
	}

	fbtk_set_handler(toolbar, 
			 FBTK_CBT_POINTERENTER, 
			 set_ptr_default_move, 
			 NULL);


	xpos = padding;

	/* loop proceeds creating widget on the left hand side until
	 * it runs out of layout or encounters a url bar declaration
	 * wherupon it works backwards from the end of the layout
	 * untill the space left is for the url bar
	 */
	while ((itmtype >= toolbar_layout) && 
	       (*itmtype != 0) && 
	       (xdir !=0)) {

		LOG(("toolbar adding %c", *itmtype));


		switch (*itmtype) {

		case 'b': /* back */
			widget = fbtk_create_button(toolbar, 
						    (xdir == 1) ? xpos : 
						     xpos - left_arrow.width, 
						    padding, 
						    22, 
						    22, 
						    frame_col, 
						    load_bitmap(add_theme_path("/back_g.png")), 
						    fb_leftarrow_click, 
						    gw);
			gw->back = widget; /* keep reference */
			fbtk_set_handler(widget, FBTK_CBT_POINTERENTER, 
				 set_back_status, gw->bw);					
			break;

		case 'l': /* local history */
			widget = fbtk_create_button(toolbar,
						    (xdir == 1) ? xpos : 
						     xpos - left_arrow.width,
						    padding,
						    22,
						    22,
						    frame_col,
						    load_bitmap(add_theme_path("/history.png")), 
						    fb_localhistory_btn_clik,

						    gw);							
			fbtk_set_handler(widget, FBTK_CBT_POINTERENTER, 
				 set_local_status, gw->bw);							
			break;

		case 'f': /* forward */
			widget = fbtk_create_button(toolbar,
						    (xdir == 1)?xpos : 
						    xpos - right_arrow.width,
						    padding,
						    22,
						    22,
						    frame_col,
						    load_bitmap(add_theme_path("/forward_g.png")),
						    fb_rightarrow_click,
						    gw);
			gw->forward = widget;
			fbtk_set_handler(widget, FBTK_CBT_POINTERENTER, 
				 set_forward_status, gw->bw);					
			break;

		case 'c': /* close the current window */
			widget = fbtk_create_button(toolbar,
						    (xdir == 1)?xpos : 
						    xpos - stop_image_g.width,
						    padding,
						    22,
						    22,
						    frame_col,
						    load_bitmap(add_theme_path("/close.png")),
						    fb_close_click,
						    gw->bw);
							
			fbtk_set_handler(widget, 
				 FBTK_CBT_POINTERENTER, 
				 set_close_status, 
				 gw->bw);
			 
			break;

		case 's': /* stop  */
			widget = fbtk_create_button(toolbar,
						    (xdir == 1)?xpos : 
						    xpos - stop_image.width,
						    padding,
						    22,
						    22,
						    frame_col,
						    load_bitmap(add_theme_path("/stop.png")),
						    fb_stop_click,
						    gw->bw);
							
			fbtk_set_handler(widget, 
				 FBTK_CBT_POINTERENTER, 
				 set_stop_status, 
				 gw->bw);	
				 
			break;

		case 'r': /* reload */
			widget = fbtk_create_button(toolbar,
						    (xdir == 1)?xpos : 
						    xpos - 22,
						    padding,
						    22,
						    22,
						    frame_col,
						    load_bitmap(add_theme_path("/reload_g.png")),
						    fb_reload_click,
						    gw->bw);	 
							
			fbtk_set_handler(widget, 
				 FBTK_CBT_POINTERENTER, 
				 set_reload_status, 
				 gw->bw);
				 
			break;

		case 'h': /* home */
			widget = fbtk_create_button(toolbar,
						    (xdir == 1)?xpos : 
						    xpos - stop_image.width,
						    padding,
						    22,
						    22,
						    frame_col,
						    load_bitmap(add_theme_path("/home.png")),
						    fb_home_click,
						    gw->bw);
							
			fbtk_set_handler(widget, 
				 FBTK_CBT_POINTERENTER, 
				 set_home_status, 
				 gw->bw);
										
			break;			

		case 'i': /* favicon */
			favicon = fbtk_create_bitmap(toolbar,
						    (xdir == 1)?xpos : 
						    xpos - 16,
						    5,
						    16,
						    16,
						    frame_col, 
						    load_bitmap("PROGDIR:Resources/icons/favicon.png"));
			#define WITH_FAVICON				
			break;	
			
		case 'u': /* url bar*/
			width = fbtk_get_width(gw->window) - xpos - 287;
			//url_xpos = xpos;
			//url_width = width ;

			widget = fbtk_create_writable_text(toolbar,
						    xpos,
						    padding+2,
						    width,
						    21,
						    FB_COLOUR_WHITE,
							FB_COLOUR_BLACK,
							true,
						    fb_url_enter,
						    gw->bw);
							
			fbtk_set_handler(widget, 
					 FBTK_CBT_POINTERENTER, 
					 fb_url_move, gw->bw);
					 
			/*widget = fbtk_create_text(gw->window,
				      xpos,
				      7,
				      20, 12,
				      FB_FRAME_COLOUR, FB_COLOUR_BLACK,
				      false);
			url = widget;
			fbtk_set_text(widget, "Url:");
			
			AG_Url(xpos+9,width-9);*/
			gw->url = widget;
			break;	

		case 'v': /* add to favourites button */
			widget = fbtk_create_button(toolbar,
						    fbtk_get_width(gw->window) - throbber0.width - 3 - (22 * 3) - 190,
						    padding,
						    22,
						    22,
						    frame_col,
						    load_bitmap(add_theme_path("/add_fav.png")),
						    fb_add_fav_click,
						    gw->bw);
			fbtk_set_handler(widget, FBTK_CBT_POINTERENTER, 
				 set_add_fav_status, gw->bw);								
			break;
			
		case 'a': /* add to bookmarks button */
			widget = fbtk_create_button(toolbar,
						    fbtk_get_width(gw->window) - throbber0.width - 3 - (22 * 2) - 190,
						    padding,
						    22,
						    22,
						    frame_col,
						    load_bitmap(add_theme_path("/add_bookmark.png")),
						    fb_add_bookmark_click,
						    gw->bw);
							
			fbtk_set_handler(widget, FBTK_CBT_POINTERENTER, 
				 set_add_bookmark_status, gw->bw);	
				 
			break;
			
		case 'q': /* quick search button */
			widget = fbtk_create_button(toolbar,
						    fbtk_get_width(gw->window) - throbber0.width - 3 - 22 - 190 + 3,
						    padding + 3,
						    16,
						    16,
						    frame_col,
						    load_bitmap("PROGDIR:Resources/Icons/search.png"),
						    fb_search_click,
						    gw->bw);
	
			fbtk_set_handler(widget, FBTK_CBT_POINTERENTER, 
				 set_search_status, gw->bw);						
			break;			

		case 'e': /* quick search bar*/
			widget = fbtk_create_writable_text(toolbar,
						    xpos+2,
						    padding+2,
						    185,
						    21,
						    FB_COLOUR_WHITE,
							FB_COLOUR_BLACK,
							true,
						    fb_searchbar_enter,
						    gw->bw);						   

			fbtk_set_handler(widget, 
					 FBTK_CBT_POINTERENTER, 
					 fb_url_move, gw->bw);	
			break;
			
		case 't': /* throbber/activity indicator */
			widget = fbtk_create_bitmap(toolbar,
						    fbtk_get_width(gw->window) - throbber0.width - 3,
						    padding,
						    throbber0.width,
						    throbber0.height,
						    frame_col, 
						    &throbber0);
			gw->throbber = widget;
			break;			

		case 'k': /* open bookmarks button */
			xpos = 2;
			widget = fbtk_create_button(toolbar,
						    xpos,
						    padding + 27,
						    22,
						    22,
						    frame_col,
						    load_bitmap(add_theme_path("/bookmarks.png")),
						    fb_bookmarks_click,
						    gw->bw);
							
			fbtk_set_handler(widget, 
					 FBTK_CBT_POINTERENTER, 
					 set_bookmarks_status, gw->bw);								
			break;
			
		case '1': /* fav 1 button */
			text_w = 75;
			xpos= xpos + 4;
			
			if (strlen(nsoption_charp(favourite_1_label)) < 10)
				text_w = strlen(nsoption_charp(favourite_1_label)) * 8
				+ (8 - strlen(nsoption_charp(favourite_1_label)));
			fav1 = fbtk_create_button(toolbar,
						    xpos,
						    padding + 30,
						    16,
						    16,
						    frame_col,
						    load_bitmap("PROGDIR:Resources/Icons/favicon1.png"),
						    fb_fav1_click,
						    gw->bw);
			fbtk_set_handler(fav1, FBTK_CBT_POINTERENTER, 
				 set_fav1_status, gw->bw);
				 
			widget = fbtk_create_text(gw->window,
				      xpos+22,
				      padding + 27,
				      text_w, 20,
				      FB_FRAME_COLOUR, FB_COLOUR_BLACK,
				      false);
					 			
			label = strndup(nsoption_charp(favourite_1_label), 10);			
			fbtk_set_text(widget, label);	
			
			fbtk_set_handler(widget, FBTK_CBT_CLICK, 
				 fb_fav1_click, gw->bw);	
			fbtk_set_handler(widget, FBTK_CBT_POINTERENTER, 
				 set_fav1_status, gw->bw);					 
			label1 = widget;
			break;	

		case '2': /* fav 2 button */
			text_w = 75;
			if (strlen(nsoption_charp(favourite_2_label)) < 10)
				text_w = strlen(nsoption_charp(favourite_2_label)) * 8
				+ (8 - strlen(nsoption_charp(favourite_2_label)));		
			fav2 = fbtk_create_button(toolbar,
						    xpos+30,
						    padding + 30,
						    16,
						    16,
						    frame_col,
						    load_bitmap("PROGDIR:Resources/Icons/favicon2.png"),
						    fb_fav2_click,
						    gw->bw);
			fbtk_set_handler(fav2, FBTK_CBT_POINTERENTER, 
				 set_fav2_status, gw->bw);	
				 
			widget = fbtk_create_text(gw->window,
				      xpos+52,
				      padding + 27,
				      text_w, 20,
				      FB_FRAME_COLOUR, FB_COLOUR_BLACK,
				      false);
					 			
			label = strndup(nsoption_charp(favourite_2_label), 10);		
			fbtk_set_text(widget, label);	
			
			fbtk_set_handler(widget, FBTK_CBT_CLICK, 
				 fb_fav2_click, gw->bw);	
			fbtk_set_handler(widget, FBTK_CBT_POINTERENTER, 
				 set_fav2_status, gw->bw);
			label2 = widget;				 
			break;	
			
		case '3': /* fav 3 button */
			text_w = 75;
			if (strlen(nsoption_charp(favourite_3_label)) < 10)
				text_w = strlen(nsoption_charp(favourite_3_label)) * 8
				+ (8 - strlen(nsoption_charp(favourite_3_label)));	
			fav3 = fbtk_create_button(toolbar,
						    xpos+60,
						    padding + 30,
						    16,
						    16,
						    frame_col,
						    load_bitmap("PROGDIR:Resources/Icons/favicon3.png"),
						    fb_fav3_click,
						    gw->bw);
			fbtk_set_handler(fav3, FBTK_CBT_POINTERENTER, 
				 set_fav3_status, gw->bw);		
				 
			widget = fbtk_create_text(gw->window,
				      xpos+82,
				      padding + 27,
				      text_w, 20,
				      FB_FRAME_COLOUR, FB_COLOUR_BLACK,
				      false);
					 			
			label = strndup(nsoption_charp(favourite_3_label), 10);			
			fbtk_set_text(widget, label);	
			
			fbtk_set_handler(widget, FBTK_CBT_CLICK, 
				 fb_fav3_click, gw->bw);
			fbtk_set_handler(widget, FBTK_CBT_POINTERENTER, 
				 set_fav3_status, gw->bw);	
			label3 = widget;				 
			 break;

		case '4': /* fav 4 button */
			text_w = 75;
			if (strlen(nsoption_charp(favourite_4_label)) < 10)
				text_w = strlen(nsoption_charp(favourite_4_label)) * 8 
						+ (8 - strlen(nsoption_charp(favourite_4_label)));	
			if (nsoption_int(window_width) - 280 < (xpos+90)) break;			
			fav4 = fbtk_create_button(toolbar,
						    xpos+90,
						    padding + 30,
						    16,
						    16,
						    frame_col,
						    load_bitmap("PROGDIR:Resources/Icons/favicon4.png"),
						    fb_fav4_click,
						    gw->bw);
			fbtk_set_handler(fav4, FBTK_CBT_POINTERENTER, 
				 set_fav4_status, gw->bw);		
				 
			widget = fbtk_create_text(gw->window,
				      xpos+112,
				      padding + 27,
				      text_w, 20,
				      FB_FRAME_COLOUR, FB_COLOUR_BLACK,
				      false);
					 			
			label = strndup(nsoption_charp(favourite_4_label), 10);			
			fbtk_set_text(widget, label);	
			
			fbtk_set_handler(widget, FBTK_CBT_CLICK, 
				 fb_fav4_click, gw->bw);	
			fbtk_set_handler(widget, FBTK_CBT_POINTERENTER, 
				 set_fav4_status, gw->bw);					 
			label4 = widget;				 				 
			break;	

		case '5': /* fav 5 button */
			text_w = 75;
			if (strlen(nsoption_charp(favourite_5_label)) < 10)
				text_w = strlen(nsoption_charp(favourite_5_label)) * 8 
				+ (8 - strlen(nsoption_charp(favourite_5_label)));	
			if (nsoption_int(window_width) - 280 < (xpos+120) ) break;
			fav5 = fbtk_create_button(toolbar,

						    xpos+120,
						    padding + 30,

						    16,
						    16,
						    frame_col,
						    load_bitmap("PROGDIR:Resources/Icons/favicon5.png"),
						    fb_fav5_click,
						    gw->bw);
			fbtk_set_handler(fav5, FBTK_CBT_POINTERENTER, 
				 set_fav5_status, gw->bw);		
				 
			widget = fbtk_create_text(gw->window,
				      xpos+142,
				      padding + 27,

				      text_w, 20,
				      FB_FRAME_COLOUR, FB_COLOUR_BLACK,


				      false);
					 			
			label = strndup(nsoption_charp(favourite_5_label), 10);			
			fbtk_set_text(widget, label);	
			
			fbtk_set_handler(widget, FBTK_CBT_CLICK, 
				 fb_fav5_click, gw->bw);	
			fbtk_set_handler(widget, FBTK_CBT_POINTERENTER, 
				 set_fav5_status, gw->bw);					 
			label5 = widget;				 				 
			break;	

		case '6': /* fav 6 button */
			text_w = 75;
			if (strlen(nsoption_charp(favourite_6_label)) < 10)
				text_w = strlen(nsoption_charp(favourite_6_label)) * 8
				+ (8 - strlen(nsoption_charp(favourite_6_label)));		
			if (nsoption_int(window_width) - 280 < (xpos+150) ) break;
			fav6 = fbtk_create_button(toolbar,
						    xpos+150,
						    padding + 30,
						    16,
						    16,
						    frame_col,
						    load_bitmap("PROGDIR:Resources/Icons/favicon6.png"),
						    fb_fav6_click,
						    gw->bw);
			fbtk_set_handler(fav6, FBTK_CBT_POINTERENTER, 


				 set_fav6_status, gw->bw);		
				 
			widget = fbtk_create_text(gw->window,
				      xpos+172,
				      padding + 27,
				      text_w, 20,
				      FB_FRAME_COLOUR, FB_COLOUR_BLACK,
				      false);
					 			
			label = strndup(nsoption_charp(favourite_6_label), 10);			
			fbtk_set_text(widget, label);	
			
			fbtk_set_handler(widget, FBTK_CBT_CLICK, 
				 fb_fav6_click, gw->bw);	
			fbtk_set_handler(widget, FBTK_CBT_POINTERENTER, 
				 set_fav6_status, gw->bw);					 
			label6 = widget;				 				 
			break;	


		case '7': /* fav 7 button */	
			text_w = 75;
			if (strlen(nsoption_charp(favourite_7_label)) < 10)
				text_w = strlen(nsoption_charp(favourite_7_label)) * 8
				+ (8 - strlen(nsoption_charp(favourite_7_label)));			
			if (nsoption_int(window_width) - 280 < (xpos+180) ) break;
			fav7 = fbtk_create_button(toolbar,
						    xpos+180,
						    padding + 30,
						    16,
						    16,
						    frame_col,
						    load_bitmap("PROGDIR:Resources/Icons/favicon7.png"),
						    fb_fav7_click,
						    gw->bw);
			fbtk_set_handler(fav7, FBTK_CBT_POINTERENTER, 
				 set_fav7_status, gw->bw);		
				 
			widget = fbtk_create_text(gw->window,
				      xpos+202,
				      padding + 27,
				      text_w, 20,
				      FB_FRAME_COLOUR, FB_COLOUR_BLACK,
				      false);
					 			
			label = strndup(nsoption_charp(favourite_7_label), 10);		
			fbtk_set_text(widget, label);	
			
			fbtk_set_handler(widget, FBTK_CBT_CLICK, 
				 fb_fav7_click, gw->bw);	
			fbtk_set_handler(widget, FBTK_CBT_POINTERENTER, 
				 set_fav7_status, gw->bw);				 
			label7 = widget;				 				 
			break;	

		case '8': /* fav 8 button */
			text_w = 75;
			if (strlen(nsoption_charp(favourite_8_label)) < 10)
				text_w = strlen(nsoption_charp(favourite_8_label))* 8
				+ ( 8 - strlen(nsoption_charp(favourite_8_label)));			
			if (nsoption_int(window_width) - 280 < (xpos+210) ) break;
			fav8 = fbtk_create_button(toolbar,
						    xpos+210,
						    padding + 30,
						    16,
						    16,
						    frame_col,
						    load_bitmap("PROGDIR:Resources/Icons/favicon8.png"),
						    fb_fav8_click,
						    gw->bw);
			fbtk_set_handler(fav8, FBTK_CBT_POINTERENTER, 
				 set_fav8_status, gw->bw);		
				 
			widget = fbtk_create_text(gw->window,
				      xpos+232,
				      padding + 27,
				      text_w, 20,
				      FB_FRAME_COLOUR, FB_COLOUR_BLACK,
				      false);
					 			
			label = strndup(nsoption_charp(favourite_8_label), 10);			
			fbtk_set_text(widget, label);	
			
			fbtk_set_handler(widget, FBTK_CBT_CLICK, 
				 fb_fav8_click, gw->bw);	
			fbtk_set_handler(widget, FBTK_CBT_POINTERENTER, 
				 set_fav8_status, gw->bw);						 
			label8 = widget;				 				 
			break;	

		case '9': /* fav 9 button */
			text_w = 75;
			if (strlen(nsoption_charp(favourite_9_label)) < 10)
				text_w = strlen(nsoption_charp(favourite_9_label)) * 8
				+ (8 - strlen(nsoption_charp(favourite_9_label)));		
			if (nsoption_int(window_width) - 280 < (xpos+230)) break;
			fav9 = fbtk_create_button(toolbar,
						    xpos+230,
						    padding + 30,
						    16,
						    16,
						    frame_col,
						    load_bitmap("PROGDIR:Resources/Icons/favicon9.png"),
						    fb_fav9_click,
						    gw->bw);
			fbtk_set_handler(fav9, FBTK_CBT_POINTERENTER, 
				 set_fav9_status, gw->bw);				
				 
			widget = fbtk_create_text(gw->window,
				      xpos+252,
				      padding + 27,
				      text_w, 20,
				      FB_FRAME_COLOUR, FB_COLOUR_BLACK,
				      false);
					 			
			label = strndup(nsoption_charp(favourite_9_label), 10);			
			fbtk_set_text(widget, label);	
			
			fbtk_set_handler(widget, FBTK_CBT_CLICK, 
				 fb_fav9_click, gw->bw);
			fbtk_set_handler(widget, FBTK_CBT_POINTERENTER, 
				 set_fav9_status, gw->bw);						 
			label9 = widget;				 				 
			break;	

		case 'x': /* fav 10 button */
			text_w = 75;
			if (strlen(nsoption_charp(favourite_10_label)) < 10)
				text_w = strlen(nsoption_charp(favourite_10_label)) * 8
				+ (8 - strlen(nsoption_charp(favourite_10_label)));		
			if (nsoption_int(window_width) - 280 < (xpos+250) ) break;
			fav10 = fbtk_create_button(toolbar,
						    xpos+250,
						    padding + 30,
						    16,
						    16,
						    frame_col,
						    load_bitmap("PROGDIR:Resources/Icons/favicon10.png"),
						    fb_fav10_click,
						    gw->bw);
			fbtk_set_handler(fav10, FBTK_CBT_POINTERENTER, 
				 set_fav10_status, gw->bw);		
				 
			widget = fbtk_create_text(gw->window,
				      xpos+272,
				      padding + 27,
				      text_w, 20,
				      FB_FRAME_COLOUR, FB_COLOUR_BLACK,
				      false);
					 			
			label = strndup(nsoption_charp(favourite_10_label), 10);			
			fbtk_set_text(widget, label);	
			
			fbtk_set_handler(widget, FBTK_CBT_CLICK, 
				 fb_fav10_click, gw->bw);	
			fbtk_set_handler(widget, FBTK_CBT_POINTERENTER, 
				 set_fav10_status, gw->bw);				 
			label10 = widget;				 				 
			break;			

		case 'w': /* fav 11 button */
			text_w = 75;
			if (strlen(nsoption_charp(favourite_11_label)) < 10)
				text_w = strlen(nsoption_charp(favourite_11_label)) * 8
				+ (8 - strlen(nsoption_charp(favourite_11_label)));			
			if (nsoption_int(window_width) - 280 < (xpos + 280) ) break;
			fav11 = fbtk_create_button(toolbar,
						    xpos+280,
						    padding + 30,
						    16,
						    16,
						    frame_col,
						    load_bitmap("PROGDIR:Resources/Icons/favicon11.png"),
						    fb_fav11_click,
						    gw->bw);
			fbtk_set_handler(fav11, FBTK_CBT_POINTERENTER, 
				 set_fav11_status, gw->bw);		
				 
			widget = fbtk_create_text(gw->window,
				      xpos+302,
				      padding + 27,
				      text_w, 20,
				      FB_FRAME_COLOUR, FB_COLOUR_BLACK,
				      false);
					 			
			label = strndup(nsoption_charp(favourite_11_label), 10);			
			fbtk_set_text(widget, label);	
			
			fbtk_set_handler(widget, FBTK_CBT_CLICK, 
				 fb_fav11_click, gw->bw);	
			fbtk_set_handler(widget, FBTK_CBT_POINTERENTER, 
				 set_fav11_status, gw->bw);						 
			label11 = widget;				 				 
			break;	

		case 'z': /* fav 12 button */
			text_w = 75;
			if (strlen(nsoption_charp(favourite_12_label)) < 10)
				text_w = strlen(nsoption_charp(favourite_12_label)) * 8
				+ (8 - strlen(nsoption_charp(favourite_12_label)));				
			if (nsoption_int(window_width) - 280 < (xpos+310)) break;
			fav12 = fbtk_create_button(toolbar,
						    xpos+310,
						    padding + 30,
						    16,
						    16,
						    frame_col,
						    load_bitmap("PROGDIR:Resources/Icons/favicon12.png"),
						    fb_fav12_click,
						    gw->bw);	
			fbtk_set_handler(fav12, FBTK_CBT_POINTERENTER, 
				 set_fav12_status, gw->bw);		
				 
			widget = fbtk_create_text(gw->window,
				      xpos+332,
				      padding + 27,
				      text_w, 20,
				      FB_FRAME_COLOUR, FB_COLOUR_BLACK,
				      false);
					 			
			label = strndup(nsoption_charp(favourite_12_label), 10);		
			fbtk_set_text(widget, label);	
			
			fbtk_set_handler(widget, FBTK_CBT_CLICK, 
				 fb_fav12_click, gw->bw);	
			fbtk_set_handler(widget, FBTK_CBT_POINTERENTER, 
				 set_fav12_status, gw->bw);						 
			label12 = widget;				 				 
			break;	
			
		case 'g': /* edit preferences file button */
			xpos = fbtk_get_width(gw->window) - (6*(24 + 3)) - 3;	
			widget = fbtk_create_button(toolbar,
						    xpos,
						    padding + 35,
						    17,
						    17,
						    frame_col,
						    load_bitmap(add_theme_path("/prefs.png")),
						    fb_prefs_click,
						    gw->bw);
			fbtk_set_handler(widget, FBTK_CBT_POINTERENTER, 
				 set_prefs_status, gw->bw);								
			break;	
			
		case 'd': /* download page or website button */
			widget = fbtk_create_button(toolbar,
						    xpos,
						    padding + 30,
						    22,
						    22,
						    frame_col,
						    load_bitmap(add_theme_path("/getpage.png")),
						    fb_getpage_click,
						    gw->bw);
			fbtk_set_handler(widget, FBTK_CBT_POINTERENTER, 
				 set_getpage_status, gw->bw);									
			break;	

		case 'n': /* open file button */
			widget = fbtk_create_button(toolbar,
						    xpos,
						    padding + 30,
						    22,
						    22,
						    frame_col,
						    load_bitmap(add_theme_path("/openfile.png")),
						    fb_openfile_click,
						    gw->bw);
			fbtk_set_handler(widget, FBTK_CBT_POINTERENTER, 
				 set_openfile_status, gw->bw);									
			break;	

		case 'm': /* set home button */
			widget = fbtk_create_button(toolbar,
						    xpos,
						    padding + 30,
						    22,
						    22,
						    frame_col,
						    load_bitmap(add_theme_path("/sethome.png")),
						    fb_sethome_click,
						    gw->bw);
			fbtk_set_handler(widget, FBTK_CBT_POINTERENTER, 
				 set_sethome_status, gw->bw);									
			break;		

		case 'y': /* getvideo button */
			widget = fbtk_create_button(toolbar,
						    xpos,
						    padding + 30,
						    22,
						    22,
						    frame_col,
						    load_bitmap(add_theme_path("/getvideo.png")),
						    fb_getvideo_click,
						    gw->bw);
			fbtk_set_handler(widget, FBTK_CBT_POINTERENTER, 
				 set_getvideo_status, gw->bw);									
			break;				

		case 'o': /* copy text button */
			widget = fbtk_create_button(toolbar,
						    xpos,
						    padding + 30,
						    22,
						    22,
						    frame_col,
						    load_bitmap(add_theme_path("/copy.png")),
						    fb_copy_click,
						    gw->bw);
			fbtk_set_handler(widget, FBTK_CBT_POINTERENTER, 
				 set_copy_status, gw->bw);			
			break;	
			
		case 'p': /* paste text button */
			widget = fbtk_create_button(toolbar,
						    xpos,
						    padding + 30,
						    22,
						    22,
						    frame_col,
						    load_bitmap(add_theme_path("/paste.png")),
						    fb_paste_click,
						    gw->bw);
			fbtk_set_handler(widget, FBTK_CBT_POINTERENTER, 
				 set_paste_status, gw->bw);		
			/* toolbar is complete */
			xdir = 0;				 
			break;		

					
			
			/* met url going forwards, note position and
			 * reverse direction 
			 */
			itmtype = toolbar_layout + strlen(toolbar_layout);
			xdir = -1;
			xlhs = xpos;
			xpos = (1 * fbtk_get_width(toolbar)); /* ?*/
			widget = toolbar;
	
			break;

		default:
			widget = NULL;
			xdir = 0;
			LOG(("Unknown element %c in toolbar layout", *itmtype));
		        break;

		}

		if (widget != NULL) {
			xpos += (xdir * (fbtk_get_width(widget) + padding));
		}

		LOG(("xpos is %d",xpos));

		itmtype += xdir;
	}

	fbtk_set_mapping(toolbar, true);

	return toolbar;
}

/** Routine called when "stripped of focus" event occours for browser widget.
 *
 * @param widget The widget reciving "stripped of focus" event.
 * @param cbi The callback parameters.
 * @return The callback result.
 */
static int
fb_browser_window_strip_focus(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	fbtk_set_caret(widget, false, 0, 0, 0, NULL);

	return 0;
}

static void
create_browser_widget(struct gui_window *gw, int toolbar_height, int furniture_width)
{
	struct browser_widget_s *browser_widget;
	browser_widget = calloc(1, sizeof(struct browser_widget_s));

	gw->browser = fbtk_create_user(gw->window,
				       0,
				       toolbar_height,
				       -furniture_width,
				       -furniture_width,
				       browser_widget);

	fbtk_set_handler(gw->browser, FBTK_CBT_REDRAW, fb_browser_window_redraw, gw);
	fbtk_set_handler(gw->browser, FBTK_CBT_INPUT, fb_browser_window_input, gw);
	fbtk_set_handler(gw->browser, FBTK_CBT_CLICK, fb_browser_window_click, gw);

	fbtk_set_handler(gw->browser, FBTK_CBT_POINTERMOVE, fb_browser_window_move, gw);
}

static void
create_normal_browser_window(struct gui_window *gw, int furniture_width)
{
	fbtk_widget_t *widget;
	fbtk_widget_t *toolbar;
	int statusbar_width = 0;
	int toolbar_height = nsoption_int(fb_toolbar_size);

	LOG(("Normal window"));

	gw->window = fbtk_create_window(fbtk, 0, 0, 0, 0, 0);

	statusbar_width = nsoption_int(toolbar_status_width) *
		fbtk_get_width(gw->window) / 10000;

		
	/* toolbar */
	toolbar = create_toolbar(gw, 
				 toolbar_height, 
				 2, 
				 FB_FRAME_COLOUR, 
				 nsoption_charp(fb_toolbar_layout));

	printf((char *)FB_FRAME_COLOUR);
	/* set the actually created toolbar height */
	if (toolbar != NULL) {
		toolbar_height = fbtk_get_height(toolbar);
	} else {
		toolbar_height = 0;
	}

	/* status bar */
	gw->status = fbtk_create_text(gw->window,
				      0,
				      fbtk_get_height(gw->window) - furniture_width,
				      statusbar_width, furniture_width,
				      FB_FRAME_COLOUR, FB_COLOUR_BLACK,
				      false);
	//fbtk_set_handler(gw->status, FBTK_CBT_POINTERENTER, set_ptr_default_move, NULL);

	LOG(("status bar %p at %d,%d", gw->status, fbtk_get_absx(gw->status), fbtk_get_absy(gw->status)));

	/* create horizontal scrollbar */
	gw->hscroll = fbtk_create_hscroll(gw->window,
					  statusbar_width,
					  fbtk_get_height(gw->window) - furniture_width,
					  fbtk_get_width(gw->window) - statusbar_width - furniture_width,
					  furniture_width,

					  nsoption_colour(sys_colour_Scrollbar),
					  FB_FRAME_COLOUR,
					  fb_scroll_callback,
					  gw);

	/* fill bottom right area */


	if (nsoption_charp(fb_osk) == true) {
		widget = fbtk_create_text_button(gw->window,
						 fbtk_get_width(gw->window) - furniture_width,
						 fbtk_get_height(gw->window) - furniture_width,
						 furniture_width,
						 furniture_width,
						 FB_FRAME_COLOUR, FB_COLOUR_BLACK,
						 fb_osk_click,
						 NULL);
		fbtk_set_text(widget, "\xe2\x8c\xa8");
	} else { 
		widget = fbtk_create_fill(gw->window,
					  fbtk_get_width(gw->window) - furniture_width,
					  fbtk_get_height(gw->window) - furniture_width,
					  furniture_width,
					  furniture_width,
					  FB_FRAME_COLOUR);

		fbtk_set_handler(widget, FBTK_CBT_POINTERENTER, set_ptr_default_move, NULL);
	}

	
	
	/* create vertical scrollbar */
	gw->vscroll = fbtk_create_vscroll(gw->window,
					  fbtk_get_width(gw->window) - furniture_width,
					  toolbar_height,
					  furniture_width,
					  fbtk_get_height(gw->window) - toolbar_height - furniture_width,

					  nsoption_colour(sys_colour_Scrollbar),
					  FB_FRAME_COLOUR,
					  fb_scroll_callback,
					  gw);

	/* browser widget */
	create_browser_widget(gw, toolbar_height, nsoption_int(fb_furniture_size));

	/* Give browser_window's user widget input focus */
	fbtk_set_focus(gw->browser);
}


struct gui_window *
gui_create_browser_window(struct browser_window *bw,
			  struct browser_window *clone,
			  bool new_tab)
{
	struct gui_window *gw;

	gw = calloc(1, sizeof(struct gui_window));

	if (gw == NULL)
		return NULL;

	/* seems we need to associate the gui window with the underlying
	 * browser window
	 */
	gw->bw = bw;

	create_normal_browser_window(gw, nsoption_int(fb_furniture_size));
	gw->localhistory = fb_create_localhistory(bw, fbtk, nsoption_int(fb_furniture_size));

	/* map and request redraw of gui window */
	fbtk_set_mapping(gw->window, true);

	return gw;
}

void
gui_window_destroy(struct gui_window *gw)
{
	fbtk_destroy_widget(gw->window);

	free(gw);
}

void
gui_window_set_title(struct gui_window *g, const char *title)
{
	stitle = strdup(title);
	utf8_to_local_encoding(title,strlen(title),&stitle);
	strcat(stitle," - NetSurf");
	SDL_WM_SetCaption(stitle, "NetSurf");
	utf8_to_local_encoding(title,strlen(title),&stitle);	
	stitle = strdup(title);
	
	LOG(("%p, %s", g, title));
}

void
gui_window_redraw(struct gui_window *g, int x0, int y0, int x1, int y1)
{
	fb_queue_redraw(g->browser, x0, y0, x1, y1);
}

void
gui_window_redraw_window(struct gui_window *g)
{
	fb_queue_redraw(g->browser, 0, 0, fbtk_get_width(g->browser), fbtk_get_height(g->browser) );
	g_ui = g;
}

void
gui_window_update_box(struct gui_window *g, const struct rect *rect)
{
	struct browser_widget_s *bwidget = fbtk_get_userpw(g->browser);
	fb_queue_redraw(g->browser,
			rect->x0 - bwidget->scrollx,
			rect->y0 - bwidget->scrolly,
			rect->x1 - bwidget->scrollx,
			rect->y1 - bwidget->scrolly);
}

bool
gui_window_get_scroll(struct gui_window *g, int *sx, int *sy)
{
	struct browser_widget_s *bwidget = fbtk_get_userpw(g->browser);

	*sx = bwidget->scrollx / g->bw->scale;
	*sy = bwidget->scrolly / g->bw->scale;

	return true;
}

void
gui_window_set_scroll(struct gui_window *gw, int sx, int sy)
{
	struct browser_widget_s *bwidget = fbtk_get_userpw(gw->browser);

	assert(bwidget);

	widget_scroll_x(gw, sx * gw->bw->scale, true);
	widget_scroll_y(gw, sy * gw->bw->scale, true);
}

void
gui_window_scroll_visible(struct gui_window *g, int x0, int y0,
			  int x1, int y1)
{
	LOG(("%s:(%p, %d, %d, %d, %d)", __func__, g, x0, y0, x1, y1));
}

void
gui_window_position_frame(struct gui_window *g, int x0, int y0, int x1, int y1)
{
	struct gui_window *parent;
	int px, py;
	int w, h;
	LOG(("%s: %d, %d, %d, %d", g->bw->name, x0, y0, x1, y1));
	parent = g->bw->parent->window;

	if (parent->window == NULL)
		return; /* doesnt have an fbtk widget */

	px = fbtk_get_absx(parent->browser) + x0;
	py = fbtk_get_absy(parent->browser) + y0;
	w = x1 - x0;
	h = y1 - y0;
	if (w > (fbtk_get_width(parent->browser) - px))
		w = fbtk_get_width(parent->browser) - px;

	if (h > (fbtk_get_height(parent->browser) - py))
		h = fbtk_get_height(parent->browser) - py;

	fbtk_set_pos_and_size(g->window, px, py , w , h);

	fbtk_request_redraw(parent->browser);

}

void
gui_window_get_dimensions(struct gui_window *g,
			  int *width,
			  int *height,
			  bool scaled)
{
	*width = fbtk_get_width(g->browser);
	*height = fbtk_get_height(g->browser);

	if (scaled) {
		*width /= g->bw->scale;
		*height /= g->bw->scale;
	}
}

void
gui_window_update_extent(struct gui_window *gw)
{
	float scale = gw->bw->scale;


	fbtk_set_scroll_parameters(gw->hscroll, 0,
			content_get_width(gw->bw->current_content) * scale,
			fbtk_get_width(gw->browser), 100);

	fbtk_set_scroll_parameters(gw->vscroll, 0,
			content_get_height(gw->bw->current_content) * scale,
			fbtk_get_height(gw->browser), 100);
}

void
gui_window_set_status(struct gui_window *g, const char *text)
{
	fbtk_set_text(g->status, text);
	status_txt = strdup(text);
}

void 
gui_window_set_pointer(struct gui_window *g, gui_pointer_shape shape)
{
	switch (shape) {
	case GUI_POINTER_POINT:
		framebuffer_set_cursor(&hand_image);
		SDL_ShowCursor(SDL_DISABLE);
		break;

	case GUI_POINTER_CARET:
		framebuffer_set_cursor(&caret_image);
		SDL_ShowCursor(SDL_DISABLE);
		break;

	case GUI_POINTER_MENU:
		framebuffer_set_cursor(&menu_image);
		SDL_ShowCursor(SDL_ENABLE);        
		break;

	case GUI_POINTER_PROGRESS:
		framebuffer_set_cursor(&progress_image);
		SDL_ShowCursor(SDL_DISABLE);                
		break;

				
	case GUI_POINTER_MOVE:
		framebuffer_set_cursor(&hand_image);
		SDL_ShowCursor(SDL_DISABLE); 
		break;
        default:	            
				framebuffer_set_cursor(NULL);
				SDL_ShowCursor(SDL_ENABLE);		
		break;			           
        }
}

void
gui_window_hide_pointer(struct gui_window *g)
{
	SDL_ShowCursor(SDL_DISABLE);
}

void
gui_window_set_url(struct gui_window *g, const char *url)
{
	fbtk_set_text(g->url, url);
}

static void
throbber_advance(void *pw)
{
	struct gui_window *g = pw;
	struct fbtk_bitmap *image;

	switch (g->throbber_index) {
	case 0:
		image = &throbber1;
		g->throbber_index = 1;
		break;

	case 1:
		image = &throbber2;
		g->throbber_index = 2;
		break;

	case 2:
		image = &throbber3;
		g->throbber_index = 3;
		break;

	case 3:
		image = &throbber4;
		g->throbber_index = 4;
		break;

	case 4:
		image = &throbber5;
		g->throbber_index = 5;
		break;

	case 5:
		image = &throbber6;
		g->throbber_index = 6;
		break;

	case 6:
		image = &throbber7;
		g->throbber_index = 7;
		break;

	case 7:
		image = &throbber8;

		g->throbber_index = 8;
		break;

	case 8:
		image = &throbber9;
		g->throbber_index = 9;
		break;

	case 9:
		image = &throbber10;
		g->throbber_index = 10;
		break;

	case 10:
		image = &throbber11;
		g->throbber_index = 11;
		break;
		
	case 11:
		image = &throbber12;
		g->throbber_index = 12;
		break;	
		
	case 12:
		image = &throbber0;
		g->throbber_index = 0;
		break;			
	default:
		return;
	}

	if (g->throbber_index >= 0) {
		fbtk_set_bitmap(g->throbber, image);
		schedule(10, throbber_advance, g);
	}
}

void
gui_window_start_throbber(struct gui_window *g)
{
	g->throbber_index = 0;
	schedule(10, throbber_advance, g);
}

void
gui_window_stop_throbber(struct gui_window *gw)
{
	gw->throbber_index = -1;
	fbtk_set_bitmap(gw->throbber, &throbber0);

	fb_update_back_forward(gw);

}

static void
gui_window_remove_caret_cb(fbtk_widget_t *widget)
{
	struct browser_widget_s *bwidget = fbtk_get_userpw(widget);
	int c_x, c_y, c_h;

	if (fbtk_get_caret(widget, &c_x, &c_y, &c_h)) {
		/* browser window already had caret:
		 * redraw its area to remove it first */
		fb_queue_redraw(widget,
				c_x - bwidget->scrollx,
				c_y - bwidget->scrolly,
				c_x + 1 - bwidget->scrollx,
				c_y + c_h - bwidget->scrolly);
	}

}

void
gui_window_place_caret(struct gui_window *g, int x, int y, int height)
{
	struct browser_widget_s *bwidget = fbtk_get_userpw(g->browser);

	/* set new pos */
	fbtk_set_caret(g->browser, true, x, y, height,
			gui_window_remove_caret_cb);

	/* redraw new caret pos */
	fb_queue_redraw(g->browser,
			x - bwidget->scrollx,
			y - bwidget->scrolly,
			x + 1 - bwidget->scrollx,
			y + height - bwidget->scrolly);
}

void
gui_window_remove_caret(struct gui_window *g)
{
	int c_x, c_y, c_h;

	if (fbtk_get_caret(g->browser, &c_x, &c_y, &c_h)) {
		/* browser window owns the caret, so can remove it */
		fbtk_set_caret(g->browser, false, 0, 0, 0, NULL);
	}
}

void
gui_window_new_content(struct gui_window *g)
{
}

bool
gui_window_scroll_start(struct gui_window *g)
{
	return true;
}

bool
gui_window_drag_start(struct gui_window *g, gui_drag_type type,
					  const struct rect *rect)
{
	return true;
}

bool
gui_window_frame_resize_start(struct gui_window *g)
{
	LOG(("resize frame\n"));
	return true;
}

void
gui_window_save_link(struct gui_window *g, const char *url, const char *title)
{
}

void
gui_window_set_scale(struct gui_window *g, float scale)
{
	LOG(("set scale\n"));
}

struct gui_window *g_ico;
void *
download_icon(void *argument){
	/* Download favicon */		
	
	char *wget = AllocVec(1000,MEMF_CLEAR | MEMF_ANY);
	char *url = strdup((char *)hlcache_handle_get_url(g_ico->bw->current_content));
	
	strcpy(wget, "wget ");
	strcat(wget, "-PPROGDIR:Resources/Icons/ ");
	
	strcat(wget, " http://fvicon.com/");		
	strcat(wget, url);
	strcat(wget, "?format=png&width=16&height=16 ");
	strcat(wget, "-OResources/Icons/favicon.png ");			   

	Execute(wget, 0, 0);
	
	fbtk_set_bitmap(favicon, load_bitmap("PROGDIR:Resources/Icons/favicon.png"));
	
	FreeVec(wget);	
	
	return NULL;
}

/**
 * set favicon
 */
void
gui_window_set_icon(struct gui_window *g, hlcache_handle *icon)
{
}

/**
 * set gui display of a retrieved favicon representing the search provider
 * \param ico may be NULL for local calls; then access current cache from
 * search_web_ico()
 */
void
gui_window_set_search_ico(hlcache_handle *ico)
{
}

struct gui_download_window *
gui_download_window_create(download_context *ctx, struct gui_window *parent)
{
	BPTR in;
	char *url = strdup(download_context_get_url(ctx));
	BPTR fh;
	
	if (strcmp(nsoption_charp(download_path), "")  == 0 )
		{
		AslBase = OpenLibrary("asl.library", 0);
		struct FileRequester *savereq;

		savereq = (struct FileRequester *)AllocAslRequestTags(ASL_FileRequest,
							ASLFR_DoSaveMode,TRUE,
							ASLFR_RejectIcons,TRUE,
							ASLFR_InitialDrawer,0,
							TAG_DONE);
		AslRequest(savereq,NULL);
		nsoption_charp(download_path) = strdup(savereq->fr_Drawer);
		nsoption_write("PROGDIR:Resources/Options");

		FreeAslRequest(savereq);
		CloseLibrary(AslBase);
		}
	char *wget = AllocVec(1000,MEMF_CLEAR | MEMF_ANY);		
	
	if (mouse_2_click == 1)	{
	
		mouse_2_click = 0;	

		strcpy(wget, "echo \" run wget ");
		strcat(wget, "-P");
		strcat(wget, nsoption_charp(download_path));
		strcat(wget, " ");
	
		strcat(wget,url);	

		strcat(wget, " \" >T:nsscript");

		fh = Open("CON:", MODE_NEWFILE);

		Execute(wget, 0, 0);
		Execute("echo \"endcli\" >>T:nsscript", 0, 0);
		Execute("execute T:nsscript", 0, 0);
		Execute("delete >nil: T:nsscript", 0, 0);

		Close(fh);
		}
	else {
		strcpy(wget, " run sys:c/httpresume ");
		strcat(wget, " ");
		strcat(wget, url);
		strcat(wget, " GUI STARTDIR=");
		strcat(wget, nsoption_charp(download_path));	
		
		Execute(wget, 0, 0);
		}
	
	free(url);
	FreeVec(wget);
	
	return NULL;
}

nserror
gui_download_window_data(struct gui_download_window *dw,
			 const char *data,
			 unsigned int size)
{
	return NSERROR_OK;
}

void
gui_download_window_error(struct gui_download_window *dw,
			  const char *error_msg)
{
}

void
gui_download_window_done(struct gui_download_window *dw)
{
}

void
gui_drag_save_object(gui_save_type type,
		     hlcache_handle *c,
		     struct gui_window *w)
{
}

void
gui_drag_save_selection(struct selection *s, struct gui_window *g)
{
}

void
gui_start_selection(struct gui_window *g)
{
}

void
gui_clear_selection(struct gui_window *g)
{
}


void
gui_create_form_select_menu(struct browser_window *bw,
			    struct form_control *control)
{
}

void
gui_launch_url(const char *url)
{
	if ((!strncmp("mailto:", url, 7)) || (!strncmp("ftp:", url, 4)))
	{
		if (OpenURLBase == NULL)
			OpenURLBase = OpenLibrary("openurl.library", 6);

		if (OpenURLBase)		{
			URL_OpenA((STRPTR)url, NULL);
			CloseLibrary(OpenURLBase);	
			}
	}
}

void
gui_cert_verify(nsurl*url,
		const struct ssl_cert_info *certs,
		unsigned long num,
		nserror (*cb)(bool proceed, void *pw),
		void *cbpw)
{
	cb(false, cbpw);
}
		
void PDF_Password(char **owner_pass, char **user_pass, char *path)
{
	/*TODO:this waits to be written, until then no PDF encryption*/
	*owner_pass = NULL;
}

void gui_options_init_defaults(void)
{
}

/*
 * Local Variables:
 * c-basic-offset:8
 * End:
 */
