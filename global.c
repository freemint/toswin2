/*
 * global.c
 *
 * Globale Typen, Variablen und Hilfsfunktionen
 */

#include <ctype.h>
#include <sys/types.h>
#include <gem.h>

#include "global.h"
#include "ansicol.h"
#include "toswin2.h"

/*
 * Globale Variablen
 */
OBJECT	*winicon,
	*conicon,
	*strings;
int	exit_code;
int	vdi_handle;
int	font_anz;
int	windirty = 0;

/*
 * Translation tables
 */
unsigned char st_to_iso[256] = 
{
	/*
	** Atari -> ISO 8859-1   Jo Even Skarstein, 11/11-97
	*/
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
	0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
	0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
	0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
	0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
	0xe7, 0xfc, 0xe9, 0xe2, 0xe4, 0xe0, 0xe5, 0xc7, 0xea, 0xeb, 0xea, 0xef, 0xee, 0xec, 0xc4, 0xc5,
	0xc9, 0xe6, 0xc6, 0xf4, 0xf6, 0xf2, 0xfb, 0xf9, 0xff, 0xf6, 0xdc, 0xa2, 0xa3, 0xa5, 0xdf, 0x9f,
	0xe1, 0xed, 0xf3, 0xfa, 0xf1, 0xd1, 0xaa, 0xba, 0xbf, 0xa9, 0xac, 0xbd, 0xbc, 0xa1, 0xab, 0xbd,
	0xe3, 0xf5, 0xd8, 0xf8, 0xb4, 0xb5, 0xc0, 0xc3, 0xd5, 0xa8, 0xb4, 0xbb, 0xbb, 0xa9, 0xae, 0xbf,
	0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
	0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
	0xe0, 0xdf, 0xe2, 0xe3, 0xe4, 0xe5, 0xb5, 0xe7, 0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
	0xf0, 0xb1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf7, 0xf7, 0xb0, 0xb7, 0xfa, 0xfb, 0xfc, 0xb2, 0xb3, 0xaf,
};

unsigned char iso_to_st[256] = 
{
	/*
	** ISO 8859-1 -> Atari  Jo Even Skarstein, 13/2-99
	*/
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
	0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
	0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
	0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
	0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
	0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f, 
	0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
	0xa0, 0xad, 0x9b, 0x9c, 0xa4, 0x9d, 0x7c, 0xdd, 0xb9, 0xbd, 0xa6, 0xae, 0xaa, 0x2d, 0xbe, 0xff,
	0xf8, 0xf1, 0xfd, 0xfe, 0xba, 0xe6, 0xbc, 0xf9, 0x20, 0x20, 0xa7, 0xaf, 0xac, 0xab, 0x20, 0xa8,
	0xb6, 0x41, 0x41, 0xb7, 0x8e, 0x8f, 0x92, 0x87, 0x45, 0x90, 0x45, 0x45, 0x8d, 0x49, 0x8c, 0x8b,
	0x20, 0xa5, 0x4f, 0x4f, 0x4f, 0xb8, 0x4f, 0xc2, 0xb2, 0x55, 0x55, 0x55, 0x9a, 0x20, 0x20, 0xe1,
	0x85, 0xa0, 0x83, 0xb0, 0x84, 0x86, 0x91, 0x87, 0x8a, 0x82, 0x88, 0x89, 0x20, 0x20, 0x20, 0x20,
	0x20, 0xa4, 0x95, 0xa2, 0x93, 0xb1, 0x94, 0xf6, 0xb3, 0x97, 0xa3, 0x96, 0x81, 0x79, 0x20, 0x98
};


/*
 * Lokale Variablen
*/
static char	**alertarray;
static int 	lasttextcolor = -1, 
		lastfillcolor = -1, 
		lastwrmode = -1, 
		lastheight = -1, 
		lastfont = -1;
static int 	laststyle = -1,
		lastindex = -1;
static int 	lasteffects = -1;


void	set_fillcolor(int col)
{
	if (col == lastfillcolor) 
		return;
	vsf_color(vdi_handle, col);
	lastfillcolor = col;
}

void	set_textcolor(int col)
{
	if (col == lasttextcolor) 
		return;
	vst_color(vdi_handle, col);
	lasttextcolor = col;
}

void	set_texteffects(int effects)
{
	if (effects == lasteffects) 
		return;
	lasteffects = vst_effects(vdi_handle, effects);
}

void	set_wrmode(int mode)
{
	if (mode == lastwrmode) 
		return;
	vswr_mode(vdi_handle, mode);
	lastwrmode = mode;
}

/*
 * This function sets both the font and the size (in points).
 */
void	set_font(int font, int height)
{
	short cw, ch, bw, bh;

	if (font != lastfont) 
	{
		vst_font(vdi_handle, font);
		lastfont = font;
		lastheight = height-1;
	}
	if (height != lastheight) 
		lastheight = vst_point(vdi_handle, height, &cw, &ch, &bw, &bh);
}

void set_fillstyle(int style, int nr)
{
	if (laststyle != style)
		laststyle = vsf_interior(vdi_handle, style);

	if (nr != lastindex)
		lastindex = vsf_style(vdi_handle, nr);
}

int alert(int def, int undo, int num)
{
	graf_mouse(ARROW, NULL);
	return do_walert(def, undo, (char *)alertarray[num], " TosWin2 ");
}


void global_init(void)
{
	short work_out[57];
	
	rsrc_gaddr(R_TREE, WINICON, &winicon);
	rsrc_gaddr(R_TREE, CONICON, &conicon);
	rsrc_gaddr(R_TREE, STRINGS, &strings);
	rsrc_gaddr(R_FRSTR, NOAES41, &alertarray);
	exit_code = 0;
	
	vdi_handle = open_vwork(work_out);
	font_anz = work_out[10];
	if (gl_gdos)
		font_anz += vst_load_fonts(vdi_handle, 0);
	
	init_ansi_colors (work_out);
}

void global_term(void)
{
	if (gl_gdos)
		vst_unload_fonts(vdi_handle, 0);
	v_clsvwk(vdi_handle);
}

/* Like memset bug WHAT is an unsigned long and SIZE
   denotes the number of unsigned longs to write.  */
void
memulset (void* dest, unsigned long what, size_t size)
{
	unsigned long* dst = (unsigned long*) dest;
	size_t chunk = 1;
	size_t max_chunk = size >> 1;
	
	if (size == 0)
		return;
	
	*dst++ = what;
	
	while (chunk < max_chunk) {
		memcpy (dst, dest, chunk * sizeof what);
		dst += chunk;
		chunk <<= 1;
	}
	
	chunk = size - chunk;
	if (chunk != 0)
		memcpy (dst, dest, chunk * sizeof what);
}

#ifdef MEMDEBUG

/* Memory debugging.  We benefit from a nitty-gritty feature of the 
   GNU linker.  The linker will resolve all references to SYMBOL
   with __wrap_SYMBOL and all references to __real_SYMBOL with
   the wrapped symbol.
   
   Note that we can neither use printf nor fprintf because stdio
   itself uses malloc and friends.  */
   
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

static int fd_memdebug = -1;
static int tried = 0;

extern void* __real_malloc (size_t);
extern void* __real_calloc (size_t, size_t);
extern void* __real_realloc (void*, size_t);
extern void __real_free (void*);
extern void* __wrap_malloc (size_t);
extern void* __wrap_calloc (size_t, size_t);
extern void* __wrap_realloc (void*, size_t);
extern void __wrap_free (void*);
extern unsigned long _base;

#define FPUTS(s) (write (fd_memdebug, (s), strlen (s)))
#define PRINT_HEX(n) (print_hex ((unsigned long) n))
#define PRINT_CALLER \
	(PRINT_HEX ((unsigned long) __builtin_return_address (0) - \
	            (_base + 0x100UL)))

static
void print_hex (unsigned long hex)
{
	int shift;
	static char* hex_digits = "0123456789ABCDEF";
	static char outbuf[10];
	char* cursor = outbuf + 2;
	
	outbuf[0] = '0';
	outbuf[1] = 'x';
	
	for (shift = 28; shift >= 0; shift -= 4) {
		*cursor++ = hex_digits[0xf & (hex >> shift)];
	}
	write (fd_memdebug, outbuf, 10);
}

void*
__wrap_malloc (size_t bytes)
{
	void* retval;
	
	retval = __real_malloc (bytes);
	
	if (fd_memdebug < 0 && tried == 0) {
		fd_memdebug = open (MEMDEBUG, O_CREAT|O_WRONLY|O_TRUNC);
		++tried;
	}
	
	if (fd_memdebug >= 0) {
		FPUTS ("malloc: ");
		PRINT_CALLER;
		FPUTS (": ");
		PRINT_HEX (bytes);
		FPUTS (" bytes at ");
		PRINT_HEX (retval);
		FPUTS ("\n");
	}
	
	return retval;
}

void*
__wrap_calloc (size_t nmemb, size_t bytes)
{
	void* retval;
	
	retval = __real_calloc (nmemb, bytes);
	
	if (fd_memdebug < 0 && tried == 0) {
		fd_memdebug = open (MEMDEBUG, O_CREAT|O_WRONLY|O_TRUNC);
		++tried;
	}
	
	if (fd_memdebug >= 0) {
		FPUTS ("calloc: ");
		PRINT_CALLER;
		FPUTS (": ");
		PRINT_HEX (nmemb);
		FPUTS (" x ");
		PRINT_HEX (bytes);
		FPUTS (" bytes at ");
		PRINT_HEX (retval);
		FPUTS ("\n");
	}
	
	return retval;
}

void*
__wrap_realloc (void* where, size_t bytes)
{
	void* retval;
	void* old_buf = where;
	
	if (fd_memdebug < 0 && tried == 0) {
		fd_memdebug = open (MEMDEBUG, O_CREAT|O_WRONLY|O_TRUNC);
		++tried;
	}
	
	if (fd_memdebug >= 0) {
		/* Mainly to detect a segmentation fault due to
		   faulty realloc pointer.  */
		FPUTS ("pre_realloc: ");
		PRINT_CALLER;
		FPUTS (": to ");
		PRINT_HEX (bytes);
		FPUTS (" bytes at ");
		PRINT_HEX (where);
		FPUTS ("\n");
	}
	
	retval = __real_realloc (where, bytes);
	
	if (fd_memdebug >= 0) {
		FPUTS ("realloc: ");
		PRINT_CALLER;
		FPUTS (": ");
		PRINT_HEX (bytes);
		FPUTS (" from ");
		PRINT_HEX (old_buf);
		FPUTS (" to ");
		PRINT_HEX (retval);
		FPUTS ("\n");
	}
	
	return retval;
}

void
__wrap_free (void* where) 
{
	if (fd_memdebug < 0 && tried == 0) {
		fd_memdebug = open (MEMDEBUG, O_CREAT|O_WRONLY|O_TRUNC);
		++tried;
	}
	
	if (fd_memdebug) {
		FPUTS ("free: ");
		PRINT_CALLER;
		FPUTS (": ");
		FPUTS ("at ");
		PRINT_HEX (where);
		FPUTS ("\n");
	}
	
	__real_free (where);
}

#endif
