/*
 * Copyright 2011 Sven Weidauer <sven.weidauer@gmail.com>
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

#import "BrowserView.h"

#import "desktop/browser.h"
#import "desktop/history_core.h"
#import "desktop/plotters.h"
#import "desktop/textinput.h"

@implementation BrowserView

@synthesize browser;
@synthesize spinning;
@synthesize status;
@synthesize caretTimer;

static const CGFloat CaretWidth = 1.0;
static const NSTimeInterval CaretBlinkTime = 0.8;

static inline NSRect cocoa_get_caret_rect( BrowserView *view )
{
	NSRect caretRect = {
		.origin = view->caretPoint,
		.size = NSMakeSize( CaretWidth, view->caretHeight )
	};
	
	return caretRect;
}

- (void) removeCaret;
{
	hasCaret = NO;
	[self setNeedsDisplayInRect: cocoa_get_caret_rect( self )];

	[caretTimer invalidate];
	[self setCaretTimer: nil];
}

- (void) addCaretAt: (NSPoint) point height: (CGFloat) height;
{
	if (hasCaret) {
		[self setNeedsDisplayInRect: cocoa_get_caret_rect( self )];
	}
	
	caretPoint = point;
	caretHeight = height;
	hasCaret = YES;
	caretVisible = YES;
	
	if (nil == caretTimer) {
		[self setCaretTimer: [NSTimer scheduledTimerWithTimeInterval: CaretBlinkTime target: self selector: @selector(caretBlink:) userInfo: nil repeats: YES]];
	} else {
		[caretTimer setFireDate: [NSDate dateWithTimeIntervalSinceNow: CaretBlinkTime]];
	}
	
	[self setNeedsDisplayInRect: cocoa_get_caret_rect( self )];
}
		 
		 
- (void) caretBlink: (NSTimer *)timer;
{
	if (hasCaret) {
		caretVisible = !caretVisible;
		[self setNeedsDisplayInRect: cocoa_get_caret_rect( self )];
	}
}

- (void)drawRect:(NSRect)dirtyRect; 
{

	if (NULL == browser->current_content) return;
	
	NSRect frame = [self bounds];

	plot.clip(0, 0, frame.size.width, frame.size.height);

	
	const NSRect *rects = NULL;
	NSInteger count = 0;
	[self getRectsBeingDrawn: &rects count: &count];
	
	for (NSInteger i = 0; i < count; i++) {
		content_redraw(browser->current_content,
					   0,
					   0,
					   NSWidth( frame ),
					   NSHeight( frame ),
					   NSMinX( rects[i] ),
					   NSMinY( rects[i] ),
					   NSMaxX( rects[i] ),
					   NSMaxY( rects[i] ),
					   browser->scale,
					   0xFFFFFF);
	}

	NSRect caretRect = cocoa_get_caret_rect( self );
	if (hasCaret && caretVisible && [self needsToDrawRect: caretRect]) {
		[[NSColor blackColor] set];
		[NSBezierPath fillRect: caretRect];
	}
	
}

- (BOOL) isFlipped;
{
	return YES;
}

- (void) mouseDown: (NSEvent *)theEvent;
{
	NSPoint location = [self convertPoint: [theEvent locationInWindow] fromView: nil];

	browser_window_mouse_click( browser, BROWSER_MOUSE_PRESS_1, location.x, location.y );
}

- (void) mouseUp: (NSEvent *)theEvent;
{
	NSPoint location = [self convertPoint: [theEvent locationInWindow] fromView: nil];
	
	browser_window_mouse_click( browser, BROWSER_MOUSE_CLICK_1, location.x, location.y );
}

- (void) mouseDragged: (NSEvent *)theEvent;
{
}

- (void) mouseMoved: (NSEvent *)theEvent;
{
	NSPoint location = [self convertPoint: [theEvent locationInWindow] fromView: nil];
	browser_window_mouse_click( browser, 0, location.x, location.y );
}

- (void) keyDown: (NSEvent *)theEvent;
{
	[self interpretKeyEvents: [NSArray arrayWithObject: theEvent]];
}

- (void) insertText: (id)string;
{
	for (NSUInteger i = 0, length = [string length]; i < length; i++) {
		unichar ch = [string characterAtIndex: i];
		browser_window_key_press( browser, ch );
	}
}

- (void) moveLeft: (id)sender;
{
	browser_window_key_press( browser, KEY_LEFT );
}

- (void) moveRight: (id)sender;
{
	browser_window_key_press( browser, KEY_RIGHT );
}

- (void) moveUp: (id)sender;
{
	browser_window_key_press( browser, KEY_UP );
}

- (void) moveDown: (id)sender;
{
	browser_window_key_press( browser, KEY_DOWN );
}

- (void) deleteBackward: (id)sender;
{
	browser_window_key_press( browser, KEY_DELETE_LEFT );
}

- (void) deleteForward: (id)sender;
{
	browser_window_key_press( browser, KEY_DELETE_RIGHT );
}

- (void) cancelOperation: (id)sender;
{
	browser_window_key_press( browser, KEY_ESCAPE );
}

- (void) scrollPageUp: (id)sender;
{
	browser_window_key_press( browser, KEY_PAGE_UP );
}

- (void) scrollPageDown: (id)sender;
{
	browser_window_key_press( browser, KEY_PAGE_DOWN );
}

- (void) insertTab: (id)sender;
{
	browser_window_key_press( browser, KEY_TAB );
}

- (void) insertBacktab: (id)sender;
{
	browser_window_key_press( browser, KEY_SHIFT_TAB );
}

- (void) moveToBeginningOfLine: (id)sender;
{
	browser_window_key_press( browser, KEY_LINE_START );
}

- (void) moveToEndOfLine: (id)sender;
{
	browser_window_key_press( browser, KEY_LINE_END );
}

- (void) moveToBeginningOfDocument: (id)sender;
{
	browser_window_key_press( browser, KEY_TEXT_START );
}

- (void) moveToEndOfDocument: (id)sender;
{
	browser_window_key_press( browser, KEY_TEXT_END );
}

- (void) insertNewline: (id)sender;
{
	browser_window_key_press( browser, KEY_NL );
}

- (void) setFrame: (NSRect)frameRect;
{
	[super setFrame: frameRect];
	browser_window_reformat( browser, [self bounds].size.width, [self bounds].size.height );
}


- (IBAction) goBack: (id) sender;
{
	if (browser && history_back_available( browser->history )) {
		history_back(browser, browser->history);
	}
}

- (IBAction) goForward: (id) sender;
{
	if (browser && history_forward_available( browser->history )) {
		history_forward(browser, browser->history);
	}
}

- (IBAction) showHistory: (id) sender;
{
}

- (IBAction) reloadPage: (id) sender;
{
	browser_window_reload( browser, true );
}

- (BOOL) validateToolbarItem: (NSToolbarItem *)theItem;
{
	SEL action = [theItem action];
	
	if (action == @selector( goBack: )) {
		return browser != NULL && history_back_available( browser->history );
	} 
	
	if (action == @selector( goForward: )) {
		return browser != NULL && history_forward_available( browser->history );
	}
	
	if (action == @selector( reloadPage: )) {
		return browser_window_reload_available( browser );
	}
	
	return YES;
}
- (BOOL) acceptsFirstResponder;
{
	return YES;
}

@end