
#undef DEBUG_AV
#include <support.h>
#include <unistd.h>
#include <osbind.h>

#include "av.h"
#include "environ.h"
#include "toswin2.h"
#include "proc.h"
#include "drag.h"

int		av_shell_id = -1;		/* ID des Desktops */
unsigned short	av_shell_status = 0;		/* Welche AV_* kann Desktop */

bool	gl_avcycle = FALSE;

static char	*global_str = NULL;
static short	msgbuff[8];

static bool send_msg(void)
{
	int	ret;

	msgbuff[1] = gl_apid;
	msgbuff[2] = 0;
	ret = appl_write(av_shell_id, sizeof(msgbuff), msgbuff);
	return (ret > 0);
}

static void send_avstarted(int id, int i1, int i2)
{
	short	msg[8];
	
	msg[0] = AV_STARTED;
	msg[1] = gl_apid;
	msg[2] = 0;
	msg[3] = i1;
	msg[4] = i2;
#ifdef DEBUG_AV
debug("AV_STARTED (%d)\n", id);
#endif
	appl_write(id, (int)sizeof(msg), msg);	/* mu� nicht av_shell_id sein!! */
}

static void send_avprot(void)
{
	if (av_shell_id >= 0)
	{
		memset(msgbuff, 0, (int)sizeof(msgbuff));
		msgbuff[0] = AV_PROTOKOLL;
		msgbuff[3] = (2/*|16*/);		/* VA_START, Quoting */
		strcpy(global_str, TW2NAME);
		ol2ts((long)global_str, &msgbuff[6], &msgbuff[7]);
#ifdef DEBUG_AV
debug("AV_PROTOKOLL (%d)\n", av_shell_id);
#endif
		send_msg();
	}
}

static void send_avexit(void)
{
	if ((av_shell_id >= 0) && (av_shell_status & 1024))
	{
		memset(msgbuff, 0, sizeof(msgbuff));
		msgbuff[0] = AV_EXIT;
		msgbuff[3] = gl_apid;
#ifdef DEBUG_AV
debug("AV_EXIT\n");
#endif
		send_msg();
	}
}

bool send_avkey(short ks, short kr)
{
	bool	b = FALSE;

	if ((av_shell_id >= 0) && (av_shell_status & 1))
	{
		memset(msgbuff, 0, sizeof(msgbuff));
		msgbuff[0] = AV_SENDKEY;
		msgbuff[1] = gl_apid;
		msgbuff[3] = ks;
		msgbuff[4] = kr;
#ifdef DEBUG_AV
debug("AV_SENDKEY (%d,%d)\n", ks, kr);
#endif
		b = send_msg();
	}
	return b;
}

void send_avwinopen(short handle)
{
	if ((av_shell_id >= 0) && gl_avcycle)
	{
		memset(msgbuff, 0, (int)sizeof(msgbuff));
		msgbuff[0] = AV_ACCWINDOPEN;
		msgbuff[3] = handle;
#ifdef DEBUG_AV
debug("AV_ACCWINDOPEN (%d)\n", handle);
#endif
		send_msg();
	}
}

void send_avwinclose(short handle)
{
	if ((av_shell_id >= 0) && gl_avcycle)
	{
		memset(msgbuff, 0, (int)sizeof(msgbuff));
		msgbuff[0] = AV_ACCWINDCLOSED;
		msgbuff[3] = handle;
#ifdef DEBUG_AV
debug("AV_ACCWINDCLOSED (%d)\n", handle);
#endif
		send_msg();
	}
}

void handle_av(short *msg)
{
	char *p;
	
	switch (msg[0])
	{
		case VA_START :
			p = (char *)ts2ol(msg[3], msg[4]);
			if (p != NULL)
			{
#ifdef DEBUG_AV
debug("VA_START %s\n", p);
#endif
				if (strcmp(p, "-l") == 0)
					new_shell();
				else
				{	char charsearch, *command=p, filename[256] = "", path[256] = "", *ptpath=path, *env, arg[125] = "";
					int count=0;
					WINCFG	*cfg;
					TEXTWIN	*t;

					if(*p=='\"') 
					{
						charsearch='"';
						command++;
					}
					else charsearch=' ';
					while((*command!=charsearch) && (*command!=0) && (count<255))
					{
						*ptpath++ = *command++;  /* copy name of tos application to launch */
						count++;
					}
					*ptpath=0;
					unx2dos(path, filename);
					strcpy(path,filename);
					ptpath = strrchr(path, '\\');
					if (ptpath == NULL)
						getcwd(path, (int)sizeof(path));
					else
					*ptpath = '\0';
					cfg = get_wincfg(filename);
					if(*command!=0) 
					{
						command++;
						while(((*command==charsearch)||(*command==' ')) && (*command!=0)) /*search for start of arguments */
						{
							command++;
						}
						if(*command!=0) 
						{
							count=0;
							p=arg;
							*p++=' ';
							while((*command!=charsearch) && (*command!=0) && (count<123))
							{
								*p++ =*command++;
								count++;
							}
							*p=0;
							arg[0] = strlen(arg);
						}
					}
					env = normal_env(cfg->col, cfg->row, cfg->vt_mode);
					t = new_proc(filename, arg, env, path, cfg, -1, NULL);
					if (t)
						open_window(t->win, cfg->iconified);
					free(env);
				}
			}
			send_avstarted(msg[1], msg[3], msg[4]);
			break;

		case VA_PROTOSTATUS :
#ifdef DEBUG_AV
{
	unsigned short m;
	m = (unsigned short)msg[3];
debug("VA_PROTSTATUS %u\n", m);
}
#endif
			av_shell_status = msg[3];
			if (gl_avcycle && !(av_shell_status & 64))
				gl_avcycle = FALSE;			/* glob. Fensterwechsel abschalten */
			break;

		case VA_DRAGACCWIND :				/* bei D&D mit glob. Fensterwechsel */
			p = (char *)ts2ol(msg[6], msg[7]);
			if (p != NULL)
			{
#ifdef DEBUG_AV
debug("VA_DRAGACCWIND %s\n", p);
#endif
				handle_avdd(msg[3], p);
			}
			break;

		default:
#ifdef DEBUG_AV
debug("Unbekannte AV-Meldung: %d, %X\n", msg[0], msg[0]);
#endif
			break;
	}
}

#define DL_PATHMAX	256
#define DL_NAMEMAX	64

void call_stguide(char *data, bool with_hyp)
{
	int	stg_id;
	char *path, fname[9];
	char command[DL_PATHMAX];
	char *ptr;
	char str[80] = "*:\\toswin2.hyp ";

	shel_envrn(&path, "HELPVIEWER=");
	if (path)
	{
		int i;

		ptr = strrchr(path, '\\');
		if (!ptr++)
			ptr = path;

		strncpy(fname, ptr, 8);
		fname[8] = '\0';
		ptr = strrchr(fname, '.');
		if (ptr)
			*ptr = '\0';
		for (i = strlen(fname); i < 8; i++)
			strcat(fname, " ");
	} else
	{
		strcpy(fname, "ST-GUIDE");
	}

	stg_id = appl_find(fname);
	if (stg_id > 0)
	{
		if (with_hyp)
		{
			strcat(str, data);
			send_vastart(stg_id, str);
		} else
		{
			send_vastart(stg_id, data);
		}
	} else
	{
		if (with_hyp)
		{
			strcpy(command + 1, str);
			strcat(command + 1, "'");
			strcat(command + 1, data);
			strcat(command + 1, "'");
		} else
		{
			strcpy(command + 1, data);
		}
		command[0] = (char)strlen(command + 1);

		if (path)
			stg_id = shel_write(SHW_EXEC, 1, SHW_PARALLEL, path, command);

		if (stg_id < 0)
			alert(1, 0, NOSTGUIDE);
	}
}


void send_to(char *appname, char *str)
{
	int	id;
	
	id = appl_find(appname);
	if (id >= 0)
		send_vastart(id, str);
}



bool av_init(void)
{
	int	i;
	char	name[9], *p;

	p = getenv("AVSERVER");
	if (p != NULL)
	{
		strncpy(name, p, 8);
		name[8] = '\0';
		for (i = strlen(name); i < 8; i++)
			strcat(name, " ");
		i = appl_find(name);
		if (i >= 0)
		{
			av_shell_id = i;
			global_str = malloc_global(80);
			send_avprot();
		}
	}
	return (av_shell_id >= 0);
}

void av_term(void)
{
	send_avexit();
	if (global_str != NULL)
		Mfree(global_str);
}
