/* vi: set sw=4 ts=4:
 *
 * ash.h - ash header file.
 *
 * Copyright 2012 Sandeep Sharma <sandeep.jack2756@gmail.com>
 * Copyright 2012 Ranjan Kumar <ranjankumar.bth@gmail.com>
 *
 * Not in SUSv4.
 *
*/

/*-
 * Copyright (c) 1991 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Kenneth Almquist.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *  notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *  notice, this list of conditions and the following disclaimer in the
 *  documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *  must display the following acknowledgement:
 *    This product includes software developed by the University of
 *    California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *  may be used to endorse or promote products derived from this software
 *  without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
//==================================================================================
//Header files.
#include <stdio.h>  /* defines BUFSIZ */
#include <string.h>
#include "ash_syntax.h"

#ifdef __STDC__
# include "stdarg.h"
#else
# include <varargs.h>
#endif

#include <stdlib.h>

#include "ash_shell.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include "ash_eval.h"
#include "ash_jobs.h"

#ifdef  BSD
#undef BSD    /* temporary, already defined in <sys/param.h> */
#include <sys/param.h>
#include <unistd.h>
#endif

#ifndef BSD
#define BSD
#endif

#if JOBS
# include "sgtty.h"
# undef CEOF      /* syntax.h redefines this */
#endif

#ifdef BSD
# include <sys/types.h>
# include <sys/wait.h>
# include <sys/time.h>
# include <sys/resource.h>
#endif

#include "ash_syntax.h"

#define DEFINE_OPTIONS
#include "ash_options.h"
#undef DEFINE_OPTIONS


#if ! DIRENT
# include <dirent.h>
#endif

/* values returned by readtoken */
#include "ash_token.def"
#include <setjmp.h>
#include <termios.h>

//==================================================================================
//defines.
#define PROFILE 0

#ifndef S_ISDIR        /* macro to test for directory file */
#define  S_ISDIR(mode)    (((mode) & S_IFMT) == S_IFDIR)
#endif

/* flags in argument to evaltree */
#define EV_EXIT 01    /* exit after evaluating tree */
#define EV_TESTED 02    /* exit status is checked; ignore -e flag */
#define EV_BACKCMD 04    /* command executing within back quotes */

/* reasons for skipping commands (see comment on breakcmd routine) */
#define SKIPBREAK 1
#define SKIPCONT 2
#define SKIPFUNC 3

#define CMDTABLESIZE 31    /* should be prime */
#define ARB 1      /* actual size determined at run time */

#define EOF_NLEFT -99    /* value of parsenleft when EOF pushed back */
#define MAXMBOXES 10
#define MINSIZE 504    /* minimum size of a block */
#define EOFMARKLEN 79
#ifdef EMPTY
  #undef EMPTY
  #define EMPTY -2    /* marks an unused slot in redirtab */
#endif
#define PIPESIZE 4096    /* amount of buffering in a pipe */
#define S_DFL 1      /* default signal handling (SIG_DFL) */
#define S_CATCH 2    /* signal is caught */
#define S_IGN 3      /* signal is ignored (SIG_IGN) */
#define S_HARD_IGN 4    /* signal is ignored permenantly */
#define OUTBUFSIZ BUFSIZ
#define BLOCK_OUT -2    /* output to a fixed block of memory */
#define MEM_OUT -3    /* output to dynamically allocated memory */
#define OUTPUT_ERR 01    /* error occurred on output */
#define VTABSIZE 39

//builtin commands.
#define BLTINCMD 0
#define BGCMD 1
#define BREAKCMD 2
#define CDCMD 3
#define DOTCMD 4
#define ECHOCMD 5
#define EVALCMD 6
#define EXECCMD 7
#define EXITCMD 8
#define EXPORTCMD 9
#define FALSECMD 10
#define FGCMD 11
#define GETOPTSCMD 12
#define HASHCMD 13
#define JOBIDCMD 14
#define JOBSCMD 15
#define LOCALCMD 16
#define PWDCMD 17
#define READCMD 18
#define RETURNCMD 19
#define SETCMD 20
#define SETVARCMD 21
#define SHIFTCMD 22
#define TRAPCMD 23
#define TRUECMD 24
#define UMASKCMD 25
#define UNSETCMD 26
#define WAITCMD 27
#define TIMESCMD 28
#define HELPCMD 29
#define KILLCMD 30
#define TESTCMD 31
#define PRINTFCMD 32
#define ALIASCMD 33
#define UNALIASCMD 34
#define TYPECMD 35
#define ULIMITCMD 36
#define HISTORYCMD 37
#define EXPCMD 38

/* values of cmdtype */
#define CMDUNKNOWN -1    /* no entry in table for command */
#define CMDNORMAL 0      /* command is an executable program */
#define CMDBUILTIN 1    /* command is a shell builtin */
#define CMDFUNCTION 2    /* command is a shell function */
#define CMDSPLBLTIN 3     /* command is a special shell builtin */

/* flags passed to redirect */
#define REDIR_PUSH 01    /* save previous values of file descriptors */
#define REDIR_BACKQ 02    /* save the command output in memory */

/* flags */
#define VEXPORT    01  /* variable is exported */
#define VREADONLY  02  /* variable cannot be modified */
#define VSTRFIXED  04  /* variable struct is staticly allocated */
#define VTEXTFIXED  010  /* text is staticly allocated */
#define VSTACK    020  /* text is allocated on the stack */
#define VUNSET    040  /* the variable is not set */

/*
 * The following macros access the values of the above variables.
 * They have to skip over the name.  They return the null string
 * for unset variables.
 */
#define ifsval()  (vifs.text + 4)
#define mailval()  (vmail.text + 5)
#define mpathval()  (vmpath.text + 9)
#define pathval()  (vpath.text + 5)
#define ps1val()  (vps1.text + 4)
#define ps2val()  (vps2.text + 4)
#if ATTY
#define termval()  (vterm.text + 5)
#endif

#if ATTY
#define attyset()  ((vatty.flags & VUNSET) == 0)
#endif
#define mpathset()  ((vmpath.flags & VUNSET) == 0)

/* control characters in argument strings */
#define CTLESC '\201'
#define CTLVAR '\202'
#define CTLENDVAR '\203'
#define CTLBACKQ '\204'
#define CTLQUOTE 01    /* ored with CTLBACKQ code if in quotes */

/* variable substitution byte (follows CTLVAR) */
#define VSTYPE 0x0f     /* type of variable substitution */
#define VSNUL 0x10     /* colon--treat the empty string as unset */
#define VSQUOTE 0x80     /* inside double quotes--suppress splitting */

/* values of VSTYPE field */
#define VSNORMAL 1    /* normal variable:  $var or ${var} */
#define VSMINUS 2    /* ${var-text} */
#define VSPLUS 3    /* ${var+text} */
#define VSQUESTION 4    /* ${var?message} */
#define VSASSIGN 5    /* ${var=text} */
#define VSTRIMRIGHT     0x6     /* ${var%pattern} */
#define VSTRIMRIGHTMAX  0x7     /* ${var%%pattern} */
#define VSTRIMLEFT      0x8     /* ${var#pattern} */
#define VSTRIMLEFTMAX   0x9     /* ${var##pattern} */
#define VSLENGTH        0xa     /* ${#var} */

/*
 * NEOF is returned by parsecmd when it encounters an end of file.  It
 * must be distinct from NULL, so we use the address of a variable that
 * happens to be handy.
 */
#define NEOF ((union node *)&tokpushback)

#define NSEMI 0
#define NCMD 1
#define NPIPE 2
#define NREDIR 3
#define NBACKGND 4
#define NSUBSHELL 5
#define NAND 6
#define NOR 7
#define NIF 8
#define NWHILE 9
#define NUNTIL 10
#define NFOR 11
#define NCASE 12
#define NCLIST 13
#define NDEFUN 14
#define NARG 15
#define NTO 16
#define NFROM 17
#define NAPPEND 18
#define NTOFD 19
#define NFROMFD 20
#define NHERE 21
#define NXHERE 22
#define NNOT 23

#define pgetc_macro()  (--parsenleft >= 0? *parsenextc++ : preadbuffer())

#define stackblock() stacknxt
#define stackblocksize() stacknleft
#define STARTSTACKSTR(p)  p = stackblock(), sstrnleft = stackblocksize()
#define STPUTC(c, p)  (--sstrnleft >= 0? (*p++ = (c)) : (p = growstackstr(), *p++ = (c)))
#define CHECKSTRSPACE(n, p)  if (sstrnleft < n) p = makestrspace(); else
#define USTPUTC(c, p)  (--sstrnleft, *p++ = (c))
#define STACKSTRNUL(p)  (sstrnleft == 0? (p = growstackstr(), *p = '\0') : (*p = '\0'))
#define STUNPUTC(p)  (++sstrnleft, --p)
#define STTOPC(p)  p[-1]
#define STADJUST(amount, p)  (p += (amount), sstrnleft -= (amount))
#define grabstackstr(p)  stalloc(stackblocksize() - sstrnleft)

#define ckfree(p)  free((pointer)(p))

/*
 * Types of operations (passed to the errmsg routine).
 */
#define E_OPEN 01  /* opening a file */
#define E_CREAT 02  /* creating a file */
#define E_EXEC 04  /* executing a program */

/* exceptions */
#define EXINT 0    /* SIGINT received */
#define EXERROR 1  /* a generic error */
#define EXSHELLPROC 2  /* execute a shell procedure */

#define INTOFF suppressint++
#define INTON if (--suppressint == 0 && intpending) onint(); else
#define FORCEINTON {suppressint = 0; if (intpending) onint();}
#define CLEAR_PENDING_INT intpending = 0
#define int_pending() intpending

#ifndef SYSV
#define strchr mystrchr
#endif

#define equal(s1, s2)  (strcmp(s1, s2) == 0)
#define scopy(s1, s2)  ((void)strcpy(s2, s1))
#define bcopy(src, dst, n)  mybcopy((pointer)(src), (pointer)(dst), n)

#ifndef ALIGN
union align {
  int i;
  char *cp;
};

#define ALIGN(nbytes) (((nbytes) + sizeof(union align) - 1) & (~(sizeof(union align) - 1)))
#endif

#define MAXSIG 31

/*
 * This file is included by programs which are optionally built into the
 * shell.  If SHELL is defined, we try to map the standard UNIX library
 * routines to ash routines using defines.
 */

#ifdef SHELL
#define stdout out1
#define stderr out2
#define printf out1fmt
#define putc(c, file)  outc(c, file)
#define putchar(c)  out1c(c)
#define fprintf outfmt
#define fputs outstr
#define fflush flushout
#define INITARGS(argv)
#else
#define INITARGS(argv)  if ((commandname = argv[0]) == NULL) {fputs("Argc is zero\n", stderr); exit(2);} else
#endif
#define ISDOTORDOTDOT(s) ((s)[0] == '.' && (!(s)[1] || ((s)[1] == '.' && !(s)[2])))

#define EXP_FULL    0x1   /* perform word splitting & file globbing */
#define EXP_TILDE     0x2   /* do normal tilde expansion */
#define EXP_VARTILDE  0x4   /* expand tildes in an assignment */
#define EXP_REDIR     0x8   /* file glob for a redirection (1 match only) */
#define EXP_CASE    0x10  /* keeps quotes around for CASE pattern */
#define EXP_IFS_SPLIT   0x20  /* need to record arguments for ifs breakup */

//==================================================================================
//struct.
struct cmdentry {
  int cmdtype;
  union param {
    int index;
    union node *func;
  } u;
};

struct tblentry {
  struct tblentry *next;  /* next entry in hash chain */
  union param param;  /* definition of builtin function */
  short cmdtype;    /* index identifying command */
  char rehash;    /* if set, cd done since entry created */
  char cmdname[ARB];  /* name of command */
};

/*
 * Structure specifying which parts of the string should be searched
 * for IFS characters.
 */
struct ifsregion {
  struct ifsregion *next;  /* next region in list */
  int begoff;    /* offset of start of region */
  int endoff;    /* offset of end of region */
  int nulonly;    /* search for nul bytes only */
};

struct strpush {
    struct strpush *prev;   /* preceding string on stack */
    char *prevstring;
    int prevnleft;
    int prevlleft;
    struct alias *ap;     /* if push was associated with an alias */
};

/*
 * The parsefile structure pointed to by the global variable parsefile
 * contains information about the current file being read.
 */
struct parsefile {
    int linno;        /* current line */
    int fd;         /* file descriptor (or -1 if string) */
    int nleft;        /* number of chars left in buffer */
    char *nextc;      /* next char in buffer */
    int lleft;        /* number of chars left in this line */
    struct parsefile *prev; /* preceding file on stack */
    char *buf;        /* input buffer */
    struct strpush *strpush; /* for pushing strings at this level */
    struct strpush basestrpush; /* so pushing one is fast */

};

/*
 * Parse trees for commands are allocated in lifo order, so we use a stack
 * to make this more efficient, and also to avoid all sorts of exception
 * handling code to handle interrupts in the middle of a parse.
 *
 * The size 504 was chosen because the Ultrix malloc handles that size
 * well.
 */
struct stack_block {
  struct stack_block *prev;
  char space[MINSIZE];
};

struct heredoc {
  struct heredoc *next;  /* next here document in list */
  union node *here;    /* redirection node */
  char *eofmark;    /* string indicating end of input */
  int striptabs;    /* if set, strip leading tabs */
};

struct redirtab {
  struct redirtab *next;
  short renamed[10];
};

struct varinit {
  struct var *var;
  int flags;
  char *text;
};

struct strlist {
  struct strlist *next;
  char *text;
};


struct arglist {
  struct strlist *list;
  struct strlist **lastp;
};

struct builtincmd {
    char *name;
    int code;
};

struct var {
  struct var *next;    /* next entry in hash list */
  int flags;    /* flags are defined above */
  char *text;    /* name=value */
};


struct localvar {
  struct localvar *next;  /* next local variable in list */
  struct var *vp;    /* the variable that was made local */
  int flags;    /* saved flags */
  char *text;    /* saved text */
};


struct nbinary {
    int type;
    union node *ch1;
    union node *ch2;
};


struct ncmd {
    int type;
    int backgnd;
    union node *args;
    union node *redirect;
};


struct npipe {
    int type;
    int backgnd;
    struct nodelist *cmdlist;
};


struct nredir {
    int type;
    union node *n;
    union node *redirect;
};


struct nif {
    int type;
    union node *test;
    union node *ifpart;
    union node *elsepart;
};

struct nfor {
    int type;
    union node *args;
    union node *body;
    char *var;
};

struct ncase {
    int type;
    union node *expr;
    union node *cases;
};

struct nclist {
    int type;
    union node *next;
    union node *pattern;
    union node *body;
};

struct narg {
    int type;
    union node *next;
    char *text;
    struct nodelist *backquote;
};

struct nfile {
    int type;
    union node *next;
    int fd;
    union node *fname;
    char *expfname;
};

struct ndup {
    int type;
    union node *next;
    int fd;
    int dupfd;
};

struct nhere {
    int type;
    union node *next;
    int fd;
    union node *doc;
};

struct nnot {
    int type;
    union node *com;
};

union node {
    int type;
    struct nbinary nbinary;
    struct ncmd ncmd;
    struct npipe npipe;
    struct nredir nredir;
    struct nif nif;
    struct nfor nfor;
    struct ncase ncase;
    struct nclist nclist;
    struct narg narg;
    struct nfile nfile;
    struct ndup ndup;
    struct nhere nhere;
    struct nnot nnot;
};

struct nodelist {
  struct nodelist *next;
  union node *n;
};

struct stackmark {
  struct stack_block *stackp;
  char *stacknxt;
  int stacknleft;
};

/*
 * We enclose jmp_buf in a structure so that we can declare pointers to
 * jump locations.  The global variable handler contains the location to
 * jump to when an exception occurs, and the global variable exception
 * contains a code identifying the exeception.  To implement nested
 * exception handlers, the user should save the value of handler on entry
 * to an inner scope, set handler to point to a jmploc structure for the
 * inner scope, and restore handler on exit from the scope.
 */
struct jmploc {
  jmp_buf loc;
};
//==================================================================================
//Function declarations.
#ifdef __STDC__
void readcmdfile(char *);
void cmdloop(int);
STATIC void read_profile(char *);
STATIC int docd(char *, int);
STATIC void updatepwd(char *);
STATIC void getpwd(void);
STATIC char *getcomponent(void);
STATIC void evalloop(union node *);
STATIC void evalfor(union node *);
STATIC void evalcase(union node *, int);
STATIC void evalsubshell(union node *, int);
STATIC void expredir(union node *);
STATIC void evalpipe(union node *);
STATIC void evalcommand(union node *, int, struct backcmd *);
STATIC void prehash(union node *);
STATIC void tryexec(char *, char **, char **);
STATIC void execinterp(char **, char **);
STATIC void printentry(struct tblentry *);
STATIC void clearcmdentry(int);
STATIC struct tblentry *cmdlookup(char *, int);
STATIC void delete_cmd_entry(void);
STATIC void argstr(char *, int);
STATIC void expbackq(union node *, int, int);
STATIC char *evalvar(char *, int);
STATIC int varisset(int);
STATIC void varvalue(int, int, int);
STATIC void recordregion(int, int, int);
STATIC void ifsbreakup(char *, struct arglist *);
STATIC void expandmeta(struct strlist *);
STATIC void expmeta(char *, char *);
STATIC void addfname(char *);
STATIC struct strlist *expsort(struct strlist *);
STATIC struct strlist *msort(struct strlist *, int);
STATIC int pmatch(char *, char *);
STATIC void pushfile(void);
STATIC void restartjob(struct job *);
STATIC struct job *getjob(char *);
STATIC void freejob(struct job *);
STATIC int dowait(int, struct job *);
STATIC int waitproc(int, int *);
STATIC char *commandtext(union node *);
STATIC void sh_options(int);
STATIC void setoption(char, int);
STATIC void openredirect(union node *, char *);
STATIC int openhere(union node *);
STATIC void calcsize(union node *);
STATIC void sizenodelist(struct nodelist *);
STATIC union node *copynode(union node *);
STATIC struct nodelist *copynodelist(struct nodelist *);
STATIC char *nodesavestr(char *);
union node;
void expandarg(union node *, struct arglist *, int);
void expandhere(union node *, int);
int patmatch(char *, char *);
void rmescapes(char *);
int casematch(union node *, char *);
void shellexec(char **, char **, char *, int);
char *padvance(char **, char *);
void find_command(char *, struct cmdentry *, int);
int find_builtin(char *);
void hashcd(void);
void changepath(char *);
void defun(char *, union node *);
void unsetfunc(char *);
union node;
STATIC void redirect(union node *, int);
void popredir(void);
void clearredir(void);
int copyfd(int, int);
int fd0_redirected_p(void);
void initvar();
void setvar(char *, char *, int);
void setvareq(char *, int);
struct strlist;
void listsetvar(struct strlist *);
char *lookupvar(char *);
char *bltinlookup(char *, int);
char **environment();
int showvarscmd(int, char **);
void mklocal(char *);
void poplocalvars(void);
void chkmail(int);
union node *parsecmd(int);
int goodname(char *);
union node *copyfunc(union node *);
void freefunc(union node *);
char *pfgets(char *, int);
static int pgetc(void);
int preadbuffer(void);
void pungetc(void);
void ppushback(char *, int);
void pushstring(char *, int , void *);
void popstring(void);
void setinputfile(char *, int);
void setinputfd(int, int);
void setinputstring(char *, int);
void popfile(void);
void popallfiles(void);
void closescript(void);
void clear_traps(void);
int setsignal(int);
void ignoresig(int);
void dotrap(void);
void setinteractive(int);
void exitshell(int);
pointer ckmalloc(int);
pointer ckrealloc(pointer, int);
void free(pointer);    /* defined in C library */
char *savestr(char *);
pointer stalloc(int);
void stunalloc(pointer);
void setstackmark(struct stackmark *);
void popstackmark(struct stackmark *);
void growstackblock(void);
void grabstackblock(int);
char *growstackstr(void);
char *makestrspace(void);
void ungrabstackstr(char *, char *);
void exraise(int);
static void onint(void);
void error2(char *, char *);
void sh_error(char *, ...);
char *errmsg(int, int);
void init(void);
void reset(void);
void initshellproc(void);
void scopyn(const char *, char *, int);
char *strchr(const char *, char);
void mybcopy(const pointer, pointer, int);
int prefix(const char *, const char *);
int number(const char *);
static int is_number(const char *);
int strcmp(const char *, const char *);  /* from C library */
char *strcpy(char *, const char *);  /* from C library */
char *strcat(char *, const char *);  /* from C library */
pointer stalloc(int);
void error(char *, ...);
int nextopt(char *optstring);
int trputs(char *s);
STATIC int peektoken();

#else
void readcmdfile();
void cmdloop();
STATIC void read_profile();
char *getenv();
STATIC int docd();
STATIC void updatepwd();
STATIC void getpwd();
STATIC char *getcomponent();
STATIC void evalloop();
STATIC void evalfor();
STATIC void evalcase();
STATIC void evalsubshell();
STATIC void expredir();
STATIC void evalpipe();
STATIC void evalcommand();
STATIC void prehash();
STATIC void tryexec();
STATIC void execinterp();
STATIC void printentry();
STATIC void clearcmdentry();
STATIC struct tblentry *cmdlookup();
STATIC void delete_cmd_entry();
STATIC void argstr();
STATIC void expbackq();
STATIC char *evalvar();
STATIC int varisset();
STATIC void varvalue();
STATIC void recordregion();
STATIC void ifsbreakup();
STATIC void expandmeta();
STATIC void expmeta();
STATIC void addfname();
STATIC struct strlist *expsort();
STATIC struct strlist *msort();
STATIC int pmatch();
STATIC void pushfile();
STATIC void restartjob();
STATIC struct job *getjob();
STATIC void freejob();
STATIC int dowait();
STATIC int waitproc();
STATIC char *commandtext();
STATIC void sh_options();
STATIC void setoption();
STATIC void openredirect();
STATIC int openhere();
STATIC void calcsize();
STATIC void sizenodelist();
STATIC union node *copynode();
STATIC struct nodelist *copynodelist();
STATIC char *nodesavestr();
void expandarg();
void expandhere();
int patmatch();
void rmescapes();
int casematch();
void shellexec();
char *padvance();
void find_command();
int find_builtin();
void hashcd();
void changepath();
void defun();
void unsetfunc();
STATIC void redirect();
void popredir();
void clearredir();
int copyfd();
int fd0_redirected_p();
void initvar();
void setvar();
void setvareq();
void listsetvar();
char *lookupvar();
char *bltinlookup();
char **environment();
int showvarscmd();
void mklocal();
void poplocalvars();
void chkmail();
union node *parsecmd();
int goodname();
union node *copyfunc();
void freefunc();
char *pfgets();
static int pgetc();
int preadbuffer();
void pungetc();
void ppushback();
void pushstring();
void popstring();
void setinputfile();
void setinputfd();
void setinputstring();
void popfile();
void popallfiles();
void closescript();
void clear_traps();
int setsignal();
void ignoresig();
void dotrap();
void setinteractive();
void exitshell();
pointer ckmalloc();
pointer ckrealloc();
void free();    /* defined in C library */
char *savestr();
pointer stalloc();
void stunalloc();
void setstackmark();
void popstackmark();
void growstackblock();
void grabstackblock();
char *growstackstr();
char *makestrspace();
void ungrabstackstr();
void exraise();
static void onint();
void error2();
void sh_error();
char *errmsg();
void init();
void reset();
void initshellproc();
void scopyn();
char *strchr();
void mybcopy();
int prefix();
int number();
static int is_number();
int strcmp();
char *strcpy();
int strlen();
char *strcat();
pointer stalloc();
void error();
int nextopt();
int trputs();
int peektoken();
#endif

#if UDIR
#ifdef __STDC__
STATIC char *expudir(char *);
#else
STATIC char *expudir();
#endif
#endif /* UDIR */

STATIC union node *list __P((int));
STATIC union node *andor __P((void));
STATIC union node *pipeline __P((void));
STATIC union node *command __P((void));
STATIC union node *simplecmd __P((union node **, union node *));
STATIC void parsefname __P((void));
STATIC void parseheredoc __P((void));
STATIC int readtoken __P((void));
STATIC int readtoken1 __P((int, char const *, char *, int));
STATIC int noexpand __P((char *));
STATIC void synexpect __P((int));
STATIC void synerror __P((char *));

#if ATTY
STATIC void putprompt __P((char *));
#else /* not ATTY */
#define putprompt(s)  out2str(s)
#endif

void shprocvar();
int jobctl;
void deletefuncs();

//==================================================================================
//output
#ifndef OUTPUT_INCL
struct output {
  char *nextc;
  int nleft;
  char *buf;
  int bufsize;
  short fd;
  short flags;
};

extern struct output output;
extern struct output errout;
extern struct output memout;
extern struct output *out1;
extern struct output *out2;

#ifdef __STDC__
void outstr(char *, struct output *);
void out1str(char *);
void out2str(char *);
void outfmt(struct output *, char *, ...);
void out1fmt(char *, ...);
void fmtstr(char *, int, char *, ...);
void doformat();
void emptyoutbuf(struct output *);
void flushall(void);
void flushout(struct output *);
void freestdout(void);
int xxwrite(int, char *, int);
#else
void outstr();
void out1str();
void out2str();
void outfmt();
void out1fmt();
void fmtstr();
void doformat();
void emptyoutbuf();
void flushall();
void flushout();
void freestdout();
int xxwrite();
#endif

#define outc(c, file)  (--(file)->nleft < 0? (emptyoutbuf(file), *(file)->nextc++ = (c)) : (*(file)->nextc++ = (c)))
#define out1c(c)  outc(c, out1);
#define out2c(c)  outc(c, out2);

#define OUTPUT_INCL
#endif
//==================================================================================
//for alias
#define ALIASINUSE 1
struct alias {
  struct alias *next;
  char *name;
  char *val;
  int flag;
};

#define ATABSIZE 39
struct alias *atab[ATABSIZE];

struct alias *lookupalias(char *, int);
char *get_alias_text(char *);
void rmaliases(void);
static void setalias(char *, char *);
static int unalias(char *);
static struct alias **hashalias(char *);
//==================================================================================
//ulimit command
struct limits {
  const char *name;
  int  cmd;
  int factor; /* multiply by to get rlim_{cur,max} values */
  char option;
};

static const struct limits limits[] = {
#ifdef RLIMIT_CPU
    { "time(seconds)",      RLIMIT_CPU,    1, 't' },
#endif
#ifdef RLIMIT_FSIZE
    { "file(blocks)",      RLIMIT_FSIZE,  512, 'f' },
#endif
#ifdef RLIMIT_DATA
    { "data(kbytes)",       RLIMIT_DATA,  1024, 'd' },
#endif
#ifdef RLIMIT_STACK
    { "stack(kbytes)",      RLIMIT_STACK,   1024, 's' },
#endif
#ifdef  RLIMIT_CORE
    { "coredump(blocks)",     RLIMIT_CORE,   512, 'c' },
#endif
#ifdef RLIMIT_RSS
    { "memory(kbytes)",     RLIMIT_RSS,   1024, 'm' },
#endif
#ifdef RLIMIT_MEMLOCK
    { "locked memory(kbytes)",  RLIMIT_MEMLOCK, 1024, 'l' },
#endif
#ifdef RLIMIT_NPROC
    { "process(processes)",   RLIMIT_NPROC,    1, 'p' },
#endif
#ifdef RLIMIT_NOFILE
    { "nofiles(descriptors)",   RLIMIT_NOFILE,   1, 'n' },
#endif
#ifdef RLIMIT_VMEM
    { "vmemory(kbytes)",    RLIMIT_VMEM,  1024, 'v' },
#endif
#ifdef RLIMIT_SWAP
    { "swap(kbytes)",       RLIMIT_SWAP,  1024, 'w' },
#endif
    { (char *) 0,         0,         0,  '\0' }
};
//==================================================================================
//For History
#define NUM_OF_HISTORY_ITEMS 256
void get_history(); //get cmds from file and fill in the history buff.
void save_history(); //write history buff to file.
void add_to_history(const char *p);
char *search_in_history(int key);

struct double_list *hist_list = NULL;
struct double_list *key_list_ptr = NULL;
struct double_list *p_list = NULL;
struct double_list *path_list = NULL;
int up_key_pressed = 0;
int enter_flag = 1;
//==================================================================================
