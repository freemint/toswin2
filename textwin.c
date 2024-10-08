/* This is the mother of all bastard code.  It mixes character (cell)
 * calculations with pixel shuffling and the result makes every sane
 * human being copiously vomit!
 *
 * A much better organization would split this monster up into three
 * different classes:  A character grid that exposes a vertically
 * scrollable region, keeps track of the cursor position and so on.
 * Another class that transforms this grid into the interiors of the
 * window, always relative to the window origin.  And finally the
 * class that transforms the relative data for the workspace to actual
 * screen coordinates, takes window decorations into account and
 * interacts with the window system.
 *
 * This would probably even be faster, because it would be a lot easier
 * to stuff multiple update operations into one single call to the
 * 
 */
#include <mintbind.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>

#include "textwin.h"
#include "event.h"
#include "console.h"
#include "ansicol.h"
#include "isotost.h"
#include "vt.h"
#include "clipbrd.h"
#include "proc.h"
#include "av.h"
#include "drag.h"

#define CLEARED 2			/* redrawing a cleared area */

/*
 * lokale Variablen
*/
static MFDB scr_mfdb;	/* left NULL so it refers to the screen by default */

/*
 * lokale Prototypen
*/
static void draw_buf(TEXTWIN *t, char *buf, short x, short y, unsigned long flag, short force);
static void update_chars(TEXTWIN *t, short firstcol, short lastcol, short firstline, short lastline, short force);
static void update_screen (TEXTWIN *t, short xc, short yc, short wc, short hc,
			   short force);
static void update_cursor (WINDOW* win, int top);
static void draw_textwin(WINDOW *v, short x, short y, short w, short h);
static void close_textwin(WINDOW *v);
static void full_textwin(WINDOW *v);
static void move_textwin(WINDOW *v, short x, short y, short w, short h);
static void size_textwin(WINDOW *v, short x, short y, short w, short h);
static void newyoff(TEXTWIN *t, short y);
static void scrollupdn(TEXTWIN *t, short off, short direction);
static void arrow_textwin(WINDOW *v, short msg);
static void vslid_textwin(WINDOW *v, short vpos);
static void set_scroll_bars(TEXTWIN *t);
static void set_cwidths(TEXTWIN *t);

static void notify_winch (TEXTWIN* tw);
static void reread_size (TEXTWIN* t);
static void change_scrollback (TEXTWIN* t, short scrollback);
static void change_height (TEXTWIN* t, short rows);
static void change_width (TEXTWIN* t, short cols);

/* functions for converting x, y pixels to/from character coordinates */
/* NOTES: these functions give the upper left corner; to actually draw
 * a character, they must be adjusted down by t->cbase
 * Also: char2pixel accepts out of range character/column combinations,
 * but pixel2char never will generate such combinations.
 */
void char2pixel(TEXTWIN *t, short col, short row, short *xp, short *yp)
{
	short *WIDE = t->cwidths;
	short x;

	*yp = t->win->work.g_y - t->offy + row * t->cheight;
	if (!WIDE)
	{
		*xp = t->win->work.g_x + col * t->cmaxwidth;
	}
	else
	{
		if (col >= NCOLS (t))
			*xp = t->win->work.g_x + t->win->work.g_w;
		else
		{
			x = t->win->work.g_x;
			while(--col >= 0)
				x += WIDE[t->cdata[row][col]];
			*xp = x;
		}
	}
}

static void
char2grect (TEXTWIN* t, short col, short row, GRECT* g)
{
	short* WIDE = t->cwidths;

	g->g_y = t->win->work.g_y - t->offy + row * t->cheight;
	g->g_h = t->cheight;

	if (!WIDE) {
		g->g_x = t->win->work.g_x + col * t->cmaxwidth;
		g->g_w = t->cmaxwidth;
	} else {
		if (col >= NCOLS (t)) {
			g->g_x = t->win->work.g_x + t->win->work.g_w;
			g->g_w = 0;
		} else {
			short x = t->win->work.g_x;
			while(--col >= 0)
				x += WIDE[t->cdata[row][col]];
			g->g_x = x;
			g->g_w = WIDE[t->cdata[row][col]];
		}
	}
}

void pixel2char(TEXTWIN *t, short x, short y, short *colp, short *rowp)
{
	short col, row, count, nextcount;
	short *WIDE = t->cwidths;

	row = (y - t->win->work.g_y + t->offy) / t->cheight;
	x -= t->win->work.g_x;

	if (WIDE == 0)
		col = x / t->cmaxwidth;
	else
	{
		count = 0;
		for (col = 0; col <= NCOLS (t); col++)
		{
			nextcount = count + WIDE[t->cdata[row][col]];
			if (count <= x && x < nextcount)
				break;
			count = nextcount;
		}
	}
	*rowp = row;
	*colp = col;
}

/* Draw a string in the alternate character set.  */
static void
draw_acs_text(TEXTWIN* t, short textcolor, short x, short y, char* buf)
{
	char* crs = buf;
	short max_x = x + t->win->work.g_w; /* FIXME: -1 ? */
	short cwidth = t->cmaxwidth;
	short cheight = t->cheight;
	short pxy[8];
	short box = cwidth > cheight ? cheight - 1 : cwidth - 1;
	char letter[2] = { '\0', '\0' };

	while (*crs && x < max_x)
	{
		switch (*crs)
		{
			case '}': /* ACS_STERLING */
				/* We assume that if the Atari font is
				    * selected,
				 * we have a font that is already in ISO-Latin 1.
				 */
				letter[0] = t->cfg->char_tab == TAB_ATARI ?
					'\243' : '\234';
				set_textcolor (textcolor);
				v_gtext (vdi_handle, x, y + t->cbase, letter);
				break;

			case '.': /* ACS_DARROW */
				/* We assume that if the Atari font is
				    * selected,
				 * we have a font that is already in ISO-Latin 1.
				 */
				letter[0] = t->cfg->char_tab == TAB_ATARI ?
					'v' : '\002';
				set_textcolor (textcolor);
				v_gtext (vdi_handle, x, y + t->cbase, letter);
				break;

			case ',': /* ACS_LARROW */
				/* We assume that if the Atari font is
				    * selected,
				 * we have a font that is already in ISO-Latin 1.
				 */
				letter[0] = t->cfg->char_tab == TAB_ATARI ?
					'<' : '\004';
				set_textcolor (textcolor);
				v_gtext (vdi_handle, x, y + t->cbase, letter);
				break;

			case '+': /* ACS_RARROW */
				/* We assume that if the Atari font is
				    * selected,
				 * we have a font that is already in ISO-Latin 1.
				 */
				letter[0] = t->cfg->char_tab == TAB_ATARI ?
					'>' : '\003';
				set_textcolor (textcolor);
				v_gtext (vdi_handle, x, y + t->cbase, letter);
				break;

			case '-': /* ACS_UARROW */
				/* We assume that if the Atari font is
				    * selected,
				 * we have a font that is already in ISO-Latin 1.
				 */
				letter[0] = t->cfg->char_tab == TAB_ATARI ?
					'^' : '\001';
				set_textcolor (textcolor);
				v_gtext (vdi_handle, x, y + t->cbase, letter);
				break;

			case 'h': /* ACS_BOARD */
				pxy[0] = x;
				pxy[1] = y;
				pxy[2] = x + cwidth - 1;
				pxy[3] = y + cheight - 1;
				set_fillcolor (textcolor);
				set_fillstyle (2, 22);
				vsf_perimeter (vdi_handle, 0);
				v_bar (vdi_handle, pxy);
				break;

			case '~': /* ACS_BULLET */
				/* We assume that if the Atari font is
				    * selected,
				 * we have a font that is already in ISO-Latin 1.
				 */
				letter[0] = t->cfg->char_tab == TAB_ATARI ?
					'o' : '\372';
				set_textcolor (textcolor);
				v_gtext (vdi_handle, x, y + t->cbase, letter);
				break;

			case 'a': /* ACS_CKBOARD */
				pxy[0] = x;
				pxy[1] = y;
				pxy[2] = x + cwidth - 1;
				pxy[3] = y + t->cheight - 1;
				set_fillcolor (textcolor);
				set_fillstyle (2, 2);
				vsf_perimeter (vdi_handle, 0);
				v_bar (vdi_handle, pxy);
				break;

			case 'f': /* ACS_DEGREE */
				/* We assume that if the Atari font is
				    * selected,
				 * we have a font that is already in ISO-Latin 1.
				 */
				letter[0] = t->cfg->char_tab == TAB_ATARI ?
					'\260' : '\370';
				set_textcolor (textcolor);
				set_texteffects (0);
				v_gtext(vdi_handle, x, y + t->cbase, letter);
				break;

			case '`': /* ACS_DIAMOND */
				pxy[0] = x + (cwidth >> 1);
				pxy[1] = y + (cheight >> 1) - (box >> 1);
				pxy[2] = pxy[0] + (box >> 1);
				pxy[3] = y + (cheight >> 1);
				pxy[4] = pxy[0];
				pxy[5] = pxy[3] + (box >> 1);
				pxy[6] = pxy[0] - (box >> 1);
				pxy[7] = pxy[3];
				set_fillcolor (textcolor);
				set_fillstyle (1, 1);
				v_fillarea (vdi_handle, 4, pxy);
				break;

			case 'z': /* ACS_GEQUAL */
				/* We assume that if the Atari font is
				    * selected,
				 * we have a font that is already in ISO-Latin 1.
				 */
				letter[0] = t->cfg->char_tab == TAB_ATARI ?
					'>' : '\362';
				set_textcolor (textcolor);
				v_gtext (vdi_handle, x, y + t->cbase, letter);
				if (t->cfg->char_tab != TAB_ATARI) {
					letter[0] = '_';
					v_gtext (vdi_handle, x, y + t->cbase, letter);
				}
				break;

			case '{': /* ACS_PI */
				/* We assume that if the Atari font is
				    * selected,
				 * we have a font that is already in ISO-Latin 1.
				 */
				if (t->cfg->char_tab != TAB_ATARI) {
					letter[0] = '\343';
					set_textcolor (textcolor);
					v_gtext (vdi_handle, x, y + t->cbase, letter);
				} else {
					/* FIXME: Paint greek letter pi.  */
					letter[0] = '*';
					set_textcolor (textcolor);
					v_gtext (vdi_handle, x, y + t->cbase, letter);
				}
				break;

			case 'q': /* ACS_HLINE */
				pxy[0] = x;
				pxy[1] = y + (cheight >> 1);
				pxy[2] = x + cwidth - 1;
				pxy[3] = pxy[1];
				set_fillcolor (textcolor);
				set_fillstyle (1, 1);
				v_bar (vdi_handle, pxy);
				break;

			case 'i': /* ACS_LANTERN */
				set_fillcolor (textcolor);
				set_fillstyle (1, 1);
				pxy[0] = x;
				pxy[1] = y + (cheight >> 1) - 1;
				pxy[2] = x + cwidth - 1;
				pxy[3] = pxy[1];
				v_bar (vdi_handle, pxy);

				pxy[1] = y + (cheight >> 1) + 1;
				pxy[3] = pxy[1];
				v_bar (vdi_handle, pxy);

				pxy[0] = x + (cwidth >> 1) - 1;
				pxy[1] = y;
				pxy[2] = pxy[0];
				pxy[3] = y + (cheight >> 1) - 1;
				v_bar (vdi_handle, pxy);

				pxy[0] = x + (cwidth >> 1) + 1;
				pxy[1] = y;
				pxy[2] = pxy[0];
				pxy[3] = y + (cheight >> 1) - 1;
				v_bar (vdi_handle, pxy);

				pxy[1] = y + cheight - 1;
				v_bar (vdi_handle, pxy);

				pxy[0] = x + (cwidth >> 1) - 1;
				pxy[2] = pxy[0];
				v_bar (vdi_handle, pxy);

				break;

			case 'n': /* ACS_PLUS */
				pxy[0] = x;
				pxy[1] = y + (cheight >> 1);
				pxy[2] = x + cwidth - 1;
				pxy[3] = pxy[1];
				pxy[4] = x + (cwidth >> 1);
				pxy[5] = y;
				pxy[6] = pxy[4];
				pxy[7] = y + cheight - 1;
				set_fillcolor (textcolor);
				set_fillstyle (1, 1);
				v_bar (vdi_handle, pxy);
				v_bar (vdi_handle, pxy + 4);
				break;

			case 'y': /* ACS_LEQUAL */
				/* We assume that if the Atari font is
				    * selected,
				 * we have a font that is already in ISO-Latin 1.
				 */
				letter[0] = t->cfg->char_tab == TAB_ATARI ?
					'y' : '\363';
				set_textcolor (textcolor);
				v_gtext (vdi_handle, x, y + t->cbase, letter);
				if (t->cfg->char_tab != TAB_ATARI) {
					letter[0] = '_';
					v_gtext (vdi_handle, x, y + t->cbase, letter);
				}
				break;

			case 'm': /* ACS_LLCORNER */
				pxy[0] = x + (cwidth >> 1);
				pxy[1] = y;
				pxy[2] = pxy[0];
				pxy[3] = y + (cheight >> 1);
				pxy[4] = x + cwidth - 1;
				pxy[5] = pxy[3];
				vsl_type (vdi_handle, 1);
				vsl_color (vdi_handle, textcolor);
				v_pline (vdi_handle, 3, pxy);
				break;

			case 'j': /* ACS_LRCORNER */
				pxy[0] = x + (cwidth >> 1);
				pxy[1] = y;
				pxy[2] = pxy[0];
				pxy[3] = y + (cheight >> 1);
				pxy[4] = x;
				pxy[5] = pxy[3];
				vsl_type (vdi_handle, 1);
				vsl_color (vdi_handle, textcolor);
				v_pline (vdi_handle, 3, pxy);
				break;

			case '|': /* ACS_NEQUAL */
				letter[0] = '=';
				set_textcolor (textcolor);
				v_gtext (vdi_handle, x, y + t->cbase, letter);
				letter[0] = '/';
				v_gtext (vdi_handle, x, y + t->cbase, letter);
				break;

			case 'g': /* ACS_PLMINUS */
				/* We assume that if the Atari font is
				    * selected,
				 * we have a font that is already in ISO-Latin 1.
				 */
				letter[0] = t->cfg->char_tab == TAB_ATARI ?
					'\261' : '\361';
				set_textcolor (textcolor);
				v_gtext (vdi_handle, x, y + t->cbase, letter);
				break;

			case 'o': /* ACS_S1 */
				pxy[0] = x;
				pxy[1] = y;
				pxy[2] = x + cwidth - 1;
				pxy[3] = y;
				set_fillcolor (textcolor);
				set_fillstyle (1, 1);
				v_bar (vdi_handle, pxy);
				break;

			case 'p': /* ACS_S3 */
				pxy[0] = x;
				pxy[1] = y + cheight / 3;
				pxy[2] = x + cwidth - 1;
				pxy[3] = pxy[1];
				set_fillcolor (textcolor);
				set_fillstyle (1, 1);
				v_bar (vdi_handle, pxy);
				break;

			case 'r': /* ACS_S7 */
				pxy[0] = x;
				pxy[1] = y + cheight - cheight / 3;
				pxy[2] = x + cwidth - 1;
				pxy[3] = pxy[1];
				set_fillcolor (textcolor);
				set_fillstyle (1, 1);
				v_bar (vdi_handle, pxy);
				break;

			case 's': /* ACS_S9 */
				pxy[0] = x;
				pxy[1] = y + cheight - 1;
				pxy[2] = x + cwidth - 1;
				pxy[3] = pxy[1];
				set_fillcolor (textcolor);
				set_fillstyle (1, 1);
				v_bar (vdi_handle, pxy);
				break;

			case '0': /* ACS_BLOCK */
				pxy[0] = x;
				pxy[1] = y;
				pxy[2] = x + cwidth - 1;
				pxy[3] = y + cheight - 1;
				set_fillcolor (textcolor);
				set_fillstyle (1, 1);
				vsf_perimeter (vdi_handle, 0);
				v_bar (vdi_handle, pxy);
				break;

			case 'w': /* ACS_TTEE */
				pxy[0] = x;
				pxy[1] = y + (cheight >> 1);
				pxy[2] = x + cwidth - 1;
				pxy[3] = pxy[1];
				pxy[4] = x + (cwidth >> 1);
				pxy[5] = pxy[1];
				pxy[6] = pxy[4];
				pxy[7] = y + cheight - 1;
				set_fillcolor (textcolor);
				set_fillstyle (1, 1);
				v_bar (vdi_handle, pxy);
				v_bar (vdi_handle, pxy + 4);
				break;

			case 'u': /* ACS_RTEE */
				pxy[0] = x + (cwidth >> 1);
				pxy[1] = y;
				pxy[2] = pxy[0];
				pxy[3] = y + cheight - 1;
				pxy[4] = x;
				pxy[5] = y + (cheight >> 1);
				pxy[6] = pxy[0];
				pxy[7] = pxy[5];
				set_fillcolor (textcolor);
				set_fillstyle (1, 1);
				v_bar (vdi_handle, pxy);
				v_bar (vdi_handle, pxy + 4);
				break;

			case 't': /* ACS_LTEE */
				pxy[0] = x + (cwidth >> 1);
				pxy[1] = y;
				pxy[2] = pxy[0];
				pxy[3] = y + cheight - 1;
				pxy[4] = pxy[0];
				pxy[5] = y + (cheight >> 1);
				pxy[6] = x + cwidth - 1;
				pxy[7] = pxy[5];
				set_fillcolor (textcolor);
				set_fillstyle (1, 1);
				v_bar (vdi_handle, pxy);
				v_bar (vdi_handle, pxy + 4);
				break;

			case 'v': /* ACS_BTEE */
				pxy[0] = x;
				pxy[1] = y + (cheight >> 1);
				pxy[2] = x + cwidth - 1;
				pxy[3] = pxy[1];
				pxy[4] = x + (cwidth >> 1);
				pxy[5] = y;
				pxy[6] = pxy[4];
				pxy[7] = pxy[1];
				set_fillcolor (textcolor);
				set_fillstyle (1, 1);
				v_bar (vdi_handle, pxy);
				v_bar (vdi_handle, pxy + 4);
				break;

			case 'l': /* ACS_ULCORNER */
				pxy[0] = x + (cwidth >> 1);
				pxy[1] = y + cheight - 1;
				pxy[2] = pxy[0];
				pxy[3] = y + (cheight >> 1);
				pxy[4] = x + cwidth - 1;
				pxy[5] = pxy[3];
				vsl_type (vdi_handle, 1);
				vsl_color (vdi_handle, textcolor);
				v_pline (vdi_handle, 3, pxy);
				break;

			case 'k': /* ACS_URCORNER */
				pxy[0] = x;
				pxy[1] = y + (cheight >> 1);
				pxy[2] = x + (cwidth >> 1);
				pxy[3] = pxy[1];
				pxy[4] = pxy[2];
				pxy[5] = y + cheight - 1;
				vsl_type (vdi_handle, 1);
				vsl_color (vdi_handle, textcolor);
				v_pline (vdi_handle, 3, pxy);
				break;

			case 'x': /* ACS_VLINE */
				pxy[0] = x + (cwidth >> 1);
				pxy[1] = y;
				pxy[2] = pxy[0];
				pxy[3] = y + cheight - 1;
				set_fillcolor (textcolor);
				set_fillstyle (1, 1);
				v_bar (vdi_handle, pxy);
				break;

			default:
				break;
		}

		++crs;
		x += cwidth;
	}
}

/*
 * draw a (part of a) line on screen, with certain attributes (e.g.
 * inverse video) indicated by "flag". (x, y) is the upper left corner
 * of the box which will contain the line.
 * If "force" is 1, we may assume that the screen is already cleared
 * (this is done in update_screen() for us).
 * SPECIAL CASE: if buf is an empty string, we clear from "x" to
 * the end of the window.
 */
static void draw_buf(TEXTWIN *t, char *buf, short x, short y, unsigned long flag, short force)
{
	char *s, *lastnonblank;
	int x2, fillcolor, textcolor;
	int texteffects;
	short *WIDE = t->cwidths;
	short temp[4];
	int acs = flag & CLINEDRAW;

	use_ansi_colors (t, flag, &textcolor, &fillcolor, &texteffects);
	x2 = x;
	s = buf;
	if (*s)
	{
		lastnonblank = s-1;
		while (*s)
		{
			if (*s != ' ') lastnonblank = s;
			if (WIDE)
				x2 += WIDE[(short)*s];
			else
				x2 += t->cmaxwidth;
			s++;
		}
		lastnonblank++;
		if (!(flag & CE_UNDERLINE))
			*lastnonblank = 0;
	}
	else
		x2 = t->win->work.g_x + t->win->work.g_w;

	set_wrmode(2);		/* transparent text */
	if (fillcolor >= 0 || (force != CLEARED))
	{
		temp[0] = x;
		temp[1] = y;
		temp[2] = x2 - 1;
		temp[3] = y + t->cheight - 1;
		set_fillcolor(fillcolor);
		set_fillstyle(1, 1);
		v_bar(vdi_handle, temp);
	}

	/* skip leading blanks -- we don't need to draw them again! */
	if (!(flag & CE_UNDERLINE))
	{
		while (*buf == ' ')
		{
			buf++;
			x += WIDE ? WIDE[' '] : t->cmaxwidth;
		}
	}

	if (*buf)
	{
		if (acs)
		{
			draw_acs_text(t, textcolor, x, y, buf);
		}
		else
		{
			set_textcolor(textcolor);
			set_texteffects(texteffects);
			v_gtext(vdi_handle, x, y + t->cbase, buf);
		}
	}
}

/*
 * update the characters on screen between "firstline,firstcol" and
 * "lastline-1,lastcol-1" (inclusive)
 * if force == 1, the redraw must occur, otherwise it occurs only for
 * "dirty" characters. Note that we assume here that clipping
 * rectanges and wind_update() have already been set for us.
 */
static void
update_chars (TEXTWIN *t, short firstcol, short lastcol, short firstline,
	      short lastline, short force)
{
#define CBUFSIZ 127
	char buf[CBUFSIZ+1];
	unsigned char c;
	short px, py, ax, i, cnt, bufwidth;
	unsigned long flag;
	short *WIDE = t->cwidths;
	short lineforce = 0;
	unsigned long curflag;

#define flushbuf()	\
	{ 		\
		buf[i] = 0;	\
		draw_buf(t, buf, px, py, flag, lineforce); \
		px += bufwidth; \
		i = bufwidth = 0; \
	}

	if (t->windirty)
		reread_size (t);

	t->windirty = 0;

	/* make sure the font is set correctly */
	set_font(t->cfont, t->cpoints);

	/* find the place to start writing */
	char2pixel(t, firstcol, firstline, &ax, &py);

	/* now write the characters we need to */
	while (firstline < lastline)
	{
		/* if no characters on the line need re-writing, skip the loop */
		if (!force && t->dirty[firstline] == 0)
		{
			py += t->cheight;
			firstline++;
			continue;
		}
		px = ax;
		/*
		 * now, go along collecting characters to write into the buffer
		 * we add a character to the buffer if and only if (1) the
		 * character's attributes (inverse video, etc.) match the
		 * attributes of the character already in the buffer, and
		 * (2) the character needs redrawing. Otherwise, if there are
		 * characters in the buffer, we flush the buffer.
		 */
		i = bufwidth = 0;
		cnt = firstcol;
		flag = 0;
		lineforce = force;
		if (!lineforce && (t->dirty[firstline] & ALLDIRTY))
			lineforce = 1;
		while (cnt < lastcol)
		{
		 	c = t->cdata[firstline][cnt];
		 	if (lineforce || (t->cflags[firstline][cnt] & (CDIRTY)))
		 	{
				/* yes, this character needs drawing */
				/* if the font is proportional and the character has really changed,
				 * then all remaining characters will have to be redrawn, too
				 */
				if (WIDE && (lineforce == 0) && (t->cflags[firstline][cnt] & CDIRTY))
					lineforce = 1;

				curflag = t->cflags[firstline][cnt] & ~(CDIRTY);

				/* watch out for characters that can't be drawn in this font */
				if (!(curflag & CLINEDRAW) && (c < t->minADE || c > t->maxADE))
					c = '?';

				if (flag == curflag)
				{
					 buf[i++] = c;
					 bufwidth += (WIDE ? WIDE[c] : t->cmaxwidth);
					 if (i == CBUFSIZ)
					 {
						flushbuf();
					}
				}
				else
				{
				 	if (i)
				 	{
				 		flushbuf();
					}
					flag = curflag;
					buf[i++] = c;
					bufwidth += (WIDE ? WIDE[c] : t->cmaxwidth);
				}
		  	}
		  	else
		  	{
				if (i)
				{
					flushbuf();
				}
				px += (WIDE ? WIDE[c] : t->cmaxwidth);
		  	}
		   cnt++;
		} /* while */
		if (i)
		{
			flushbuf();
		}
		if (WIDE)
		{
			/* the line's 'tail' */
			draw_buf(t, "", px, py, t->cflags[firstline][NCOLS (t) - 1], lineforce);
		}
		py += t->cheight;
		firstline++;
	}
	t->curs_drawn = 0;
	if (t->curr_tflags & TCURS_ON)
		update_cursor (t->win, -1);
}

/* Update the cursor.  FIXME: This function nests to deep.  */
static void
update_cursor (WINDOW* win, int top)
{
	TEXTWIN* tw = win->extra;
	bool force_draw = FALSE;
	GRECT curr, work;
	GRECT new_curs = { 0, 0, 0, 0 };
	GRECT old_curs = { 0, 0, 0, 0 };
	bool off = FALSE;
	unsigned long old_flag = 0;
	unsigned long new_flag = 0;
	
	if (win->redraw)
		return;
	
	if (top == 1)
		tw->wintop = 1;
	else if (top == 0) {
		tw->wintop = 0;
		if (!tw->curs_drawn)
			force_draw = TRUE;
	} else if (top == -1) {
		force_draw = TRUE;
	}

	/* Exits if window isn't visible or was iconified/shaded */
	if (win->handle < 0 || (win->flags & WICONIFIED) ||
	    (win->flags & WSHADED) ||
	    (!(tw->curr_tflags & TCURS_ON) && !tw->curs_drawn)) {
	    	tw->last_cx = tw->last_cy = -1;
		return;
	}

	/* If the window is not on top we only have to draw something if a
	   redraw has been forced by the caller or the cursor is currently
	   not showing.  */
	if (!force_draw && !tw->wintop && tw->curs_drawn)
		return;

	work = win->work;
	if (!rc_intersect (&gl_desk, &work))
		return;

	/* If the cursor simply flashes there is no need to remove it from
	   the old position.  */
	if ((tw->last_cx != tw->cx || tw->last_cy != tw->cy) &&
	    (tw->last_cx >= 0 && tw->last_cy >= tw->miny) &&
	     tw->last_cx < tw->maxx && tw->last_cy < tw->maxy) {
		old_flag = tw->cflags[tw->last_cy][tw->last_cx];

		char2grect (tw, tw->last_cx, tw->last_cy, &old_curs);
		if (tw->curr_tflags & TCURS_VVIS) {
			int selected = old_flag & CINVERSE;

			if (!selected)
				old_flag &= ~CINVERSE;
			else
				old_flag |= CINVERSE;
		}
	}
	char2grect (tw, tw->cx, tw->cy, &new_curs);

	if (tw->cx >= 0 && tw->cx < tw->maxx &&
	    tw->cy >= tw->miny && tw->cy < tw->maxy)
		new_flag = tw->cflags[tw->cy][tw->cx];
	else
		new_curs.g_w = 0;

	if (force_draw || !tw->curs_drawn)
		tw->curs_drawn = 1;
	else
		tw->curs_drawn = 0;

	if (tw->curr_tflags & TCURS_VVIS) {
		int selected = new_flag & CINVERSE;

		if (selected ^ tw->curs_drawn)
			new_flag |= CINVERSE;
		else
			new_flag &= ~CINVERSE;
	} else if (tw->curs_drawn) {
		new_curs.g_y += tw->curs_offy;
		new_curs.g_h = tw->curs_height;
	}

	tw->dirty[tw->cy] |= SOMEDIRTY;
	tw->cflags[tw->cy][tw->cx] |= CDIRTY;
	
	if (win->redraw)
		fprintf (stderr, "ALERT: Another redraw is already in progress!\n");
	win->redraw = 1;
	update_window_lock();
	get_window_rect (win, WF_FIRSTXYWH, &curr);

	while (curr.g_w && curr.g_h) {
		if (rc_intersect (&work, &curr)) {
			if (old_curs.g_w) {
				GRECT curr2 = curr;
				if (rc_intersect (&old_curs, &curr2)) {
					char buf[2];

					if (!off)
						off = hide_mouse_if_needed(&curr2);

					set_clipping (vdi_handle,
							    curr2.g_x,
							    curr2.g_y,
						   		  curr2.g_w,
						   		  curr2.g_h,
						   		  TRUE);

					buf[0]= tw->cdata[tw->last_cy][tw->last_cx];
					buf[1] = '\000';
					draw_buf (tw, buf, curr2.g_x, curr2.g_y,
							old_flag, 0);
				}
			}

			if (new_curs.g_w && rc_intersect (&new_curs, &curr)) {
				if (!off)
					off = hide_mouse_if_needed(&curr);

				if ((tw->curr_tflags & TCURS_VVIS) 
				    || !tw->curs_drawn) {
					char buf[2] = {
						tw->cdata[tw->cy][tw->cx],
						'\000'};

					set_clipping (vdi_handle, curr.g_x, curr.g_y,
							    curr.g_w, curr.g_h, TRUE);

					draw_buf (tw, buf, curr.g_x, curr.g_y,
							new_flag, 0);
				} else {
					int fillcolor, textcolor, texteffects;
					short xy[4] = {
						curr.g_x, curr.g_y,
						curr.g_x + curr.g_w - 1,
						curr.g_y + curr.g_h - 1
					};
					use_ansi_colors (tw, tw->curr_cattr,
							 &textcolor,
							 &fillcolor,
							 &texteffects);
					set_fillcolor (textcolor);
					set_fillstyle (1, 1);
					set_clipping (vdi_handle, curr.g_x, curr.g_y,
							    curr.g_w, curr.g_h, FALSE);
					v_bar (vdi_handle, xy);
				}
			}
		}

		get_window_rect (win, WF_NEXTXYWH, &curr);
	}

	if (off)
		show_mouse();

	update_window_unlock();
	win->redraw = 0;

	tw->last_cx = tw->cx;
	tw->last_cy = tw->cy;
}

/*
 * mark_clean: mark a window as having been completely updated
 */
void mark_clean(TEXTWIN *t)
{
	int line, col;

	for (line = 0; line < t->maxy; line++)
	{
		if (t->dirty[line] == 0)
			continue;
		for (col = 0; col < NCOLS (t); col++)
			t->cflags[line][col] &= ~(CDIRTY);
		t->dirty[line] = 0;
	}
}

/*
 * redraw all parts of window v which are contained within the
 * given rectangle. Assumes that the clipping rectange has already
 * been set correctly.
 * NOTE: more than one rectangle may cover the same area, so we
 * can't mark the window clean during the update; we have to do
 * it in a separate routine (mark_clean)
 */
static void
update_screen (TEXTWIN *t, short xc, short yc, short wc, short hc,
		short force)
{
	short firstline, lastline, firstscroll;
	short firstcol, lastcol;
	short pxy[8];
	long scrollht = 0;

	if (t->win->flags & WSHADED)
		return;

	if (t->win->flags & WICONIFIED)
	{
		draw_winicon(t->win);
		return;
	}

	if (t->windirty)
		reread_size (t);
	t->windirty = 0;

	/* if t->scrolled is set, then the output routines faked the "dirty"
	 * flags on the scrolled lines under the assumption that we would
	 * do a blit scroll; so we do it here.
	 */
	if ((force == 0) && t->scrolled && (scrollht = t->scrolled * t->cheight) < hc)
	{
		pxy[0] = xc;
		pxy[1] = yc + (int)scrollht;
		pxy[2] = xc + wc - 1;
		pxy[3] = yc + hc - 1;
		pxy[4] = xc;
		pxy[5] = yc;
		pxy[6] = pxy[2];
		pxy[7] = pxy[3] - (int)scrollht;
		vro_cpyfm(vdi_handle, S_ONLY, pxy, &scr_mfdb, &scr_mfdb);
	}

#if 1
	/* if `force' is set, clear the area to be redrawn -- it looks better */
	if (force == CLEARED)
	{
		unsigned long flag = 0;
		WINCFG* cfg = t->cfg;
		int bg_color, fg_color, style;

		if (cfg->vdi_colors) {
			flag |= cfg->bg_color;
		} else {
			flag |= (ANSI_DEFAULT | cfg->bg_effects);
		}
		use_ansi_colors (t, flag, &fg_color, &bg_color, &style);
		set_fillcolor (bg_color);
		set_fillstyle (1, 1);
		pxy[0] = xc;
		pxy[1] = yc;
		pxy[2] = xc + wc - 1;
		pxy[3] = yc + hc - 1;
		vr_recfl(vdi_handle, pxy);
	}
#endif

	/* convert from on-screen coordinates to window rows & columns */
	pixel2char(t, xc, yc, &firstcol, &firstline);

	if (firstline < 0)
		firstline = 0;
	else if (firstline >= t->maxy)
		firstline = t->maxy - 1;

	lastline = 1 + firstline + (hc + t->cheight - 1) / t->cheight;

	if (lastline > t->maxy)
		lastline = t->maxy;

	/* kludge for proportional fonts */
	if (t->cwidths)
	{
		firstcol = 0;
		lastcol = NCOLS (t);
	}
	else
		pixel2char(t, xc+wc+t->cmaxwidth-1, yc, &lastcol, &firstline);

	/* if t->scrolled is set, the last few lines *must* be updated */
	if (t->scrolled && force == 0)
	{
		firstscroll = firstline + (hc - (int)scrollht)/t->cheight;
		if (firstscroll <= firstline)
			force = TRUE;
		else
		{
			update_chars(t, firstcol, lastcol, firstscroll, lastline, TRUE);
			lastline = firstscroll;
		}
	}
	update_chars(t, firstcol, lastcol, firstline, lastline, force);
}

/*
 * redraw all parts of a window that need redrawing; this is called
 * after, for example, writing some text into the window
 */
void refresh_textwin(TEXTWIN *t, short force)
{
	WINDOW	*v = t->win;
	GRECT t1, t2;
	bool off = FALSE;

	/* exits if window isn't visible or was iconified/shaded */
	if (v->handle < 0 || (v->flags & WICONIFIED) || (v->flags & WSHADED))
		return;

	t2 = v->work;
	if (!rc_intersect(&gl_desk, &t2))
		return;
	
	if (v->redraw)
		fprintf (stderr, "ALERT: Another redraw is already in progress!\n");
	v->redraw = 1;
	update_window_lock();
	get_window_rect(v, WF_FIRSTXYWH, &t1);
	while (t1.g_w && t1.g_h)
	{
		if (rc_intersect(&t2, &t1))
		{
			if (!off)
				off = hide_mouse_if_needed(&t1);
			set_clipping (vdi_handle, t1.g_x, t1.g_y,
					  t1.g_w, t1.g_h, TRUE);
			update_screen (t, t1.g_x, t1.g_y, t1.g_w, t1.g_h,
					   force);
		}
		get_window_rect(v, WF_NEXTXYWH, &t1);
	}
	t->scrolled = t->nbytes = t->draw_time = 0;
	if (off)
		show_mouse();
	mark_clean(t);
	update_window_unlock();
	v->redraw = 0;
}

/*
 * Methods for reacting to user events
 */
/* draw part of a window */
static void draw_textwin (WINDOW *v, short x, short y, short w, short h)
{
	TEXTWIN *t = v->extra;

	t->scrolled = 0;
	update_screen (v->extra, x, y, w, h, CLEARED);
	t->nbytes = t->draw_time = 0;
}


/* close a window (called when the closed box is clicked on) */
static void close_textwin(WINDOW *v)
{
	destroy_textwin(v->extra);
}

/* resize a window to its "full" size */
static void full_textwin(WINDOW *v)
{
	GRECT new;
	TEXTWIN	*t = v->extra;

	if (v->flags & WFULLED)
		get_window_rect(v, WF_PREVXYWH, &new);
	else
		get_window_rect(v, WF_FULLXYWH, &new);
	wind_calc_grect(WC_WORK, v->kind, &new, &v->work);

	/* Snap */
	v->work.g_w -= (v->work.g_w % t->cmaxwidth);
	v->work.g_h -= (v->work.g_h % t->cheight);
	wind_calc_grect(WC_BORDER, v->kind, &v->work, &new);
	wind_set_grect(v->handle, WF_CURRXYWH, &new);

	v->flags ^= WFULLED;
	set_scroll_bars(t);

	t->windirty = 1;
}

/* resize a window */
static void move_textwin(WINDOW *v, short x, short y, short w, short h)
{
	GRECT full;
	TEXTWIN	*t = v->extra;

	get_window_rect(v, WF_FULLXYWH, &full);

#if 1
	if (w > full.g_w)
		w = full.g_w;
	if (h > full.g_h)
		h = full.g_h;
#endif

	wind_calc(WC_WORK, v->kind, x, y, w, h, &v->work.g_x, &v->work.g_y, &v->work.g_w, &v->work.g_h);

	if (!(v->flags & WICONIFIED))
	{
		/* Snap */
		v->work.g_w -= (v->work.g_w % t->cmaxwidth);
		v->work.g_h -= (v->work.g_h % t->cheight);
	}
	wind_calc(WC_BORDER, v->kind, v->work.g_x, v->work.g_y, v->work.g_w, v->work.g_h, &x, &y, &w, &h);
	wind_set(v->handle, WF_CURRXYWH, x, y, w, h);

	if (w != full.g_w || h != full.g_h)
		v->flags &= ~WFULLED;

	if (!(v->flags & WICONIFIED))
	{
		/* das sind BORDER-Gr��en! */
		t->cfg->xpos = x;
		t->cfg->ypos = y;
	}
	t->windirty = 1;
}

static void size_textwin(WINDOW *v, short x, short y, short w, short h)
{
	TEXTWIN *t = v->extra;

	t->windirty = 1;
	v->moved (v, x, y, w, h);
	set_scroll_bars(t);
}

/*
 * handle an arrow event to a window
 */
static void newyoff(TEXTWIN *t, short y)
{
	t->offy = y;
	set_scroll_bars(t);
}

#define UP 0
#define DOWN 1

#define scrollup(t, off) scrollupdn(t, off, UP)
#define scrolldn(t, off) scrollupdn(t, off, DOWN)

static void
scrollupdn (TEXTWIN *t, short off, short direction)
{
	WINDOW	*v = t->win;
	GRECT t1, t2;
	short pxy[8];
	bool m_off = FALSE;

	if (off <= 0)
		return;

	t2 = v->work;
	if (!rc_intersect(&gl_desk, &t2))
		return;
	
	if (v->redraw)
		fprintf (stderr, "ALERT: Another redraw is already in progress!\n");
	v->redraw = 1;
	update_window_lock();
	
	get_window_rect(v, WF_FIRSTXYWH, &t1);
	while (t1.g_w && t1.g_h) {
		if (rc_intersect(&t2, &t1)) {
			if (!m_off)
				m_off = hide_mouse_if_needed(&t1);
			set_clipping (vdi_handle, t1.g_x, t1.g_y,
							t1.g_w, t1.g_h, TRUE);

			if (off >= t1.g_h)
				update_screen(t, t1.g_x, t1.g_y,
						 t1.g_w, t1.g_h, TRUE);
			else {
				if (direction  == UP) {
					pxy[0] = t1.g_x;	/* "from" address */
					pxy[1] = t1.g_y + off;
					pxy[2] = t1.g_x + t1.g_w - 1;
					pxy[3] = t1.g_y + t1.g_h - 1;
					pxy[4] = t1.g_x;	/* "to" address */
					pxy[5] = t1.g_y;
					pxy[6] = t1.g_x + t1.g_w - 1;
					pxy[7] = t1.g_y + t1.g_h - off - 1;
				} else {
					pxy[0] = t1.g_x;
					pxy[1] = t1.g_y;
					pxy[2] = t1.g_x + t1.g_w - 1;
					pxy[3] = t1.g_y + t1.g_h - off - 1;
					pxy[4] = t1.g_x;
					pxy[5] = t1.g_y + off;
					pxy[6] = t1.g_x + t1.g_w - 1;
					pxy[7] = t1.g_y + t1.g_h - 1;
				}
				vro_cpyfm (vdi_handle, S_ONLY, pxy, &scr_mfdb, &scr_mfdb);
				if (direction == UP)
					update_screen (t, t1.g_x, pxy[7],
							     t1.g_w, off, TRUE);
				else
					update_screen (t, t1.g_x, t1.g_y,
							     t1.g_w, off, TRUE);
			}
		}
		get_window_rect(v, WF_NEXTXYWH, &t1);
	}
	if (m_off)
		show_mouse();
	update_window_unlock();
	v->redraw = 0;
}

static void arrow_textwin(WINDOW *v, short msg)
{
	TEXTWIN	*t = (TEXTWIN *)v->extra;
	short oldoff;

/*
	refresh_textwin(t, FALSE);
*/
	switch(msg)
	{
		case WA_UPPAGE:
			newyoff(t, t->offy - v->work.g_h);
			break;
		case WA_DNPAGE:
			newyoff(t, t->offy + v->work.g_h);
			break;
		case WA_UPLINE:
			oldoff = t->offy;
			newyoff(t, t->offy - t->cheight);
			scrolldn(t, oldoff - t->offy);
			return;
		case WA_DNLINE:
			oldoff = t->offy;
			newyoff(t, t->offy + t->cheight);
			scrollup(t, t->offy - oldoff);
			return;
		default:
			return;
	}
	refresh_textwin(t, TRUE);
}
	
static void vslid_textwin(WINDOW *v, short vpos)
{
	TEXTWIN	*t = (TEXTWIN *)v->extra;
	long height;
	short oldoff;

	height = t->cheight * t->maxy - v->work.g_h;
	oldoff = t->offy;
	newyoff(t, (int)((vpos * height) / 1000L));
	oldoff -= t->offy;
	if (oldoff < 0)
		scrollup(t, -oldoff);
	else
		scrolldn(t, oldoff);
}

/*
 * correctly set up the horizontal and vertical scroll bars for TEXTWIN t
 */
static void set_scroll_bars(TEXTWIN *t)
{
	WINDOW	*v = t->win;
	short vsize;
	short vpos;
	long height;

	height = t->cheight * t->maxy;

	/* see if the new offset is too big for the window */
	if (t->offy + v->work.g_h > height)
		t->offy = (int)(height - v->work.g_h);
	if (t->offy < 0)
		t->offy = 0;

	vsize = (int)(1000L * v->work.g_h / height);
	if (vsize > 1000)
		vsize = 1000;
	else if (vsize < 1)
		vsize = 1;

	if (height > v->work.g_h)
		vpos = (int)(1000L * t->offy / (height - v->work.g_h));
	else
		vpos = 1;

	if (vpos < 1)
		vpos = 1;
	else if (vpos > 1000)
		vpos = 1000;

	if (v->kind & VSLIDE)
	{
		wind_set(v->handle, WF_VSLIDE, vpos, 0, 0, 0);
		wind_set(v->handle, WF_VSLSIZE, vsize, 0, 0, 0);
	}
}


/*
 * text_type: type the user's input into a window. Note that this routine is
 * called when doing a 'paste' operation, so we must watch out for possible
 * deadlock conditions
 */


static void text_appl_cursor(TEXTWIN *t, long shiftmask)
{
	switch (t->curs_mode)
	{
		case CURSOR_NORMAL:
			(void)Fputchar(t->fd, 0x001a005bL | shiftmask, 0); 	/* '[' */
			break;
		case CURSOR_TRANSMIT:
			(void)Fputchar(t->fd, 0x0018004fL | shiftmask, 0); 	/* 'O' */
			break;
	}
}


static bool text_type(WINDOW *w, short code, short shift)
{
	TEXTWIN	*t = w->extra;
	WINCFG	*cfg = t->cfg;
	long offset, height;
	long shiftmask = ((long)shift) << 24;
	long c = (code & 0x00ff) | (((long)code & 0x0000ff00L) << 8L) | shiftmask;
	
	/* Context-sensitive help */
	if (code == 0x6200)		/* HELP */
	{
		char	selected[80];

		/*
		 * if there is a selected text region send it to ST-Guide and return,
		 * else send the HELP key to the program running in the window
		*/
		copy(w, selected, 80);
		if (strlen(selected) > 0)
		{
			call_stguide(selected, FALSE);
			return FALSE;
		}
	}

	/* Window scrolling like in Linux's console */
	if (shift == 1 || shift == 2) {				/* Shift pressed */
		if (code == 0x4900 || code == 0x4838)	/* PageUp or ArrowUp */
		{
			/* XXX: Is the 0x4900 really a _shifted_ PageUp scancode? */
			arrow_textwin(w, WA_UPPAGE);
			return FALSE;
		}
		if (code == 0x5100 || code == 0x5032)	/* PageDown or ArrowDn */
		{
			/* XXX: Is the 0x5100 really a _shifted_ PageDown scancode? */
			arrow_textwin(w, WA_DNPAGE);
			return FALSE;
		}
	}

	/* no input on the console */
	if (con_fd > 0 && t->fd == con_fd)
		return FALSE;

	if (cfg->char_tab != TAB_ATARI)
	{
		int	asc;

		asc = (int) c & 0x00FF;
		switch (cfg->char_tab)
		{
			case TAB_ISO :
				asc = (int) st_to_iso[asc];
				break;
			default:
				debug("text_type: unknown char_tab: %d\n", cfg->char_tab);
				break;
		}
		c = c & 0xFFFFFF00L;
		c = c | (long) asc;
	}

	if (t->miny)
	{
		offset = t->miny * t->cheight;
		if (offset > t->offy)
		{
			/* we were looking at scroll back */
			/* now move so the cursor is visible */
			height = t->cheight * t->maxy - w->work.g_h;
			if (height <= 0)
				offset = 0;
			else
			{
				offset = 1000L * t->cy * t->cheight/height;
				if (offset > 1000L)
					offset = 1000L;
			}
			(*w->vslid)(w, (int)offset);
		}
	}
	if (t->fd)
	{
#define READBUFSIZ 256
		char buf[READBUFSIZ];

		long r = Foutstat(t->fd);
		if (r <= 0)
		{
			r = Fread(t->fd, (long)READBUFSIZ, buf);
			if (r > 0)
				write_text(t, buf, r);
			(void)Fselect(500, 0L, 0L, 0L);
			r = Foutstat(t->fd);
		}
		if (r > 0)
		{
			/* vt52 -> vt100 cursor/function key remapping */
			if (t->vt_mode == MODE_VT100 && code >= 0x3b00 && code <= 0x5000 && ((code&0xFF)<=0x20))
			{
				(void)Fputchar(t->fd, 0x0001001bL | shiftmask, 0);	  /* 'ESC' */
			  	switch (code)
			  	{
					case 0x3b00: 	/* F1 */
						(void)Fputchar(t->fd, 0x0018004fL | shiftmask, 0);	 /* 'O' */
						(void)Fputchar(t->fd, 0x00180050L | shiftmask, 0);	 /* 'P' */
						break;
					case 0x3c00: 	/* F2 */
						(void)Fputchar(t->fd, 0x0018004fL | shiftmask, 0);	 /* 'O' */
						(void)Fputchar(t->fd, 0x00100051L | shiftmask, 0);	 /* 'Q' */
						break;
					case 0x3d00: 	/* F3 */
						(void)Fputchar(t->fd, 0x0018004fL | shiftmask, 0);	 /* 'O' */
						(void)Fputchar(t->fd, 0x00130052L | shiftmask, 0);	 /* 'R' */
						break;
				  	case 0x3e00: 	/* F4 */
						(void)Fputchar(t->fd, 0x0018004fL | shiftmask, 0);	 /* 'O' */
						(void)Fputchar(t->fd, 0x001f0053L | shiftmask, 0);	 /* 'S' */
						break;
				  	case 0x3f00: 	/* F5 */
						(void)Fputchar(t->fd, 0x0018004fL | shiftmask, 0);	 /* 'O' */
						(void)Fputchar(t->fd, 0x00140054L | shiftmask, 0);	 /* 'T' */
						break;
				  	case 0x4000: 	/* F6 */
						(void)Fputchar(t->fd, 0x0018004fL | shiftmask, 0);	 /* 'O' */
						(void)Fputchar(t->fd, 0x00160055L | shiftmask, 0);	 /* 'U' */
						break;
				  	case 0x4100: 	/* F7 */
						(void)Fputchar(t->fd, 0x0018004fL | shiftmask, 0);	 /* 'O' */
						(void)Fputchar(t->fd, 0x002f0056L | shiftmask, 0);	 /* 'V' */
						break;
				  	case 0x4200: 	/* F8 */
						(void)Fputchar(t->fd, 0x0018004fL | shiftmask, 0);	 /* 'O' */
						(void)Fputchar(t->fd, 0x00110057L | shiftmask, 0);	 /* 'W' */
						break;
				 	case 0x4300: 	/* F9 */
						(void)Fputchar(t->fd, 0x0018004fL | shiftmask, 0);	 /* 'O' */
						(void)Fputchar(t->fd, 0x002d0058L | shiftmask, 0);	 /* 'X' */
						break;
				  	case 0x4400: 	/* F10 */
						(void)Fputchar(t->fd, 0x0018004fL | shiftmask, 0);	 /* 'O' */
						(void)Fputchar(t->fd, 0x00150059L | shiftmask, 0);	 /* 'Y' */
 						break;
				  	case 0x4800: 	/* up arrow */
						text_appl_cursor(t, shiftmask);
						(void)Fputchar(t->fd, 0x001e0041L | shiftmask, 0);	 /* 'A' */
						break;
				  	case 0x4b00: 	/* left arrow */
						text_appl_cursor(t, shiftmask);
						(void)Fputchar(t->fd, 0x00200044L | shiftmask, 0);	 /* 'D' */
						break;
				  	case 0x4d00: 	/* right arrow */
						text_appl_cursor(t, shiftmask);
						(void)Fputchar(t->fd, 0x002e0043L | shiftmask, 0);	 /* 'C' */
						break;
				  	case 0x5000: 	/* down arrow */
						text_appl_cursor(t, shiftmask);
						(void)Fputchar(t->fd, 0x00300042L | shiftmask, 0);	 /* 'B' */
						break;
					case 0x6200: /* help */
						(void)Fputchar(t->fd, 0x00230048L | shiftmask, 0);	 /* 'H' */
						break;
					case 0x4700: /* home */
						(void)Fputchar(t->fd, 0x001a005bL | shiftmask, 0); 	/* '[' */
						(void)Fputchar(t->fd, 0x00230048L | shiftmask, 0);	 /* 'H' */
						break;
					case 0x4f00: /* end */
						(void)Fputchar(t->fd, 0x001a005bL | shiftmask, 0); 	/* '[' */
						(void)Fputchar(t->fd, 0x00210046L | shiftmask, 0);	 /* 'F' */
						break;
				  	default:
						(void)Fputchar(t->fd, c, 0);
						break;
				}
			} else if (c == 0x001c000dL && (t->curr_tflags & TCRNL))
			{
				(void)Fputchar(t->fd, c, 0);
				(void)Fputchar(t->fd, 0x0024000aL, 0);
			} else
			{
				SYSLOG((LOG_ERR, "Writing code 0x%08lx", c));
				(void)Fputchar(t->fd, c, 0);
			}
			return TRUE;
		}
	}
	else
	{
		if ((char)code == '\r')			/* Return/Enter */
			(*w->closed)(w);
	}
	return FALSE;
}

/*
 * Auswertung der Mausklicks innerhalb eines Fensters.
*/
static bool text_click(WINDOW *w, short clicks, short x, short y, short kshift, short button)
{
	TEXTWIN	*t;
	short x1, y1, d;

	t = w->extra;

	if (button == 1)									/* left click */
	{
		/* convert to character coordinates */
		pixel2char(t, x, y, &x1, &y1);

		if (clicks == 2)								/* double click -> select word */
		{
			if (select_word(t, x1, y1))
				copy(w, NULL, 0);
		}
		else
		{
			graf_mkstate(&d, &d, &button, &kshift);
			if (button == 1)							/* hold down -> mark region */
			{
				if (t->cflags[y1][x1] & CSELECTED)
					drag_selection(t);
				else
				{
					if (select_text(t, x1, y1, kshift))
						copy(w, NULL, 0);
				}
			}
			else
			{
				if (t->cflags[y1][x1] & CSELECTED)		/* clicked on selected text */
				{
					char sel[84];

					copy(w, sel, sizeof (sel) - 4);
					send_to("STRNGSRV", sel);		/* send it to StringServer */
				}
				else
					unselect(t);						/* single click -> deselect all */
			}
		}
	}
	else if (button == 2)							/* right click */
	{
		if (clicks == 1)								/* single click -> insert */
			paste(w);
	}
	return TRUE;
}


static void calc_cursor(TEXTWIN *t, WINCFG *cfg)
{
	if (cfg->blockcursor)
	{
		t->curs_height = t->cheight;
		t->curs_offy = 0;
	} else
	{
		t->curs_height = t->cheight >> 3;
		t->curs_offy = t->cheight - 2 - t->curs_height;
	}
	if (t->curs_height < 1)
		t->curs_height = 1;
	if (t->curs_offy + t->curs_height > t->cheight)
		t->curs_offy = 0;
}


/*
 * Create a new text window with title t, w columns, and h rows,
 * and place it at x, y on the screen; the new window should have the
 * set of gadgets specified by "kind", and should provide "s"
 * lines of scrollback.
 */
TEXTWIN *create_textwin(char *title, WINCFG *cfg)
{
	WINDOW *v;
	TEXTWIN *t;
	short firstchar, lastchar, distances[5], maxwidth, effects[3];
	short i;
	unsigned long flag;
	short bwidth, bheight, dummy;

	t = malloc (sizeof *t);
	if (!t)
		return NULL;

	memset (t, 0, sizeof *t);

	t->maxx = cfg->col;
	t->maxy = cfg->row + cfg->scroll;
	t->miny = cfg->scroll;

	t->vt_mode = cfg->vt_mode;
	if (cfg->vt_mode == MODE_VT100)
	{
		t->curs_mode = CURSOR_NORMAL;
		t->tabs = NULL;
	}

	t->scroll_top = t->miny;
	t->scroll_bottom = t->maxy;

	/* we get font data from the VDI */
	set_font(cfg->font_id, cfg->font_pts);
	vqt_fontinfo(vdi_handle, &firstchar, &lastchar, distances, &maxwidth, effects);
	t->cfont = cfg->font_id;
	t->cpoints = cfg->font_pts;
	t->cmaxwidth = maxwidth;
	t->cheight = distances[0]+distances[4]+1;
	t->cbase = distances[4];
	t->minADE = firstchar;
	t->maxADE = lastchar;
	t->cwidths = 0;
	set_cwidths(t);

	/* initialize the window data */
	t->alloc_width = (NCOLS (t) + 15) & 0xfffffff0;
	t->alloc_height = (t->maxy + 15) & 0xfffffff0;

	t->cdata = calloc(t->alloc_height, sizeof(t->cdata[0]));
	t->cflags = calloc(t->alloc_height, sizeof(t->cflags[0]));
	t->dirty = calloc(t->alloc_height, sizeof(t->dirty[0]));

	if (!t->dirty || !t->cflags || !t->cdata)
		goto bail_out;

	t->vdi_colors = cfg->vdi_colors;
	t->fg_effects = cfg->fg_effects;
	t->bg_effects = cfg->bg_effects;
	t->curr_tflags = (TWRAPAROUND | TCURS_ON);
	t->gsets[0] = 'B';
	t->gsets[1] = '0';
	t->gsets[2] = 'B';
	t->gsets[3] = 'B';
	t->wintop = 0;

	t->cfg = cfg;
	original_colors (t);
	flag = t->curr_cattr;

	for (i = 0; i < t->alloc_height; ++i)
	{
		t->cdata[i] = malloc(sizeof(t->cdata[0][0]) * t->alloc_width);
		t->cflags[i] = malloc(sizeof(t->cflags[0][0]) * t->alloc_width);
		if (!t->cflags[i] || !t->cdata[i])
			goto bail_out;

		memset (t->cdata[i], ' ', t->alloc_width);

		if (i == 0)
			memulset (t->cflags[i], flag, t->alloc_width);
		else
			memcpy (t->cflags[i], t->cflags[0],
				t->alloc_width * sizeof t->cflags[0][0]);
	}

	t->scrolled = t->nbytes = t->draw_time = 0;

	/* initialize the WINDOW struct */
	wind_calc (WC_BORDER, cfg->kind, 100, 100,
		   cfg->col * t->cmaxwidth, cfg->row * t->cheight,
		   &dummy, &dummy, &bwidth, &bheight);

	v = create_window(title, cfg->kind, cfg->xpos, cfg->ypos, 
			  bwidth, bheight, SHRT_MAX >> 1, SHRT_MAX >> 1);
	if (!v)
	{
		goto bail_out;
	}

	v->extra = t;
	t->win = v;

	if (t->vt_mode == MODE_VT100)
		reset_tabs (t);

	/* overwrite the methods for v */
	if (t->vt_mode == MODE_VT100)
		t->output = vt100_putch;
	else
		t->output = vt52_putch;

	v->draw = draw_textwin;
	v->closed = close_textwin;
	v->fulled = full_textwin;
	v->moved = move_textwin;
	v->sized = size_textwin;
	v->arrowed = arrow_textwin;
	v->vslid = vslid_textwin;
	v->timer = update_cursor;

	v->keyinp = text_type;
	v->mouseinp = text_click;

	t->cx = 0;
	t->offy = t->maxy * t->cheight - v->work.g_h;
	t->cy = t->maxy - (v->work.g_h / t->cheight);

	t->fd = t->pgrp = 0;

	t->pty[0] = '\0';
	t->shell = NO_SHELL;

	t->prog = t->cmdlin = t->progdir = 0;

	t->block_x1 = 0;
	t->block_x2 = 0;
	t->block_y1 = 0;
	t->block_y2 = 0;

	t->saved_x = t->saved_y = -1;
	t->saved_cattr = t->curr_cattr;
	t->saved_tflags = t->curr_tflags;
	t->last_cx = t->last_cy = -1;

	calc_cursor(t, cfg);

	return t;

bail_out:
	if (t->cflags)
		for (i = 0; i < t->maxy; ++i)
			free (t->cflags[i]);
	if (t->cdata)
		for (i = 0; i < t->maxy; ++i)
			free (t->cdata[i]);
	free (t->cdata);
	free (t->dirty);
	free (t->cflags);
	free(t);
	return NULL;
}

/*
 * destroy a text window
 */
void destroy_textwin(TEXTWIN *t)
{
	int i;

	if (con_win != NULL && t->win == con_win->win)
		con_win = NULL;
	destroy_window(t->win);
	for (i = 0; i < t->maxy; i++)
	{
		free(t->cdata[i]);
		free(t->cflags[i]);
	}
	free(t->prog);
	free(t->cmdlin);
	free(t->progdir);

	free(t->cflags);
	free(t->cdata);
	free(t->dirty);
	free(t->cwidths);

	/* Proze� korrekt abmelden */
	term_proc(t);

	free(t);
}


void textwin_term(void)
{
	WINDOW	*v;
	TEXTWIN	*t;

	v = gl_winlist;
	while (v)
	{
		t = v->extra;
		term_proc(t);
		v = v->next;
	}
}

/*
 * reset a window's font: this involves resizing the window, too
 */
void textwin_setfont(TEXTWIN *t, short font, short points)
{
	WINDOW *w;
	short firstchar, lastchar, distances[5], maxwidth, effects[3];
	short width, height;
	short dummy;
	int reopen = 0;

	w = t->win;
	if (t->cfont == font && t->cpoints == points)
		return;		/* no real change happens */

	if (w->handle >= 0)
	{
		wind_close(w->handle);
		wind_delete(w->handle);
		reopen = 1;
		gl_winanz--;
		w->handle = -1;
	}

	t->cfont = font;
	t->cpoints = points;
	set_font(font, points);
	vqt_fontinfo(vdi_handle, &firstchar, &lastchar, distances, &maxwidth, effects);
// 	t->cmaxwidth = maxwidth;
// 	t->cheight = distances[0]+distances[4]+1;
	t->cbase = distances[4];
	t->minADE = firstchar;
	t->maxADE = lastchar;
	set_cwidths(t);

	t->cmaxwidth = maxwidth;
	t->cheight = distances[0]+distances[4]+1;
	t->win->max_w = width = NCOLS(t) * t->cmaxwidth;
	t->win->max_h = height = NROWS(t) * t->cheight;

	wind_calc(WC_BORDER, w->kind, w->full.g_x, w->full.g_y, width, height, &dummy, &dummy, &w->full.g_w, &w->full.g_h);
	if (w->full.g_w > gl_desk.g_w)
		w->full.g_w = gl_desk.g_w;
	if (w->full.g_h > gl_desk.g_h)
		w->full.g_h = gl_desk.g_h;

	if (w->full.g_x + w->full.g_w > gl_desk.g_x + gl_desk.g_w)
		w->full.g_x = gl_desk.g_x + (gl_desk.g_w - w->full.g_w)/2;
	if (w->full.g_y + w->full.g_h > gl_desk.g_y + gl_desk.g_h)
		w->full.g_y = gl_desk.g_y + (gl_desk.g_h - w->full.g_h)/2;

	wind_calc(WC_WORK, w->kind, w->full.g_x,w->full.g_y, w->full.g_w, w->full.g_h, &dummy, &dummy, &w->work.g_w, &w->work.g_h);
	if (reopen)
		open_window(w, FALSE);
}

static void
emergency_close (TEXTWIN* tw)
{
	close_textwin (tw->win);
	form_alert (1, "[3][ Virtual memory exhausted ][ OK ]");
}

/* Change scrollback buffer of new window to new
   amount of lines.  */
static void
change_scrollback (TEXTWIN* tw, short scrollback)
{
	short diff = scrollback - tw->miny;

	if (diff == 0)
		return;
	else if (diff < 0)
	{
		unsigned char* tmp_data = alloca (-diff * sizeof tmp_data);
		unsigned long* tmp_cflag = alloca ((-diff) * sizeof tmp_cflag);

		/* Scrollback is becoming smaller.  We move the lines
		   at the top to the bottom.	*/

		memmove (tw->dirty, tw->dirty - diff,
			 (sizeof *tw->dirty) * (tw->maxy + diff));

		memcpy (tmp_data, tw->cdata + tw->maxy + diff,
			(sizeof tmp_data) * (-diff));
		memcpy (tmp_cflag, tw->cflags + tw->maxy + diff,
			(sizeof tmp_cflag) * (-diff));

		memmove (tw->cdata, tw->cdata - diff,
			 (sizeof tmp_data) * (tw->maxy + diff));
		memmove (tw->cflags, tw->cflags - diff,
			 (sizeof tmp_cflag) * (tw->maxy + diff));

		memcpy (tw->cdata + tw->maxy + diff, tmp_data,
			(sizeof tmp_data) * (-diff));
		memcpy (tw->cflags + tw->maxy + diff, tmp_cflag,
			(sizeof tmp_cflag) * (-diff));
	} else {
		int i;
		/* Scrollback is becoming bigger.  */
		if (tw->maxy + diff > tw->alloc_height)
		{
			unsigned short saved_height = tw->alloc_height;

			size_t data_chunk;
			size_t cflag_chunk;

			tw->alloc_height = (tw->maxy + diff + 15) & 0xfffffff0;
			data_chunk = (sizeof tw->cdata[0][0]) * tw->alloc_width;
			cflag_chunk = (sizeof tw->cflags[0][0]) * tw->alloc_width;

			tw->dirty = realloc (tw->dirty, (sizeof *tw->dirty) * tw->alloc_height);
			if (tw->dirty == NULL)
				goto bail_out;
			tw->cdata = realloc (tw->cdata, (sizeof *tw->cdata) * tw->alloc_height);
			if (tw->cdata == NULL)
				goto bail_out;
			tw->cflags = realloc (tw->cflags, (sizeof *tw->cflags) * tw->alloc_height);
			if (tw->cflags == NULL)
				goto bail_out;

			for (i = saved_height; i < tw->alloc_height; ++i)
			{
				tw->cdata[i] = malloc (data_chunk);
				tw->cflags[i] = malloc (cflag_chunk);

				if (tw->cdata[i] == NULL || tw->cflags[i] == NULL)
					goto bail_out;
			}
		}

		memmove (tw->dirty, tw->dirty + diff,
			 (sizeof *tw->dirty) * tw->maxy);

		memset (tw->dirty, 0,
			  (sizeof tw->dirty[0]) * tw->maxy);

		memmove (tw->cdata + diff, tw->cdata,
			 (sizeof *tw->cdata) * tw->maxy);
		memmove (tw->cflags + diff, tw->cflags,
			 (sizeof *tw->cflags) * tw->maxy);

		for (i = 0; i < diff; ++i) {
			memset (tw->cdata[i], ' ', (sizeof tw->cdata[0][0]) * NCOLS (tw));

			if (i == 0)
				memulset (tw->cflags[i], tw->curr_cattr,
					  NCOLS (tw));
			else
				memcpy (tw->cflags[i], tw->cflags[0],
					(sizeof tw->cflags[0][0]) *
					 NCOLS (tw));
		}

	}

	tw->miny += diff;
	tw->maxy += diff;
	tw->cy += diff;

	/* Reset scrolling region.  */
	tw->scroll_top += diff;
	tw->scroll_bottom += diff;

	tw->offy += diff * tw->cheight;

	set_scroll_bars (tw);

	return;

bail_out:
	emergency_close (tw);

}

/* Change number of visible lines in window.  */
static void
change_height (TEXTWIN* tw, short rows)
{
	short diff = rows - NROWS (tw);

	if (diff == 0)
		return;
	else if (diff > 0)
	{
		int i;

		/* We need to enlarge the buffer.  */
		if (tw->maxy + diff > tw->alloc_height)
		{
			unsigned short saved_height = tw->alloc_height;
			size_t data_chunk;
			size_t cflag_chunk;

			tw->alloc_height = (tw->maxy + diff + 15) & 0xfffffff0;
			data_chunk = (sizeof tw->cdata[0][0]) *
					tw->alloc_width;
			cflag_chunk = (sizeof tw->cflags[0][0]) *
					  tw->alloc_width;

			tw->dirty = realloc (tw->dirty, (sizeof *tw->dirty) *
						  tw->alloc_height);
			if (tw->dirty == NULL)
				goto bail_out;
			tw->cdata = realloc (tw->cdata, (sizeof *tw->cdata) *
						 tw->alloc_height);
			if (tw->cdata == NULL)
				goto bail_out;
			tw->cflags = realloc (tw->cflags, (sizeof *tw->cflags) *
						  tw->alloc_height);
			if (tw->cflags == NULL)
				goto bail_out;

			for (i = saved_height; i < tw->alloc_height; ++i) {
				tw->cdata[i] = malloc (data_chunk);
				tw->cflags[i] = malloc (cflag_chunk);

				if (tw->cdata[i] == NULL || tw->cflags[i] == NULL)
					goto bail_out;
			}
		}

		for (i = tw->maxy; i < tw->maxy + diff; ++i)
		{
			memset (tw->cdata[i], ' ', (sizeof tw->cdata[0][0]) * NCOLS (tw));

			if (i == tw->maxy)
				memulset (tw->cflags[i], tw->curr_cattr,
					  NCOLS (tw));
			else
				memcpy (tw->cflags[i], tw->cflags[tw->maxy],
					(sizeof tw->cflags[tw->maxy][0]) *
					 NCOLS (tw));
		}

		 memset (tw->dirty + tw->maxy, ALLDIRTY, diff);
	}

	tw->maxy += diff;
	if (tw->cy > tw->maxy)
		tw->cy = tw->maxy;

	tw->scroll_bottom = tw->maxy;
	tw->scroll_top = tw->miny;
	tw->curr_tflags &= ~TORIGIN;
	
	/* Dunno why this is necessary.  */
	tw->offy += diff * tw->cheight;

	set_scroll_bars (tw);

	return;

bail_out:
	emergency_close (tw);

}

static void
change_width (TEXTWIN* tw, short cols)
{
	short diff = cols - NCOLS (tw);

	if (diff == 0)
		return;
	else if (diff > 0)
	{
		int i;
		unsigned long flag = tw->curr_cattr | CDIRTY;
		int old_cols = NCOLS (tw);
		int new_cols = old_cols + diff;

		/* We need to enlarge the buffer.  */
		if (new_cols + 1 > tw->alloc_width)
		{
			size_t data_chunk;
			size_t cflag_chunk;

			tw->alloc_width = (tw->maxx + diff + 15) & 0xfffffff0;
			data_chunk = (sizeof tw->cdata[0][0]) * tw->alloc_width;
			cflag_chunk = (sizeof tw->cflags[0][0]) * tw->alloc_width;

			for (i = 0; i < tw->alloc_height; ++i)
			{
				tw->cdata[i] = realloc (tw->cdata[i], data_chunk);
				tw->cflags[i] = realloc (tw->cflags[i], cflag_chunk);

				if (tw->cdata[i] == NULL || tw->cflags[i] == NULL)
					goto bail_out;
			}
		}

		/* Initialize the freshly exposed columns.  */
		for (i = 0; i < tw->maxy; ++i)
		{
			memset (tw->cdata[i] + old_cols, ' ', diff);

			if (i == 0)
				memulset (tw->cflags[i] + old_cols,
					  flag, diff);
			else
				memcpy (tw->cflags[i] + old_cols,
					tw->cflags[0] + old_cols,
					(sizeof tw->cflags[0][0]) *
					 diff);
		}
		memset (tw->dirty, ALLDIRTY, tw->maxy);
	}

	tw->maxx += diff;
	
	if (tw->cx >= NCOLS (tw))
		tw->cx = NCOLS (tw) - 1;

	return;

bail_out:
	emergency_close (tw);

}

/*
 * make a text window have a new number of rows and columns, and
 * a new amount of scrollback
 */
void
resize_textwin (TEXTWIN* tw, short cols, short rows, short scrollback)
{
	int changed = 0;

	if (NCOLS (tw) == cols &&
	    SCROLLBACK (tw) == scrollback &&
	    tw->maxy == rows + scrollback)
		return;		/* no change */

	if (NCOLS(tw) != cols && (changed = 1))
		change_width (tw, cols);

	if (NROWS (tw) != rows && (changed = 1))
		change_height (tw, rows);

	if (tw->miny != scrollback)
		change_scrollback (tw, scrollback);

	if (changed)
	{
		GRECT border;
		WINDOW* win = tw->win;

		tw->cfg->col = NCOLS (tw);
		tw->cfg->row = NROWS (tw);
		tw->cfg->scroll = tw->miny;

		win->work.g_w = NCOLS (tw) * tw->cmaxwidth;
		win->work.g_h = NROWS (tw) * tw->cheight;

		wind_calc (WC_BORDER, win->kind,
			   win->work.g_x, win->work.g_y,
			   win->work.g_w, win->work.g_h,
		   		 &border.g_x, &border.g_y,
		   		 &border.g_w, &border.g_h);
		wind_set (win->handle, WF_CURRXYWH,
			  border.g_x, border.g_y,
		  	  border.g_w, border.g_h);

		notify_winch (tw);
	}

	return;
}

static void
reread_size (TEXTWIN* tw)
{
	int rows = tw->win->work.g_h / tw->cheight;
	int cols = tw->win->work.g_w / tw->cmaxwidth;
	int changed = 0;

	if (cols != NCOLS (tw) && (changed = 1))
		change_width (tw, cols);
	if (rows != NROWS (tw) && (changed = 1))
		change_height (tw, rows);

	tw->cfg->col = cols;
	tw->cfg->row = rows;

	if (changed)
		notify_winch (tw);
}

void
notify_winch (TEXTWIN* tw)
{
	struct winsize ws;

	ws.ws_row = NROWS (tw);
	ws.ws_col = NCOLS (tw);
	ws.ws_xpixel = ws.ws_ypixel = 0;
	(void) Fcntl (tw->fd, &ws, TIOCSWINSZ);
	(void) Pkill (-(tw->pgrp), SIGWINCH);
}

void
reconfig_textwin(TEXTWIN *t, WINCFG *cfg)
{
	change_window_gadgets(t->win, cfg->kind);
	t->curs_drawn = FALSE;
	calc_cursor(t, cfg);
	if (cfg->title[0] != '\0')
		title_window(t->win, cfg->title);
	resize_textwin(t, cfg->col, cfg->row, cfg->scroll);
	textwin_setfont(t, cfg->font_id, cfg->font_pts);

	original_colors (t);

	memset (t->dirty, ALLDIRTY, t->alloc_height);

	refresh_textwin(t, FALSE);

	/* cfg->vt_mode wird bewu�t ignoriert -> wirkt erst bei neuem Fenster */
}

/* set the "cwidths" array for the given window correctly;
 * this function may be called ONLY when the font & height are already
 * set correctly, and only after t->cmaxwidth is set
 */
static void set_cwidths(TEXTWIN *t)
{
	short i, status, dummy, wide;
	short widths[256];
	short monospaced = 1;
	short dfltwide;

	if (t->cwidths)
	{
		free(t->cwidths);
		t->cwidths = 0;
	}
	vqt_width(vdi_handle, '?', &dfltwide, &dummy, &dummy);

	for (i = 0; i < 255; i++)
	{
		status = vqt_width(vdi_handle, i, &wide, &dummy, &dummy);
		if (status == -1)
			wide = dfltwide;
		if (wide != t->cmaxwidth)
			monospaced = 0;
		widths[i] = wide;
	}
	if (!monospaced)
	{
		t->cwidths = malloc(256 * sizeof(short));
		if (!t->cwidths)
			return;
		for (i = 0; i < 255; i++)
			t->cwidths[i] = widths[i];
	}
}


/*
 * output a string to text window t, just as though the user typed it.
 */
void sendstr(TEXTWIN *t, char *s)
{
	long c;

	if (t->fd > 0 && t->fd != con_fd)
	{
		while (*s)
		{
			c = *((unsigned char *)s);
			s++;
			(void)Fputchar(t->fd, c, 0);
		}
	}
}

void write_text(TEXTWIN *t, char *b, long len)
{
	unsigned char *src = (unsigned char *) b, c;
	long cnt;
	if (len == -1)
		cnt = strlen(b);
	else
		cnt = len;

	while (cnt-- > 0)
	{
		c = *src++;
		if (c != '\0')
		{
			(*t->output)(t, c);
			t->nbytes++;

			/* The number of scrolled lines is ignored
			   as an experimental feature.  There is no
			   point in artificially pulling a break
			   when output is too fast too read.
			   This may look strange but it enhance the
			   usability and subjective performance.  */
			if ( needs_redraw(t) )
				refresh_textwin(t, FALSE);
		}
	}
}

/*
 * Sucht ein Fenster zu handle bzw. talkID.
 */
TEXTWIN *get_textwin(short handle, short talkID)
{
	WINDOW *w;
	TEXTWIN	*t;

	if (handle < 0 || talkID < 0)
		return NULL;

	for (w = gl_winlist; w; w = w->next)
	{
		if (handle > 0 && w->handle == handle)
			return w->extra;
		t = w->extra;
		if (talkID > 0 && t->talkID == talkID)
			return t;
	}
	return NULL;
}
