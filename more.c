/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef lint
char copyright[] =
"@(#) Copyright (c) 1980 Regents of the University of California.\n\
 All rights reserved.\n";
#endif /* not lint */

/*
** more.c - General purpose tty output filter and file perusal program
**
**      by Eric Shienbrood, UC Berkeley
**
**      modified by Geoff Peck, UCB to add underlining, single spacing
**      modified by John Foderaro, UCB to add -c and MORE environment variable
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>
#include <setjmp.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <termios.h>
#include <termcap.h>
#include <regex.h>
#include "morefile.h"
#include "pathnames.h"

#define TBUFSIZ 1024
#define LINSIZ  256
#define ctrl(letter)    (letter & 077)
#define RUBOUT  '\177'
#define ESC     '\033'
#define QUIT    '\034'
#ifndef LONG_MAX
#define LONG_MAX 0x7fffffff
#endif

struct termios  otty, Msavetty;
struct mfile    *cur_mfile;
long            file_size;
int             fnum, no_intty, no_tty, slow_tty;
int             dum_opt, dlines;
int             nscroll = 11;   /* Number of lines scrolled by 'd' */
int             fold_opt = 1;   /* Fold long lines */
int             stop_opt = 1;   /* Stop after form feeds */
int             ssp_opt = 0;    /* Suppress white space */
int             ul_opt = 1;     /* Underline as best we can */
int             promptlen;
int             Currline;       /* Line we are currently at */
int             startup = 1;
int             firstf = 1;
int             notell = 1;
int             docrterase = 0;
int             docrtkill = 0;
int             bad_so; /* True if overwriting does not turn off standout */
int             inwait, Pause, errors;
int             within; /* true if we are within a file,
                        false if we are between files */
int             hard, dumb, noscroll, hardtabs, clreol, eatnl;
int             catch_susp;     /* We should catch the SIGTSTP signal */
char            **fnames;       /* The list of file names */
int             nfiles;         /* Number of files left to process */
char            *shell;         /* The name of the shell to use */
int             shellp;         /* A previous shell command exists */
char            ch;
jmp_buf         restore;
char            Line[LINSIZ];   /* Line buffer */
int             Lpp = 24;       /* lines per page */
char            *Clear;         /* clear screen */
char            *eraseln;       /* erase line */
char            *Senter, *Sexit;/* enter and exit standout mode */
char            *ULenter, *ULexit;      /* enter and exit underline mode */
char            *chUL;          /* underline character */
char            *chBS;          /* backspace character */
char            *Home;          /* go to home */
char            *cursorm;       /* cursor movement */
char            cursorhome[40]; /* contains cursor movement to home */
char            *EodClr;        /* clear rest of screen */
int             Mcol = 80;      /* number of columns */
int             Wrap = 1;       /* set if automargins */
int             soglitch;       /* terminal has standout mode glitch */
int             ulglitch;       /* terminal has underline mode glitch */
int             pstate = 0;     /* current UL state */
char            last_expr[80];  /* last used regular expression */
struct {
    long chrctr, line;
} context, screen_start;

static char version_string[] = "more v1.4";

void argscan (char *s);
struct mfile *checkf (char *fs, int *clearfirst);
int putch (int ch);
void screen (struct mfile *f, int num_lines);
void onquit (int sig);
void chgwinsz (int sig);
void end_it (void);
void end_on_sig (int sig);
void copy_file (FILE *f);
int printd (int n);
void scanstr (int n, char *str);
int tailequ (char *path, char *string);
void prompt (char *filename);
int mgetline (struct mfile *f, int *length);
void erase (int col);
void kill_line (void);
void cleareol (void);
void clreos (void);
int pr (char *s1);
void prbuf (char *s, int n);
void doclear (void);
void home (void);
int command (char *filename, struct mfile *f);
int colon (char *filename, int cmd, int nlines);
int number (char *cmd);
void do_shell (char *filename);
void search (char *buf, struct mfile *file, int n);
void execute (char *filename, char *cmd, ...);
void skiplns (int n, struct mfile *f);
void skipf (int nskip);
void initterm (void);
int readch (void);
void ttyin (char *buf, int nmax, int pchar);
int expand (char *outbuf, char *inbuf);
void show (int ch);
void error (char *mess);
void set_tty (void);
void reset_tty (void);
void rdline (struct mfile *f);
void onsusp (int sig);
char *mybasename (char *pathname);
int magic (FILE *f, char *fs);
void print_help (void);

int
main(int argc, char **argv)
{
    struct mfile        *f;
    register char       *s;
    register char       *p;
    register char       ch;
    register int        left;
    int                 prnames = 0;
    int                 initopt = 0;
    int                 srchopt = 0;
    int                 clearit = 0;
    int                 initline = 0;
    char                initbuf[80];

    nfiles = argc;
    fnames = argv;
    initterm ();
    nscroll = Lpp/2 - 1;
    if (nscroll <= 0)
        nscroll = 1;
    if ((s = getenv("MORE")) != NULL)
        argscan(s);
    while (--nfiles > 0) {
        if ((ch = (*++fnames)[0]) == '-') {
            argscan(*fnames+1);
        }
        else if (ch == '+') {
            s = *fnames;
            if (*++s == '/') {
                srchopt++;
                for (++s, p = initbuf; p < initbuf + 79 && *s != '\0';)
                    *p++ = *s++;
                *p = '\0';
            }
            else {
                initopt++;
                for (initline = 0; *s != '\0'; s++)
                    if (isdigit (*s))
                        initline = initline*10 + *s -'0';
                --initline;
            }
        }
        else break;
    }
    /* allow clreol only if Home and eraseln and EodClr strings are
     *  defined, and in that case, make sure we are in noscroll mode
     */
    if(clreol)
    {
        if((Home == NULL) || (*Home == '\0') ||
           (eraseln == NULL) || (*eraseln == '\0') ||
           (EodClr == NULL) || (*EodClr == '\0') )
              clreol = 0;
        else noscroll = 1;
    }
    if (dlines == 0)
        dlines = Lpp - (noscroll ? 1 : 2);
    left = dlines;
    if (nfiles > 1)
        prnames++;
    if (!no_intty && nfiles == 0) {
        fputs("usage: ", stderr);
        fputs(mybasename(argv[0]), stderr);
        fputs(" [-cpdflsuv] [+linenum | +/pattern] name1 name2 ...\n", stderr);
        exit(1);
    }
    if (!no_tty) {
        signal(SIGQUIT, onquit);
        signal(SIGINT, end_on_sig);
#ifdef SIGWINCH
        signal(SIGWINCH, chgwinsz);
#endif
        if (signal (SIGTSTP, SIG_IGN) == SIG_DFL) {
            signal(SIGTSTP, onsusp);
            catch_susp++;
        }
        tcsetattr (fileno(stderr), TCSANOW, &otty);
    }
    if (no_intty) {
        if (no_tty)
            copy_file (stdin);
        else {
            f = cur_mfile = Fopen_stdin();
            if ((ch = Getc (f)) == '\f')
                doclear();
            else {
                Ungetc (ch, f);
                if (noscroll && (ch != EOF)) {
                    if (clreol)
                        home ();
                    else
                        doclear ();
                }
            }
            if (srchopt)
            {
                search (initbuf, f, 1);
                if (noscroll)
                    left--;
            }
            else if (initopt)
                skiplns (initline, f);
            screen (f, left);
            Fclose (f);
        }
        no_intty = 0;
        prnames++;
        firstf = 0;
    }

    while (fnum < nfiles) {
        if ((f = checkf (fnames[fnum], &clearit)) != NULL) {
            cur_mfile = f;
            context.line = context.chrctr = 0;
            Currline = 0;
            if (firstf)
                setjmp (restore);
            if (firstf) {
                firstf = 0;
                if (srchopt)
                {
                    search (initbuf, f, 1);
                    if (noscroll)
                        left--;
                }
                else if (initopt)
                    skiplns (initline, f);
            }
            else if (fnum < nfiles && !no_tty) {
                setjmp (restore);
                left = command (fnames[fnum], f);
            }
            if (left != 0) {
                if ((noscroll || clearit) && (file_size != LONG_MAX)) {
                    if (clreol) {
                        home ();
                    } else {
                        doclear ();
                    }
                }
                if (prnames) {
                    if (bad_so)
                        erase (0);
                    if (clreol)
                        cleareol ();
                    fputs("::::::::::::::", stdout);
                    if (promptlen > 14)
                        erase (14);
                    fputs("\n", stdout);
                    if(clreol) cleareol();
                    fputs(fnames[fnum], stdout);
                    fputs("\n", stdout);
                    if(clreol) cleareol();
                    fputs("::::::::::::::\n", stdout);
                    if (left > Lpp - 4)
                        left = Lpp - 4;
                }
                if (no_tty)
                    copy_file (f->fp);
                else {
                    within++;
                    screen(f, left);
                    within = 0;
                }
            }
            setjmp (restore);
            fflush(stdout);
            Fclose(f);
            screen_start.line = screen_start.chrctr = 0L;
            context.line = context.chrctr = 0L;
        }
        fnum++;
        firstf = 0;
    }
    reset_tty ();
    return(0);
}

void
argscan(char *s)
{
        int seen_num = 0;

        while (*s != '\0') {
                switch (*s) {
                  case '0': case '1': case '2':
                  case '3': case '4': case '5':
                  case '6': case '7': case '8':
                  case '9':
                        if (!seen_num) {
                                dlines = 0;
                                seen_num = 1;
                        }
                        dlines = dlines*10 + *s - '0';
                        break;
                  case 'd':
                        dum_opt = 1;
                        break;
                  case 'l':
                        stop_opt = 0;
                        break;
                  case 'f':
                        fold_opt = 0;
                        break;
                  case 'p':
                        noscroll++;
                        break;
                  case 'c':
                        clreol++;
                        break;
                  case 's':
                        ssp_opt = 1;
                        break;
                  case 'u':
                        ul_opt = 0;
                        break;
                  case 'v':
                        puts(version_string);
                        exit(0);
                }
                s++;
        }
}


/*
** Check whether the file named by fs is an ASCII file which the user may
** access.  If it is, return the opened file. Otherwise return NULL.
*/

struct mfile *
checkf (register char *fs, int *clearfirst)
{
        struct stat stbuf;
        register struct mfile *f;
        char c;

        if (stat (fs, &stbuf) == -1) {
                fflush(stdout);
                if (clreol)
                        cleareol ();
                perror(fs);
                return(NULL);
        }
        if ((stbuf.st_mode & S_IFMT) == S_IFDIR) {
                printf("\n*** %s: directory ***\n\n", fs);
                return(NULL);
        }
        if ((f = Fopen(fs)) == NULL) {
                fflush(stdout);
                perror(fs);
                return(NULL);
        }
        if (magic(f->fp, fs))
                return(NULL);
        c = Getc(f);
        *clearfirst = c == '\f';
        Ungetc (c, f);
        if ((file_size = stbuf.st_size) == 0)
                file_size = LONG_MAX;
        return(f);
}

/*
** A real function, for the tputs routine in termlib
*/

int
putch (int ch)
{
    return (putchar (ch));
}

int
vwrite (char *cp, int s)
{
    return write (2, cp, s);
}

/*
** Print out the contents of the file f, one screenful at a time.
*/

#define STOP -10

void
screen (register struct mfile *f, register int num_lines)
{
    register int c;
    register int nchars;
    int length;                 /* length of current line */
    static int prev_len = 1;    /* length of previous line */

    while (1) {
        while (num_lines > 0 && !Pause) {
            if ((nchars = mgetline (f, &length)) == EOF)
            {
                if (clreol)
                    clreos();
                return;
            }
            if (ssp_opt && length == 0 && prev_len == 0)
                continue;
            prev_len = length;
            if (bad_so || ((Senter && *Senter == ' ') && promptlen > 0))
                erase (0);
            /* must clear before drawing line since tabs on some terminals
             * do not erase what they tab over.
             */
            if (clreol)
                cleareol ();
            prbuf (Line, length);
            if (nchars < promptlen)
                erase (nchars); /* erase () sets promptlen to 0 */
            else promptlen = 0;
            /* is this needed? */
            /* if (clreol)
                cleareol(); */  /* must clear again in case we wrapped */
            if (nchars < Mcol || !fold_opt)
                prbuf("\n", 1); /* will turn off UL if necessary */
            if (nchars == STOP)
                break;
            num_lines--;
        }
        if (pstate) {
                tputs(ULexit, 1, putch);
                pstate = 0;
        }
        fflush(stdout);
        if ((c = Getc(f)) == EOF)
        {
            if (clreol)
                clreos ();
            return;
        }

        if (Pause && clreol)
            clreos ();
        Ungetc (c, f);
        setjmp (restore);
        Pause = 0; startup = 0;
        if ((num_lines = command (NULL, f)) == 0)
            return;
        if (hard && promptlen > 0)
                erase (0);
        if (noscroll && num_lines >= dlines)
        {
            if (clreol)
                home();
            else
                doclear ();
        }
        screen_start.line = Currline;
        screen_start.chrctr = Ftell (f);
    }
}

/*
** Come here if a quit signal is received
*/

void onquit(int sig)
{
    signal(SIGQUIT, SIG_IGN);
    if (!inwait) {
        putchar ('\n');
        if (!startup) {
            signal(SIGQUIT, onquit);
            longjmp (restore, 1);
        }
        else
            Pause++;
    }
    else if (!dum_opt && notell) {
        vwrite ("[Use q or Q to quit]", 20);
        promptlen += 20;
        notell = 0;
    }
    signal(SIGQUIT, onquit);
}

/*
** Come here if a signal for a window size change is received
*/

#ifdef SIGWINCH
void chgwinsz(int sig)
{
    struct winsize win;

    signal(SIGWINCH, SIG_IGN);
    if (ioctl(fileno(stdout), TIOCGWINSZ, &win) != -1) {
        if (win.ws_row != 0) {
            Lpp = win.ws_row;
            nscroll = Lpp/2 - 1;
            if (nscroll <= 0)
                nscroll = 1;
            dlines = Lpp - (noscroll ? 1 : 2);
        }
        if (win.ws_col != 0)
            Mcol = win.ws_col;
    }
     signal(SIGWINCH, chgwinsz);
}
#endif

/*
** Clean up terminal state and exit.
** Also come here if interrupt signal received.
*/

void end_it (void)
{
    reset_tty ();
    if (clreol) {
        putchar ('\r');
        clreos ();
        fflush (stdout);
    }
    else if (!clreol && (promptlen > 0)) {
        kill_line ();
        fflush (stdout);
    }
    else
        vwrite ("\n", 1);
    _exit(0);
}

void end_on_sig (int sig)
{
    end_it ();
}

void
copy_file(register FILE *f)
{
    register int c;

    while ((c = getc(f)) != EOF)
        putchar(c);
}

/*
** Print an integer as a string of decimal digits,
** returning the length of the print representation.
*/

int
printd (int n)
{
    int a, nchars;

    if ((a = n/10) != 0)
        nchars = 1 + printd(a);
    else
        nchars = 1;
    putchar (n % 10 + '0');
    return (nchars);
}

/* Put the print representation of an integer into a string */

void
scanstr (int n, char *str)
{
    sprintf (str, "%d", n);
}


static char bell = ctrl('G');

/* See whether the last component of the path name "path" is equal to the
** string "string"
*/

int
tailequ (char *path, register char *string)
{
        register char *tail;

        tail = path + strlen(path);
        while (tail >= path)
                if (*(--tail) == '/')
                        break;
        ++tail;
        while (*tail++ == *string++)
                if (*tail == '\0')
                        return(1);
        return(0);
}

void
prompt (char *filename)
{
    if (clreol)
        cleareol ();
    else if (promptlen > 0)
        kill_line ();
    if (!hard) {
        promptlen = 8;
        if (Senter && Sexit) {
            tputs (Senter, 1, putch);
            promptlen += (2 * soglitch);
        }
        if (clreol)
            cleareol ();
        fputs("--More--", stdout);
        if (filename != NULL) {
            promptlen += printf("(Next file: %s)", filename);
        }
        else if (!no_intty) {
            promptlen += printf("(%d%%)", (int)((cur_mfile->file_pos * 100) /
                                                file_size));
        }
        if (dum_opt) {
            promptlen += pr("[Press space to continue, 'q' to quit.]");
        }
        if (Senter && Sexit)
            tputs (Sexit, 1, putch);
        if (clreol)
            clreos ();
        fflush(stdout);
    }
    else
        vwrite (&bell, 1);
    inwait++;
}

/*
** Get a logical line
*/

int
mgetline(register struct mfile *f, int *length)
{
    register int        c;
    register char       *p;
    register int        column;
    static int          colflg;

    p = Line;
    column = 0;
    c = Getc (f);
    if (colflg && c == '\n') {
        Currline++;
        c = Getc (f);
    }
    while (p < &Line[LINSIZ - 1]) {
        if (c == EOF) {
            if (p > Line) {
                *p = '\0';
                *length = p - Line;
                return (column);
            }
            *length = p - Line;
            return (EOF);
        }
        if (c == '\n') {
            Currline++;
            break;
        }
        *p++ = c;
        if (c == '\t')
            if (!hardtabs || (column < promptlen && !hard)) {
                if (hardtabs && eraseln && !dumb) {
                    column = 1 + (column | 7);
                    tputs (eraseln, 1, putch);
                    promptlen = 0;
                }
                else {
                    for (--p; p < &Line[LINSIZ - 1];) {
                        *p++ = ' ';
                        if ((++column & 7) == 0)
                            break;
                    }
                    if (column >= promptlen) promptlen = 0;
                }
            }
            else
                column = 1 + (column | 7);
        else if (c == '\b' && column > 0)
            column--;
        else if (c == '\r')
            column = 0;
        else if (c == '\f' && stop_opt) {
                p[-1] = '^';
                *p++ = 'L';
                column += 2;
                Pause++;
        }
        else if (c == EOF) {
            *length = p - Line;
            return (column);
        }
        else if (c >= ' ' && c != RUBOUT)
            column++;
        if (column >= Mcol && fold_opt) break;
        c = Getc (f);
    }
    if (column >= Mcol && Mcol > 0) {
        if (!Wrap) {
            *p++ = '\n';
        }
    }
    colflg = column == Mcol && fold_opt;
    if (colflg && eatnl && Wrap) {
        *p++ = '\n'; /* simulate normal wrap */
    }
    *length = p - Line;
    *p = 0;
    return (column);
}

/*
** Erase the rest of the prompt, assuming we are starting at column col.
*/

void
erase (register int col)
{
    if (promptlen == 0)
        return;
    if (hard) {
        putchar ('\n');
    }
    else {
        if (col == 0)
            putchar ('\r');
        if (!dumb && eraseln)
            tputs (eraseln, 1, putch);
        else
            for (col = promptlen - col; col > 0; col--)
                putchar (' ');
    }
    promptlen = 0;
}

/*
** Erase the current line entirely
*/

void
kill_line (void)
{
    erase (0);
    if (!eraseln || dumb) putchar ('\r');
}

/*
 * force clear to end of line
 */
void
cleareol(void)
{
    tputs(eraseln, 1, putch);
}

void
clreos(void)
{
    tputs(EodClr, 1, putch);
}

/*
**  Print string and return number of characters
*/

int
pr(char *s1)
{
    register char       *s;

    for (s = s1; *s != 0; s++)
        putchar(*s);
    return (s - s1 - 1);
}


/* Print a buffer of n characters */

#define wouldul(s,n)    ((n) >= 2 && (((s)[0] == '_' && (s)[1] == '\b') || ((s)[1] == '\b' && (s)[2] == '_')))

void
prbuf (register char *s, register int n)
{
    register char c;                    /* next output character */
    register int state;                 /* next output char's UL state */

    while (--n >= 0)
        if (!ul_opt)
            putchar (*s++);
        else {
            if (*s == ' ' && pstate == 0 && ulglitch && wouldul(s+1, n-1)) {
                s++;
                continue;
            }
            if ((state = wouldul(s, n)) != 0) {
                c = (*s == '_')? s[2] : *s ;
                n -= 2;
                s += 3;
            } else {
                c = *s++;
            }
            if (state != pstate) {
                if (c == ' ' && state == 0 && ulglitch && wouldul(s, n-1))
                    state = 1;
                else
                    tputs(state ? ULenter : ULexit, 1, putch);
            }
            if (c != ' ' || pstate == 0 || state != 0 || ulglitch == 0)
                putchar(c);
            if (state && *chUL) {
                pr(chBS);
                tputs(chUL, 1, putch);
            }
            pstate = state;
        }
}

/*
**  Clear the screen
*/

void
doclear(void)
{
    if (Clear && !hard) {
        tputs(Clear, 1, putch);

        /* Put out carriage return so that system doesn't
        ** get confused by escape sequences when expanding tabs
        */
        putchar ('\r');
        promptlen = 0;
    }
}

/*
 * Go to home position
 */
void
home(void)
{
    tputs(Home,1,putch);
}

static int lastcmd, lastarg, lastp;
static int lastcolon;
char shell_line[132];

/*
** Read a command and do it. A command consists of an optional integer
** argument followed by the command character.  Return the number of lines
** to display in the next screenful.  If there is nothing more to display
** in the current file, zero is returned.
*/

int
command (char *filename, register struct mfile *f)
{
    register int nlines;
    register int retval;
    register char c;
    char colonch;
    int done;
    char comchar, cmdbuf[80], *p;
    int initline;

#define ret(val) retval=val;done++;break

    done = 0;
    if (!errors)
        prompt (filename);
    else
        errors = 0;
    while (1) {
        nlines = number (&comchar);
        lastp = colonch = 0;
        if (comchar == '.') {   /* Repeat last command */
                lastp++;
                comchar = lastcmd;
                nlines = lastarg;
                if (lastcmd == ':')
                        colonch = lastcolon;
        }
        lastcmd = comchar;
        lastarg = nlines;
        if (comchar == otty.c_cc[VERASE]) {
            kill_line ();
            prompt (filename);
            continue;
        }
        switch (comchar) {
        case ':':
            retval = colon (filename, colonch, nlines);
            if (retval >= 0)
                done++;
            break;
        case 'b':
        case ctrl('B'):
            if (nlines == 0)
                nlines++;
            putchar ('\r');
            erase (0);
            printf ("\n");
            if (clreol)
                cleareol ();
            printf ("...back %d page", nlines);
            if (nlines > 1)
                putchar('s');
            putchar('\n');

            if (clreol)
                cleareol ();
            putchar('\n');

            initline = Currline - dlines * (nlines + 1);
            if (! noscroll)
                --initline;
            if (initline < 0)
                initline = 0;
            Fseek(f, 0L);
            Currline = 0;       /* skiplns() will make Currline correct */
            skiplns(initline, f);
            if (! noscroll) {
                ret(dlines + 1);
            }
            ret(dlines);
        case ' ':
        case 'z':
            if (nlines == 0) nlines = dlines;
            else if (comchar == 'z') dlines = nlines;
            ret (nlines);
        case 'd':
        case ctrl('D'):
            if (nlines != 0) nscroll = nlines;
            ret (nscroll);
        case 'q':
        case 'Q':
            end_it ();
        case 's':
        case 'f':
            if (nlines == 0) nlines++;
            if (comchar == 'f')
                nlines *= dlines;
            putchar ('\r');
            erase (0);
            putchar('\n');
            if (clreol)
                cleareol ();
            printf ("...skipping %d line", nlines);
            if (nlines > 1)
                putchar('s');
            putchar('\n');

            if (clreol)
                cleareol ();
            putchar('\n');

            while (nlines > 0) {
                while ((c = Getc (f)) != '\n') {
                    if (c == EOF) {
                        retval = 0;
                        done++;
                        goto endsw;
                    }
                }
                Currline++;
                nlines--;
            }
            ret (dlines);
        case '\n':
            if (nlines != 0)
                dlines = nlines;
            else
                nlines = 1;
            ret (nlines);
        case '\f':
            doclear ();
            Fseek (f, screen_start.chrctr);
            Currline = screen_start.line;
            ret (dlines);
        case '\'':
            kill_line ();
            fputs ("\n***Back***\n\n", stdout);
            Fseek (f, context.chrctr);
            Currline = context.line;
            ret (dlines);
        case '=':
            kill_line ();
            promptlen = printd (Currline);
            fflush (stdout);
            break;
        case 'n':
            lastp++;
        case '/':
            if (nlines == 0) nlines++;
            kill_line ();
            putchar ('/');
            promptlen = 1;
            fflush (stdout);
            if (lastp) {
                vwrite ("\r", 1);
                search (NULL, f, nlines);       /* Use previous r.e. */
            }
            else {
                ttyin (cmdbuf, 78, '/');
                vwrite ("\r", 1);
                search (cmdbuf, f, nlines);
            }
            ret (dlines-1);
        case '!':
            do_shell (filename);
            break;
        case '?':
        case 'h':
            if (noscroll) doclear ();
            print_help ();
            prompt (filename);
            break;
        case 'v':       /* This case should go right before default */
            if (!no_intty) {
                kill_line ();
                cmdbuf[0] = '+';
                scanstr (Currline - dlines < 0 ? 0 :
                         Currline - (dlines + 1) / 2, &cmdbuf[1]);
                p = mybasename (_PATH_VI);
                printf ("%s %s %s", p, cmdbuf, fnames[fnum]);
                execute (filename, _PATH_VI, p, cmdbuf, fnames[fnum], NULL);
                break;
            }
        default:
            if (dum_opt) {
                kill_line ();
                if (Senter && Sexit) {
                    tputs (Senter, 1, putch);
                    promptlen = pr ("[Press 'h' for instructions.]") + (2 * soglitch);
                    tputs (Sexit, 1, putch);
                }
                else
                    promptlen = pr ("[Press 'h' for instructions.]");
                fflush (stdout);
            }
            else
                vwrite (&bell, 1);
            break;
        }
        if (done) break;
    }
    putchar ('\r');
endsw:
    inwait = 0;
    notell++;
    return (retval);
}

char ch;

/*
 * Execute a colon-prefixed command.
 * Returns <0 if not a command that should cause
 * more of the file to be printed.
 */

int
colon (char *filename, int cmd, int nlines)
{
    if (cmd == 0)
        ch = readch ();
    else
        ch = cmd;
    lastcolon = ch;
    switch (ch) {
    case 'f':
        kill_line ();
        if (!no_intty)
            promptlen = printf ("\"%s\" line %d", fnames[fnum], Currline);
        else
            promptlen = printf ("[Not a file] line %d", Currline);
        fflush (stdout);
        return (-1);
    case 'n':
        if (nlines == 0) {
            if (fnum >= nfiles - 1)
                end_it ();
            nlines++;
        }
        putchar ('\r');
        erase (0);
        skipf (nlines);
        return (0);
    case 'p':
        if (no_intty) {
            vwrite (&bell, 1);
            return (-1);
        }
        putchar ('\r');
        erase (0);
        if (nlines == 0)
            nlines++;
        skipf (-nlines);
        return (0);
    case '!':
        do_shell (filename);
        return (-1);
    case 'q':
    case 'Q':
        end_it ();
    default:
        vwrite (&bell, 1);
        return (-1);
    }
}

/*
** Read a decimal number from the terminal. Set cmd to the non-digit which
** terminates the number.
*/

int
number(char *cmd)
{
    register int i;

    ch = otty.c_cc[VKILL];
    i = 0;
    while (1) {
        ch = readch ();
        if (ch >= '0' && ch <= '9')
            i = i*10 + ch - '0';
        else if (ch == otty.c_cc[VKILL])
            i = 0;
        else {
            *cmd = ch;
            break;
        }
    }
    return (i);
}

void
do_shell (char *filename)
{
    char cmdbuf[80];

    kill_line ();
    putchar ('!');
    fflush (stdout);
    promptlen = 1;
    if (lastp)
        fputs (shell_line, stdout);
    else {
        ttyin (cmdbuf, 78, '!');
        if (expand (shell_line, cmdbuf)) {
            kill_line ();
            promptlen = printf ("!%s", shell_line);
        }
    }
    fflush (stdout);
    vwrite ("\n", 1);
    promptlen = 0;
    shellp = 1;
    execute (filename, shell, shell, "-c", shell_line, NULL);
}

/*
** Search for nth ocurrence of the given regular expression in the file
*/

void
search (char *expr, struct mfile *file, register int n)
{
    long startline = Ftell (file);
    register long line1 = startline;
    register long line2 = startline;
    register long line3 = startline;
    register int lncount;
    int saveln, rv;
    regex_t reg;
    char errbuf[256];

    if (expr == NULL || *expr == 0)
        expr = last_expr;
    else
        strcpy (last_expr, expr);
    if ((rv = regcomp (&reg, expr, REG_NOSUB)) != 0) {
        regerror (rv, &reg, errbuf, sizeof (errbuf));
        error (errbuf);
        return;
    }
    context.line = saveln = Currline;
    context.chrctr = startline;
    lncount = 0;
    while (!feof (file->fp)) {
        line3 = line2;
        line2 = line1;
        line1 = Ftell (file);
        rdline (file);
        lncount++;
        if ((rv = regexec (&reg, Line, 0, NULL, 0)) == 0) {
            if (--n == 0) {
                if (lncount > 3 || (lncount > 1 && no_intty)) {
                    putchar ('\n');
                    if (clreol)
                        cleareol ();
                    fputs("...skipping\n", stdout);
                }
                Currline -= (lncount >= 3 ? 3 : lncount);
                Fseek (file, line3);
                if (noscroll) {
                    if (clreol) {
                        home ();
                        cleareol ();
                    } else {
                        doclear ();
                    }
                }
                break;
            }
        }
        else if (rv != REG_NOMATCH) {
            regerror (rv, &reg, errbuf, sizeof (errbuf));
            error (errbuf);
        }
    }
    if (feof (file->fp)) {
        Currline = saveln;
        clearerr (file->fp);
        Fseek (file, startline);
        error ("Pattern not found");
    }
    regfree (&reg);
}

void execute (char *filename, char *cmd, ...)
{
    int id;
    int n;
    va_list argp;

    fflush (stdout);
    reset_tty ();
    for (n = 10; (id = fork ()) < 0 && n > 0; n--)
        sleep (5);
    if (id == 0) {
        char *argv[10];
        if (!isatty(0)) {
            close(0);
            open("/dev/tty", 0);
        }
        va_start(argp, cmd);
        for (n = 0; (argv[n] = va_arg (argp, char *)) != NULL; n++);
        va_end(argp);
        execv (cmd, argv);
        vwrite ("exec failed\n", 12);
        exit (1);
    }
    if (id > 0) {
        signal (SIGINT, SIG_IGN);
        signal (SIGQUIT, SIG_IGN);
        if (catch_susp)
            signal(SIGTSTP, SIG_DFL);
        while (wait(NULL) > 0);
        signal (SIGINT, end_on_sig);
        signal (SIGQUIT, onquit);
        if (catch_susp)
            signal(SIGTSTP, onsusp);
    } else {
        vwrite("can't fork\n", 11);
    }
    set_tty ();
    fputs ("------------------------\n", stdout);
    prompt (filename);
}

/*
** Skip n lines in the file f
*/

void
skiplns (register int n, register struct mfile *f)
{
    register char c;

    while (n > 0) {
        while ((c = Getc (f)) != '\n')
            if (c == EOF)
                return;
        n--;
        Currline++;
    }
}

/*
** Skip nskip files in the file list (from the command line). Nskip may be
** negative.
*/

void
skipf (int nskip)
{
    if (nskip == 0)
        return;
    if (nskip > 0) {
        if (fnum + nskip > nfiles - 1)
            nskip = nfiles - fnum - 1;
    }
    else if (within)
        ++fnum;
    fnum += nskip;
    if (fnum < 0)
        fnum = 0;
    fputs ("\n...Skipping \n", stdout);
    if (clreol)
        cleareol ();
    fputs ("...Skipping ", stdout);
    fputs (nskip > 0 ? "to file " : "back to file ", stdout);
    fputs (fnames[fnum], stdout);
    fputs ("\n", stdout);
    if (clreol)
        cleareol ();
    fputs ("\n", stdout);
    fnum--;
}

/*----------------------------- Terminal I/O -------------------------------*/

#if !defined(XTABS) && defined(TAB3)
#define XTABS TAB3
#endif

void
initterm (void)
{
    char        buf[TBUFSIZ];
    static char clearbuf[TBUFSIZ];
    char        *clearptr, *padstr;
    char        *term;
    struct winsize win;

    no_tty = tcgetattr(fileno(stdout), &otty);
    if (!no_tty) {      
        docrterase = (otty.c_cc[VERASE] != 255);
        docrtkill =  (otty.c_cc[VKILL] != 255);
        if ((term = getenv("TERM")) == 0 || tgetent(buf, term) <= 0) {
            dumb++; ul_opt = 0;
        }
        else {
#ifdef TIOCGWINSZ
            if (ioctl(fileno(stdout), TIOCGWINSZ, &win) < 0) {
#endif
                Lpp = tgetnum("li");
                Mcol = tgetnum("co");
#ifdef TIOCGWINSZ
            } else {
                if ((Lpp = win.ws_row) == 0)
                    Lpp = tgetnum("li");
                if ((Mcol = win.ws_col) == 0)
                    Mcol = tgetnum("co");
            }
#endif
            if ((Lpp <= 0) || tgetflag("hc")) {
                hard++; /* Hard copy terminal */
                Lpp = 24;
            }
            if (tgetflag("xn"))
                eatnl++; /* Eat newline at last column + 1; dec, concept */
            if (Mcol <= 0)
                Mcol = 80;

            if (tailequ (fnames[0], "page") || (!hard && tgetflag("ns")))
                noscroll++;
            Wrap = tgetflag("am");
            bad_so = tgetflag ("xs");
            clearptr = clearbuf;
            eraseln = tgetstr("ce",&clearptr);
            Clear = tgetstr("cl", &clearptr);
            Senter = tgetstr("so", &clearptr);
            Sexit = tgetstr("se", &clearptr);
            if ((soglitch = tgetnum("sg")) < 0)
                soglitch = 0;

            /*
             *  Set up for underlining:  some terminals don't need it;
             *  others have start/stop sequences, still others have an
             *  underline char sequence which is assumed to move the
             *  cursor forward one character.  If underline sequence
             *  isn't available, settle for standout sequence.
             */

            if (tgetflag("ul") || tgetflag("os"))
                ul_opt = 0;
            if ((chUL = tgetstr("uc", &clearptr)) == NULL )
                chUL = "";
            if (((ULenter = tgetstr("us", &clearptr)) == NULL ||
                 (ULexit = tgetstr("ue", &clearptr)) == NULL) && !*chUL) {
                if ((ULenter = Senter) == NULL || (ULexit = Sexit) == NULL) {
                        ULenter = "";
                        ULexit = "";
                } else
                        ulglitch = soglitch;
            } else {
                if ((ulglitch = tgetnum("ug")) < 0)
                    ulglitch = 0;
            }

            if ((padstr = tgetstr("pc", &clearptr)) != NULL)
                PC = *padstr;
            Home = tgetstr("ho",&clearptr);
            if (Home == 0 || *Home == '\0')
            {
                if ((cursorm = tgetstr("cm", &clearptr)) != NULL) {
                    strcpy(cursorhome, tgoto(cursorm, 0, 0));
                    Home = cursorhome;
               }
            }
            EodClr = tgetstr("cd", &clearptr);
            if ((chBS = tgetstr("bc", &clearptr)) == NULL)
                chBS = "\b";

        }
        if ((shell = getenv("SHELL")) == NULL)
            shell = "/bin/sh";
    }
    no_intty = tcgetattr(fileno(stdin), &otty);
    tcgetattr(fileno(stderr), &otty);
    Msavetty = otty;
    ospeed = cfgetospeed (&otty);
    slow_tty = ospeed < B1200;
#ifdef OXTABS
    hardtabs = (otty.c_oflag & OXTABS) != OXTABS;
#else
    hardtabs = (otty.c_oflag & TABDLY) != XTABS;
#endif
    if (!no_tty) 
        otty.c_lflag &= ~(ICANON|ECHO);
    otty.c_cc[VTIME] = 0;
    otty.c_cc[VMIN] = 1;
}

int
readch (void)
{
    char ch;

    errno = 0;
    if (read (2, &ch, 1) <= 0) {
        if (errno != EINTR) {
            end_it();
        } else {
            ch = otty.c_cc[VKILL];
        }
    }
    return (ch);
}

static char BS = '\b';
static char *BSB = "\b \b";
static char CARAT = '^';
#define ERASEONECHAR \
    if (docrterase) \
        vwrite (BSB, sizeof(BSB)); \
    else \
        vwrite (&BS, sizeof(BS));

void
ttyin (char *buf, int nmax, int pchar)
{
    register char *sptr;
    register char ch;
    register int slash = 0;
    int maxlen;
    char cbuf;

    sptr = buf;
    maxlen = 0;
    while (sptr - buf < nmax) {
        if (promptlen > maxlen)
            maxlen = promptlen;
        ch = readch ();
        if (ch == '\\') {
            slash++;
        }
        else if ((ch == otty.c_cc[VERASE]) && !slash) {
            if (sptr > buf) {
                --promptlen;
                ERASEONECHAR;
                --sptr;
                if ((*sptr < ' ' && *sptr != '\n') || *sptr == RUBOUT) {
                    --promptlen;
                    ERASEONECHAR;
                }
                continue;
            }
            else {
                if (!eraseln) promptlen = maxlen;
                longjmp (restore, 1);
            }
        }
        else if ((ch == otty.c_cc[VKILL]) && !slash) {
            if (hard) {
                show (ch);
                putchar ('\n');
                putchar (pchar);
            }
            else {
                putchar ('\r');
                putchar (pchar);
                if (eraseln)
                    erase (1);
                else if (docrtkill)
                    while (promptlen-- > 1)
                        vwrite (BSB, sizeof(BSB));
                promptlen = 1;
            }
            sptr = buf;
            fflush (stdout);
            continue;
        }
        if (slash && (ch == otty.c_cc[VKILL] || ch == otty.c_cc[VERASE])) {
            ERASEONECHAR;
            --sptr;
        }
        if (ch != '\\')
            slash = 0;
        *sptr++ = ch;
        if ((ch < ' ' && ch != '\n' && ch != ESC) || ch == RUBOUT) {
            ch += ch == RUBOUT ? -0100 : 0100;
            vwrite (&CARAT, 1);
            promptlen++;
        }
        cbuf = ch;
        if (ch != '\n' && ch != ESC) {
            vwrite (&cbuf, 1);
            promptlen++;
        }
        else
            break;
    }
    *--sptr = '\0';
    if (!eraseln)
        promptlen = maxlen;
    if (sptr - buf >= nmax - 1)
        error ("Line too long");
}

int
expand (char *outbuf, char *inbuf)
{
    register char *instr;
    register char *outstr;
    register char ch;
    char temp[200];
    int changed = 0;

    instr = inbuf;
    outstr = temp;
    while ((ch = *instr++) != '\0')
        switch (ch) {
        case '%':
            if (!no_intty) {
                strcpy (outstr, fnames[fnum]);
                outstr += strlen (fnames[fnum]);
                changed++;
            }
            else
                *outstr++ = ch;
            break;
        case '!':
            if (!shellp)
                error ("No previous command to substitute for");
            strcpy (outstr, shell_line);
            outstr += strlen (shell_line);
            changed++;
            break;
        case '\\':
            if (*instr == '%' || *instr == '!') {
                *outstr++ = *instr++;
                break;
            }
        default:
            *outstr++ = ch;
        }
    *outstr++ = '\0';
    strcpy (outbuf, temp);
    return (changed);
}

void
show (int ch)
{
    char cbuf;

    if ((ch < ' ' && ch != '\n' && ch != ESC) || ch == RUBOUT) {
        ch += ch == RUBOUT ? -0100 : 0100;
        vwrite (&CARAT, 1);
        promptlen++;
    }
    cbuf = ch;
    vwrite (&cbuf, 1);
    promptlen++;
}

void
error (char *mess)
{
    if (clreol)
        cleareol ();
    else
        kill_line ();
    promptlen += strlen (mess);
    if (Senter && Sexit) {
        tputs (Senter, 1, putch);
        fputs(mess, stdout);
        tputs (Sexit, 1, putch);
    }
    else {
        fputs(mess, stdout);
    }
    fflush(stdout);
    errors++;
    longjmp (restore, 1);
}


void
set_tty (void)
{
    otty.c_lflag &= ~(ICANON|ECHO);
    tcsetattr(fileno(stderr), TCSANOW, &otty);
}

void
reset_tty (void)
{
    if (no_tty)
        return;
    if (pstate) {
        tputs(ULexit, 1, putch);
        fflush(stdout);
        pstate = 0;
    }
    otty.c_lflag |= ICANON|ECHO;
    tcsetattr(fileno(stderr), TCSANOW, &Msavetty);
}

void
rdline (register struct mfile *f)
{
    register char c;
    register char *p;

    p = Line;
    while ((c = Getc (f)) != '\n' && c != EOF && p - Line < LINSIZ - 1)
        *p++ = c;
    if (c == '\n')
        Currline++;
    *p = '\0';
}

/* Come here when we get a suspend signal from the terminal */

void
onsusp (int sig)
{
    /* ignore SIGTTOU so we don't get stopped if csh grabs the tty */
    signal(SIGTTOU, SIG_IGN);
    reset_tty ();
    fflush (stdout);
    signal(SIGTTOU, SIG_DFL);
    /* Send the TSTP signal to suspend our process group */
    signal(SIGTSTP, SIG_DFL);
    /* sigsetmask(0);*/
    kill (0, SIGTSTP);
    /* Pause for station break */

    /* We're back */
    signal (SIGTSTP, onsusp);
    set_tty ();
    if (inwait) longjmp (restore, 1);
}

char *
mybasename (char *pathname)
{
    char *p;

    p = strrchr (pathname, '/');
    return (p ? p + 1 : pathname);
}
