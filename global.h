
#ifndef tw_global_h
# define tw_global_h 1

#include <sys/types.h>

#include <cflib.h>

#if !defined(__CFLIB__) || (__CFLIB_MAJOR__ != 0 || __CFLIB_MINOR__ < 21)
#error This Toswin2 version require an up-to-date cflib.
#endif

#ifdef FALSE
# undef FALSE
#endif
#ifdef TRUE
# undef TRUE
#endif
typedef enum {FALSE, TRUE} bool;

#define TW2NAME	"TOSWIN2 "

/* Dateinamen */
#define CFGNAME		"toswin2.cfg"
#define RSCNAME		"toswin2.rsc"
#define XCONNAME	"u:\\dev\\xconout2"
#define TWCONNAME	"u:\\dev\\tw-con"

/*
 * Globale Variablen
*/
extern OBJECT	*winicon,
		*conicon,
		*strings;
extern char **stringarray;

extern int	exit_code;		/* Enth�lt den Exitcode eines Kindprozesses */
extern int	vdi_handle;
extern int	font_anz;
extern int	draw_ticks;
#define MAX_DRAW_TICKS 3
extern int	curs_ticks;

#ifdef DEBUG
extern int do_debug;
# include <syslog.h>
#define SYSLOG(x) if (do_debug) { syslog x; }
#else
#define SYSLOG(x)
#endif


/*
 * Hilfsfunktionen
 */

#define	rsc_string(a)	stringarray[a]

void	set_fillcolor(int col);
void	set_textcolor(int col);
void	set_texteffects(int effects);
void	set_wrmode(int mode);
void	set_font(int font, int height);
void 	set_fillstyle(int style, int /* index */);

int	alert(int def, int undo, int num);

void	global_init(void);
void	global_term(void);

void	memulset (void* dest, unsigned long what, size_t size);

#endif
