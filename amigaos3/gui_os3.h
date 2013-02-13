/* 
AmigaOS 3 related 
*/

#include "image/ico.h"

	char *status_txt;
	char *icon_file;	
	char *stitle;
	char *ReadClip( void );
	int WriteClip(const char * );
	int alt, ctrl, shift, x_pos, selected;
	
	struct fbtk_bitmap *load_bitmap(const char *filename);
  	struct fbtk_bitmap *favicon_bitmap;
	
    void get_video(struct browser_window *bw);  
 
	int fb_redraw_bitmap(fbtk_widget_t *widget, fbtk_callback_info *cbi);
	int fb_leftarrow_click(fbtk_widget_t *widget, fbtk_callback_info *cbi);
	int fb_rightarrow_click(fbtk_widget_t *widget, fbtk_callback_info *cbi);
	int fb_reload_click(fbtk_widget_t *widget, fbtk_callback_info *cbi);
	int fb_stop_click(fbtk_widget_t *widget, fbtk_callback_info *cbi);
	int fb_home_click(fbtk_widget_t *widget, fbtk_callback_info *cbi);
	int fb_copy_click(fbtk_widget_t *widget, fbtk_callback_info *cbi);
	int fb_paste_click(fbtk_widget_t *widget, fbtk_callback_info *cbi);
	int fb_getvideo_click(fbtk_widget_t *widget, fbtk_callback_info *cbi);
	int fb_sethome_click(fbtk_widget_t *widget, fbtk_callback_info *cbi);
	int fb_screenmode_click(fbtk_widget_t *widget, fbtk_callback_info *cbi);
	int fb_add_bookmark_click(fbtk_widget_t *widget, fbtk_callback_info *cbi);
	int fb_search_click(fbtk_widget_t *widget, fbtk_callback_info *cbi);
	int fb_bookmarks_click(fbtk_widget_t *widget, fbtk_callback_info *cbi);
	int fb_getpage_click(fbtk_widget_t *widget, fbtk_callback_info *cbi);
	int fb_openfile_click(fbtk_widget_t *widget, fbtk_callback_info *cbi);
	int fb_searchbar_enter(void *pw, char *text);
	int fb_fav1_click(fbtk_widget_t *widget, fbtk_callback_info *cbi);
	int fb_fav2_click(fbtk_widget_t *widget, fbtk_callback_info *cbi);
	int fb_fav3_click(fbtk_widget_t *widget, fbtk_callback_info *cbi);
	int fb_fav4_click(fbtk_widget_t *widget, fbtk_callback_info *cbi);
	int fb_fav5_click(fbtk_widget_t *widget, fbtk_callback_info *cbi);
	int fb_fav6_click(fbtk_widget_t *widget, fbtk_callback_info *cbi);
	int fb_fav7_click(fbtk_widget_t *widget, fbtk_callback_info *cbi);
	int fb_fav8_click(fbtk_widget_t *widget, fbtk_callback_info *cbi);
	int fb_fav9_click(fbtk_widget_t *widget, fbtk_callback_info *cbi);
	int fb_fav10_click(fbtk_widget_t *widget, fbtk_callback_info *cbi);
	int fb_fav11_click(fbtk_widget_t *widget, fbtk_callback_info *cbi);
	int fb_fav12_click(fbtk_widget_t *widget, fbtk_callback_info *cbi);

	fbtk_widget_t *fav1;
	fbtk_widget_t *fav2;
	fbtk_widget_t *fav3;
	fbtk_widget_t *fav4;
	fbtk_widget_t *fav5;
	fbtk_widget_t *fav6;
	fbtk_widget_t *fav7;
	fbtk_widget_t *fav8;
	fbtk_widget_t *fav9;
	fbtk_widget_t *fav10;
	fbtk_widget_t *fav11;
	fbtk_widget_t *fav12;
	
	fbtk_widget_t *label1;
	fbtk_widget_t *label2;
	fbtk_widget_t *label3;
	fbtk_widget_t *label4;
	fbtk_widget_t *label5;
	fbtk_widget_t *label6;
	fbtk_widget_t *label7;	
	fbtk_widget_t *label8;
	fbtk_widget_t *label9;
	fbtk_widget_t *label10;
	fbtk_widget_t *label11;
	fbtk_widget_t *label12;

	fbtk_widget_t *toolbar;
	fbtk_widget_t *favicon;
	fbtk_widget_t *button;
	fbtk_widget_t *url;
	
	extern struct fbtk_bitmap throbber9;
	extern struct fbtk_bitmap throbber10;
	extern struct fbtk_bitmap throbber11;
	extern struct fbtk_bitmap throbber12;