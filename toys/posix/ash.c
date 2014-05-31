/* ash.c - ash shell port.
 *
 * Copyright 2012 Sandeep Sharma <sandeep.jack2756@gmail.com>
 * Copyright 2012 Ranjan Kumar <ranjankumar.bth@gmail.com>
 *
 * Not in SUSv4.
 *
USE_ASH(NEWTOY(ash, "?l", TOYFLAG_BIN))
USE_ASH(OLDTOY(sh,ash, "?l", TOYFLAG_BIN))
config ASH
  bool "ash"
  default y 
  help
    usage: ash [-/+OPTIONS] [-/+o OPT]... [-c 'SCRIPT' [ARG0 [ARGS]] / FILE [ARGS]]

    ASH shell Interpreter.

config ASH_TEST
  bool "ash built-in test"
  default y
  depends on ASH
  select TEST

config ASH_PRINTF
  bool "ash built-in printf"
  default y
  depends on ASH
  select PRINTF
config ASH_KILL
  bool "ash built-in kill"
  default y
  depends on ASH
  select KILL

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
#define FOR_ash
#include "toys.h"
#include "lib/ash.h"
//==================================================================================
//Global variables.
int rootpid; /* pid of main shell */
int rootshell; /* true if we aren't a child of the main shell */
STATIC union node *curcmd;
STATIC union node *prevcmd;
int errno;

#if PROFILE
short profile_buf[16384];
int etext();
#endif

char *curdir;      /* current working directory */
STATIC char *cdcomppath;

struct jmploc *handler;
int exception;
volatile int suppressint;
volatile int intpending;
char *commandname;    /* name of command--printed on error */
int evalskip;      /* set if we are skipping commands */
STATIC int skipcount;  /* number of levels to skip */
int loopnest;      /* current loop nesting level */
int funcnest;      /* depth of function calls */

struct strlist *cmdenviron;
int exitstatus;      /* exit status of last command */
STATIC struct tblentry *cmdtable[CMDTABLESIZE];
STATIC int builtinloc = -1;    /* index in path of %builtin, or -1 */
char *expdest;        /* output of current string */
struct nodelist *argbackq;  /* list of back quote expressions */
struct ifsregion ifsfirst;  /* first struct in list of ifs regions */
struct ifsregion *ifslastp;  /* last struct in list */
struct arglist exparg;    /* holds expanded arg list */

#if UDIR
/*
 * Set if the last argument processed had /u/logname expanded.  This
 * variable is read by the cd command.
 */
int didudir;
#endif

int plinno = 1;            /* input line number */
int parsenleft;            /* copy of parsefile->nleft */

int parselleft;            /* copy of parsefile->lleft */
static int noalias = 0;       /* when set, don't handle aliases */

char *parsenextc;          /* copy of parsefile->nextc */
struct parsefile basepf;      /* top level input file */
char basebuf[BUFSIZ];        /* buffer for top level input file */
struct parsefile *parsefile = &basepf;  /* current input file */
char *pushedstring;          /* copy of parsenextc when text pushed back */
int pushednleft;          /* copy of parsenleft when text pushed back */
STATIC int nmboxes;          /* number of mailboxes */
STATIC time_t mailtime[MAXMBOXES];  /* times of mailboxes */
struct stack_block stackbase;
struct stack_block *stackp = &stackbase;
char *stacknxt = stackbase.space;
int stacknleft = MINSIZE;
int sstrnleft;
int herefd = -1;
struct job *jobtab;        /* array of jobs */
int njobs;            /* size of array */
short backgndpid = -1;      /* pid of last background process */

#if JOBS
int initialpgrp;        /* pgrp of shell on invocation */
short curjob;          /* current job */
#endif

char nullstr[1];        /* zero length string */
char *arg0;            /* value of $0 */
struct shparam shellparam;    /* current positional parameters */
char **argptr;          /* argument list for builtin commands */
char *optarg;          /* set by nextopt (like getopt) */
char *optptr;          /* used by nextopt */
char *minusc;          /* argument to -c option */
struct nodelist *backquotelist;
union node *redirnode;
struct heredoc *heredoc;
struct heredoc *heredoclist;  /* list of here documents to read */
int parsebackquote;        /* nonzero if we are inside backquotes */
int doprompt;          /* if set, prompt the user */
int needprompt;          /* true if interactive and at start of line */
int lasttoken;          /* last token read */
int tokpushback;        /* last token pushed back */
char *wordtext;          /* text of last word returned by readtoken */
int checkkwd;           /* 1 == check for kwds, 2 == also eat newlines */
int quoteflag;          /* set if (part of) last token was quoted */
int startlinno;          /* line # where last token started */

#define OPENBRACE '{'
#define CLOSEBRACE '}'
#define GDB_HACK 1         /* avoid local declarations which gdb can't handle */
#ifdef GDB_HACK
static const char argvars[5] = {CTLVAR, VSNORMAL|VSQUOTE, '@', '=', '\0'};
static const char types[] = "}-+?=";
#endif

struct redirtab *redirlist;

/* We keep track of whether or not fd0 has been redirected.  This is for
 * background commands, where we want to redirect fd0 to /dev/null only
 * if it hasn't already been redirected.
*/
int fd0_redirected = 0;
char *trap[MAXSIG+1];    /* trap handler commands */
char sigmode[MAXSIG];    /* current value of signal */
char gotsig[MAXSIG];    /* indicates specified signal received */
int pendingsigs;      /* indicates some signal received */

struct output output = {NULL, 0, NULL, OUTBUFSIZ, 1, 0};
struct output errout = {NULL, 0, NULL, 100, 2, 0};;
struct output memout = {NULL, 0, NULL, 0, MEM_OUT, 0};
struct output *out1 = &output;
struct output *out2 = &errout;

int funcblocksize;    /* size of structures in function */
int funcstringsize;    /* size of strings in node */
#ifdef __STDC__
pointer funcblock;    /* block to allocate function from */
#else
char *funcblock;    /* block to allocate function from */
#endif
char *funcstring;    /* block to allocate strings from */
char *pathopt;       /* set by padvance */
struct localvar *localvars;
struct tblentry **lastcmdentry;
char *expdir;

static const short nodesize[24] = {
    ALIGN(sizeof (struct nbinary)),
    ALIGN(sizeof (struct ncmd)),
    ALIGN(sizeof (struct npipe)),
    ALIGN(sizeof (struct nredir)),
    ALIGN(sizeof (struct nredir)),
    ALIGN(sizeof (struct nredir)),
    ALIGN(sizeof (struct nbinary)),
    ALIGN(sizeof (struct nbinary)),
    ALIGN(sizeof (struct nif)),
    ALIGN(sizeof (struct nbinary)),
    ALIGN(sizeof (struct nbinary)),
    ALIGN(sizeof (struct nfor)),
    ALIGN(sizeof (struct ncase)),
    ALIGN(sizeof (struct nclist)),
    ALIGN(sizeof (struct narg)),
    ALIGN(sizeof (struct narg)),
    ALIGN(sizeof (struct nfile)),
    ALIGN(sizeof (struct nfile)),
    ALIGN(sizeof (struct nfile)),
    ALIGN(sizeof (struct ndup)),
    ALIGN(sizeof (struct ndup)),
    ALIGN(sizeof (struct nhere)),
    ALIGN(sizeof (struct nhere)),
    ALIGN(sizeof (struct nnot)),
};

int bltincmd();
int bgcmd();
int breakcmd();
int cdcmd();
int dotcmd();
int echocmd();
int evalcmd();
int execcmd();
int exitcmd();
int exportcmd();
int falsecmd();
int fgcmd();
int getoptscmd();
int hashcmd();
int jobidcmd();
int jobscmd();
int localcmd();
int pwdcmd();
int readcmd();
int returncmd();
int setcmd();
int setvarcmd();
int shiftcmd();
int trapcmd();
int truecmd();
int umaskcmd();
int unsetcmd();
int waitcmd();
int timescmd();
int helpcmd();
#if CFG_ASH_KILL
int killcmd();
#endif
#if CFG_ASH_TEST
int testcmd();
#endif

#if CFG_ASH_PRINTF
int printfcmd();
#endif
int aliascmd();
int unaliascmd();
int typecmd();
int ulimitcmd();
int historycmd();
int expcmd();


int (*const builtinfunc[])() = {
  bltincmd,
  bgcmd,
  breakcmd,
  cdcmd,
  dotcmd,
  echocmd,
  evalcmd,
  execcmd,
  exitcmd,
  exportcmd,
  falsecmd,
  fgcmd,
  getoptscmd,
  hashcmd,
  jobidcmd,
  jobscmd,
  localcmd,
  pwdcmd,
  readcmd,
  returncmd,
  setcmd,
  setvarcmd,
  shiftcmd,
  trapcmd,
  truecmd,
  umaskcmd,
  unsetcmd,
  waitcmd,
  timescmd,
  helpcmd,
#if CFG_ASH_KILL
  killcmd,
#endif
#if CFG_ASH_TEST
  testcmd,
#endif
#if CFG_ASH_PRINTF
  printfcmd,
#endif
  aliascmd,
  unaliascmd,
  typecmd,
  ulimitcmd,
  historycmd,
  expcmd,
};

/*builtins command functions. */
const struct builtincmd builtincmd[] = {
  {"command", 0},
  {"bg", 1},
  {"break", 2},
  {"continue", 2},
  {"cd", 3},
  {"chdir", 3},
  {".", 4},
  {"source", 4},
  {"echo", 5},
  {"eval", 6},
  {"exec", 7},
  {"exit", 8},
  {"export", 9},
  {"readonly", 9},
  {"false", 10},
  {"fg", 11},
  {"getopts", 12},
  {"hash", 13},
  {"jobid", 14},
  {"jobs", 15},
  {"local", 16},
  {"pwd", 17},
  {"read", 18},
  {"return", 19},
  {"set", 20},
  {"setvar", 21},
  {"shift", 22},
  {"trap", 23},
  {":", 24},
  {"true", 24},
  {"umask", 25},
  {"unset", 26},
  {"wait", 27},
  {"times", 28},
  {"help", 29},
#if CFG_ASH_KILL
  {"kill", 30},
#endif
#if CFG_ASH_TEST
  {"test", 31},
  {"[", 31},
  {"[[", 31},
#endif
#if CFG_ASH_PRINTF
  {"printf", 32},
#endif
  {"alias", 33},
  {"unalias", 34},
  {"type", 35},
  {"ulimit", 36},
  {"history", 37},
  {"exp", 38},
  {"let", 38},
  {NULL, 0}
};

/*signames - signal names */
char *const sigmesg[32] = {
  0,
  "Hangup",
  "Interrupt",
  "Quit",
  "Illegal instruction",
  "Trace/BPT trap",
  "abort",
  "Bus error",
  "Floating exception",
  "Killed",
  "User signal 1",
  "Memory fault",
  "User signal 2",
  "Broken pipe",
  "Alarm call",
  "Terminated",
  0,
  0,
  0,
  "Stopped",
  "Stopped",
  "Stopped (input)",
  "Stopped (output)",
  0,
  "Time limit exceeded",
  0,
  0,
  "Profiling alarm",
  0,
  0,
  "Power fail",
  "Bad system call",
};

/* syntax table used when not in quotes */
const char basesyntax[257] = {
    CEOF,  CWORD,   CCTL,  CCTL,
    CCTL,  CCTL,  CCTL,  CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CSPCL,   CNL,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CSPCL,   CWORD,   CDQUOTE,
    CWORD,   CVAR,  CWORD,   CSPCL,
    CSQUOTE, CSPCL,   CSPCL,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CSPCL,   CSPCL,   CWORD,   CSPCL,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CBACK,   CWORD,   CWORD,
    CWORD,   CBQUOTE, CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CSPCL,   CENDVAR, CWORD,
    CWORD
};

/* syntax table used when in double quotes */
const char dqsyntax[257] = {
    CEOF,  CWORD,   CCTL,  CCTL,
    CCTL,  CCTL,  CCTL,  CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CNL,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CCTL,  CENDQUOTE,
    CWORD,   CVAR,  CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CCTL,
    CWORD,   CWORD,   CCTL,  CWORD,
    CCTL,  CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CCTL,
    CWORD,   CWORD,   CCTL,  CWORD,
    CCTL,  CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CCTL,  CBACK,   CWORD,   CWORD,
    CWORD,   CBQUOTE, CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CENDVAR, CCTL,
    CWORD
};

/* syntax table used when in single quotes */
const char sqsyntax[257] = {
    CEOF,  CWORD,   CCTL,  CCTL,
    CCTL,  CCTL,  CCTL,  CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CNL,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CCTL,  CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CENDQUOTE,CWORD,  CWORD,   CCTL,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CCTL,  CWORD,
    CCTL,  CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CCTL,  CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD,   CWORD,   CWORD,   CWORD,
    CWORD
};

/* character classification table */
const char is_type[257] = {
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     0,     0,
    0,     0,     ISSPECL, 0,
    ISSPECL, ISSPECL, 0,     0,
    0,     0,     0,     ISSPECL,
    0,     0,     ISSPECL, 0,
    0,     ISDIGIT, ISDIGIT, ISDIGIT,
    ISDIGIT, ISDIGIT, ISDIGIT, ISDIGIT,
    ISDIGIT, ISDIGIT, ISDIGIT, 0,
    0,     0,     0,     0,
    ISSPECL, ISSPECL, ISUPPER, ISUPPER,
    ISUPPER, ISUPPER, ISUPPER, ISUPPER,
    ISUPPER, ISUPPER, ISUPPER, ISUPPER,
    ISUPPER, ISUPPER, ISUPPER, ISUPPER,
    ISUPPER, ISUPPER, ISUPPER, ISUPPER,
    ISUPPER, ISUPPER, ISUPPER, ISUPPER,
    ISUPPER, ISUPPER, ISUPPER, ISUPPER,
    0,     0,     0,     0,
    ISUNDER, 0,     ISLOWER, ISLOWER,
    ISLOWER, ISLOWER, ISLOWER, ISLOWER,
    ISLOWER, ISLOWER, ISLOWER, ISLOWER,
    ISLOWER, ISLOWER, ISLOWER, ISLOWER,
    ISLOWER, ISLOWER, ISLOWER, ISLOWER,
    ISLOWER, ISLOWER, ISLOWER, ISLOWER,
    ISLOWER, ISLOWER, ISLOWER, ISLOWER,
    0,     0,     0,     0,
    0
};

//var - environment variable (get, set etc.).
#if ATTY
struct var vatty;
#endif
struct var vifs;
struct var vmail;
struct var vmpath;
struct var vpath;
struct var vps1;
struct var vps2;
struct var vvers;
#if ATTY
struct var vterm;
#endif

const struct varinit varinit[] = {
#if ATTY
  {&vatty,  VSTRFIXED|VTEXTFIXED|VUNSET,  "ATTY="},
#endif
  {&vifs,   VSTRFIXED|VTEXTFIXED,       "IFS= \t\n"},
  {&vmail,  VSTRFIXED|VTEXTFIXED|VUNSET,  "MAIL="},
  {&vmpath,   VSTRFIXED|VTEXTFIXED|VUNSET,  "MAILPATH="},
  {&vpath,  VSTRFIXED|VTEXTFIXED,       "PATH=:/bin:/usr/bin"},
  /*
   * vps1 depends on uid
   */
  {&vps2,   VSTRFIXED|VTEXTFIXED,       "PS2=> "},
#if ATTY
  {&vterm,  VSTRFIXED|VTEXTFIXED|VUNSET,  "TERM="},
#endif
  {NULL,    0,                NULL}
};

struct var *vartab[VTABSIZE];
STATIC void unsetvar __P((char *));
STATIC struct var **hashvar __P((char *));
STATIC int varequal __P((char *, char *));

#define TEMPSIZE 24
#ifdef __STDC__
static const char digit[16] = "0123456789ABCDEF";
#else
static const char digit[17] = "0123456789ABCDEF";
#endif

int is_interactive;
FILE *tracefile;

/*Shell command parser.*/
void setparam(char **argv);
void freeparam(struct shparam *param);
STATIC int xxreadtoken();

STATIC char *cmdnextc;
STATIC int cmdnleft;
STATIC void cmdtxt(), cmdputs();


/* for keyboard input. */ 
#define LEFT  0
#define RIGHT   1

enum CODES{
  KEY_UP = 0x100,
  KEY_DOWN,
  KEY_RIGHT,
  KEY_LEFT,
  KEY_HOME,
  KEY_END,
  KEY_INSERT,
  KEY_DELETE,
  KEY_PAGEUP,
  KEY_PAGEDN,
  KEY_CTLLF,
  KEY_CTLRT,
  KEY_CTRLD,
  KEY_CTRLU
};

typedef struct keycode_map_s {
  char *key;
  int code;
}keycode_map_t;

/*
 * Table of error messages.
 */
struct errname {
  short errcode;    /* error number */
  short action;     /* operation which encountered the error */
  char *msg;      /* text describing the error */
};

#define ALL (E_OPEN|E_CREAT|E_EXEC)
STATIC const struct errname errormsg[] = {
  {EINTR,   ALL,    "interrupted"},
  {EACCES,  ALL,    "permission denied"},
  {EIO,     ALL,    "I/O error"},
  {ENOENT,  E_OPEN,   "no such file"},
  {ENOENT,  E_CREAT,  "directory nonexistent"},
  {ENOENT,  E_EXEC,   "not found"},
  {ENOTDIR,   E_OPEN,   "no such file"},
  {ENOTDIR,   E_CREAT,  "directory nonexistent"},
  {ENOTDIR,   E_EXEC,   "not found"},
  {EISDIR,  ALL,    "is a directory"},
/*  {EMFILE,  ALL,    "too many open files"}, */
  {ENFILE,  ALL,    "file table overflow"},
  {ENOSPC,  ALL,    "file system full"},
#ifdef EDQUOT
  {EDQUOT,  ALL,    "disk quota exceeded"},
#endif
#ifdef ENOSR
  {ENOSR,   ALL,    "no streams resources"},
#endif
  {ENXIO,   ALL,    "no such device or address"},
  {EROFS,   ALL,    "read-only file system"},
  {ETXTBSY,   ALL,    "text busy"},
#ifdef SYSV
  {EAGAIN,  E_EXEC,   "not enough memory"},
#endif
  {ENOMEM,  ALL,    "not enough memory"},
#ifdef ENOLINK
  {ENOLINK,   ALL,    "remote access failed"},
#endif
#ifdef EMULTIHOP
  {EMULTIHOP, ALL,    "remote access failed"},
#endif
#ifdef ECOMM
  {ECOMM,   ALL,    "remote access failed"},
#endif
#ifdef ESTALE
  {ESTALE,  ALL,    "remote access failed"},
#endif
#ifdef ETIMEDOUT
  {ETIMEDOUT, ALL,    "remote access failed"},
#endif
#ifdef ELOOP
  {ELOOP,   ALL,    "symbolic link loop"},
#endif
  {E2BIG,   E_EXEC,   "argument list too long"},
#ifdef ELIBACC
  {ELIBACC,   E_EXEC,   "shared library missing"},
#endif
  {0,     0,      NULL}
};

#define MAXPWD PATH_MAX
static int history_items = 0;

//Done till here:

//==================================================================================
/*
 * Initialization code.
 */
void init()
{
  char **envp;
  extern char **environ;

  initvar();
  for (envp = environ ; *envp ; envp++) {
    if (strchr(*envp, '='))
      setvareq(*envp, VEXPORT|VTEXTFIXED);
  }

  {
    extern char basebuf[];
    basepf.nextc = basepf.buf = basebuf;
  }
}

/*
 * This routine is called when an error or an interrupt occurs in an
 * interactive shell and control is returned to the main command loop.
 */
void reset()
{
  out1 = &output;
  out2 = &errout;
  if (memout.buf != NULL) {
    ckfree(memout.buf);
    memout.buf = NULL;
  }

  while (redirlist)
    popredir();

  tokpushback = 0;

  if (exception != EXSHELLPROC)
    parsenleft = 0;      /* clear input buffer */
  popallfiles();

  evalskip = 0;
  loopnest = 0;
  funcnest = 0;
}

/*
 * This routine is called to initialize the shell to run a shell procedure.
 */
void initshellproc()
{
  shprocvar();

    {
      char *sm;

      clear_traps();
      for (sm = sigmode ; sm < sigmode + MAXSIG ; sm++) {
        if (*sm == S_IGN)
          *sm = S_HARD_IGN;
      }
    }

    clearredir();

    {
      char *p;
      for (p = optval ; p < optval + sizeof optval ; p++)
        *p = 0;
    }


    {
      backgndpid = -1;
#if JOBS
      jobctl = 0;
#endif
    }

    popallfiles();
    deletefuncs();
    exitstatus = 0;
}

//==================================================================================
/*
 * Echo command.
 */
int echocmd(int argc, char **argv)
{
  register char **ap;
  register char *p;
  register char c;
  int count;
  int nnflag = 0;
  int eeflag = 0;

  ap = argv;
  if (argc)
    ap++;
again:
  if ((p = *ap) != NULL) {
    if (equal(p, "-n")) {
      nnflag++;
      ap++;
      goto again;
    } else if (equal(p, "-e")) {
      eeflag++;
      ap++;
      goto again;
    } else if (equal(p, "-ne") || equal(p, "-en")) {
      eeflag++;
      nnflag++;
      ap++;
      goto again;
    }
  }
  while ((p = *ap++) != NULL) {
    while ((c = *p++) != '\0') {
      if (c == '\\' && eeflag) {
        switch (*p++) {
        case 'b':  c = '\b';  break;
        case 'c':  return 0;    /* exit */
        case 'f':  c = '\f';  break;
        case 'n':  c = '\n';  break;
        case 'r':  c = '\r';  break;
        case 't':  c = '\t';  break;
        case 'v':  c = '\v';  break;
        case '\\':  break;    /* c = '\\' */
        case '0':
          c = 0;
          count = 3;
          while (--count >= 0 && (unsigned)(*p - '0') < 8)
            c = (c << 3) + (*p++ - '0');
          break;
        default:
          p--;
          break;
        }
      }
      out1c(c);
    }
    if (*ap) out1c(' ');
  }
  if (! nnflag) out1c('\n');
  return 0;
}

//==================================================================================
//miscbltin commands
/*
 * The read builtin.  The -e option causes backslashes to escape the
 * following character.
 *
 * This uses unbuffered input, which may be avoidable in some cases.
 */
int readcmd(int argc, char **argv)
{
  char **ap;
  int backslash;
  char c;
  int eflag;
  char *prompt;
  char *ifs;
  char *p;
  int startword;
  int status;
  int i;

  char *buffer = NULL;
  int bufpos = 0; /* need to be able to hold -1 */

  eflag = 0;
  prompt = NULL;
  while ((i = nextopt("ep:")) != '\0') {
    if (i == 'p')
      prompt = optarg;
    else
      eflag = 1;
  }
  if (prompt && isatty(0)) {
    out2str(prompt);
    flushall();
  }
  if ((ap = argptr) == NULL)
    sh_error("arg count");
  if ((ifs = bltinlookup("IFS", 1)) == NULL)
    ifs = nullstr;
  status = 0;
  startword = 1;
  backslash = 0;
  STARTSTACKSTR(p);

  for (;;) {

    if ((bufpos & 0xff) == 0)
      buffer = xrealloc(buffer, bufpos + 0x100);

    if (read(0, &c, 1) != 1) {
      status = 1;
      break;
    }

    buffer[bufpos++] = c;

    if (c == '\0')
      continue;
    if (backslash) {
      backslash = 0;
      if (c != '\n')
        STPUTC(c, p);
      continue;
    }
    if (eflag && c == '\\') {
      backslash++;
      continue;
    }
    if (c == '\n')
      break;
    if (startword && *ifs == ' ' && strchr(ifs, c)) {
      continue;
    }
    startword = 0;
    if (backslash && c == '\\') {
      if (read(0, &c, 1) != 1) {
        status = 1;
        break;
      }
      STPUTC(c, p);
    } else if (ap[1] != NULL && strchr(ifs, c) != NULL) {
      STACKSTRNUL(p);
      if(*ap) {
        setvar(*ap, stackblock(), 0);
        ap++;
      }
      startword = 1;
      STARTSTACKSTR(p);
    } else {
      STPUTC(c, p);
    }
  }STACKSTRNUL(p);
  STACKSTRNUL(p);
  if(*ap == NULL) {
    buffer[bufpos + 1] = '\0';
    setvar("REPLY", buffer, 0);
    if(buffer) free(buffer);
    return status;
  }
  setvar(*ap, stackblock(), 0);
  while (*++ap != NULL)
    setvar(*ap, nullstr, 0);
  if(buffer) free(buffer);
  return status;
}

/*
 * umask command.
 */
int umaskcmd(int argc, char **argv)
{
  int mask;
  char *p;
  int i, j;
  int print_symbolically = 0;

  while (nextopt("S") != '\0')
    print_symbolically = 1;

  if ((p = *argptr) == NULL) {
    INTOFF;
    mask = umask(0);
    umask(mask);
    INTON;
    if(!print_symbolically)
      out1fmt("%.4o\n", mask);  /* %#o might be better */
  } else {
    if (isdigit((unsigned char) *p)) {
      mask = 0;
      do {
        if ((unsigned)(i = *p - '0') >= 8)
          sh_error("Illegal number: %s", argv[1]);
        mask = (mask << 3) + i;
      } while (*++p != '\0');
      umask(mask);
    }
    else {
      INTOFF;
      mask = umask(0);
      umask(mask);
      INTON;
      mask = ~mask & 0777;
      if(!m_parse(p, (mode_t *)&mask))
        sh_error("Illegal mask: %s", p);
      umask(~mask & 0777);
    }
  }

  if(print_symbolically) {
    const char who_chars[] = "ugo";
    const char perms[] = "rwx";
    const mode_t perm_mask[] = {
        S_IRUSR, S_IWUSR, S_IXUSR,
        S_IRGRP, S_IWGRP, S_IXGRP,
        S_IROTH, S_IWOTH, S_IXOTH
      };
    char buf[20] = {0,};
    int buf_index = 0;
    i = j = 0;
    while(j = 0, i < 3) {
      buf[buf_index++] = who_chars[i];
      buf[buf_index++] = '=';
      while(j < 3) {
        if ((mask & perm_mask[3 * i + j]) == 0)
          buf[buf_index++] = perms[j];
        j++;
      }
      if(++i != 3) {
        buf[buf_index++] = ',';
        buf[buf_index++] = ' ';
      }
    }
    out1fmt("%s\n", buf);
  }
  return 0;
}
//==================================================================================
//nodes - Routine for dealing with parsed shell commands.
/*
 * Make a copy of a parse tree.
 */
union node *copyfunc(union node *n)
{
  if (n == NULL)
    return NULL;
  funcblocksize = 0;
  funcstringsize = 0;
  calcsize(n);
  funcblock = ckmalloc(funcblocksize + funcstringsize);
  funcstring = funcblock + funcblocksize;
  return copynode(n);
}

STATIC void calcsize(union node *n)
{
  if (n == NULL)
    return;
  funcblocksize += nodesize[n->type];
  switch (n->type) {
    case NSEMI:
    case NAND:
    case NOR:
    case NWHILE:
    case NUNTIL:
      calcsize(n->nbinary.ch2);
      calcsize(n->nbinary.ch1);
      break;
    case NCMD:
      calcsize(n->ncmd.redirect);
      calcsize(n->ncmd.args);
      break;
    case NPIPE:
      sizenodelist(n->npipe.cmdlist);
      break;
    case NREDIR:
    case NBACKGND:
    case NSUBSHELL:
      calcsize(n->nredir.redirect);
      calcsize(n->nredir.n);
      break;
    case NIF:
      calcsize(n->nif.elsepart);
      calcsize(n->nif.ifpart);
      calcsize(n->nif.test);
      break;
    case NFOR:
      funcstringsize += strlen(n->nfor.var) + 1;
      calcsize(n->nfor.body);
      calcsize(n->nfor.args);
      break;
    case NCASE:
      calcsize(n->ncase.cases);
      calcsize(n->ncase.expr);
      break;
    case NCLIST:
      calcsize(n->nclist.body);
      calcsize(n->nclist.pattern);
      calcsize(n->nclist.next);
      break;
    case NDEFUN:
    case NARG:
      sizenodelist(n->narg.backquote);
      funcstringsize += strlen(n->narg.text) + 1;
      calcsize(n->narg.next);
      break;
    case NTO:
    case NFROM:
    case NAPPEND:
      calcsize(n->nfile.fname);
      calcsize(n->nfile.next);
      break;
    case NTOFD:
    case NFROMFD:
      calcsize(n->ndup.next);
      break;
    case NHERE:
    case NXHERE:
      calcsize(n->nhere.doc);
      calcsize(n->nhere.next);
      break;
    };
}

STATIC void sizenodelist(struct nodelist *lp)
{
  while (lp) {
    funcblocksize += ALIGN(sizeof (struct nodelist));
    calcsize(lp->n);
    lp = lp->next;
  }
}

STATIC union node *copynode(union node *n)
{
  union node *new;

  if (n == NULL)
    return NULL;
  new = funcblock;
  funcblock += nodesize[n->type];
  switch (n->type) {
    case NSEMI:
    case NAND:
    case NOR:
    case NWHILE:
    case NUNTIL:
      new->nbinary.ch2 = copynode(n->nbinary.ch2);
      new->nbinary.ch1 = copynode(n->nbinary.ch1);
      break;
    case NCMD:
      new->ncmd.redirect = copynode(n->ncmd.redirect);
      new->ncmd.args = copynode(n->ncmd.args);
      new->ncmd.backgnd = n->ncmd.backgnd;
      break;
    case NPIPE:
      new->npipe.cmdlist = copynodelist(n->npipe.cmdlist);
      new->npipe.backgnd = n->npipe.backgnd;
      break;
    case NREDIR:
    case NBACKGND:
    case NSUBSHELL:
      new->nredir.redirect = copynode(n->nredir.redirect);
      new->nredir.n = copynode(n->nredir.n);
      break;
    case NIF:
      new->nif.elsepart = copynode(n->nif.elsepart);
      new->nif.ifpart = copynode(n->nif.ifpart);
      new->nif.test = copynode(n->nif.test);
      break;
    case NFOR:
      new->nfor.var = nodesavestr(n->nfor.var);
      new->nfor.body = copynode(n->nfor.body);
      new->nfor.args = copynode(n->nfor.args);
      break;
    case NCASE:
      new->ncase.cases = copynode(n->ncase.cases);
      new->ncase.expr = copynode(n->ncase.expr);
      break;
    case NCLIST:
      new->nclist.body = copynode(n->nclist.body);
      new->nclist.pattern = copynode(n->nclist.pattern);
      new->nclist.next = copynode(n->nclist.next);
      break;
    case NDEFUN:
    case NARG:
      new->narg.backquote = copynodelist(n->narg.backquote);
      new->narg.text = nodesavestr(n->narg.text);
      new->narg.next = copynode(n->narg.next);
      break;
    case NTO:
    case NFROM:
    case NAPPEND:
      new->nfile.fname = copynode(n->nfile.fname);
      new->nfile.fd = n->nfile.fd;
      new->nfile.next = copynode(n->nfile.next);
      break;
    case NTOFD:
    case NFROMFD:
      new->ndup.dupfd = n->ndup.dupfd;
      new->ndup.fd = n->ndup.fd;
      new->ndup.next = copynode(n->ndup.next);
      break;
    case NHERE:
    case NXHERE:
      new->nhere.doc = copynode(n->nhere.doc);
      new->nhere.fd = n->nhere.fd;
      new->nhere.next = copynode(n->nhere.next);
      break;
  };
  new->type = n->type;
  return new;
}

STATIC struct nodelist *copynodelist(struct nodelist *lp)
{
  struct nodelist *start;
  struct nodelist **lpp;

  lpp = &start;
  while (lp) {
    *lpp = funcblock;
    funcblock += ALIGN(sizeof (struct nodelist));
    (*lpp)->n = copynode(lp->n);
    lp = lp->next;
    lpp = &(*lpp)->next;
  }
  *lpp = NULL;
  return start;
}

STATIC char *nodesavestr(char *s)
{
  register char *p = s;
  register char *q = funcstring;
  char *rtn = funcstring;

  while ((*q++ = *p++));
  funcstring = q;
  return rtn;
}

/*
 * Free a parse tree.
 */
void freefunc(union node *n)
{
  if (n)
    ckfree(n);
}

//==================================================================================
//var - environment variable (get, set etc.).
/*
 * This routine initializes the builtin variables.  It is called when the
 * shell is initialized and again when a shell procedure is spawned.
 */
void initvar()
{
  const struct varinit *ip;
  struct var *vp;
  struct var **vpp;

  for (ip = varinit ; (vp = ip->var) != NULL ; ip++) {
    if ((vp->flags & VEXPORT) == 0) {
      vpp = hashvar(ip->text);
      vp->next = *vpp;
      *vpp = vp;
      vp->text = ip->text;
      vp->flags = ip->flags;
    }
  }
  /*
   * PS1 depends on uid
   */
  if ((vps1.flags & VEXPORT) == 0) {
    vpp = hashvar("PS1=");
    vps1.next = *vpp;
    *vpp = &vps1;
    vps1.text = getuid() ? "PS1=$ " : "PS1=# ";
    vps1.flags = VSTRFIXED|VTEXTFIXED;
  }
}

/*
 * Set the value of a variable.  The flags argument is ored with the
 * flags of the variable.  If val is NULL, the variable is unset.
 */
void setvar(char *name, char *val, int flags)
{
  char *p, *q;
  int len;
  int namelen;
  char *nameeq;
  int isbad;

  isbad = 0;
  p = name;
  if (! is_name(*p++))
    isbad = 1;
  for (;;) {
    if (! is_in_name(*p)) {
      if (*p == '\0' || *p == '=')
        break;
      isbad = 1;
    }
    p++;
  }
  namelen = p - name;
  if (isbad)
    sh_error("%.*s: is read only", namelen, name);
  len = namelen + 2;    /* 2 is space for '=' and '\0' */
  if (val == NULL) {
    flags |= VUNSET;
  } else {
    len += strlen(val);
  }
  p = nameeq = ckmalloc(len);
  q = name;
  while (--namelen >= 0)
    *p++ = *q++;
  *p++ = '=';
  *p = '\0';
  if (val)
    scopy(val, p);
  setvareq(nameeq, flags);
}

/*
 * Same as setvar except that the variable and value are passed in
 * the first argument as name=value.  Since the first argument will
 * be actually stored in the table, it should not be a string that
 * will go away.
 */
void setvareq(char *s, int flags)
{
  struct var *vp, **vpp;

  vpp = hashvar(s);
  flags |= (VEXPORT & (((unsigned) (1 - aflag)) - 1));
  for (vp = *vpp ; vp ; vp = vp->next) {
    if (varequal(s, vp->text)) {
      if (vp->flags & VREADONLY) {
        int len = strchr(s, '=') - s;
        sh_error("%.*s: is read only", len, s);
      }
      INTOFF;
      if (vp == &vpath)
        changepath(s + 5);  /* 5 = strlen("PATH=") */
      if ((vp->flags & (VTEXTFIXED|VSTACK)) == 0)
        ckfree(vp->text);
      vp->flags &=~ (VTEXTFIXED|VSTACK|VUNSET);
      vp->flags |= flags;
      vp->text = s;
      if (vp == &vmpath || (vp == &vmail && ! mpathset()))
        chkmail(1);
      INTON;
      return;
    }
  }
  /* not found */
  vp = ckmalloc(sizeof (*vp));
  vp->flags = flags;
  vp->text = s;
  vp->next = *vpp;
  *vpp = vp;
}

/*
 * Process a linked list of variable assignments.
 */
void listsetvar(struct strlist *list)
{
  struct strlist *lp;

  INTOFF;
  for (lp = list ; lp ; lp = lp->next) {
    setvareq(savestr(lp->text), 0);
  }
  INTON;
}

/*
 * Find the value of a variable.  Returns NULL if not set.
 */
char *lookupvar(char *name)
{
  struct var *v;

  for (v = *hashvar(name) ; v ; v = v->next) {
    if (varequal(v->text, name)) {
      if (v->flags & VUNSET)
        return NULL;
      return strchr(v->text, '=') + 1;
    }
  }
  return NULL;
}

/*
 * Search the environment of a builtin command.  If the second argument
 * is nonzero, return the value of a variable even if it hasn't been
 * exported.
 */
char *bltinlookup(char *name, int doall)
{
  struct strlist *sp;
  struct var *v;

  for (sp = cmdenviron ; sp ; sp = sp->next) {
    if (varequal(sp->text, name))
      return strchr(sp->text, '=') + 1;
  }
  for (v = *hashvar(name) ; v ; v = v->next) {
    if (varequal(v->text, name)) {
      if ((v->flags & VUNSET)
       || (! doall && (v->flags & VEXPORT) == 0))
        return NULL;
      return strchr(v->text, '=') + 1;
    }
  }
  return NULL;
}

/*
 * Generate a list of exported variables.  This routine is used to construct
 * the third argument to execve when executing a program.
 */
char **environment()
{
  int nenv;
  struct var **vpp;
  struct var *vp;
  char **env, **ep;

  nenv = 0;
  for (vpp = vartab ; vpp < vartab + VTABSIZE ; vpp++) {
    for (vp = *vpp ; vp ; vp = vp->next)
      if (vp->flags & VEXPORT)
        nenv++;
  }
  ep = env = stalloc((nenv + 1) * sizeof *env);
  for (vpp = vartab ; vpp < vartab + VTABSIZE ; vpp++) {
    for (vp = *vpp ; vp ; vp = vp->next)
      if (vp->flags & VEXPORT)
        *ep++ = vp->text;
  }
  *ep = NULL;
  return env;
}

/*
 * Called when a shell procedure is invoked to clear out nonexported
 * variables.  It is also necessary to reallocate variables of with
 * VSTACK set since these are currently allocated on the stack.
 */
void shprocvar()
{
  struct var **vpp;
  struct var *vp, **prev;

  for (vpp = vartab ; vpp < vartab + VTABSIZE ; vpp++) {
    for (prev = vpp ; (vp = *prev) != NULL ; ) {
      if ((vp->flags & VEXPORT) == 0) {
        *prev = vp->next;
        if ((vp->flags & VTEXTFIXED) == 0)
          ckfree(vp->text);
        if ((vp->flags & VSTRFIXED) == 0)
          ckfree(vp);
      } else {
        if (vp->flags & VSTACK) {
          vp->text = savestr(vp->text);
          vp->flags &=~ VSTACK;
        }
        prev = &vp->next;
      }
    }
  }
  initvar();
}

/*
 * Command to list all variables which are set.  Currently this command
 * is invoked from the set command when the set command is called without
 * any variables.
 */
int showvarscmd(int argc, char **argv)
{
  struct var **vpp;
  struct var *vp;

  for (vpp = vartab ; vpp < vartab + VTABSIZE ; vpp++) {
    for (vp = *vpp ; vp ; vp = vp->next) {
      if ((vp->flags & VUNSET) == 0)
        out1fmt("%s\n", vp->text);
    }
  }
  return 0;
}

/*
 * The export and readonly commands.
 */
int exportcmd(int argc, char **argv)
{
  struct var **vpp;
  struct var *vp;
  char *name;
  char *p;
  int flag = argv[0][0] == 'r'? VREADONLY : VEXPORT;

  listsetvar(cmdenviron);
  if (argc > 1) {
    while ((name = *argptr++) != NULL) {
      if ((p = strchr(name, '=')) != NULL) {
        p++;
      } else {
        vpp = hashvar(name);
        for (vp = *vpp ; vp ; vp = vp->next) {
          if (varequal(vp->text, name)) {
            vp->flags |= flag;
            goto found;
          }
        }
      }
      setvar(name, p, flag);
found:;
    }
  } else {
    for (vpp = vartab ; vpp < vartab + VTABSIZE ; vpp++) {
      for (vp = *vpp ; vp ; vp = vp->next) {
        if (vp->flags & flag) {
          out1str("export ");
          out1str(vp->text);
          out1c('\n');
        }
      }
    }
  }
  return 0;
}

/*
 * The "local" command.
 */
int localcmd(int argc, char **argv)
{
  char *name;

  while ((name = *argptr++) != NULL) {
    mklocal(name);
  }
  return 0;
}

/*
 * Make a variable a local variable.  When a variable is made local, it's
 * value and flags are saved in a localvar structure.  The saved values
 * will be restored when the shell function returns.  We handle the name
 * "-" as a special case.
 */
void mklocal(char *name)
{
  struct localvar *lvp;
  struct var **vpp;
  struct var *vp;

  INTOFF;
  lvp = ckmalloc(sizeof (struct localvar));
  if (name[0] == '-' && name[1] == '\0') {
    lvp->text = ckmalloc(sizeof optval);
    bcopy(optval, lvp->text, sizeof optval);
    vp = NULL;
  } else {
    vpp = hashvar(name);
    for (vp = *vpp ; vp && ! varequal(vp->text, name) ; vp = vp->next);
    if (vp == NULL) {
      if (strchr(name, '='))
        setvareq(savestr(name), VSTRFIXED);
      else
        setvar(name, NULL, VSTRFIXED);
      vp = *vpp;  /* the new variable */
      lvp->text = NULL;
      lvp->flags = VUNSET;
    } else {
      lvp->text = vp->text;
      lvp->flags = vp->flags;
      vp->flags |= VSTRFIXED|VTEXTFIXED;
      if (strchr(name, '='))
        setvareq(savestr(name), 0);
    }
  }
  lvp->vp = vp;
  lvp->next = localvars;
  localvars = lvp;
  INTON;
}

/*
 * Called after a function returns.
 */
void poplocalvars()
{
  struct localvar *lvp;
  struct var *vp;

  while ((lvp = localvars) != NULL) {
    localvars = lvp->next;
    vp = lvp->vp;
    if (vp == NULL) {  /* $- saved */
      bcopy(lvp->text, optval, sizeof optval);
      ckfree(lvp->text);
    } else if ((lvp->flags & (VUNSET|VSTRFIXED)) == VUNSET) {
      unsetvar(vp->text);
    } else {
      if ((vp->flags & VTEXTFIXED) == 0)
        ckfree(vp->text);
      vp->flags = lvp->flags;
      vp->text = lvp->text;
    }
    ckfree(lvp);
  }
}

int setvarcmd(int argc, char **argv)
{
  if (argc <= 2)
    return unsetcmd(argc, argv);
  else if (argc == 3)
    setvar(argv[1], argv[2], 0);
  else
    sh_error("List assignment not implemented");
  return 0;
}

/*
 * The unset builtin command.  We unset the function before we unset the
 * variable to allow a function to be unset when there is a readonly variable
 * with the same name.
 */
int unsetcmd(int argc, char **argv)
{
  char **ap;
  int i;
  int flg_func = 0;
  int flg_var = 0;

  while ((i = nextopt("vf")) != 0) {
    if (i == 'f')
      flg_func = 1;
    else
      flg_var = i;
  }

  if (flg_func == 0 && flg_var == 0)
    flg_var = 1;

  for (ap = argv + 1 ; *ap ; ap++) {
     if (flg_func)
    unsetfunc(*ap);
     if (flg_var)
    unsetvar(*ap);
  }
  return 0;
}

/*
 * Unset the specified variable.
 */
STATIC void unsetvar(char *s)
{
  struct var **vpp;
  struct var *vp;

  vpp = hashvar(s);
  for (vp = *vpp ; vp ; vpp = &vp->next, vp = *vpp) {
    if (varequal(vp->text, s)) {
      INTOFF;
      if (*(strchr(vp->text, '=') + 1) != '\0'
       || vp->flags & VREADONLY) {
        setvar(s, nullstr, 0);
      }
      vp->flags &=~ VEXPORT;
      vp->flags |= VUNSET;
      if ((vp->flags & VSTRFIXED) == 0) {
        if ((vp->flags & VTEXTFIXED) == 0)
          ckfree(vp->text);
        *vpp = vp->next;
        ckfree(vp);
      }
      INTON;
      return;
    }
  }
}

/*
 * Find the appropriate entry in the hash table from the name.
 */
STATIC struct var **hashvar(register char *p)
{
  unsigned int hashval;

  hashval = *p << 4;
  while (*p && *p != '=')
    hashval += *p++;
  return &vartab[hashval % VTABSIZE];
}

/*
 * Returns true if the two strings specify the same varable.  The first
 * variable name is terminated by '='; the second may be terminated by
 * either '=' or '\0'.
 */
STATIC int varequal(register char *p, register char *q)
{
  while (*p == *q++) {
    if (*p++ == '=')
      return 1;
  }
  if (*p == '=' && *(q - 1) == '\0')
    return 1;
  return 0;
}

//==================================================================================
//output - formating outputs.
/*
 * Shell output routines.  We use our own output routines because:
 *  When a builtin command is interrupted we have to discard
 *    any pending output.
 *  When a builtin command appears in back quotes, we want to
 *    save the output of the command in a region obtained
 *    via malloc, rather than doing a fork and reading the
 *    output of the command via a pipe.
 *  Our output routines may be smaller than the stdio routines.
 */
#ifdef notdef  /* no longer used */
/*
 * Set up an output file to write to memory rather than a file.
 */
void open_mem(char *block, int length, struct output *file)
{
  file->nextc = block;
  file->nleft = --length;
  file->fd = BLOCK_OUT;
  file->flags = 0;
}
#endif


void out1str(char *p)
{
  outstr(p, out1);
}

void out2str(char *p)
{
  outstr(p, out2);
}

void outstr(register char *p, register struct output *file)
{
  while (*p)
    outc(*p++, file);
}

char out_junk[16];

void emptyoutbuf(struct output *dest)
{
  int offset;

  if (dest->fd == BLOCK_OUT) {
    dest->nextc = out_junk;
    dest->nleft = sizeof out_junk;
    dest->flags |= OUTPUT_ERR;
  } else if (dest->buf == NULL) {
    INTOFF;
    dest->buf = ckmalloc(dest->bufsize);
    dest->nextc = dest->buf;
    dest->nleft = dest->bufsize;
    INTON;
  } else if (dest->fd == MEM_OUT) {
    offset = dest->bufsize;
    INTOFF;
    dest->bufsize <<= 1;
    dest->buf = ckrealloc(dest->buf, dest->bufsize);
    dest->nleft = dest->bufsize - offset;
    dest->nextc = dest->buf + offset;
    INTON;
  } else {
    flushout(dest);
  }
  dest->nleft--;
}

void flushall()
{
  flushout(&output);
  flushout(&errout);
}

void flushout(struct output *dest)
{
  if (dest->buf == NULL || dest->nextc == dest->buf || dest->fd < 0)
    return;
  if (xxwrite(dest->fd, dest->buf, dest->nextc - dest->buf) < 0)
    dest->flags |= OUTPUT_ERR;
  dest->nextc = dest->buf;
  dest->nleft = dest->bufsize;
}

void freestdout()
{
  INTOFF;
  if (output.buf) {
    ckfree(output.buf);
    output.buf = NULL;
    output.nleft = 0;
  }
  INTON;
}

#ifdef __STDC__
void outfmt(struct output *file, char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  doformat(file, fmt, ap);
  va_end(ap);
}

void out1fmt(char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  doformat(out1, fmt, ap);
  va_end(ap);
}

void fmtstr(char *outbuf, int length, char *fmt, ...)
{
  va_list ap;
  struct output strout;

  va_start(ap, fmt);
  strout.nextc = outbuf;
  strout.nleft = length;
  strout.fd = BLOCK_OUT;
  strout.flags = 0;
  doformat(&strout, fmt, ap);
  outc('\0', &strout);
  if (strout.flags & OUTPUT_ERR)
    outbuf[length - 1] = '\0';
}

#else /* not __STDC__ */

void outfmt(va_alist)
  va_dcl
{
  va_list ap;
  struct output *file;
  char *fmt;

  va_start(ap);
  file = va_arg(ap, struct output *);
  fmt = va_arg(ap, char *);
  doformat(file, fmt, ap);
  va_end(ap);
}

void out1fmt(va_alist)
  va_dcl
{
  va_list ap;
  char *fmt;

  va_start(ap);
  fmt = va_arg(ap, char *);
  doformat(out1, fmt, ap);
  va_end(ap);
}

void fmtstr(va_alist)
  va_dcl
{
  va_list ap;
  struct output strout;
  char *outbuf;
  int length;
  char *fmt;

  va_start(ap);
  outbuf = va_arg(ap, char *);
  length = va_arg(ap, int);
  fmt = va_arg(ap, char *);
  strout.nextc = outbuf;
  strout.nleft = length;
  strout.fd = BLOCK_OUT;
  strout.flags = 0;
  doformat(&strout, fmt, ap);
  outc('\0', &strout);
  if (strout.flags & OUTPUT_ERR)
    outbuf[length - 1] = '\0';
}
#endif /* __STDC__ */

/*
 * Formatted output.  This routine handles a subset of the printf formats:
 * - Formats supported: d, u, o, X, s, and c.
 * - The x format is also accepted but is treated like X.
 * - The l modifier is accepted.
 * - The - and # flags are accepted; # only works with the o format.
 * - Width and precision may be specified with any format except c.
 * - An * may be given for the width or precision.
 * - The obsolete practice of preceding the width with a zero to get
 *   zero padding is not supported; use the precision field.
 * - A % may be printed by writing %% in the format string.
 */
void doformat(register struct output *dest, register char *f, va_list ap)
{
  register char c;
  char temp[TEMPSIZE];
  int flushleft;
  int sharp;
  int width;
  int prec;
  int islong;
  char *p;
  int sign;
  long l;
  unsigned long num;
  unsigned base;
  int len;
  int size;
  int pad;

  while ((c = *f++) != '\0') {
    if (c != '%') {
      outc(c, dest);
      continue;
    }
    flushleft = 0;
    sharp = 0;
    width = 0;
    prec = -1;
    islong = 0;
    for (;;) {
      if (*f == '-')
        flushleft++;
      else if (*f == '#')
        sharp++;
      else
        break;
      f++;
    }
    if (*f == '*') {
      width = va_arg(ap, int);
      f++;
    } else {
      while (is_digit(*f)) {
        width = 10 * width + digit_val(*f++);
      }
    }
    if (*f == '.') {
      if (*++f == '*') {
        prec = va_arg(ap, int);
        f++;
      } else {
        prec = 0;
        while (is_digit(*f)) {
          prec = 10 * prec + digit_val(*f++);
        }
      }
    }
    if (*f == 'l') {
      islong++;
      f++;
    }
    switch (*f) {
    case 'd':
      if (islong)
        l = va_arg(ap, long);
      else
        l = va_arg(ap, int);
      sign = 0;
      num = l;
      if (l < 0) {
        num = -l;
        sign = 1;
      }
      base = 10;
      goto number;
    case 'u':
      base = 10;
      goto uns_number;
    case 'o':
      base = 8;
      goto uns_number;
    case 'x':
      /* we don't implement 'x'; treat like 'X' */
    case 'X':
      base = 16;
uns_number:    /* an unsigned number */
      sign = 0;
      if (islong)
        num = va_arg(ap, unsigned long);
      else
        num = va_arg(ap, unsigned int);
number:      /* process a number */
      p = temp + TEMPSIZE - 1;
      *p = '\0';
      while (num) {
        *--p = digit[num % base];
        num /= base;
      }
      len = (temp + TEMPSIZE - 1) - p;
      if (prec < 0)
        prec = 1;
      if (sharp && *f == 'o' && prec <= len)
        prec = len + 1;
      pad = 0;
      if (width) {
        size = len;
        if (size < prec)
          size = prec;
        size += sign;
        pad = width - size;
        if (flushleft == 0) {
          while (--pad >= 0)
            outc(' ', dest);
        }
      }
      if (sign)
        outc('-', dest);
      prec -= len;
      while (--prec >= 0)
        outc('0', dest);
      while (*p)
        outc(*p++, dest);
      while (--pad >= 0)
        outc(' ', dest);
      break;
    case 's':
      p = va_arg(ap, char *);
      pad = 0;
      if (width) {
        len = strlen(p);
        if (prec >= 0 && len > prec)
          len = prec;
        pad = width - len;
        if (flushleft == 0) {
          while (--pad >= 0)
            outc(' ', dest);
        }
      }
      prec++;
      while (--prec != 0 && *p)
        outc(*p++, dest);
      while (--pad >= 0)
        outc(' ', dest);
      break;
    case 'c':
      c = va_arg(ap, int);
      outc(c, dest);
      break;
    default:
      outc(*f, dest);
      break;
    }
    f++;
  }
}

/*
 * Version of write which resumes after a signal is caught.
 */
int xxwrite(int fd, char *buf, int nbytes)
{
  int ntry;
  int i;
  int n;

  n = nbytes;
  ntry = 0;
  for (;;) {
    i = write(fd, buf, n);
    if (i > 0) {
      if ((n -= i) <= 0)
        return nbytes;
      buf += i;
      ntry = 0;
    } else if (i == 0) {
      if (++ntry > 10)
        return nbytes - n;
    } else if (errno != EINTR) {
      return -1;
    }
  }
  /*Not reached*/
  return 0;
}

//==================================================================================
//trap - commmand to trap signals.
/*
 * Sigmode records the current value of the signal handlers for the various
 * modes.  A value of zero means that the current handler is not known.
 * S_HARD_IGN indicates that the signal was ignored on entry to the shell,
 */

/*
 * The trap builtin.
 */
int trapcmd(int argc, char **argv)
{
  char *action;
  char **ap;
  int signo;

  if (argc <= 1) {
    for (signo = 0 ; signo <= MAXSIG ; signo++) {
      if (trap[signo] != NULL)
        out1fmt("%d: %s\n", signo, trap[signo]);
    }
    return 0;
  }
  ap = argv + 1;
  if (is_number(*ap))
    action = NULL;
  else
    action = *ap++;
  while (*ap) {
    if ((signo = number(*ap)) < 0 || signo > MAXSIG)
      sh_error("%s: bad trap", *ap);
    INTOFF;
    if (action)
      action = savestr(action);
    if (trap[signo])
      ckfree(trap[signo]);
    trap[signo] = action;
    if (signo != 0)
      setsignal(signo);
    INTON;
    ap++;
  }
  return 0;
}

/*
 * Clear traps on a fork.
 */
void clear_traps()
{
  char **tp;

  for (tp = trap ; tp <= &trap[MAXSIG] ; tp++) {
    if (*tp && **tp) {  /* trap not NULL or SIG_IGN */
      INTOFF;
      ckfree(*tp);
      *tp = NULL;
      if (tp != &trap[0])
        setsignal(tp - trap);
      INTON;
    }
  }
}

/*
 * Set the signal handler for the specified signal.  The routine figures
 * out what it should be set to.
 */
int setsignal(int signo)
{
  int action;
  sig_t sigact = 0;
  char *t;
  extern void onsig();

  if ((t = trap[signo]) == NULL)
    action = S_DFL;
  else if (*t != '\0')
    action = S_CATCH;
  else
    action = S_IGN;
  if (rootshell && action == S_DFL) {
    switch (signo) {
    case SIGINT:
      if (iflag)
        action = S_CATCH;
      break;
    case SIGQUIT:
#ifdef DEBUG
      {
      extern int debug;

      if (debug)
        break;
      }
#endif
      /* FALLTHROUGH */
    case SIGTERM:
      if (iflag)
        action = S_IGN;
      break;
#if JOBS
    case SIGTSTP:
    case SIGTTOU:
      if (jflag)
        action = S_IGN;
      break;
#endif
    }
  }
  t = &sigmode[signo - 1];
  if (*t == 0) {  /* current setting unknown */
    /*
     * There is a race condition here if action is not S_IGN.
     * A signal can be ignored that shouldn't be.
     */
    if ((int)(sigact = signal(signo, SIG_IGN)) == -1)
      sh_error("Signal system call failed");
    if (sigact == SIG_IGN) {
      *t = S_HARD_IGN;
    } else {
      *t = S_IGN;
    }
  }
  if (*t == S_HARD_IGN || *t == action)
    return 0;
  switch (action) {
    case S_DFL:  sigact = SIG_DFL;  break;
    case S_CATCH:    sigact = onsig;    break;
    case S_IGN:  sigact = SIG_IGN;  break;
  }
  *t = action;
  return (int)signal(signo, sigact);
}

/*
 * Ignore a signal.
 */
void ignoresig(int signo)
{
  if (sigmode[signo - 1] != S_IGN && sigmode[signo - 1] != S_HARD_IGN) {
    signal(signo, SIG_IGN);
  }
  sigmode[signo - 1] = S_HARD_IGN;
}

/*
 * Signal handler.
 */
void onsig(int signo)
{
  struct termios set;
  tcgetattr(0,&set);
  set.c_lflag |= (ICANON | ECHO);
  tcsetattr(0, TCSANOW, &set);
  signal(signo, onsig);
  if (signo == SIGINT && trap[SIGINT] == NULL) {
    onint();
    return;
  }
  gotsig[signo - 1] = 1;
  pendingsigs++;
}

/*
 * Called to execute a trap.  Perhaps we should avoid entering new trap
 * handlers while we are executing a trap handler.
 */
void dotrap()
{
  int i;
  int savestatus;

  for (;;) {
    for (i = 1 ; ; i++) {
      if (gotsig[i - 1])
        break;
      if (i >= MAXSIG)
        goto done;
    }
    gotsig[i - 1] = 0;
    savestatus=exitstatus;
    evalstring(trap[i]);
    exitstatus=savestatus;
  }
done:
  pendingsigs = 0;
}

/*
 * Controls whether the shell is interactive or not.
 */
void setinteractive(int on)
{
  if (on == is_interactive)
    return;
  setsignal(SIGINT);
  setsignal(SIGQUIT);
  setsignal(SIGTERM);
  is_interactive = on;
}

#if DEBUG == 2
int debug = 1;
#else
int debug = 0;
#endif

int trace(fmt, a1, a2, a3, a4, a5, a6, a7, a8)
  char *fmt;
{
#ifdef DEBUG
  if (tracefile == NULL)
    return -1;
  fprintf(tracefile, fmt, a1, a2, a3, a4, a5, a6, a7, a8);
  if (strchr(fmt, '\n'))
    fflush(tracefile);
#endif
  return 0;
}
/*
 * Called to exit the shell.
 */
void exitshell(int status)
{
  struct jmploc loc1, loc2;
  char *p;

  save_history();
  TRACE(("exitshell(%d) pid=%d\n", status, getpid()));
  if (setjmp(loc1.loc))  goto l1;
  if (setjmp(loc2.loc))  goto l2;
  handler = &loc1;
  if ((p = trap[0]) != NULL && *p != '\0') {
    trap[0] = NULL;
    evalstring(p);
  }
l1:   handler = &loc2;      /* probably unnecessary */
  flushall();
#if JOBS
  setjobctl(0);
#endif
l2:   exit(status);
}

//==================================================================================
//show - used for debug printing.
#ifdef DEBUG
static void shtree();
static void shcmd();
static void sharg();
static void indent();

void showtree(union node *n)
{
  trputs("showtree called\n");
  shtree(n, 1, NULL, stdout);
}

static void shtree(union node *n, int ind, char *pfx, FILE *fp)
{
  struct nodelist *lp;
  char *s;

  indent(ind, pfx, fp);
  switch(n->type) {
  case NSEMI:
    s = "; ";
    goto binop;
  case NAND:
    s = " && ";
    goto binop;
  case NOR:
    s = " || ";
binop:
    shtree(n->nbinary.ch1, ind, NULL, fp);
     /*  if (ind < 0) */
      fputs(s, fp);
    shtree(n->nbinary.ch2, ind, NULL, fp);
    break;
  case NCMD:
    shcmd(n, fp);
    if (ind >= 0)
      putc('\n', fp);
    break;
  case NPIPE:
    for (lp = n->npipe.cmdlist ; lp ; lp = lp->next) {
      shcmd(lp->n, fp);
      if (lp->next)
        fputs(" | ", fp);
    }
    if (n->npipe.backgnd)
      fputs(" &", fp);
    if (ind >= 0)
      putc('\n', fp);
    break;
  default:
    fprintf(fp, "<node type %d>", n->type);
    if (ind >= 0)
      putc('\n', fp);
    break;
  }
}

static void shcmd(union node *cmd, FILE *fp)
{
  union node *np;
  int first;
  char *s = NULL;
  int dftfd = 0;

  first = 1;
  for (np = cmd->ncmd.args ; np ; np = np->narg.next) {
    if (! first)
      putchar(' ');
    sharg(np, fp);
    first = 0;
  }
  for (np = cmd->ncmd.redirect ; np ; np = np->nfile.next) {
    if (! first)
      putchar(' ');
    switch (np->nfile.type) {
      case NTO:  s = ">";  dftfd = 1; break;
      case NAPPEND:  s = ">>"; dftfd = 1; break;
      case NTOFD:  s = ">&"; dftfd = 1; break;
      case NFROM:  s = "<";  dftfd = 0; break;
      case NFROMFD:  s = "<&"; dftfd = 0; break;
    }
    if (np->nfile.fd != dftfd)
      fprintf(fp, "%d", np->nfile.fd);
    fputs(s, fp);
    if (np->nfile.type == NTOFD || np->nfile.type == NFROMFD) {
      fprintf(fp, "%d", np->ndup.dupfd);
    } else {
      sharg(np->nfile.fname, fp);
    }
    first = 0;
  }
}

static void sharg(union node *arg, FILE *fp)
{
  char *p;
  struct nodelist *bqlist;
  int subtype;

  if (arg->type != NARG) {
    printf("<node type %d>\n", arg->type);
    fflush(stdout);
    abort();
  }
  bqlist = arg->narg.backquote;
  for (p = arg->narg.text ; *p ; p++) {
    switch (*p) {
    case CTLESC:
      putc(*++p, fp);
      break;
    case CTLVAR:
      putc('$', fp);
      putc('{', fp);
      subtype = *++p;
      if (subtype == VSLENGTH)
        putc('#', fp);
      while (*p != '=')
        putc(*p++, fp);
      if (subtype & VSNUL)
        putc(':', fp);
      switch (subtype & VSTYPE) {
      case VSNORMAL:
        putc('}', fp);
        break;
      case VSMINUS:
        putc('-', fp);
        break;
      case VSPLUS:
        putc('+', fp);
        break;
      case VSQUESTION:
        putc('?', fp);
        break;
      case VSASSIGN:
        putc('=', fp);
        break;
      case VSTRIMLEFT:
        putc('#', fp);
        break;
      case VSTRIMLEFTMAX:
        putc('#', fp);
        putc('#', fp);
        break;
      case VSTRIMRIGHT:
        putc('%', fp);
        break;
      case VSTRIMRIGHTMAX:
        putc('%', fp);
        putc('%', fp);
        break;
      case VSLENGTH:
        break;
      default:
        printf("<subtype %d>", subtype);
      }
      break;
    case CTLENDVAR:
       putc('}', fp);
       break;
    case CTLBACKQ:
    case CTLBACKQ|CTLQUOTE:
      putc('$', fp);
      putc('(', fp);
      shtree(bqlist->n, -1, NULL, fp);
      putc(')', fp);
      break;
    default:
      putc(*p, fp);
      break;
    }
  }
}

static void indent(int amount, char *pfx, FILE *fp)
{
  int i;

  for (i = 0 ; i < amount ; i++) {
    if (pfx && i == amount - 1)
      fputs(pfx, fp);
    putc('\t', fp);
  }
}
#endif

/*
 * Debugging stuff.
 */
void trputc(int c)
{
#ifdef DEBUG
  if (tracefile == NULL)
    return;
  putc(c, tracefile);
  if (c == '\n')
    fflush(tracefile);
#endif
}

int trputs(char *s)
{
#ifdef DEBUG
  if (tracefile == NULL)
    return -1;
  fputs(s, tracefile);
  if (strchr(s, '\n'))
    fflush(tracefile);
#endif
  return 0;
}

void trstring(char *s)
{
  register char *p;
  char c;

#ifdef DEBUG
  if (tracefile == NULL)
    return;
  putc('"', tracefile);
  for (p = s ; *p ; p++) {
    switch (*p) {
    case '\n':  c = 'n';  goto backslash;
    case '\t':  c = 't';  goto backslash;
    case '\r':  c = 'r';  goto backslash;
    case '"':  c = '"';  goto backslash;
    case '\\':  c = '\\';  goto backslash;
    case CTLESC:  c = 'e';  goto backslash;
    case CTLVAR:  c = 'v';  goto backslash;
    case CTLVAR+CTLQUOTE:  c = 'V';  goto backslash;
    case CTLBACKQ:  c = 'q';  goto backslash;
    case CTLBACKQ+CTLQUOTE:  c = 'Q';  goto backslash;
backslash:    putc('\\', tracefile);
      putc(c, tracefile);
      break;
    default:
      if (*p >= ' ' && *p <= '~')
        putc(*p, tracefile);
      else {
        putc('\\', tracefile);
        putc(*p >> 6 & 03, tracefile);
        putc(*p >> 3 & 07, tracefile);
        putc(*p & 07, tracefile);
      }
      break;
    }
  }
  putc('"', tracefile);
#endif
}

void trargs(char **ap)
{
#ifdef DEBUG
  if (tracefile == NULL)
    return;
  while (*ap) {
    trstring(*ap++);
    if (*ap)
      putc(' ', tracefile);
    else
      putc('\n', tracefile);
  }
  fflush(tracefile);
#endif
}

void opentrace()
{
  char s[100];
  char *p;
  char *getenv();

#ifdef DEBUG
  if (!debug)
    return;
  if ((p = getenv("HOME")) == NULL) {
    if (getuid() == 0)
      p = "/";
    else
      p = "/tmp";
  }
  scopy(p, s);
  strcat(s, "/trace");
  if ((tracefile = fopen(s, "a")) == NULL) {
    fprintf(stderr, "Can't open %s\n", s);
    return;
  }
#ifndef O_APPEND
  {
    int flags;
    if ((flags = fcntl(fileno(tracefile), F_GETFL, 0)) >= 0)
      fcntl(fileno(tracefile), F_SETFL, flags | O_APPEND);
  }
#endif
  fputs("\nTracing started.\n", tracefile);
  fflush(tracefile);
#endif
}
//==================================================================================
//redir - Code for dealing with input/output redirection.
/*
 * Process a list of redirection commands.  If the REDIR_PUSH flag is set,
 * old file descriptors are stashed away so that the redirection can be
 * undone by calling popredir.  If the REDIR_BACKQ flag is set, then the
 * standard output, and the standard error if it becomes a duplicate of
 * stdout, is saved in memory.
 */
STATIC void redirect(union node *redir, int flags)
{
  union node *n;
  struct redirtab *sv = NULL;
  int i;
  int fd;
  char memory[10];    /* file descriptors to write to memory */

  for (i = 10 ; --i >= 0 ; )
    memory[i] = 0;
  memory[1] = flags & REDIR_BACKQ;
  if (flags & REDIR_PUSH) {
    sv = ckmalloc(sizeof (struct redirtab));
    for (i = 0 ; i < 10 ; i++)
      sv->renamed[i] = EMPTY;
    sv->next = redirlist;
    redirlist = sv;
  }
  for (n = redir ; n ; n = n->nfile.next) {
    fd = n->nfile.fd;
    if ((flags & REDIR_PUSH) && sv->renamed[fd] == EMPTY) {
      INTOFF;
      if ((i = copyfd(fd, 10)) != EMPTY) {
        sv->renamed[fd] = i;
        close(fd);
      }
      INTON;
      if (i == EMPTY)
        sh_error("Out of file descriptors");
    } else {
      close(fd);
    }
    if (fd == 0)
      fd0_redirected++;
    openredirect(n, memory);
  }
  if (memory[1])
    out1 = &memout;
  if (memory[2])
    out2 = &memout;
}

static int noclobber(const char *file_name)
{
  int fd = -1;
  struct stat sbuf;

  //If the file exists and is a regular file, set the error number and return.
  fd = stat(file_name, &sbuf);
  if( (fd == 0) && (S_ISREG(sbuf.st_mode)) ) {
    errno = EEXIST;
    return -1;
  }

  if(fd != 0) {
    fd = open(file_name, O_WRONLY|O_CREAT|O_EXCL, 0666);
    return fd;
  }
  return fd;
}

STATIC void openredirect(union node *redir, char memory[10])
{
  int fd = redir->nfile.fd;
  char *fname;
  int f;

  /*
   * We suppress interrupts so that we won't leave open file
   * descriptors around.  This may not be such a good idea because
   * an open of a device or a fifo can block indefinitely.
   */
  INTOFF;
  memory[fd] = 0;
  switch (redir->nfile.type) {
  case NFROM:
    fname = redir->nfile.expfname;
    if ((f = open(fname, O_RDONLY)) < 0)
      sh_error("cannot open %s: %s", fname, errmsg(errno, E_OPEN));
movefd:
    if (f != fd) {
      copyfd(f, fd);
      close(f);
    }
    break;
  case NTO:
    /* Take care of noclobber mode. */
    if(Cflag) {
      fname = redir->nfile.expfname;
      if( (f = noclobber(fname)) < 0)
        sh_error("cannot create %s: %s", fname, strerror(errno));
      goto movefd;
    }
    fname = redir->nfile.expfname;
#ifdef O_CREAT
    if ((f = open(fname, O_WRONLY|O_CREAT|O_TRUNC, 0666)) < 0)
      sh_error("cannot create %s: %s", fname, errmsg(errno, E_CREAT));
#else
    if ((f = creat(fname, 0666)) < 0)
      sh_error("cannot create %s: %s", fname, errmsg(errno, E_CREAT));
#endif
    goto movefd;
  case NAPPEND:
    fname = redir->nfile.expfname;
#ifdef O_APPEND
    if ((f = open(fname, O_WRONLY|O_CREAT|O_APPEND, 0666)) < 0)
      sh_error("cannot create %s: %s", fname, errmsg(errno, E_CREAT));
#else
    if ((f = open(fname, O_WRONLY)) < 0
     && (f = creat(fname, 0666)) < 0)
      sh_error("cannot create %s: %s", fname, errmsg(errno, E_CREAT));
    lseek(f, 0L, 2);
#endif
    goto movefd;
  case NTOFD:
  case NFROMFD:
    if (redir->ndup.dupfd >= 0) {  /* if not ">&-" */
      if (memory[redir->ndup.dupfd])
        memory[fd] = 1;
      else
        copyfd(redir->ndup.dupfd, fd);
    }
    break;
  case NHERE:
  case NXHERE:
    f = openhere(redir);
    goto movefd;
  default:
    abort();
  }
  INTON;
}

/*
 * Handle here documents.  Normally we fork off a process to write the
 * data to a pipe.  If the document is short, we can stuff the data in
 * the pipe without forking.
 */
STATIC int openhere(union node *redir)
{
  int pip[2];
  int len = 0;

  if (pipe(pip) < 0)
    sh_error("Pipe call failed");
  if (redir->type == NHERE) {
    len = strlen(redir->nhere.doc->narg.text);
    if (len <= PIPESIZE) {
      xxwrite(pip[1], redir->nhere.doc->narg.text, len);
      goto out;
    }
  }
  if (forkshell((struct job *)NULL, (union node *)NULL, FORK_NOJOB) == 0) {
    close(pip[0]);
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
#ifdef SIGTSTP
    signal(SIGTSTP, SIG_IGN);
#endif
    signal(SIGPIPE, SIG_DFL);
    if (redir->type == NHERE)
      xxwrite(pip[1], redir->nhere.doc->narg.text, len);
    else
      expandhere(redir->nhere.doc, pip[1]);
    exit(0);
  }
out:
  close(pip[1]);
  return pip[0];
}

/*
 * Undo the effects of the last redirection.
 */
void popredir()
{
  register struct redirtab *rp = redirlist;
  int i;

  for (i = 0 ; i < 10 ; i++) {
    if (rp->renamed[i] != EMPTY) {
      if (i == 0)
        fd0_redirected--;
      close(i);
      if (rp->renamed[i] >= 0) {
        copyfd(rp->renamed[i], i);
        close(rp->renamed[i]);
      }
    }
  }
  INTOFF;
  redirlist = rp->next;
  ckfree(rp);
  INTON;
}

/*
 * Discard all saved file descriptors.
 */
void clearredir()
{
  register struct redirtab *rp;
  int i;

  for (rp = redirlist ; rp ; rp = rp->next) {
    for (i = 0 ; i < 10 ; i++) {
      if (rp->renamed[i] >= 0) {
        close(rp->renamed[i]);
      }
      rp->renamed[i] = EMPTY;
    }
  }
}

/*
 * Copy a file descriptor, like the F_DUPFD option of fcntl.  Returns -1
 * if the source file descriptor is closed, EMPTY if there are no unused
 * file descriptors left.
 */
int copyfd(int from, int to)
{
#ifdef F_DUPFD
  int newfd;

  newfd = fcntl(from, F_DUPFD, to);
  if (newfd < 0 && errno == EMFILE)
    return EMPTY;
  return newfd;
#else
  char toclose[32];
  int i;
  int newfd;
  int e;

  for (i = 0 ; i < to ; i++)
    toclose[i] = 0;
  INTOFF;
  while ((newfd = dup(from)) >= 0 && newfd < to)
    toclose[newfd] = 1;
  e = errno;
  for (i = 0 ; i < to ; i++) {
    if (toclose[i])
      close(i);
  }
  INTON;
  if (newfd < 0 && e == EMFILE)
    return EMPTY;
  return newfd;
#endif
}

/* Return true if fd 0 has already been redirected at least once.  */
int fd0_redirected_p ()
{
  return fd0_redirected != 0;
}

//==================================================================================
//Shell command parser.
/*
 * Read and parse a command.  Returns NEOF on end of file.  (NULL is a
 * valid parse tree indicating a blank line.)
 */
union node *parsecmd(int interact)
{
  int t;

  doprompt = interact;
  if (doprompt)
    putprompt(ps1val());
  needprompt = 0;
  if ((t = readtoken()) == TEOF)
    return NEOF;
  if (t == TNL)
    return NULL;
  tokpushback++;
  return list(1);
}

STATIC union node *list(int nlflag)
{
  union node *n1, *n2, *n3;

  checkkwd = 2;
  if (nlflag == 0 && tokendlist[peektoken()])
    return NULL;
  n1 = andor();
  for (;;) {
    switch (readtoken()) {
    case TBACKGND:
      if (n1->type == NCMD || n1->type == NPIPE) {
        n1->ncmd.backgnd = 1;
      } else if (n1->type == NREDIR) {
        n1->type = NBACKGND;
      } else {
        n3 = (union node *)stalloc(sizeof (struct nredir));
        n3->type = NBACKGND;
        n3->nredir.n = n1;
        n3->nredir.redirect = NULL;
        n1 = n3;
      }
      goto tsemi;
    case TNL:
      tokpushback++;
      /* fall through */
tsemi:    case TSEMI:
      if (readtoken() == TNL) {
        parseheredoc();
        if (nlflag)
          return n1;
      } else {
        tokpushback++;
      }
      checkkwd = 2;
      if (tokendlist[peektoken()])
        return n1;
      n2 = andor();
      n3 = (union node *)stalloc(sizeof (struct nbinary));
      n3->type = NSEMI;
      n3->nbinary.ch1 = n1;
      n3->nbinary.ch2 = n2;
      n1 = n3;
      break;
    case TEOF:
      if (heredoclist)
        parseheredoc();
      else
        pungetc();    /* push back EOF on input */
      return n1;
    default:
      if (nlflag)
        synexpect(-1);
      tokpushback++;
      return n1;
    }
  }
  /*not reached*/
  return NULL;
}

STATIC union node *andor()
{
  union node *n1, *n2, *n3;
  int t;

  n1 = pipeline();
  for (;;) {
    if ((t = readtoken()) == TAND) {
      t = NAND;
    } else if (t == TOR) {
      t = NOR;
    } else {
      tokpushback++;
      return n1;
    }
    n2 = pipeline();
    n3 = (union node *)stalloc(sizeof (struct nbinary));
    n3->type = t;
    n3->nbinary.ch1 = n1;
    n3->nbinary.ch2 = n2;
    n1 = n3;
  }
  /*not reached*/
  return NULL;
}

STATIC union node *pipeline()
{
  union node *n1, *n2, *pipenode;
  struct nodelist *lp, *prev;

  int negate;

  TRACE(("pipeline: entered\n"));

  negate = 0;
  checkkwd = 2;
  while (readtoken() == TNOT) {
      TRACE(("pipeline: TNOT recognized\n"));
      negate = !negate;
  }
  tokpushback++;

  n1 = command();
  if (readtoken() == TPIPE) {
    pipenode = (union node *)stalloc(sizeof (struct npipe));
    pipenode->type = NPIPE;
    pipenode->npipe.backgnd = 0;
    lp = (struct nodelist *)stalloc(sizeof (struct nodelist));
    pipenode->npipe.cmdlist = lp;
    lp->n = n1;
    do {
      prev = lp;
      lp = (struct nodelist *)stalloc(sizeof (struct nodelist));
      lp->n = command();
      prev->next = lp;
    } while (readtoken() == TPIPE);
    lp->next = NULL;
    n1 = pipenode;
  }
  tokpushback++;
  if (negate) {
      TRACE(("negate pipeline\n"));
      n2 = (union node *)stalloc(sizeof (struct nnot));
      n2->type = NNOT;
      n2->nnot.com = n1;
      return n2;
  } else
    return n1;
}

STATIC union node *command()
{
  union node *n1 = NULL, *n2;
  union node *ap, **app;
  union node *cp, **cpp;
  union node *redir, **rpp;
  int t, negate = 0;

  checkkwd = 2;
  redir = 0;
  rpp = &redir;
  /* Check for redirection which may precede command */
  while (readtoken() == TREDIR) {
    *rpp = n2 = redirnode;
    rpp = &n2->nfile.next;
    parsefname();
  }
  tokpushback++;

  while (readtoken() == TNOT) {
      TRACE(("command: TNOT recognized\n"));
      negate = !negate;
  }
  tokpushback++;


  switch (readtoken()) {
  case TIF:
    n1 = (union node *)stalloc(sizeof (struct nif));
    n1->type = NIF;
    n1->nif.test = list(0);
    if (readtoken() != TTHEN)
      synexpect(TTHEN);
    n1->nif.ifpart = list(0);
    n2 = n1;
    while (readtoken() == TELIF) {
      n2->nif.elsepart = (union node *)stalloc(sizeof (struct nif));
      n2 = n2->nif.elsepart;
      n2->type = NIF;
      n2->nif.test = list(0);
      if (readtoken() != TTHEN)
        synexpect(TTHEN);
      n2->nif.ifpart = list(0);
    }
    if (lasttoken == TELSE)
      n2->nif.elsepart = list(0);
    else {
      n2->nif.elsepart = NULL;
      tokpushback++;
    }
    if (readtoken() != TFI)
      synexpect(TFI);
    checkkwd = 1;
    break;
  case TWHILE:
  case TUNTIL: {
    int got;
    n1 = (union node *)stalloc(sizeof (struct nbinary));
    n1->type = (lasttoken == TWHILE)? NWHILE : NUNTIL;
    n1->nbinary.ch1 = list(0);
    if ((got=readtoken()) != TDO) {
      TRACE(("expecting DO got %s %s\n", tokname[got], got == TWORD ? wordtext : ""));
      synexpect(TDO);
    }
    n1->nbinary.ch2 = list(0);
    if (readtoken() != TDONE)
      synexpect(TDONE);
    checkkwd = 1;
    break;
  }
  case TFOR:
    if (readtoken() != TWORD || quoteflag || ! goodname(wordtext))
      synerror("Bad for loop variable");
    n1 = (union node *)stalloc(sizeof (struct nfor));
    n1->type = NFOR;
    n1->nfor.var = wordtext;
    if (readtoken() == TWORD && ! quoteflag && equal(wordtext, "in")) {
      app = &ap;
      while (readtoken() == TWORD) {
        n2 = (union node *)stalloc(sizeof (struct narg));
        n2->type = NARG;
        n2->narg.text = wordtext;
        n2->narg.backquote = backquotelist;
        *app = n2;
        app = &n2->narg.next;
      }
      *app = NULL;
      n1->nfor.args = ap;
      /* A newline or semicolon is required here to end
         the list.  */
      if (lasttoken != TNL && lasttoken != TSEMI)
        synexpect(-1);
    } else {
#ifndef GDB_HACK
      static const char argvars[5] = {CTLVAR, VSNORMAL|VSQUOTE,
                   '@', '=', '\0'};
#endif
      n2 = (union node *)stalloc(sizeof (struct narg));
      n2->type = NARG;
      n2->narg.text = (char *)argvars;
      n2->narg.backquote = NULL;
      n2->narg.next = NULL;
      n1->nfor.args = n2;
      /* A newline or semicolon is optional here. Anything
         else gets pushed back so we can read it again.  */
      if (lasttoken != TNL && lasttoken != TSEMI)
        tokpushback++;
    }
    checkkwd = 2;
    if ((t = readtoken()) == TDO)
      t = TDONE;
    else if (t == TBEGIN)
      t = TEND;
    else
      synexpect(-1);
    n1->nfor.body = list(0);
    if (readtoken() != t)
      synexpect(t);
    checkkwd = 1;
    break;
  case TCASE:
    n1 = (union node *)stalloc(sizeof (struct ncase));
    n1->type = NCASE;
    if (readtoken() != TWORD)
      synexpect(TWORD);
    n1->ncase.expr = n2 = (union node *)stalloc(sizeof (struct narg));
    n2->type = NARG;
    n2->narg.text = wordtext;
    n2->narg.backquote = backquotelist;
    n2->narg.next = NULL;
    while (readtoken() == TNL);
    if (lasttoken != TWORD || ! equal(wordtext, "in"))
      synerror("expecting \"in\"");
    cpp = &n1->ncase.cases;

    noalias = 1;

    while (checkkwd = 2, readtoken() == TWORD) {
      *cpp = cp = (union node *)stalloc(sizeof (struct nclist));
      cp->type = NCLIST;
      app = &cp->nclist.pattern;
      for (;;) {
        *app = ap = (union node *)stalloc(sizeof (struct narg));
        ap->type = NARG;
        ap->narg.text = wordtext;
        ap->narg.backquote = backquotelist;
        if (readtoken() != TPIPE)
          break;
        app = &ap->narg.next;
        if (readtoken() != TWORD)
          synexpect(TWORD);
      }
      ap->narg.next = NULL;

      noalias = 0;

      if (lasttoken != TRP)
        synexpect(TRP);
      cp->nclist.body = list(0);
      if ((t = readtoken()) == TESAC)
        tokpushback++;
      else if (t != TENDCASE) {
        noalias = 0;
        synexpect(TENDCASE);
      }
      else {
          noalias = 1;
          checkkwd = 2;
          readtoken();
      }

      cpp = &cp->nclist.next;
    }
    *cpp = NULL;

    noalias = 0;

    if (lasttoken != TESAC)
      synexpect(TESAC);
    checkkwd = 1;
    break;
  case TLP:
    n1 = (union node *)stalloc(sizeof (struct nredir));
    n1->type = NSUBSHELL;
    n1->nredir.n = list(0);
    n1->nredir.redirect = NULL;
    if (readtoken() != TRP)
      synexpect(TRP);
    checkkwd = 1;
    break;
  case TBEGIN:
    n1 = list(0);
    if (readtoken() != TEND)
      synexpect(TEND);
    checkkwd = 1;
    break;
  /* Handle an empty command like other simple commands.  */
  case TNL:
  case TWORD:
    tokpushback++;
    n1 = simplecmd(rpp, redir);
    goto checkneg;    
  default:
    synexpect(-1);
  }

  /* Now check for redirection which may follow command */
  while (readtoken() == TREDIR) {
    *rpp = n2 = redirnode;
    rpp = &n2->nfile.next;
    parsefname();
  }
  tokpushback++;
  *rpp = NULL;
  if (redir) {
    if (n1->type != NSUBSHELL) {
      n2 = (union node *)stalloc(sizeof (struct nredir));
      n2->type = NREDIR;
      n2->nredir.n = n1;
      n1 = n2;
    }
    n1->nredir.redirect = redir;
  }
checkneg:
  if (negate) {
      TRACE(("negate command\n"));
      n2 = (union node *)stalloc(sizeof (struct nnot));
      n2->type = NNOT;
      n2->nnot.com = n1;
      return n2;
  }
  else
    return n1;
}

#define _name(c)    ((c) == '_' || isalpha((unsigned char)(c)))
#define _in_name(c)   ((c) == '_' || isalnum((unsigned char)(c)))
/*           
 * Return the pointer to the first char which is not part of a legal variable name
 * (a letter or underscore followed by letters, underscores, and digits).
 */          
static const char*   
endofname(const char *name)                                                                           
{            
  if (!_name(*name))
    return name;   
  while (*++name) {  
    if (!_in_name(*name))
      break;   
  }
  return name;
}
static int        
isassignment(const char *p)
{             
  const char *q = endofname(p);                                                                         
  if (p == q)     
    return 0;     
  return *q == '=';   
}




STATIC union node *simplecmd(union node **rpp, union node *redir)
{
  union node *args, **app;
  union node **orig_rpp = rpp;
  union node *n = NULL, *n2;
  int negate = 0;
  int savecheckkwd;
  int is_double_brackets = 0;
  /* If we don't have any redirections already, then we must reset
     rpp to be the address of the local redir variable.  */
  if (redir == 0)
    rpp = &redir;

  args = NULL;
  app = &args;
  /* We save the incoming value, because we need this for shell
     functions.  There can not be a redirect or an argument between
     the function name and the open parenthesis.  */
  orig_rpp = rpp;

  while (readtoken() == TNOT) {
      TRACE(("simplcmd: TNOT recognized\n"));
      negate = !negate;
  }
  tokpushback++;
  savecheckkwd = 1;

  for (;;) {
    int t = 0;
    checkkwd = savecheckkwd;
    t = readtoken();
    if ((t == TWORD) || (t == TAND || t == TOR)) {
      if (t == TAND || t == TOR) {
        if (!is_double_brackets) {
          tokpushback = 1;
          goto out;
        }
        wordtext = (char *) (t == TAND ? "-a" : "-o");
      }
      n = (union node *)stalloc(sizeof (struct narg));
      n->type = NARG;
      n->narg.text = wordtext;
      if (strcmp("[[", wordtext) == 0)
        is_double_brackets = 1;
      else if (strcmp("]]", wordtext) == 0)
        is_double_brackets = 0;
      n->narg.backquote = backquotelist;
      *app = n;
      app = &n->narg.next;
      if(!(savecheckkwd && isassignment(wordtext))) {
        savecheckkwd = 0;
      }
    } else if (lasttoken == TREDIR) {
      *rpp = n = redirnode;
      rpp = &n->nfile.next;
      parsefname();  /* read name of redirection file */
    } else if (lasttoken == TLP && app == &args->narg.next
            && rpp == orig_rpp) {
      /* We have a function */
      if (readtoken() != TRP)
        synexpect(TRP);
#ifdef notdef
      if (! goodname(n->narg.text))
        synerror("Bad function name");
#endif
      n->type = NDEFUN;
      n->narg.next = command();
      goto checkneg;      
    } else {
      tokpushback++;
      break;
    }
  }
out:
  *app = NULL;
  *rpp = NULL;
  n = (union node *)stalloc(sizeof (struct ncmd));
  n->type = NCMD;
  n->ncmd.backgnd = 0;
  n->ncmd.args = args;
  n->ncmd.redirect = redir;
checkneg:
  if (negate) {
      TRACE(("negate simplecmd\n"));
      n2 = (union node *)stalloc(sizeof (struct nnot));
      n2->type = NNOT;
      n2->nnot.com = n;
      return n2;
  }
  else
       return n;
}

STATIC void parsefname()
{
  union node *n = redirnode;

  if (readtoken() != TWORD)
    synexpect(-1);
  if (n->type == NHERE) {
    struct heredoc *here = heredoc;
    struct heredoc *p;
    int i;

    if (quoteflag == 0)
      n->type = NXHERE;
    TRACE(("Here document %d\n", n->type));
    if (here->striptabs) {
      while (*wordtext == '\t')
        wordtext++;
    }
    if (! noexpand(wordtext) || (i = strlen(wordtext)) == 0 || i > EOFMARKLEN)
      synerror("Illegal eof marker for << redirection");
    rmescapes(wordtext);
    here->eofmark = wordtext;
    here->next = NULL;
    if (heredoclist == NULL)
      heredoclist = here;
    else {
      for (p = heredoclist ; p->next ; p = p->next);
      p->next = here;
    }
  } else if (n->type == NTOFD || n->type == NFROMFD) {
    if (is_digit(wordtext[0]))
      n->ndup.dupfd = digit_val(wordtext[0]);
    else if (wordtext[0] == '-')
      n->ndup.dupfd = -1;
    else
      goto bad;
    if (wordtext[1] != '\0') {
bad:
      synerror("Bad fd number");
    }
  } else {
    n->nfile.fname = (union node *)stalloc(sizeof (struct narg));
    n = n->nfile.fname;
    n->type = NARG;
    n->narg.next = NULL;
    n->narg.text = wordtext;
    n->narg.backquote = backquotelist;
  }
}

/*
 * Input any here documents.
 */
STATIC void parseheredoc()
{
  struct heredoc *here;
  union node *n;

  while (heredoclist) {
    here = heredoclist;
    heredoclist = here->next;
    if (needprompt) {
      putprompt(ps2val());
      needprompt = 0;
    }
    readtoken1(pgetc(), here->here->type == NHERE? SQSYNTAX : DQSYNTAX,
        here->eofmark, here->striptabs);
    n = (union node *)stalloc(sizeof (struct narg));
    n->narg.type = NARG;
    n->narg.next = NULL;
    n->narg.text = wordtext;
    n->narg.backquote = backquotelist;
    here->here->nhere.doc = n;
  }
}

STATIC int peektoken()
{
  int t;

  t = readtoken();
  tokpushback++;
  return (t);
}

STATIC int readtoken()
{
  int t;
  struct alias *ap;
  int savecheckkwd = checkkwd;
#ifdef DEBUG
  int alreadyseen = tokpushback;
#endif
top:
  t = xxreadtoken();

  if (checkkwd) {
    /*
     * eat newlines
     */
    if (checkkwd == 2) {
      checkkwd = 0;
      while (t == TNL) {
        parseheredoc();
        t = xxreadtoken();
      }
    } else
      checkkwd = 0;
    /*
     * check for keywords
     */
    if (t == TWORD && !quoteflag) {
      register char *const *pp;

      for (pp = parsekwd; *pp; pp++) {
        if (**pp == *wordtext && equal(*pp, wordtext)) {
          lasttoken = t = pp - parsekwd + KWDOFFSET;
          TRACE(("keyword %s recognized\n", tokname[t]));          
          goto out;
        }
      }
      if(!noalias && (ap = lookupalias(wordtext, 1)) != NULL) {
          pushstring(ap->val, strlen(ap->val), ap);
          checkkwd = savecheckkwd;
          goto top;
      }
    }
out:
    checkkwd = (t == TNOT) ? savecheckkwd : 0;
  }
#ifdef DEBUG
  if (!alreadyseen)
    TRACE(("token %s %s\n", tokname[t], t == TWORD ? wordtext : ""));
  else
    TRACE(("reread token %s %s\n", tokname[t], t == TWORD ? wordtext : ""));
#endif
  return (t);
}

/*
 * Read the next input token.
 * If the token is a word, we set backquotelist to the list of cmds in
 *  backquotes.  We set quoteflag to true if any part of the word was
 *  quoted.
 * If the token is TREDIR, then we set redirnode to a structure containing
 *  the redirection.
 * In all cases, the variable startlinno is set to the number of the line
 *  on which the token starts.
 *
 * [Change comment:  here documents and internal procedures]
 * [Readtoken shouldn't have any arguments.  Perhaps we should make the
 *  word parsing code into a separate routine.  In this case, readtoken
 *  doesn't need to have any internal procedures, but parseword does.
 *  We could also make parseoperator in essence the main routine, and
 *  have parseword (readtoken1?) handle both words and redirection.]
 */
#define RETURN(token)  return lasttoken = token

STATIC int xxreadtoken()
{
  register int c;

  if (tokpushback) {
    tokpushback = 0;
    return lasttoken;
  }
  if (needprompt) {
    putprompt(ps2val());
    needprompt = 0;
  }
  startlinno = plinno;
  for (;;) {  /* until token or start of word found */
    c = pgetc_macro();
    if (c == ' ' || c == '\t')
      continue;    /* quick check for white space first */
    switch (c) {
    case ' ': case '\t':
      continue;
    case '#':
      while ((c = pgetc()) != '\n' && c != PEOF);
      pungetc();
      continue;
    case '\\':
      if (pgetc() == '\n') {
        startlinno = ++plinno;
        if (doprompt)
          putprompt(ps2val());
        continue;
      }
      pungetc();
      goto breakloop;
    case '\n':
      plinno++;
      needprompt = doprompt;
      RETURN(TNL);
    case PEOF:
      RETURN(TEOF);
    case '&':
      if (pgetc() == '&')
        RETURN(TAND);
      pungetc();
      RETURN(TBACKGND);
    case '|':
      if (pgetc() == '|')
        RETURN(TOR);
      pungetc();
      RETURN(TPIPE);
    case ';':
      if (pgetc() == ';')
        RETURN(TENDCASE);
      pungetc();
      RETURN(TSEMI);
    case '(':
      RETURN(TLP);
    case ')':
      RETURN(TRP);
    default:
      goto breakloop;
    }
  }
breakloop:
  return readtoken1(c, BASESYNTAX, (char *)NULL, 0);
#undef RETURN
}

/*
 * If eofmark is NULL, read a word or a redirection symbol.  If eofmark
 * is not NULL, read a here document.  In the latter case, eofmark is the
 * word which marks the end of the document and striptabs is true if
 * leading tabs should be stripped from the document.  The argument firstc
 * is the first character of the input token or document.
 *
 * Because C does not have internal subroutines, I have simulated them
 * using goto's to implement the subroutine linkage.  The following macros
 * will run code that appears at the end of readtoken1.
 */
#define CHECKEND()  {goto checkend; checkend_return:;}
#define PARSEREDIR()  {goto parseredir; parseredir_return:;}
#define PARSESUB()  {goto parsesub; parsesub_return:;}
#define PARSEBACKQOLD()  {oldstyle = 1; goto parsebackq; parsebackq_oldreturn:;}
#define PARSEBACKQNEW()  {oldstyle = 0; goto parsebackq; parsebackq_newreturn:;}

STATIC int readtoken1(int firstc, char const *syntax, char *eofmark, int striptabs)
{
  register int c = firstc;
  register char *out;
  int len;
  char line[EOFMARKLEN + 1];
  struct nodelist *bqlist;
  int quotef;
  int dblquote;
  int varnest;
  int oldstyle;

  startlinno = plinno;
  dblquote = 0;
  if (syntax == DQSYNTAX)
    dblquote = 1;
  quotef = 0;
  bqlist = NULL;
  varnest = 0;
  STARTSTACKSTR(out);
  loop: {  /* for each line, until end of word */
#if ATTY
    if (c == '\034' && doprompt
     && attyset() && ! equal(termval(), "emacs")) {
      attyline();
      if (syntax == BASESYNTAX)
        return readtoken();
      c = pgetc();
      goto loop;
    }
#endif
    CHECKEND();  /* set c to PEOF if at end of here document */
    for (;;) {  /* until end of line or end of word */
      CHECKSTRSPACE(3, out);  /* permit 3 calls to USTPUTC */
      switch(syntax[c]) {
      case CNL:  /* '\n' */
        if (syntax == BASESYNTAX)
          goto endword;  /* exit outer loop */
        USTPUTC(c, out);
        plinno++;
        if (doprompt) {
          putprompt(ps2val());
        }
        c = pgetc();
        goto loop;    /* continue outer loop */
      case CWORD:
        USTPUTC(c, out);
        break;
      case CCTL:
        if (eofmark == NULL || dblquote)
          USTPUTC(CTLESC, out);
        USTPUTC(c, out);
        break;
      case CBACK:  /* backslash */
        c = pgetc();
        if (c == PEOF) {
          USTPUTC('\\', out);
          pungetc();
        } else if (c == '\n') {
          if (doprompt)
            putprompt(ps2val());
        } else {
          if (dblquote && c != '\\' && c != '`' && c != '$'
               && (c != '"' || eofmark != NULL))
            USTPUTC('\\', out);
          if (SQSYNTAX[c] == CCTL)
            USTPUTC(CTLESC, out);
          USTPUTC(c, out);
          quotef++;
        }
        break;
      case CSQUOTE:
        syntax = SQSYNTAX;
        break;
      case CDQUOTE:
        syntax = DQSYNTAX;
        dblquote = 1;
        break;
      case CENDQUOTE:
        if (eofmark) {
          USTPUTC(c, out);
        } else {
          syntax = BASESYNTAX;
          quotef++;
          dblquote = 0;
        }
        break;
      case CVAR:  /* '$' */
        PARSESUB();    /* parse substitution */
        break;
      case CENDVAR:  /* '}' */
        if (varnest > 0) {
          varnest--;
          USTPUTC(CTLENDVAR, out);
        } else {
          USTPUTC(c, out);
        }
        break;
      case CBQUOTE:  /* '`' */
        PARSEBACKQOLD();
        break;
      case CEOF:
        goto endword;    /* exit outer loop */
      default:
        if (varnest == 0)
          goto endword;  /* exit outer loop */
        USTPUTC(c, out);
      }
      c = pgetc_macro();
    }
  }
endword:
  if (syntax != BASESYNTAX && ! parsebackquote && eofmark == NULL)
    synerror("Unterminated quoted string");
  if (varnest != 0) {
    startlinno = plinno;
    synerror("Missing '}'");
  }
  USTPUTC('\0', out);
  len = out - stackblock();
  out = stackblock();
  if (eofmark == NULL) {
    if ((c == '>' || c == '<')
     && quotef == 0
     && len <= 2
     && (*out == '\0' || is_digit(*out))) {
      PARSEREDIR();
      return lasttoken = TREDIR;
    } else {
      pungetc();
    }
  }
  quoteflag = quotef;
  backquotelist = bqlist;
  grabstackblock(len);
  wordtext = out;
  return lasttoken = TWORD;
/* end of readtoken routine */

/*
 * Check to see whether we are at the end of the here document.  When this
 * is called, c is set to the first character of the next input line.  If
 * we are at the end of the here document, this routine sets the c to PEOF.
 */
checkend: {
  if (eofmark) {
    if (striptabs) {
      while (c == '\t')
        c = pgetc();
    }
    if (c == *eofmark) {
      if (pfgets(line, sizeof line) != NULL) {
        register char *p, *q;

        p = line;
        for (q = eofmark + 1 ; *q && *p == *q ; p++, q++);
        if (*p == '\n' && *q == '\0') {
          c = PEOF;
          plinno++;
          needprompt = doprompt;
        } else {          
           pushstring(line, strlen(line), NULL);
        }
      }
    }
  }
  goto checkend_return;
}

/*
 * Parse a redirection operator.  The variable "out" points to a string
 * specifying the fd to be redirected.  The variable "c" contains the
 * first character of the redirection operator.
 */
parseredir: {
  char fd = *out;
  union node *np;

  np = (union node *)stalloc(sizeof (struct nfile));
  if (c == '>') {
    np->nfile.fd = 1;
    c = pgetc();
    if (c == '>')
      np->type = NAPPEND;
    else if (c == '&')
      np->type = NTOFD;
    else {
      np->type = NTO;
      pungetc();
    }
  } else {  /* c == '<' */
    np->nfile.fd = 0;
    c = pgetc();
    if (c == '<') {
      if (sizeof (struct nfile) != sizeof (struct nhere)) {
        np = (union node *)stalloc(sizeof (struct nhere));
        np->nfile.fd = 0;
      }
      np->type = NHERE;
      heredoc = (struct heredoc *)stalloc(sizeof (struct heredoc));
      heredoc->here = np;
      if ((c = pgetc()) == '-') {
        heredoc->striptabs = 1;
      } else {
        heredoc->striptabs = 0;
        pungetc();
      }
    } else if (c == '&')
      np->type = NFROMFD;
    else {
      np->type = NFROM;
      pungetc();
    }
  }
  if (fd != '\0')
    np->nfile.fd = digit_val(fd);
  redirnode = np;
  goto parseredir_return;
}


/*
 * Parse a substitution.  At this point, we have read the dollar sign
 * and nothing else.
 */

parsesub: {
	int subtype;
	int typeloc;
	int flags;
	char *p;
	static const char types[] = "}-+?=";

	c = pgetc();
	if (c != '(' && c != OPENBRACE && !is_name(c) && !is_special(c)) {
		USTPUTC('$', out);
		pungetc();
  } else if (c == '(') {	/* $(command) or $((arith)) */
    PARSEBACKQNEW();
  } else {
		USTPUTC(CTLVAR, out);
		typeloc = out - stackblock();
		USTPUTC(VSNORMAL, out);
		subtype = VSNORMAL;
		if (c == OPENBRACE) {
			c = pgetc();
			if (c == '#') {
				if ((c = pgetc()) == CLOSEBRACE)
					c = '#';
				else
					subtype = VSLENGTH;
			}
			else
				subtype = 0;
		}
		if (is_name(c)) {
			do {
				STPUTC(c, out);
				c = pgetc();
			} while (is_in_name(c));
		} else if (is_digit(c)) {
			do {
				USTPUTC(c, out);
				c = pgetc();
			} while (is_digit(c));
		}
		else if (is_special(c)) {
			USTPUTC(c, out);
			c = pgetc();
		}
		else
badsub:			synerror("Bad substitution");

    if (c != '}' && subtype == VSLENGTH) {                                                                                                                                  
      goto badsub;
    }
		STPUTC('=', out);
		flags = 0;
		if (subtype == 0) {
			switch (c) {
			case ':':
				flags = VSNUL;
				c = pgetc();
				/*FALLTHROUGH*/
			default:
				p = strchr(types, c);
				if (p == NULL)
					goto badsub;
				subtype = p - types + VSNORMAL;
				break;
			case '%':
			case '#':
				{
					int cc = c;
					subtype = c == '#' ? VSTRIMLEFT :
							     VSTRIMRIGHT;
					c = pgetc();
					if (c == cc)
						subtype++;
					else
						pungetc();
					break;
				}
			}
		} else {
			pungetc();
    }
    if (dblquote)
      flags |= VSQUOTE;
    *(stackblock() + typeloc) = subtype | flags;
    if (subtype != VSNORMAL)
      varnest++;
#if 0
		if (ISDBLQUOTE() || arinest)
			flags |= VSQUOTE;
		*(stackblock() + typeloc) = subtype | flags;
		if (subtype != VSNORMAL) {
			varnest++;
			if (varnest >= maxnest) {
				dblquotep = ckrealloc(dblquotep, maxnest / 8);
				dblquotep[(maxnest / 32) - 1] = 0;
				maxnest += 32;
			}
		}
#endif
  }
  goto parsesub_return;
}

/*
 * Called to parse command substitutions.  Newstyle is set if the command
 * is enclosed inside $(...); nlpp is a pointer to the head of the linked
 * list of commands (passed by reference), and savelen is the number of
 * characters on the top of the stack which must be preserved.
 */
parsebackq: {
  struct nodelist **nlpp;
  int savepbq;
  union node *n;
  char *volatile str;
  struct jmploc jmploc;
  struct jmploc *volatile savehandler;
  int savelen;

  savepbq = parsebackquote;
  if (setjmp(jmploc.loc)) {
    if (str)
      ckfree(str);
    parsebackquote = 0;
    handler = savehandler;
    longjmp(handler->loc, 1);
  }
  INTOFF;
  str = NULL;
  savelen = out - stackblock();
  if (savelen > 0) {
    str = ckmalloc(savelen);
    bcopy(stackblock(), str, savelen);
  }
  savehandler = handler;
  handler = &jmploc;
  INTON;
  if (oldstyle) {
    /* We must read until the closing backquote, giving special
       treatment to some slashes, and then push the string and
       reread it as input, interpreting it normally.  */
    register char *out;
    register int c;
    int savelen;
    char *str = NULL;

    STARTSTACKSTR(out);
    while ((c = pgetc ()) != '`') {
      if (c == '\\') {
        c = pgetc ();
        if (c != '\\' && c != '`' && c != '$'
          && (!dblquote || c != '"'))
          STPUTC('\\', out);
      }
      STPUTC(c, out);
    }
    STPUTC('\0', out);
    savelen = out - stackblock();
    if (savelen > 0) {
      str = ckmalloc(savelen);
      bcopy(stackblock(), str, savelen);
    }
    setinputstring(str, 1);
  }
  nlpp = &bqlist;
  while (*nlpp)
    nlpp = &(*nlpp)->next;
  *nlpp = (struct nodelist *)stalloc(sizeof (struct nodelist));
  (*nlpp)->next = NULL;
  parsebackquote = oldstyle;
  n = list(0);
  if (!oldstyle && (readtoken() != TRP))
    synexpect(TRP);
  (*nlpp)->n = n;
  /* Start reading from old file again, and clear tokpushback since
     any pushed back token from the string is no longer relevant.  */
  if (oldstyle) {
    popfile();
    tokpushback = 0;
  }
  while (stackblocksize() <= savelen)
    growstackblock();
  STARTSTACKSTR(out);
  if (str) {
    bcopy(str, out, savelen);
    STADJUST(savelen, out);
    INTOFF;
    ckfree(str);
    str = NULL;
    INTON;
  }
  parsebackquote = savepbq;
  handler = savehandler;
  USTPUTC(CTLBACKQ + dblquote, out);
  if (oldstyle)
    goto parsebackq_oldreturn;
  else
    goto parsebackq_newreturn;
  }

} /* end of readtoken */


#if ATTY
/*
 * Called to process a command generated by atty.  We execute the line,
 * and catch any errors that occur so they don't propagate outside of
 * this routine.
 */
STATIC void attyline()
{
  char line[256];
  struct stackmark smark;
  struct jmploc jmploc;
  struct jmploc *volatile savehandler;

  if (pfgets(line, sizeof line) == NULL)
    return;        /* "can't happen" */
  if (setjmp(jmploc.loc)) {
    if (exception == EXERROR)
      out2str("\033]D\n");
    handler = savehandler;
    longjmp(handler->loc, 1);
  }
  savehandler = handler;
  handler = &jmploc;
  setstackmark(&smark);
  evalstring(line);
  popstackmark(&smark);
  handler = savehandler;
  doprompt = 1;
}

/*
 * Output a prompt for atty.  We output the prompt as part of the
 * appropriate escape sequence.
 */
STATIC void putprompt(char *s)
{
  register char *p;

  if (attyset() && ! equal(termval(), "emacs")) {
    if (strchr(s, '\7'))
      out2c('\7');
    out2str("\033]P1;");
    for (p = s ; *p ; p++) {
      if ((unsigned)(*p - ' ') <= '~' - ' ')
        out2c(*p);
    }
    out2c('\n');
  } else {
    out2str(s);
  }
}
#endif

/*
 * Returns true if the text contains nothing to expand (no dollar signs
 * or backquotes).
 */
STATIC int noexpand(char *text)
{
  register char *p;
  register char c;

  p = text;
  while ((c = *p++) != '\0') {
    if (c == CTLESC)
      p++;
    else if (BASESYNTAX[c] == CCTL)
      return 0;
  }
  return 1;
}

/*
 * Return true if the argument is a legal variable name (a letter or
 * underscore followed by zero or more letters, underscores, and digits).
 */
int goodname(char *name)
{
  register char *p;

  p = name;
  if (! is_name(*p))
    return 0;
  while (*++p) {
    if (! is_in_name(*p))
      return 0;
  }
  return 1;
}

/*
 * Called when an unexpected token is read during the parse.  The argument
 * is the token that is expected, or -1 if more than one type of token can
 * occur at this point.
 */
STATIC void synexpect(int token)
{
  char msg[64];

  if (token >= 0) {
    fmtstr(msg, 64, "%s unexpected (expecting %s)",
      tokname[lasttoken], tokname[token]);
  } else {
    fmtstr(msg, 64, "%s unexpected", tokname[lasttoken]);
  }
  synerror(msg);
}

STATIC void synerror(char *msg)
{
  if (commandname)
    outfmt(&errout, "%s: %d: ", commandname, startlinno);
  outfmt(&errout, "Syntax error: %s\n", msg);
  sh_error((char *)NULL);
}

//==================================================================================
//options - Process the shell command line arguments.
/*
 * Process the shell command line arguments.
 */
void procargs(int argc, char **argv)
{
  char *p;

  argptr = argv;
  if (argc > 0)
    argptr++;
  for (p = optval ; p < optval + sizeof optval - 1 ; p++)
    *p = 2;
  sh_options(1);
  if (*argptr == NULL && minusc == NULL)
    sflag = 1;
  if (iflag == 2 && sflag == 1 && isatty(0) && isatty(1))
    iflag = 1;
  if (jflag == 2)
    jflag = iflag;
  for (p = optval ; p < optval + sizeof optval - 1 ; p++)
    if (*p == 2)
      *p = 0;
  arg0 = argv[0];
  if (sflag == 0 && minusc == NULL) {
    commandname = arg0 = *argptr++;
    setinputfile(commandname, 0);
  }
  shellparam.p = argptr;
  /* assert(shellparam.malloc == 0 && shellparam.nparam == 0); */
  while (*argptr) {
    shellparam.nparam++;
    argptr++;
  }
  setinteractive(iflag);
  setjobctl(jflag);
  if(iflag) mflag = iflag;
  for (p = optval ; p < optval + sizeof optval - 1 ; p++)
    optlist[p - optval].val = *p;
}

static void set_opt_val(size_t i, int val)
{
  size_t j;
  int flag;

  if (val && (flag = optlist[i].opt_set)) {
      /* some options (eg vi/emacs) are mutually exclusive */
      for (j = 0; j < NOPTS; j++)
        if (optlist[j].opt_set == flag) {
          optlist[j].val = 0;
          optval[j] = optlist[j].val;
        }
  }
  optlist[i].val = val;
  optval[i] = optlist[i].val;
}

static int check_plus_minus(char *name, int val)
{
  int i;

  if (name == NULL) {
    if (val) {
      out1str("Current option settings\n");
      for (i = 0; i < NOPTS; i++) {
        if(i == 4 || i == 8)
          continue;
        out1fmt("%-16s%s\n", optlist[i].name, optlist[i].val ? "on" : "off");
      }
    } else {
      for (i = 0; i < NOPTS; i++) {
        if(i == 4 || i == 8)
          continue;
        out1fmt("set %co %s", "+-"[optlist[i].val], optlist[i].name);
        out1str("\n");
      }
    }
  } else {
    for (i = 0; i < NOPTS; i++)
      if (equal(name, optlist[i].name)) {
        set_opt_val(i, val);
        return 0;
      }
    sh_error("Illegal option -o %s", name);
  }
  return 0;
}
/*
 * Process shell options.  The global variable argptr contains a pointer
 * to the argument list; we advance it past the options.
 */
STATIC void sh_options(int cmdline)
{
  register char *p;
  int val = 0;
  int c;

  if (cmdline)
    minusc = NULL;
  while ((p = *argptr) != NULL) {
    argptr++;
    if ((c = *p++) == '-') {
      val = 1;
      if ((p[0] == '\0') || (p[0] == '-' && p[1] == '\0')) {
        if (!cmdline) {
          /* "-" means turn off -x and -v */
          if (p[0] == '\0')
            xflag = vflag = 0;
          /* "--" means reset params */
          else if (*argptr == NULL)
            setparam(argptr);
        }
        break;    /* "-" or  "--" terminates options */
      }
    } else if (c == '+') {
      val = 0;
    } else {
      argptr--;
      break;
    }
    while ((c = *p++) != '\0') {
      if (c == 'c' && cmdline) {
        char *q;
#ifdef NOHACK   /* removing this code allows sh -ce 'foo' for compat */
        if (*p == '\0')
#endif
          q = *argptr++;
        if (q == NULL || minusc != NULL)
          sh_error("Bad -c option");
        minusc = q;
#ifdef NOHACK
        break;
#endif
      }
      else if (c == 'o') {
        if (check_plus_minus(*argptr, val)) {
          /* it already printed err message */
          return; /* error */
        }
        if (*argptr)
          argptr++;
      } else {
        setoption(c, val);
      }
    }
    if (! cmdline)
      break;
    }
}


STATIC void setoption(char flag, int val)
{
  register char *p;

  if ((p = strchr(optchar, flag)) == NULL)
    sh_error("Illegal option -%c", flag);
  optval[p - optchar] = val;
  optlist[p - optchar].val = val;

}

/*
 * Set the shell parameters.
 */
void setparam(char **argv)
{
  char **newparam;
  char **ap;
  int nparam;

  for (nparam = 0 ; argv[nparam] ; nparam++);
  ap = newparam = ckmalloc((nparam + 1) * sizeof *ap);
  while (*argv) {
    *ap++ = savestr(*argv++);
  }
  *ap = NULL;
  freeparam(&shellparam);
  shellparam.malloc = 1;
  shellparam.nparam = nparam;
  shellparam.p = newparam;
  shellparam.optnext = NULL;
}

/*
 * Free the list of positional parameters.
 */
void freeparam(struct shparam *param)
{
  char **ap;

  if (param->malloc) {
    for (ap = param->p ; *ap ; ap++)
      ckfree(*ap);
    ckfree(param->p);
  }
}

/*
 * The shift builtin command.
 */
int shiftcmd(int argc, char **argv)
{
  int n;
  char **ap1, **ap2;

  n = 1;
  if (argc > 1)
    n = number(argv[1]);
  if (n > shellparam.nparam)
    n = shellparam.nparam;
  INTOFF;
  shellparam.nparam -= n;
  for (ap1 = shellparam.p ; --n >= 0 ; ap1++) {
    if (shellparam.malloc)
      ckfree(*ap1);
  }
  ap2 = shellparam.p;
  while ((*ap2++ = *ap1++) != NULL);
  shellparam.optnext = NULL;
  INTON;
  return 0;
}

/*
 * The set command builtin.
 */
int setcmd(int argc, char **argv)
{
  char *p;
  if (argc == 1)
    return showvarscmd(argc, argv);
  INTOFF;
  sh_options(0);
  setinteractive(iflag);
  setjobctl(jflag);
  for (p = optval ; p < optval + sizeof optval - 1 ; p++)
     *p = optlist[p - optval].val;
  if (*argptr != NULL) {
    setparam(argptr);
  }
  INTON;
  return 0;
}

/*
 * The getopts builtin.  Shellparam.optnext points to the next argument
 * to be processed.  Shellparam.optptr points to the next character to
 * be processed in the current argument.  If shellparam.optnext is NULL,
 * then it's the first time getopts has been called.
 */
int getoptscmd(int argc, char **argv)
{
  register char *p, *q;
  char c;
  char s[10];

  if (argc != 3)
    sh_error("Usage: getopts optstring var");
  if (shellparam.optnext == NULL) {
    shellparam.optnext = shellparam.p;
    shellparam.optptr = NULL;
  }
  if ((p = shellparam.optptr) == NULL || *p == '\0') {
    p = *shellparam.optnext;
    if (p == NULL || *p != '-' || *++p == '\0') {
atend:
      fmtstr(s, 10, "%d", shellparam.optnext - shellparam.p + 1);
      setvar("OPTIND", s, 0);
      shellparam.optnext = NULL;
      return 1;
    }
    shellparam.optnext++;
    if (p[0] == '-' && p[1] == '\0')    /* check for "--" */
      goto atend;
  }
  c = *p++;
  for (q = argv[1] ; *q != c ; ) {
    if (*q == '\0') {
      out1fmt("Illegal option -%c\n", c);
      c = '?';
      goto out;
    }
    if (*++q == ':')
      q++;
  }
  if (*++q == ':') {
    if (*p == '\0' && (p = *shellparam.optnext) == NULL) {
      out1fmt("No arg for -%c option\n", c);
      c = '?';
      goto out;
    }
    shellparam.optnext++;
    setvar("OPTARG", p, 0);
    p = NULL;
  }
out:
  shellparam.optptr = p;
  s[0] = c;
  s[1] = '\0';
  setvar(argv[2], s, 0);
  return 0;
}

/*
 * Standard option processing (a la getopt) for builtin routines.  The
 * only argument that is passed to nextopt is the option string; the
 * other arguments are unnecessary.  It return the character, or '\0' on
 * end of input.
 */
int nextopt(char *optstring)
{
  register char *p, *q;
  char c;

  if ((p = optptr) == NULL || *p == '\0') {
    p = *argptr;
    if (p == NULL || *p != '-' || *++p == '\0')
      return '\0';
    argptr++;
    if (p[0] == '-' && p[1] == '\0')    /* check for "--" */
      return '\0';
  }
  c = *p++;
  for (q = optstring ; *q != c ; ) {
    if (*q == '\0')
      sh_error("Illegal option -%c", c);
    if (*++q == ':')
      q++;
  }
  if (*++q == ':') {
    if (*p == '\0' && (p = *argptr++) == NULL)
      sh_error("No arg for -%c option", c);
    optarg = p;
    p = NULL;
  }
  optptr = p;
  return c;
}
//==================================================================================
//mystring - string manipulations.
/*
 * String functions.
 *
 *  equal(s1, s2)    Return true if strings are equal.
 *  scopy(from, to)    Copy a string.
 *  scopyn(from, to, n)  Like scopy, but checks for overflow.
 *  strchr(s, c)    Find first occurance of c in s.
 *  bcopy(from, to, n)  Copy a block of memory.
 *  number(s)    Convert a string of digits to an integer.
 *  is_number(s)    Return true if s is a string of digits.
 */

/*
 * scopyn - copy a string from "from" to "to", truncating the string
 *    if necessary.  "To" is always nul terminated, even if
 *    truncation is performed.  "Size" is the size of "to".
 */
void scopyn(register char const *from, register char *to, register int size)
{
  while (--size > 0) {
    if ((*to++ = *from++) == '\0')
      return;
  }
  *to = '\0';
}

/*
 * strchr - find first occurrence of a character in a string.
 */
#ifndef SYS5
char *mystrchr(char const *s, register char charwanted)
{
  register char const *scan;
  /*
   * The odd placement of the two tests is so NUL is findable.
   */
  for (scan = s ; *scan != charwanted ; )  /* ++ moved down for opt. */
    if (*scan++ == '\0')
      return NULL;
  return (char *)scan;
}
#endif

/*
 * bcopy - copy bytes
 *
 * This routine was derived from code by Henry Spencer.
 */
void mybcopy(const pointer src, pointer dst, register int length)
{
  register char *d = dst;
  register char *s = src;

  while (--length >= 0)
    *d++ = *s++;
}

/*
 * prefix -- see if pfx is a prefix of string.
 */
int prefix(register char const *pfx, register char const *string)
{
  while (*pfx) {
    if (*pfx++ != *string++)
      return 0;
  }
  return 1;
}

/*
 * Convert a string of digits to an integer, printing an error message on
 * failure.
 */
int number(const char *s)
{
  if (! is_number(s))
    error2("Illegal number", (char *)s);
  return atoi(s);
}

/*
 * Check for a valid number.  This should be elsewhere.
 */
static int is_number(register const char *p)
{
  do {
    if (! is_digit(*p))
      return 0;
  } while (*++p != '\0');
  return 1;
}

//==================================================================================
//jobs - job controls
#if JOBS
/*
 * Turn job control on and off.
 *
 * Note:  This code assumes that the third arg to ioctl is a character
 * pointer, which is true on Berkeley systems but not System V.  Since
 * System V doesn't have job control yet, this isn't a problem now.
 */
int jobctl;

void setjobctl(int on)
{
  if (on == jobctl || rootshell == 0)
    return;
  if (on) {
    do { /* while we are in the background */
      if (ioctl(2, TIOCGPGRP, (char *)&initialpgrp) < 0) {
        out2str("ash: can't access tty; job control turned off\n");
        jflag = 0;
        return;
      }
      if (initialpgrp == -1)
        initialpgrp = getpgid(0);
      else if (initialpgrp != getpgid(0)) {
        killpg(0, SIGTTIN);
        continue;
      }
    } while (0);
#ifdef OLD_TTY_DRIVER
    {
      int ldisc;
      if (ioctl(2, TIOCGETD, (char *)&ldisc) < 0 || ldisc != NTTYDISC) {
        out2str("ash: need new tty driver to run job control; job control turned off\n");
        jflag = 0;
        return;
      }
    }
#endif
    setsignal(SIGTSTP);
    setsignal(SIGTTOU);
    setpgid(0, rootpid);
    ioctl(2, TIOCSPGRP, (char *)&rootpid);
  } else { /* turning job control off */
    setpgid(0, initialpgrp);
    ioctl(2, TIOCSPGRP, (char *)&initialpgrp);
    setsignal(SIGTSTP);
    setsignal(SIGTTOU);
  }
  jobctl = on;
}
#endif

#if JOBS
int fgcmd(int argc, char **argv)
{
	struct job *jp;
	char *name = NULL;
	int jobno;
	int pgrp;
	int status;
	name = argv[1];
	if(name == NULL) {
		for (jobno = 1, jp = jobtab ; jobno <= njobs ; jobno++, jp++) {
			if (jp->used)
				continue;
			name = xmsprintf("%c%d",'%', jobno - 1);
			break;
		}
	}

	jp = getjob(name);
	if (jp->jobctl == 0)
		sh_error("job not created under job control");
	pgrp = jp->ps[0].pid;
	ioctl(2, TIOCSPGRP, (char *)&pgrp);
	restartjob(jp);
	INTOFF;
	status = waitforjob(jp);
	INTON;
	return status;
}

#if JOBS
static int jobno(const struct job *jp)
{
  return jp - jobtab + 1;
}
#endif

int bgcmd(int argc, char **argv)
{
  struct job *jp;

  do {
    jp = getjob(*++argv);
    if (jp->jobctl == 0)
      sh_error("job not created under job control");
    printf("[%d]  %s\n", jobno(jp), jp->ps[0].cmd);
    fflush(stdout);
    restartjob(jp);
  } while (--argc > 1);
  return 0;
}

STATIC void restartjob(struct job *jp)
{
  struct procstat *ps;
  int i;

  if (jp->state == JOBDONE)
    return;
  INTOFF;
  killpg(jp->ps[0].pid, SIGCONT);
  for (ps = jp->ps, i = jp->nprocs ; --i >= 0 ; ps++) {
    if ((ps->status & 0377) == 0177) {
      ps->status = -1;
      jp->state = 0;
    }
  }
  INTON;
}
#endif

int jobscmd(int argc, char **argv)
{
  showjobs(0);
  return 0;
}

/*
 * Print a list of jobs.  If "change" is nonzero, only print jobs whose
 * statuses have changed since the last call to showjobs.
 *
 * If the shell is interrupted in the process of creating a job, the
 * result may be a job structure containing zero processes.  Such structures
 * will be freed here.
 */
void showjobs(int change)
{
  int jobno;
  int procno;
  int i;
  struct job *jp;
  struct procstat *ps;
  int col;
  char s[64];

  TRACE(("showjobs(%d) called\n", change));
  while (dowait(0, (struct job *)NULL) > 0);
  for (jobno = 1, jp = jobtab ; jobno <= njobs ; jobno++, jp++) {
    if (! jp->used)
      continue;
    if (jp->nprocs == 0) {
      freejob(jp);
      continue;
    }
    if (change && ! jp->changed)
      continue;
    procno = jp->nprocs;
    for (ps = jp->ps ; ; ps++) {  /* for each process */
      if (ps == jp->ps)
        fmtstr(s, 64, "[%d] %d ", jobno, ps->pid);
      else
        fmtstr(s, 64, "  %d ", ps->pid);

      if(mflag)
      out1str(s);
      else
        memset(s, 0, 64);

      col = strlen(s);
      s[0] = '\0';
      if (ps->status == -1) {
        /* don't print anything */
      } else if ((ps->status & 0xFF) == 0) {
        fmtstr(s, 64, "Exit %d", ps->status >> 8);
      } else {
        i = ps->status;
#if JOBS
        if ((i & 0xFF) == 0177)
          i >>= 8;
#endif
        if ((i & 0x7F) <= MAXSIG && sigmesg[i & 0x7F])
          scopy(sigmesg[i & 0x7F], s);
        else
          fmtstr(s, 64, "Signal %d", i & 0x7F);
        if (i & 0x80)
          strcat(s, " (core dumped)");
      }
      if(mflag)
      out1str(s);
      else
        memset(s, 0, 64);
      col += strlen(s);
      if(mflag) {
      do {
        out1c(' ');
        col++;
      } while (col < 30);

      out1str(ps->cmd);
      out1c('\n');
      }
      if (--procno <= 0)
        break;
    }
    jp->changed = 0;
    if (jp->state == JOBDONE) {
      freejob(jp);
    }
  }
}

/*
 * Mark a job structure as unused.
 */
STATIC void freejob(struct job *jp)
{
  struct procstat *ps;
  int i;

  INTOFF;
  for (i = jp->nprocs, ps = jp->ps ; --i >= 0 ; ps++) {
    if (ps->cmd != nullstr)
      ckfree(ps->cmd);
  }
  if (jp->ps != &jp->ps0)
    ckfree(jp->ps);
  jp->used = 0;
#if JOBS
  if (curjob == jp - jobtab + 1)
    curjob = 0;
#endif
  INTON;
}

int waitcmd(int argc, char **argv)
{
  struct job *job;
  int status;
  struct job *jp;

  if (argc > 1) {
    job = getjob(argv[1]);
  } else {
    job = NULL;
  }
  for (;;) {  /* loop until process terminated or stopped */
    if (job != NULL) {
      if (job->state) {
        status = job->ps[job->nprocs - 1].status;
        if ((status & 0xFF) == 0)
          status = status >> 8 & 0xFF;
#if JOBS
        else if ((status & 0xFF) == 0177)
          status = (status >> 8 & 0x7F) + 128;
#endif
        else
          status = (status & 0x7F) + 128;
        if (! iflag)
          freejob(job);
        return status;
      }
    } else {
      for (jp = jobtab ; ; jp++) {
        if (jp >= jobtab + njobs) {  /* no running procs */
          return 0;
        }
        if (jp->used && jp->state == 0)
          break;
      }
    }
    dowait(1, (struct job *)NULL);
  }
}

int jobidcmd(int argc, char **argv)
{
  struct job *jp;
  int i;

  jp = getjob(argv[1]);
  for (i = 0 ; i < jp->nprocs ; ) {
    out1fmt("%d", jp->ps[i].pid);
    out1c(++i < jp->nprocs? ' ' : '\n');
  }
  return 0;
}

/*
 * Convert a job name to a job structure.
 */
STATIC struct job *getjob(char *name)
{
  int jobno;
  register struct job *jp;
  int pid;
  int i;

  if (name == NULL) {
#if JOBS
currentjob:
    if ((jobno = curjob) == 0 || jobtab[jobno - 1].used == 0)
      sh_error("No current job");
    return &jobtab[jobno - 1];
#else
    sh_error("No current job");
#endif
  } else if (name[0] == '%') {
    if (is_digit(name[1])) {
      jobno = number(name + 1);
      if (jobno > 0 && jobno <= njobs
       && jobtab[jobno - 1].used != 0)
        return &jobtab[jobno - 1];
#if JOBS
    } else if (name[1] == '%' && name[2] == '\0') {
      goto currentjob;
#endif
    } else {
      register struct job *found = NULL;
      for (jp = jobtab, i = njobs ; --i >= 0 ; jp++) {
        if (jp->used && jp->nprocs > 0
         && prefix(name + 1, jp->ps[0].cmd)) {
          if (found)
            sh_error("%s: ambiguous", name);
          found = jp;
        }
      }
      if (found)
        return found;
    }
  } else if (is_number(name)) {
    pid = number(name);
    for (jp = jobtab, i = njobs ; --i >= 0 ; jp++) {
      if (jp->used && jp->nprocs > 0
       && jp->ps[jp->nprocs - 1].pid == pid)
        return jp;
    }
  }
  sh_error("No such job: %s", name);
  return NULL;
}

static struct job *
growjobtab(void)  
{         
  size_t length;   
  ptrdiff_t offset;
  struct job *j_new, *j_old;

  length = njobs * sizeof(*j_new);
  j_old = jobtab;  
  j_new = ckrealloc(j_old, length + 4 * sizeof(*j_new));

  offset = (char *)j_new - (char *)j_old;
  if (offset) { 
    /* Relocate pointers */
    size_t l = length;

    j_old = (struct job *)((char *)j_old + l);
    while (l) { 
      l -= sizeof(*j_new);
      j_old--; 
#define joff(p) ((struct job *)((char *)(p) + l))   
#define jmove(p) (p) = (void *)((char *)(p) + offset)
      if (joff(j_new)->ps == &j_old->ps0)
        jmove(joff(j_new)->ps);
    }     
#undef joff     
#undef jmove    
  }       

  jobtab = j_new;  
  j_new = (struct job *)((char *)j_new + length);
  j_old = j_new + 3;  
  do {      
    j_old->used = 0;
  } while (--j_old >= j_new);
  return j_new;
}
/*
 * Return a new job structure,
 */
struct job *makejob(union node *node, int nprocs)
{
  int i;
  struct job *jp;

  INTOFF;
  for (i = njobs, jp = jobtab ; ; jp++) {
    if (--i < 0) {
      if (njobs == 0) {
        jobtab = ckmalloc(4 * sizeof jobtab[0]);
        memset(jobtab, 0, (4 * sizeof jobtab[0]));
      } else {
        jp = growjobtab();
      }
      jp = jobtab + njobs;
      njobs += 4;   
      break;
    }
    if (jp->used == 0)
      break;
  }
  memset(jp, 0, sizeof(struct job));
  jp->state = 0;
  jp->used = 1;
  jp->changed = 0;
  jp->nprocs = 0;
#if JOBS
  jp->jobctl = jobctl;
#endif
  if (nprocs > 1) {
    jp->ps = ckmalloc(nprocs * sizeof (struct procstat));
  } else {
    jp->ps = &jp->ps0;
  }
  INTON;
  TRACE(("makejob(0x%x, %d) returns %%%d\n", (int)node, nprocs, jp - jobtab + 1));
  return jp;
}

/*
 * Fork of a subshell.  If we are doing job control, give the subshell its
 * own process group.  Jp is a job structure that the job is to be added to.
 * N is the command that will be evaluated by the child.  Both jp and n may
 * be NULL.  The mode parameter can be one of the following:
 *  FORK_FG - Fork off a foreground process.
 *  FORK_BG - Fork off a background process.
 *  FORK_NOJOB - Like FORK_FG, but don't give the process its own
 *       process group even if job control is on.
 *
 * When job control is turned off, background processes have their standard
 * input redirected to /dev/null (except for the second and later processes
 * in a pipeline).
 */
int forkshell(struct job *jp, union node *n, int mode)
{
  int pid;
  int pgrp;

  TRACE(("forkshell(%%%d, 0x%x, %d) called\n", jp - jobtab, (int)n, mode));
  INTOFF;
  pid = fork();
  if (pid == -1) {
    TRACE(("Fork failed, errno=%d\n", errno));
    INTON;
    sh_error("Cannot fork");
  }
  if (pid == 0) {
    int wasroot;

    TRACE(("Child shell %d\n", getpid()));
    wasroot = rootshell;
    rootshell = 0;

    closescript();
    INTON;
    clear_traps();
#if JOBS
    jobctl = 0;    /* do job control only in root shell */
    if (wasroot && mode != FORK_NOJOB && jflag) {
      if (jp == NULL || jp->nprocs == 0)
        pgrp = getpid();
      else
        pgrp = jp->ps[0].pid;
      setpgid(0, pgrp);
      if ((mode == FORK_FG) && (pgrp > 0)) {
        /*** this causes superfluous TIOCSPGRPS ***/
        if (ioctl(2, TIOCSPGRP, (char *)&pgrp) < 0)
          sh_error("TIOCSPGRP failed, errno=%d\n", errno);
      }
      setsignal(SIGTSTP);
      setsignal(SIGTTOU);
    } else if (mode == FORK_BG) {
      ignoresig(SIGINT);
      ignoresig(SIGQUIT);
      if ((jp == NULL || jp->nprocs == 0)
        && ! fd0_redirected_p ()) {
        close(0);
        if (open("/dev/null", O_RDONLY) != 0)
          sh_error("Can't open /dev/null");
      }
    }
#else
    if (mode == FORK_BG) {
      ignoresig(SIGINT);
      ignoresig(SIGQUIT);
      if ((jp == NULL || jp->nprocs == 0)
        && ! fd0_redirected_p ()) {
        close(0);
        if (open("/dev/null", O_RDONLY) != 0)
          sh_error("Can't open /dev/null");
      }
    }
#endif
    if (wasroot && iflag) {
      setsignal(SIGINT);
      setsignal(SIGQUIT);
      setsignal(SIGTERM);
    }
    return pid;
  }
  if (rootshell && mode != FORK_NOJOB && jflag) {
    if (jp == NULL || jp->nprocs == 0)
      pgrp = pid;
    else
      pgrp = jp->ps[0].pid;
    setpgid(pid, pgrp);
  }
  if (mode == FORK_BG)
    backgndpid = pid;    /* set $! */
  if (jp) {
    struct procstat *ps = &jp->ps[jp->nprocs++];
    ps->pid = pid;
    ps->status = -1;
    ps->cmd = nullstr;
    if (iflag && rootshell && n)
      ps->cmd = commandtext(n);
  }
  INTON;
  TRACE(("In parent shell:  child = %d\n", pid));
  return pid;
}

/*
 * Wait for job to finish.
 *
 * Under job control we have the problem that while a child process is
 * running interrupts generated by the user are sent to the child but not
 * to the shell.  This means that an infinite loop started by an inter-
 * active user may be hard to kill.  With job control turned off, an
 * interactive user may place an interactive program inside a loop.  If
 * the interactive program catches interrupts, the user doesn't want
 * these interrupts to also abort the loop.  The approach we take here
 * is to have the shell ignore interrupt signals while waiting for a
 * forground process to terminate, and then send itself an interrupt
 * signal if the child process was terminated by an interrupt signal.
 * Unfortunately, some programs want to do a bit of cleanup and then
 * exit on interrupt; unless these processes terminate themselves by
 * sending a signal to themselves (instead of calling exit) they will
 * confuse this approach.
 */
int waitforjob(register struct job *jp)
{
#if JOBS
  int mypgrp = getpgid(0);
#endif
  int status;
  int st;

  INTOFF;
  TRACE(("waitforjob(%%%d) called\n", jp - jobtab + 1));
  while (jp->state == 0) {
    dowait(1, jp);
  }
#if JOBS
  if (jp->jobctl) {
    if (ioctl(2, TIOCSPGRP, (char *)&mypgrp) < 0)
      sh_error("TIOCSPGRP failed, errno=%d\n", errno);
  }
  if (jp->state == JOBSTOPPED)
    curjob = jp - jobtab + 1;
#endif
  status = jp->ps[jp->nprocs - 1].status;
  /* convert to 8 bits */
  if ((status & 0xFF) == 0)
    st = status >> 8 & 0xFF;
#if JOBS
  else if ((status & 0xFF) == 0177)
    st = (status >> 8 & 0x7F) + 128;
#endif
  else
    st = (status & 0x7F) + 128;
  if (! JOBS || jp->state == JOBDONE)
    freejob(jp);
  CLEAR_PENDING_INT;
  if ((status & 0x7F) == SIGINT)
    kill(getpid(), SIGINT);
  INTON;
  return st;
}

/*
 * Wait for a process to terminate.
 */
STATIC int dowait(int block, struct job *job)
{
  int pid;
  int status;
  struct procstat *sp;
  struct job *jp;
  struct job *thisjob;
  int done;
  int stopped;
  int core;

  TRACE(("dowait(%d) called\n", block));
  do {
    pid = waitproc(block, &status);
    TRACE(("wait returns %d, status=%d\n", pid, status));
  } while (pid == -1 && errno == EINTR);
  if (pid <= 0)
    return pid;
  INTOFF;
  thisjob = NULL;
  for (jp = jobtab ; jp < jobtab + njobs ; jp++) {
    if (jp->used) {
      done = 1;
      stopped = 1;
      for (sp = jp->ps ; sp < jp->ps + jp->nprocs ; sp++) {
        if (sp->pid == -1)
          continue;
        if (sp->pid == pid) {
          TRACE(("Changin status of proc %d from 0x%x to 0x%x\n", pid, sp->status, status));
          sp->status = status;
          thisjob = jp;
        }
        if (sp->status == -1)
          stopped = 0;
        else if ((sp->status & 0377) == 0177)
          done = 0;
      }
      if (stopped) {    /* stopped or done */
        int state = done? JOBDONE : JOBSTOPPED;
        if (jp->state != state) {
          TRACE(("Job %d: changing state from %d to %d\n", jp - jobtab + 1, jp->state, state));
          jp->state = state;
#if JOBS
          if (done && curjob == jp - jobtab + 1)
            curjob = 0;    /* no current job */
#endif
        }
      }
    }
  }
  INTON;
  if (! rootshell || ! iflag || (job && thisjob == job)) {
#if JOBS
    if ((status & 0xFF) == 0177)
      status >>= 8;
#endif
    core = status & 0x80;
    status &= 0x7F;
    if (status != 0 && status != SIGINT && status != SIGPIPE) {
      if (thisjob != job)
        outfmt(out2, "%d: ", pid);
#if JOBS
      if (status == SIGTSTP && rootshell && iflag)
        outfmt(out2, "%%%d ", job - jobtab + 1);
#endif
      if (status <= MAXSIG && sigmesg[status])
        out2str(sigmesg[status]);
      else
        outfmt(out2, "Signal %d", status);
      if (core)
        out2str(" - core dumped");
      out2c('\n');
      flushout(&errout);
    } else {
      TRACE(("Not printing status: status=%d\n", status));
    }
  } else {
    TRACE(("Not printing status, rootshell=%d, job=0x%x\n", rootshell, job));
    if (thisjob)
      thisjob->changed = 1;
  }
  return pid;
}

/*
 * Do a wait system call.  If job control is compiled in, we accept
 * stopped processes.  If block is zero, we return a value of zero
 * rather than blocking.
 *
 * System V doesn't have a non-blocking wait system call.  It does
 * have a SIGCLD signal that is sent to a process when one of it's
 * children dies.  The obvious way to use SIGCLD would be to install
 * a handler for SIGCLD which simply bumped a counter when a SIGCLD
 * was received, and have waitproc bump another counter when it got
 * the status of a process.  Waitproc would then know that a wait
 * system call would not block if the two counters were different.
 * This approach doesn't work because if a process has children that
 * have not been waited for, System V will send it a SIGCLD when it
 * installs a signal handler for SIGCLD.  What this means is that when
 * a child exits, the shell will be sent SIGCLD signals continuously
 * until is runs out of stack space, unless it does a wait call before
 * restoring the signal handler.  The code below takes advantage of
 * this (mis)feature by installing a signal handler for SIGCLD and
 * then checking to see whether it was called.  If there are any
 * children to be waited for, it will be.
 *
 * If neither SYSV nor BSD is defined, we don't implement nonblocking
 * waits at all.  In this case, the user will not be informed when
 * a background process until the next time she runs a real program
 * (as opposed to running a builtin command or just typing return),
 * and the jobs command may give out of date information.
 */

#ifdef SYSV
STATIC int gotsigchild;

STATIC int onsigchild()
{
  gotsigchild = 1;
}
#endif

STATIC int waitproc(int block, int *status)
{
#ifdef BSD
  int flags;

#if JOBS
  flags = WUNTRACED;
#else
  flags = 0;
#endif
  if (block == 0)
    flags |= WNOHANG;
  return wait3((int *)status, flags, (struct rusage *)NULL);
#else
#ifdef SYSV
  int (*save)();

  if (block == 0) {
    gotsigchild = 0;
    save = signal(SIGCLD, onsigchild);
    signal(SIGCLD, save);
    if (gotsigchild == 0)
      return 0;
  }
  return wait(status);
#else
  if (block == 0)
    return 0;
  return wait(status);
#endif
#endif
}

/*
 * Return a string identifying a command (to be printed by the
 * jobs command.
 */
STATIC char *commandtext(union node *n)
{
  char *name;

  cmdnextc = name = ckmalloc(50);
  cmdnleft = 50 - 4;
  cmdtxt(n);
  *cmdnextc = '\0';
  return name;
}

STATIC void cmdtxt(union node *n)
{
  union node *np;
  struct nodelist *lp;
  char *p;
  int i;
  char s[2];

  switch (n->type) {
  case NSEMI:
    cmdtxt(n->nbinary.ch1);
    cmdputs("; ");
    cmdtxt(n->nbinary.ch2);
    break;
  case NAND:
    cmdtxt(n->nbinary.ch1);
    cmdputs(" && ");
    cmdtxt(n->nbinary.ch2);
    break;
  case NOR:
    cmdtxt(n->nbinary.ch1);
    cmdputs(" || ");
    cmdtxt(n->nbinary.ch2);
    break;
  case NPIPE:
    for (lp = n->npipe.cmdlist ; lp ; lp = lp->next) {
      cmdtxt(lp->n);
      if (lp->next)
        cmdputs(" | ");
    }
    break;
  case NSUBSHELL:
    cmdputs("(");
    cmdtxt(n->nredir.n);
    cmdputs(")");
    break;
  case NREDIR:
  case NBACKGND:
    cmdtxt(n->nredir.n);
    break;
  case NIF:
    cmdputs("if ");
    cmdtxt(n->nif.test);
    cmdputs("; then ");
    cmdtxt(n->nif.ifpart);
    cmdputs("...");
    break;
  case NWHILE:
    cmdputs("while ");
    goto until;
  case NUNTIL:
    cmdputs("until ");
until:
    cmdtxt(n->nbinary.ch1);
    cmdputs("; do ");
    cmdtxt(n->nbinary.ch2);
    cmdputs("; done");
    break;
  case NFOR:
    cmdputs("for ");
    cmdputs(n->nfor.var);
    cmdputs(" in ...");
    break;
  case NCASE:
    cmdputs("case ");
    cmdputs(n->ncase.expr->narg.text);
    cmdputs(" in ...");
    break;
  case NDEFUN:
    cmdputs(n->narg.text);
    cmdputs("() ...");
    break;
  case NCMD:
    for (np = n->ncmd.args ; np ; np = np->narg.next) {
      cmdtxt(np);
      if (np->narg.next)
        cmdputs(" ");
    }
    for (np = n->ncmd.redirect ; np ; np = np->nfile.next) {
      cmdputs(" ");
      cmdtxt(np);
    }
    break;
  case NARG:
    cmdputs(n->narg.text);
    break;
  case NTO:
    p = ">";  i = 1;  goto redir;
  case NAPPEND:
    p = ">>";  i = 1;  goto redir;
  case NTOFD:
    p = ">&";  i = 1;  goto redir;
  case NFROM:
    p = "<";  i = 0;  goto redir;
  case NFROMFD:
    p = "<&";  i = 0;  goto redir;
redir:
    if (n->nfile.fd != i) {
      s[0] = n->nfile.fd + '0';
      s[1] = '\0';
      cmdputs(s);
    }
    cmdputs(p);
    if (n->type == NTOFD || n->type == NFROMFD) {
      s[0] = n->ndup.dupfd + '0';
      s[1] = '\0';
      cmdputs(s);
    } else {
      cmdtxt(n->nfile.fname);
    }
    break;
  case NHERE:
  case NXHERE:
    cmdputs("<<...");
    break;
  default:
    cmdputs("???");
    break;
  }
}

STATIC void cmdputs(char *s)
{
  register char *p, *q;
  register char c;
  int subtype = 0;

  if (cmdnleft <= 0)
    return;
  p = s;
  q = cmdnextc;
  while ((c = *p++) != '\0') {
    if (c == CTLESC)
      *q++ = *p++;
    else if (c == CTLVAR) {
      *q++ = '$';
      if (--cmdnleft > 0)
        *q++ = '{';
      subtype = *p++;
      if ((subtype & VSTYPE) == VSLENGTH)
        *q++ = '#';
    } else if (c == '=' && subtype != 0) {
      *q++ = "}-+?="[(subtype & VSTYPE) - VSNORMAL];
      subtype = 0;
    } else if (c == CTLENDVAR) {
      *q++ = '}';
    } else if((c == CTLBACKQ) | (c == CTLBACKQ+CTLQUOTE))
      cmdnleft++;    /* ignore it */
    else
      *q++ = c;
    if (--cmdnleft <= 0) {
      *q++ = '.';
      *q++ = '.';
      *q++ = '.';
      break;
    }
  }
  cmdnextc = q;
}

//==================================================================================
//memalloc - Like malloc, but returns an error when out of space.
pointer ckmalloc(int nbytes)
{
  register pointer p;
  pointer malloc();

  if ((p = malloc(nbytes)) == NULL)
    sh_error("Out of space");
  return p;
}

/*
 * Same for realloc.
 */
pointer ckrealloc(register pointer p, int nbytes)
{
  pointer realloc();

  if ((p = realloc(p, nbytes)) == NULL)
    sh_error("Out of space");
  return p;
}

/*
 * Make a copy of a string in safe storage.
 */
char *savestr(char *s)
{
  register char *p;

  p = ckmalloc(strlen(s) + 1);
  scopy(s, p);
  return p;
}

pointer stalloc(int nbytes)
{
  register char *p;

  nbytes = ALIGN(nbytes);
  if (nbytes > stacknleft) {
    int blocksize;
    struct stack_block *sp;

    blocksize = nbytes;
    if (blocksize < MINSIZE)
      blocksize = MINSIZE;
    INTOFF;
    sp = ckmalloc(sizeof(struct stack_block) - MINSIZE + blocksize);
    sp->prev = stackp;
    stacknxt = sp->space;
    stacknleft = blocksize;
    stackp = sp;
    INTON;
  }
  p = stacknxt;
  stacknxt += nbytes;
  stacknleft -= nbytes;
  return p;
}

void stunalloc(pointer p)
{
  if (p == NULL) {    /*DEBUG */
    write(2, "stunalloc\n", 10);
    abort();
  }
  stacknleft += stacknxt - (char *)p;
  stacknxt = p;
}

void setstackmark(struct stackmark *mark)
{
  mark->stackp = stackp;
  mark->stacknxt = stacknxt;
  mark->stacknleft = stacknleft;
}

void popstackmark(struct stackmark *mark)
{
  struct stack_block *sp;

  INTOFF;
  while (stackp != mark->stackp) {
    sp = stackp;
    stackp = sp->prev;
    ckfree(sp);
  }
  stacknxt = mark->stacknxt;
  stacknleft = mark->stacknleft;
  INTON;
}

/*
 * When the parser reads in a string, it wants to stick the string on the
 * stack and only adjust the stack pointer when it knows how big the
 * string is.  Stackblock (defined in stack.h) returns a pointer to a block
 * of space on top of the stack and stackblocklen returns the length of
 * this block.  Growstackblock will grow this space by at least one byte,
 * possibly moving it (like realloc).  Grabstackblock actually allocates the
 * part of the block that has been used.
 */
void growstackblock()
{
  char *p;
  int newlen = stacknleft * 2 + 100;
  char *oldspace = stacknxt;
  int oldlen = stacknleft;
  struct stack_block *sp;

  if (stacknxt == stackp->space && stackp != &stackbase) {
    INTOFF;
    sp = stackp;
    stackp = sp->prev;
    sp = ckrealloc((pointer)sp, sizeof(struct stack_block) - MINSIZE + newlen);
    sp->prev = stackp;
    stackp = sp;
    stacknxt = sp->space;
    stacknleft = newlen;
    INTON;
  } else {
    p = stalloc(newlen);
    bcopy(oldspace, p, oldlen);
    stacknxt = p;      /* free the space */
    stacknleft += newlen;    /* we just allocated */
  }
}

void grabstackblock(int len)
{
  len = ALIGN(len);
  stacknxt += len;
  stacknleft -= len;
}

/*
 * The following routines are somewhat easier to use that the above.
 * The user declares a variable of type STACKSTR, which may be declared
 * to be a register.  The macro STARTSTACKSTR initializes things.  Then
 * the user uses the macro STPUTC to add characters to the string.  In
 * effect, STPUTC(c, p) is the same as *p++ = c except that the stack is
 * grown as necessary.  When the user is done, she can just leave the
 * string there and refer to it using stackblock().  Or she can allocate
 * the space for it using grabstackstr().  If it is necessary to allow
 * someone else to use the stack temporarily and then continue to grow
 * the string, the user should use grabstack to allocate the space, and
 * then call ungrabstr(p) to return to the previous mode of operation.
 *
 * USTPUTC is like STPUTC except that it doesn't check for overflow.
 * CHECKSTACKSPACE can be called before USTPUTC to ensure that there
 * is space for at least one character.
 */
char *growstackstr()
{
  int len = stackblocksize();
  if (herefd >= 0 && len >= 1024) {
    xxwrite(herefd, stackblock(), len);
    sstrnleft = len - 1;
    return stackblock();
  }
  growstackblock();
  sstrnleft = stackblocksize() - len - 1;
  return stackblock() + len;
}

/*
 * Called from CHECKSTRSPACE.
 */
char *makestrspace()
{
  int len = stackblocksize() - sstrnleft;
  growstackblock();
  sstrnleft = stackblocksize() - len;
  return stackblock() + len;
}

void ungrabstackstr(char *s, char *p)
{
  stacknleft += stacknxt - s;
  stacknxt = s;
  sstrnleft = stacknleft - (p - s);
}

//==================================================================================
//mail - Routines to check for mail.
/*
 * Print appropriate message(s) if mail has arrived.  If the argument is
 * nozero, then the value of MAIL has changed, so we just update the
 * values.
 */
void chkmail(int silent)
{
  register int i;
  char *mpath;
  char *p;
  register char *q;
  struct stackmark smark;
  struct stat statb;

  if (silent)
    nmboxes = 10;
  if (nmboxes == 0)
    return;
  setstackmark(&smark);
  mpath = mpathset()? mpathval() : mailval();
  for (i = 0 ; i < nmboxes ; i++) {
    p = padvance(&mpath, nullstr);
    if (p == NULL)
      break;
    if (*p == '\0')
      continue;
    for (q = p ; *q ; q++);
    if (q[-1] != '/')
      abort();
    q[-1] = '\0';      /* delete trailing '/' */
#ifdef notdef /* this is what the System V shell claims to do (it lies) */
    if (stat(p, &statb) < 0)
      statb.st_mtime = 0;
    if (statb.st_mtime > mailtime[i] && ! silent) {
      out2str(pathopt? pathopt : "you have mail");
      out2c('\n');
    }
    mailtime[i] = statb.st_mtime;
#else /* this is what it should do */
    if (stat(p, &statb) < 0)
      statb.st_size = 0;
    if (statb.st_size > mailtime[i] && ! silent) {
      out2str(pathopt? pathopt : "you have mail");
      out2c('\n');
    }
    mailtime[i] = statb.st_size;
#endif
  }
  nmboxes = i;
  popstackmark(&smark);
}

//==================================================================================
//input - reading scripts.
/*
 * Read a line from the script.
 */
char *pfgets(char *line, int len)
{
  register char *p = line;
  int nleft = len;
  int c;

  while (--nleft > 0) {
    c = pgetc_macro();
    if (c == PEOF) {
      if (p == line)
        return NULL;
      break;
    }
    *p++ = c;
    if (c == '\n')
      break;
  }
  *p = '\0';
  return line;
}

/*
 * Read a character from the script, returning PEOF on end of file.
 * Nul characters in the input are silently discarded.
 */
static int pgetc()
{
  return pgetc_macro();
}

static void get_win_size(unsigned int * x, unsigned int *y)                          
{                                         
  struct winsize win;                             
  char *temp;                                 

  win.ws_row = 0;                               
  win.ws_col = 0;                               
  ioctl(0, TIOCGWINSZ, &win);                         
  *y = (!win.ws_row)? ((temp = getenv("ROWS"))!=NULL)? atoi(temp):24:win.ws_row;                                                    
  *x = (!win.ws_col)?((temp = getenv("COLUMN"))!=NULL)? atoi(temp):80:win.ws_col;
}
#ifdef CTRL
#undef CTRL
#endif

#define CTRL(a) ((a) & ~0x40)

static int cmpstring(const void *p1, const void *p2)
{
  return strcmp(* (char * const *) p1, * (char * const *) p2);
}

static char *contosp_strdup(char *input)
{
  char *tmp;
  char *tmpbuf;
  if(input == NULL) return NULL;
  tmp = tmpbuf = xmalloc(strlen(input) *2);
  if((*input == '~') && (*(input+1) == '/')){
    struct passwd *pw;
    uid_t uid = getuid();
    pw = getpwuid(uid);
    strcpy(tmpbuf, pw->pw_dir);
    tmp =  tmpbuf + strlen(pw->pw_dir);
    input++;
  }
  for (; *input != '\0'; input++, tmp++) {
    if(*input == '\\'){
      if (strchr(" `\"#$%^&*()=+{}[]:;'|\\<>", *(input+1)))
        input++;
    }
    *tmp = *input;
  }
  *tmp = '\0';
  return tmpbuf;
}

static char *sptocon_strdup(char *input, char last)
{
  char *tmp;
  char *tmpbuf = xmalloc(strlen(input)*2);
  for (tmp = tmpbuf; *input != '\0'; input++, tmp++) {
    if (strchr(" `\"#$%^&*()=+{}[]:;'|\\<>", *input) && !last) {
      *tmp = '\\';
      tmp++;
    }
    *tmp = *input;
  }
  *tmp = '\0';
  return tmpbuf;
}

static char *input_tab(int istab_pressed, char *inp_str, int *cnt)
{
  char lastslash;
  char *pathlist = NULL; //':' separated list of paths
  char *slash = NULL, *path, *filecomp = NULL;
  char *back_inp_str = NULL, *back_pathlist = NULL;
  char *ret = NULL, *sl_back = NULL, *filecomp_p = NULL;
  static char **files_list = NULL;
  static char match[NAME_MAX] = {0,};
  static int count = 0;
  int myindex, str_len, filecomp_len = 0;
  static int max_line_len = 0;
  //segragate dirpath and filename and generate a paths list
  if(!inp_str) return NULL;
  back_inp_str = inp_str;
  back_pathlist = pathlist = xstrdup(pathval());
  //segregation
  lastslash = ((inp_str[strlen(inp_str) -1 ] == '\\') && (inp_str[strlen(inp_str) -2 ] != '\\'));

  while(*inp_str && isspace(*inp_str)) inp_str++;    /*remove leading spaces */
  while((slash = strchr(inp_str, ' '))){
    if(*(slash-1) == '\\') sl_back = inp_str;
    else sl_back = NULL;
    inp_str = slash + 1;    //points past space ' '.
    if(back_pathlist) free(back_pathlist);
    back_pathlist = pathlist = xgetcwd();
  }

  if(sl_back) inp_str = sl_back;
  inp_str = contosp_strdup(inp_str);
  free(back_inp_str);

  slash = strrchr(inp_str, '/');
  if(slash) {
    if(slash[1] == '\0') {
      pathlist = inp_str;
      filecomp = NULL;
    }else {
      if(back_pathlist) free(back_pathlist);
      back_pathlist = pathlist = (char*)xmalloc(slash - inp_str + 2);
      memcpy(pathlist, inp_str, slash - inp_str + 1);
      pathlist[slash - inp_str + 1] = '\0';
      filecomp = slash + 1;
    }
  }
  else filecomp = (*inp_str == '\0')? NULL: inp_str;

  if(istab_pressed) goto PRINT_GO;
  else {
    for(myindex = 0; myindex < count; myindex++)
      free(files_list[myindex]);
    free(files_list);
    files_list = NULL;
    count = 0;
    match[0] = '\0';
    max_line_len = 0;
  }

  files_list = (char**)xmalloc(256 * sizeof(char*));
  if(filecomp){
    filecomp_p = sptocon_strdup(filecomp, 0);
    filecomp_len = strlen(filecomp_p);
  }

  while((path = strsep(&pathlist, ":"))) {
    DIR *dirp = opendir(path);
    struct dirent *ent = NULL;
    if(!dirp) continue;
    while((ent = readdir(dirp)) != NULL) {
      if(filecomp && (strncmp(filecomp, ent->d_name, strlen(filecomp)) == 0)) {
        if ((ent->d_name[0] == '.') && ((!ent->d_name[1])
            || ((ent->d_name[1] == '.' && !ent->d_name[2]))))
          continue;

        if(ent->d_type == DT_DIR)
          files_list[count] = xmsprintf("%s%s", ent->d_name, "/");
        else files_list[count] = strdup(ent->d_name);

        str_len = strlen(files_list[count]);
        if(str_len > max_line_len) max_line_len = str_len;

        count++;
        if(count == 1) strcpy(match, files_list[0]);
        else {
          int pos = 0;
          for(pos = 0; match[pos] && ent->d_name[pos] && match[pos] == ent->d_name[pos]; pos++);
          if(pos) match[pos] = '\0';
          if(count%256 == 0)
            files_list = (char**)realloc(files_list, (count + 256) * sizeof(char*));
        }
      }
      else if(!filecomp) {
         if((ent->d_name[0] == '.') && ((!ent->d_name[1])
            || ((ent->d_name[1] == '.' && !ent->d_name[2]))))
          continue;

        if(ent->d_type == DT_DIR)
          files_list[count] = xmsprintf("%s%s", ent->d_name, "/");
        else files_list[count] = strdup(ent->d_name);

        str_len = strlen(files_list[count]);
        if(str_len > max_line_len) max_line_len = str_len;

        count++;
        if(count%256 == 0) {
          files_list = (char**)xrealloc(files_list, (count + 256) * sizeof(char*));
        }
      }
    }
    closedir(dirp);
  }
PRINT_GO:
  *cnt = count;
  if(count > 1) {
    unsigned x = 0, y = 0, word_perline;
    get_win_size(&x, &y);
    word_perline = x/(max_line_len + 2);
    if(!word_perline) word_perline = 1;

    qsort(files_list, count, sizeof(char*), cmpstring);
    for( myindex = 0; istab_pressed && (myindex < count); myindex++) {
      char *filelistindex_p = sptocon_strdup(files_list[myindex], 0);
      if(myindex % word_perline == 0) printf("\n");
      printf("%-*s  " , max_line_len, filelistindex_p);
      free(filelistindex_p);
    }
  } else if(count == 1){
    char *mstr = NULL;
    if(files_list[0][strlen(files_list[0]) -1] != 0x2F) //char '/'
    {
      char *filelist_p = sptocon_strdup(files_list[0], 0);
      mstr = xmsprintf("%s%c", filelist_p, ' ');
      free(filelist_p);
    }
    else mstr = sptocon_strdup(files_list[0], 0);
    ret = strdup(mstr + filecomp_len);
    free(mstr);
    goto free_ret;
  }else{
    ret = NULL;
    goto free_ret;
  }
  ret = (!filecomp)?NULL:(strlen(match) == strlen(filecomp))?NULL:(sptocon_strdup(match + filecomp_len, lastslash));

free_ret :
  if(filecomp_p) free(filecomp_p);
  if(back_pathlist) free(back_pathlist);
  free(inp_str);
  return ret;
}

void move_cursor(int dir, int num)
{
  switch(dir)
  {
    case LEFT:
      printf("\033[%dD",num);
      break;
    case RIGHT:
      printf("\033[%dC",num);
      break;
  }
}

int get_key_code(char *ch, int i)
{

  static keycode_map_t type2[] = {
    {"OA",KEY_UP},
    {"OB",KEY_DOWN},
    {"OC",KEY_RIGHT},
    {"OD",KEY_LEFT},
    {"OH",KEY_HOME},
    {"OF",KEY_END},
    {"[A",KEY_UP},
    {"[B",KEY_DOWN},
    {"[C",KEY_RIGHT},
    {"[D",KEY_LEFT},
    {"[H",KEY_HOME},
    {"[F",KEY_END},
    {NULL, 0}
  };

  static keycode_map_t type3[] = {
    {"[1~", KEY_HOME},
    {"[2~", KEY_INSERT},
    {"[3~", KEY_DELETE},
    {"[4~", KEY_END},
    {"[5~", KEY_PAGEUP},
    {"[6~", KEY_PAGEDN},
    {"[7~", KEY_HOME},
    {"[8~", KEY_END},
    {NULL, 0}
  };

  static keycode_map_t type5[] = {
    {"[1;5A", KEY_CTRLU},
    {"[1;5B", KEY_CTRLD},
    {"[1;5C", KEY_CTLRT},
    {"[1;5D", KEY_CTLLF},
    {NULL, 0}
  };

  void *keytable[5] = {NULL, type2, type3, NULL, type5};

  if( i > 5) return -1; //TODO
  keycode_map_t *table;
  table = (keycode_map_t*)keytable[i-1];

  while(table && table->key) {
    if(strncmp(ch, table->key, i) == 0)
      return table->code;
    table++;
  }
  return -1;
}

char *reverse_serach(int *index, char *buf, char *bk_up, int *curpos)
{
  struct termios old_set, new_set;
  char search_str[BUFSIZ] = {0,};
  char *p = NULL, *match = NULL, *data = NULL;
  char *backup_str = NULL;
  int len = 0, match_found = 0; //intaily no match
  int i = 0, psval_len = strlen(ps1val());
  int ctl_c = 0;
  static char ch[10] = {0,};
  tcgetattr(0, &old_set);
  new_set = old_set;
  new_set.c_lflag &= ~(ISIG);
  tcsetattr(0, TCSANOW, &new_set);

  if(*index > 0) {
    backup_str = xzalloc((*index) + 1); //+1 for null termination
    memcpy(backup_str, buf, *curpos);
    if(bk_up) {
      memcpy(&backup_str[*curpos], bk_up, *index-*curpos);
    }   
  }
  struct double_list *head = NULL;
  struct double_list *tail = NULL;
  struct double_list *cur = NULL;

  if(hist_list) {
    head = hist_list;
    tail = hist_list->prev;
    hist_list->prev->next = NULL;
    hist_list->prev = NULL;
    cur = tail;
  }
  move_cursor(LEFT, *curpos + psval_len);
  printf("\033[K");
  if(match)
    p = xmsprintf("(reverse-i-search)'%s': %s" , search_str, data);
  else {
    if(*index > 0) p = xmsprintf("(reverse-i-search)'%s': %s", search_str, backup_str);
    else p = xmsprintf("(reverse-i-search)'%s':", search_str);
  }
  printf("%s", p);
  fflush(stdout);
  if(data) {
    free(data);
    data = NULL;
  }

  while(1) {
    len = strlen(search_str);
    i = read(0, ch, 10);
    switch(*ch) {
      case (CTRL('R')):
          if(cur) {
          cur = cur->prev;
        }
        break;
      case '\b':
      case '\x7f':
        if(len != 0)
          len--;
        search_str[len] = '\0';
        cur = tail;
        break;
	  case CTRL('C'):
        ctl_c = 1;
        goto ret;
      default :
        if (*ch < ' ') {
          fflush(stdout);
          goto ret;
        }
        if(i > 0) { 
          search_str[len] = *ch;
          search_str[len+1] = '\0';
        }
        break;
      }
      
      while(cur) {
          match = strstr(cur->data, search_str);
        if(match) {
          match_found = 1;
          *curpos = strlen(p) + (match - cur->data);
          data = xstrdup(cur->data);
          char *ptr = strstr(data, "\n");
          if(ptr) *ptr = '\0';
          memset(buf ,0, BUFSIZ);
          memcpy(buf, data, strlen(data));
          free(p);
          move_cursor(LEFT, *curpos + psval_len);
          printf("\033[K");
          if(match)
            p = xmsprintf("(reverse-i-search)'%s': %s" , search_str, data);
          else {
            if(*index > 0) p = xmsprintf("(reverse-i-search)'%s': %s", search_str, backup_str);
            else p = xmsprintf("(reverse-i-search)'%s':", search_str);
          }
          printf("%s", p);
          fflush(stdout);
          if(data) {
            free(data);
            data = NULL;
          }
          break;
        }
        cur = cur->prev;
      }
	  continue;
  }
ret:
  tcsetattr(0, TCSANOW, &old_set); 
  move_cursor(LEFT, *curpos + strlen(p));
  printf("\033[K");
  printf("%s", ps1val());
  if(match_found){
    printf("%s", buf);
    *index = *curpos = strlen(buf);
  }
  else {
    if(*index > 0) {
      printf("%s", backup_str);
      memcpy(buf, backup_str, strlen(backup_str));
      *index = *curpos = strlen(backup_str);
    }
    else *index = *curpos = 0;
  }
  fflush(stdout);
  ch[i] = '\0';
  if(head) {
    head->prev = tail;
    tail->next = head;
  }
  if(ctl_c) {
    move_cursor(LEFT, *curpos + psval_len);
    *curpos = *index = 0;
    printf("\033[K");
    printf("%s", ps1val());
    fflush(stdout);
  }
  if(backup_str) free(backup_str);
  if(p) free(p);
  return ch;
}

int read_term(int fd, char *p, int size)
{
  struct termios old_set, new_set;
  char buf[size], *backup = NULL;
  static char *bk_str = NULL;
  int index = 0, curpos = 0;
  int last_tab = 0;
  static int i = 0;
  static char *rem_str = NULL;
  tcgetattr(parsefile->fd, &old_set);
  old_set.c_iflag |= ICRNL;
  new_set = old_set;
  new_set.c_lflag &= ~(ICANON | ECHO | ECHONL);
  new_set.c_cc[VMIN] = 1;
  new_set.c_cc[VTIME] = 0;
  tcsetattr(parsefile->fd, TCSANOW, &new_set);

  while(1) {

    if(i == 0)
      i = read(fd, buf + curpos, 10);
    else if(rem_str){
      memcpy(buf, rem_str, i);
      free(rem_str);
      rem_str = NULL;
    }
    if( i <= 0) {
      //handle error
      index = i;
      goto RET_HANDLE;
    }
    if(buf[curpos] != '\t') last_tab = 0;
    else last_tab++;
read:
    switch(buf[curpos]) {
      case '\t':
        {
          if(!isatty(parsefile->fd)) {
        	  index++;
        	  curpos++;
        	  break;
          }
          char *ret = NULL;
          int retlen = 0, cnt = 0;
          ret = input_tab((last_tab > 1) ?1:0, strndup(buf, curpos), &cnt);
          if(ret && ret[0]){
            retlen = strlen(ret);
            memcpy(buf+curpos, ret, retlen);
            index += retlen;
            curpos += retlen;
            last_tab = 0;
          }
          if(backup) memcpy(buf+curpos, backup, index - curpos);
          buf[index] = '\0';
          if(ret && ret[0]) {
            if(cnt > 1 && (last_tab > 1))
              printf("\n%s%s", ps1val(), buf);
            else{
              if((curpos - retlen) > 0) move_cursor(LEFT, curpos - retlen);
              printf("\033[K%s", buf);
            }
            if(curpos != index) move_cursor(LEFT, index - curpos);
          }
          else if((cnt > 1) && (last_tab > 1)) {
            printf("\n%s%s", ps1val(),buf);
            if(curpos != index) move_cursor(LEFT, index - curpos);
          }
          fflush(stdout);
          if(ret) free(ret);
        }
        break;
      case '\b':   /* ^H FALLTHROUGH*/
      case '\x7f': /* DEL */
        if(curpos > 0) {
          printf("\010 \010");
          if(curpos != index) {
            printf("\033[K%s", backup);
            move_cursor(LEFT, index - curpos);
          }
          fflush(stdout);
          index--;
          curpos--;
        }
        break;
      case CTRL('D'): //EOF [\004]
        if(index > 0) {
          --i;
          continue;
        }
        memcpy(p, buf, index);
        goto RET_HANDLE;
        break; /*NOT REACHED */
      case CTRL('K'):
        if(curpos != index) {
          printf("\033[K");
          index = curpos;
          fflush(stdout);
        }
        break;
      case CTRL('U'):
        if(curpos > 0) {
          move_cursor(LEFT, curpos);
          printf("\033[K");
          index -= curpos;
          curpos = 0;
          if(index > 0) {
            printf("%s",backup);
            memcpy(buf, backup, index);
            move_cursor(LEFT, index);
          }
          fflush(stdout);
        }
        break;

      case CTRL('L'):
        printf("\033[2J");
        printf("\033[H");

        buf[index] = '\0';
        printf("%s", ps1val());
        if(index == curpos)
          printf("%s", buf);
        else {
          buf[curpos] = '\0';
          printf("%s%s", buf, backup);
          move_cursor(LEFT, index - curpos);
        }
        fflush(stdout);
        break;
      case CTRL('W'):
        {
          int j = curpos;
          if(curpos > 0) {
            if(!backup && (curpos < index))
              backup = strndup(buf+curpos, index - curpos);
            while(j > 0 && isspace(buf[j-1])) j--;
            if( j > 0){
              while(--j > 0 && !isspace(buf[j]));
              if(isspace(buf[j])) j++;
            }
            move_cursor(LEFT, curpos - j);
            printf("\033[K");
            if(backup){
              printf("%s",backup);
              memcpy(buf + j, backup, index - curpos);
            }
            index = index - curpos + j ;
            curpos = j;
            if((index - curpos) > 0)
              move_cursor(LEFT, index - curpos);
            fflush(stdout);
          }
        }
        break;
      case CTRL('A'):
        {
          buf[curpos] = '\033';
          buf[curpos+1] =  'O';
          buf[curpos+2] = 'H';
          i = 3;
          goto ESCAPE;
        }
      case CTRL('E'): 
        {
          buf[curpos] = '\033';
          buf[curpos+1] =  'O';
          buf[curpos+2] = 'F';
          i = 3;
          goto ESCAPE;
        }
      case CTRL('N'): //For Key Down.
        {
          buf[curpos] = '\033';
          buf[curpos+1] =  '[';
          buf[curpos+2] = 'B';
          i = 3;
          goto ESCAPE;
        }
      case CTRL('P'): //For Key Up.
        {
          buf[curpos] = '\033';
          buf[curpos+1] =  '[';
          buf[curpos+2] = 'A';
          i = 3;
          goto ESCAPE;
        }
      case '\033': //escape key seq.
ESCAPE:
        {
          int key, ret = 0;
          char key_buf[10];
          i--;
          if(i) {
            memcpy(key_buf, buf+curpos+1, i);
          }
          if(backup) {
            memcpy(buf+curpos, backup, index - curpos);
            free(backup);
            backup = NULL;
          }
          if(i) {
            key = get_key_code(key_buf, i);
            switch(key) {
              case KEY_LEFT:
                if(curpos > 0) {
                  move_cursor(LEFT, 1);
                  fflush(stdout);
                  ret = -1;
                }
                break;
              case KEY_RIGHT:
                if(index > curpos) {
                  move_cursor(RIGHT, 1);
                  fflush(stdout);
                  ret = 1;
                }
                break;
              case KEY_DELETE:
                if(backup) free(backup);
                backup = strndup(buf+curpos+1, index - curpos -1);
                if(curpos >= 0 && curpos < index) {
                  if(curpos != index) {
                    printf("\033[K");
                    printf("%s",backup);
                    if((index - curpos) > 1) move_cursor(LEFT, index - curpos -1);
                  }
                  fflush(stdout);
                  index--;
                  memcpy(buf+curpos, backup, index - curpos);
                }
                free(backup);
                backup = NULL;
                break;
              case KEY_HOME:
                if(curpos != 0){
                  move_cursor(LEFT, curpos);
                  curpos = 0;
                  fflush(stdout);
                }
                break;
              case KEY_END:
                if(curpos != index){
                  move_cursor(RIGHT, index - curpos);
                  curpos = index;
                  fflush(stdout);
                }
                break;
              case KEY_CTLLF:
                {
                  int j = curpos;
                  if(curpos > 0) {
                    while(j > 0 && isspace(buf[j-1])) j--;
                    if( j > 0){
                      while(--j > 0 && !isspace(buf[j]));
                      if(isspace(buf[j])) j++;
                    }
                    move_cursor(LEFT, curpos - j);
                    curpos = j;
                    fflush(stdout);
                  }
                }
                break;
              case KEY_CTLRT:
                {
                  int j = curpos;
                  if(curpos < index) {
                    while(j <= index && isspace(buf[j])) j++;
                    if( j < index)
                      while(++j < index && !isspace(buf[j]));
                    move_cursor(RIGHT, j - curpos);
                    curpos = j;
                    fflush(stdout);
                  }
                }
                break;
              case KEY_CTRLD:
              case KEY_CTRLU:
                break;
              case KEY_UP:
              case KEY_DOWN:
                {
                  if(!bk_str && (key == KEY_UP))
                    bk_str = xstrndup(buf, index);

                  int len = 0;
                  char *cmd_str = NULL;
                  if(!hist_list)
                    break;
                  struct double_list *head = hist_list;
                  struct double_list *tail = hist_list->prev;
                  cmd_str = search_in_history(key);
                  head->prev = tail;
                  tail->next = head;
                  if(cmd_str == NULL && key == KEY_UP)
                    break;
                  if(cmd_str == NULL && key == KEY_DOWN) {
                    if(curpos) {
                      if(bk_str) {
                        move_cursor(LEFT, curpos);
                        printf("\033[K");
                        printf("%s", bk_str);
                        fflush(stdout);
                        index = curpos = strlen(bk_str);
                        memcpy(buf, bk_str, index);
                        free(bk_str);
                        bk_str = NULL;
                      }
                    }
                  }
                  else {
                    cmd_str = xstrdup(cmd_str);
                    len = strlen(cmd_str);
                    if(cmd_str[len-1] == '\n') {
                      len--;
                      cmd_str[len] = '\0';
                    }
                    memcpy(buf, cmd_str, len);
                    if(curpos) {
                      move_cursor(LEFT, curpos);
                      printf("\033[K");
                      curpos = 0;
                    }
                    printf("%s", cmd_str);
                    index = len;
                    curpos = len;
                    fflush(stdout);
                    free(cmd_str);
                  }
                }
                break;
            }
          }
          curpos += ret;
          if(backup) free(backup);
          backup = strndup(buf+curpos, index - curpos);
          i = 1;
        }
        break;
      case '\n':
        index++;
        curpos++;
        if(curpos == index)
          memcpy(p, buf, index);
        else {
          memcpy(p, buf, curpos -1);
          memcpy(p+curpos -1, backup, index - curpos);
          p[index -1] = '\n';
          free(backup);
          backup = NULL;
        }
        if(isatty(parsefile->fd)) printf("\n");
        fflush(stdout);
        --i;
        if(i > 0) {
          rem_str = xmalloc(i + 1);
          memcpy(rem_str, buf + curpos, i);
          rem_str[i] = '\0';
        }
        if(bk_str) {
          free(bk_str);
          bk_str = NULL;
        }
        goto RET_HANDLE;
        break; /*NOT REACHED */
      case CTRL('R'):
        {   
          char *ch = NULL;
          ch = reverse_serach(&index, buf, backup, &curpos);
          if(backup) {
            free(backup);
            backup = NULL;
          }
          if(ch) {
            memcpy(buf+curpos, ch, strlen(ch));
            i = strlen(ch);
          }
          else
            break;
          goto read;
        }   
        break;
      default:
        if(buf[curpos] < ' ' || buf[curpos] > 255)
          break;
        if(isatty(parsefile->fd)) {
          putchar(buf[curpos]);

          if(curpos != index /* && backup*/) {
            printf("%s",backup);
            move_cursor(LEFT, index - curpos);
          }
          fflush(stdout);
        }
        index++;
        curpos++;
        break;
    }
    i--;
  } //while(1)
RET_HANDLE:
  if(backup) free(backup);
  tcsetattr(fd, TCSANOW, &old_set);
  return index;
}

/*
 * Refill the input buffer and return the next input character:
 *
 * 1) If a string was pushed back on the input, switch back to the regular
 *  buffer.
 * 2) If an EOF was pushed back (parsenleft == EOF_NLEFT) or we are reading
 *  from a string so we can't refill the buffer, return EOF.
 * 3) Call read to read in the characters.
 * 4) Delete all nul characters from the buffer.
 */
int preadbuffer()
{
  register char *p, *q;
  register int i;

  if (parsefile->strpush) {
    popstring();
    if (--parsenleft >= 0)
      return (*parsenextc++);
  }
  if (parsenleft == EOF_NLEFT || parsefile->buf == NULL)
    return PEOF;
  flushout(&output);
  flushout(&errout);
retry:
  p = parsenextc = parsefile->buf;
  if(parsefile->fd != STDIN_FILENO)
    parselleft = i = read(parsefile->fd, p, BUFSIZ);
  else
    parselleft = i = read_term(parsefile->fd, p, BUFSIZ);
  if ((i > 1) && isatty(parsefile->fd))
    add_to_history(p);

  if (i <= 0) {
    if (i < 0) {
      if (errno == EINTR)
        goto retry;
      if (parsefile->fd == 0 && errno == EWOULDBLOCK) {
        int flags = fcntl(0, F_GETFL, 0);
        if (flags >= 0 && flags & O_NONBLOCK) {
          flags &=~ O_NONBLOCK;
          if (fcntl(0, F_SETFL, flags) >= 0) {
            out2str("sh: turning off NDELAY mode\n");
            goto retry;
          }
        }
      }
    }
    parselleft =  parsenleft = EOF_NLEFT;
    return PEOF;
  }
  parsenleft = i - 1;

  
  /* delete nul characters */
  for (;;) {
    if (*p++ == '\0')
      break;
    if ((parselleft = --i) <= 0) {
      q = p;
      goto ret;
    }
  }
  q = p - 1;
  while (--i > 0) {
    if (*p != '\0')
      *q++ = *p;
    p++;
  }
  if (q == parsefile->buf)
    goto retry;      /* buffer contained nothing but nuls */
  parsenleft = q - parsefile->buf - 1;

ret:
  if (vflag) {
    char save = *q;
    *q = '\0';
    out2str(parsenextc);
    flushout(out2);
    *q = save;                                                                                                                                                                                   
  }
 return *parsenextc++;
}

/*
 * Undo the last call to pgetc.  Only one character may be pushed back.
 * PEOF may be pushed back.
 */
void pungetc()
{
  parsenleft++;
  parsenextc--;
}

/*
 * Push a string back onto the input.  This code doesn't work if the user
 * tries to push back more than one string at once.
 */
void ppushback(char *string, int length)
{
  pushedstring = parsenextc;
  pushednleft = parsenleft;
  parsenextc = string;
  parsenleft = length;
}

/*
 * Push a string back onto the input at this current parsefile level.
 * We handle aliases this way.
 */
void pushstring(char *s, int len, void *ap)
{
    struct strpush *sp;

    INTOFF;
/*debugprintf("*** calling pushstring: %s, %d\n", s, len);*/
    if (parsefile->strpush) {
        sp = ckmalloc(sizeof (struct strpush));
        sp->prev = parsefile->strpush;
        parsefile->strpush = sp;
    } else
        sp = parsefile->strpush = &(parsefile->basestrpush);
    sp->prevstring = parsenextc;
    sp->prevnleft = parsenleft;
    sp->prevlleft = parselleft;
    sp->ap = (struct alias *)ap;
    if (ap)
        ((struct alias *)ap)->flag |= ALIASINUSE;
    parsenextc = s;
    parsenleft = len;
    INTON;
}

void popstring(void)
{
    struct strpush *sp = parsefile->strpush;

    INTOFF;
    if (sp->ap) {
      checkkwd |= 1;
      sp->ap->flag &= ~ALIASINUSE;
    }

    parsenextc = sp->prevstring;
    parsenleft = sp->prevnleft;
    parselleft = sp->prevlleft;
/*debugprintf("*** calling popstring: restoring to '%s'\n", parsenextc);*/
    if (sp->ap)
        sp->ap->flag &= ~ALIASINUSE;
    parsefile->strpush = sp->prev;
    if (sp != &(parsefile->basestrpush))
        ckfree(sp);
    INTON;
}


/*
 * Set the input to take input from a file.  If push is set, push the
 * old input onto the stack first.
 */
void setinputfile(char *fname, int push)
{
  int fd;
  int fd2;

  INTOFF;
  if ((fd = open(fname, O_RDONLY)) < 0)
    sh_error("Can't open %s", fname);
  if (fd < 10) {
    fd2 = copyfd(fd, 10);
    close(fd);
    if (fd2 < 0)
      sh_error("Out of file descriptors");
    fd = fd2;
  }
  setinputfd(fd, push);
  INTON;
}

/*
 * Like setinputfile, but takes an open file descriptor.  Call this with
 * interrupts off.
 */
void setinputfd(int fd, int push)
{
  if (push) {
    pushfile();
    parsefile->buf = ckmalloc(BUFSIZ);
  }
  if (parsefile->fd > 0)
    close(parsefile->fd);
  parsefile->fd = fd;
  if (parsefile->buf == NULL)
    parsefile->buf = ckmalloc(BUFSIZ);
  parselleft = parsenleft = 0;
  plinno = 1;
}

/*
 * Like setinputfile, but takes input from a string.
 */
void setinputstring(char *string, int push)
{
  INTOFF;
  if (push)
    pushfile();
  parsenextc = string;
  parselleft = parsenleft = strlen(string);
  parsefile->buf = NULL;
  plinno = 1;
  INTON;
}

/*
 * To handle the "." command, a stack of input files is used.  Pushfile
 * adds a new entry to the stack and popfile restores the previous level.
 */
STATIC void pushfile()
{
  struct parsefile *pf;

  parsefile->nleft = parsenleft;
  parsefile->lleft = parselleft;
  parsefile->nextc = parsenextc;
  parsefile->linno = plinno;
  pf = (struct parsefile *)ckmalloc(sizeof (struct parsefile));
  pf->prev = parsefile;
  pf->fd = -1;
  pf->strpush = NULL;
  pf->basestrpush.prev = NULL;
  parsefile = pf;
}

void popfile()
{
  struct parsefile *pf = parsefile;

  INTOFF;
  if (pf->fd >= 0)
    close(pf->fd);
  if (pf->buf)
    ckfree(pf->buf);
  while (pf->strpush)
    popstring();
  parsefile = pf->prev;
  ckfree(pf);
  parsenleft = parsefile->nleft;
  parselleft = parsefile->lleft;
  parsenextc = parsefile->nextc;
  plinno = parsefile->linno;
  INTON;
}

/*
 * Return to top level.
 */
void popallfiles()
{
  while (parsefile != &basepf)
    popfile();
}

/*
 * Close the file(s) that the shell is reading commands from.  Called
 * after a fork is done.
 */
void closescript()
{
  popallfiles();
  if (parsefile->fd > 0) {
    close(parsefile->fd);
    parsefile->fd = 0;
  }
}

//==================================================================================
//expand - Shell variable expand
/*
 * Routines to expand arguments to commands.  We have to deal with
 * backquotes, shell variables, and file metacharacters.
 */

/*
 * Expand shell variables and backquotes inside a here document.
 */
void expandhere(union node *arg, int fd)
{
  herefd = fd;
  expandarg(arg, (struct arglist *)NULL, 0);
  xxwrite(fd, stackblock(), expdest - stackblock());
}

/*
 * Perform variable substitution and command substitution on an argument,
 * placing the resulting list of arguments in arglist.  If full is true,
 * perform splitting and file name expansion.  When arglist is NULL, perform
 * here document expansion.
 */
void expandarg(union node *arg, struct arglist *arglist, int full)
{
  struct strlist *sp;
  char *p;

#if UDIR
  didudir = 0;
#endif
  argbackq = arg->narg.backquote;
  STARTSTACKSTR(expdest);
  ifsfirst.next = NULL;
  ifslastp = NULL;
  argstr(arg->narg.text, full);
  if (arglist == NULL)
    return;      /* here document expanded */
  STPUTC('\0', expdest);
  p = grabstackstr(expdest);
  exparg.lastp = &exparg.list;
  if (full) {
    ifsbreakup(p, &exparg);
    *exparg.lastp = NULL;
    exparg.lastp = &exparg.list;
    expandmeta(exparg.list);
  } else {
    sp = (struct strlist *)stalloc(sizeof (struct strlist));
    sp->text = p;
    *exparg.lastp = sp;
    exparg.lastp = &sp->next;
  }
  while (ifsfirst.next != NULL) {
    struct ifsregion *ifsp;
    INTOFF;
    ifsp = ifsfirst.next->next;
    ckfree(ifsfirst.next);
    ifsfirst.next = ifsp;
    INTON;
  }
  *exparg.lastp = NULL;
  if (exparg.list) {
    *arglist->lastp = exparg.list;
    arglist->lastp = exparg.lastp;
  }
}

STATIC char *exptilde(char *p, int flag)
{
  char c, *startp = p;
  struct passwd *pw;
  const char *home;
  int quotes = flag & (EXP_FULL | EXP_CASE);

  while ((c = *p) != '\0') {
    switch(c) {
    case CTLESC:
      return (startp);
    case '\210' /*CTLQUOTEMARK*/:
      return (startp);
    case ':':
      if (flag & EXP_VARTILDE)
        goto done;
      break;
    case '/':
      goto done;
    }
    p++;
  }
done:
  *p = '\0';
  if (*(startp+1) == '\0') {
    if ((home = lookupvar("HOME")) == NULL)
      goto lose;
  } else {
    if ((pw = getpwnam(startp+1)) == NULL)
      goto lose;
    home = pw->pw_dir;
  }
  if (*home == '\0')
    goto lose;
  *p = c;
  while ((c = *home++) != '\0') {
    if (quotes && SQSYNTAX[(int)c] == CCTL)
      STPUTC(CTLESC, expdest);
    STPUTC(c, expdest);
  }
  return (p);
lose:
  *p = c;
  return (startp);
}

/*
 * Perform variable and command substitution.  If full is set, output CTLESC
 * characters to allow for further processing.  If full is not set, treat
 * $@ like $* since no splitting will be performed.
 */
STATIC void argstr(register char *p, int full)
{
  char c;
  int firsteq = 1;

  if (*p == '~' && (full & (EXP_TILDE | EXP_VARTILDE)))
    p = exptilde(p, full);

  for (;;) {
    switch (c = *p++) {
    case '\0':
    case CTLENDVAR:
      goto breakloop;
    case CTLESC:
      if (full)
        STPUTC(c, expdest);
      c = *p++;
      STPUTC(c, expdest);
      break;
    case CTLVAR:
      p = evalvar(p, full);
      break;
    case CTLBACKQ:
    case CTLBACKQ|CTLQUOTE:
      expbackq(argbackq->n, c & CTLQUOTE, full);
      argbackq = argbackq->next;
      break;
    case ':':
    case '=':
      /*
       * sort of a hack - expand tildes in variable
       * assignments (after the first '=' and after ':'s).
       */
      STPUTC(c, expdest);
      if (full & EXP_VARTILDE && *p == '~') {
        if (c == '=') {
          if (firsteq)
            firsteq = 0;
          else
            break;
        }
        p = exptilde(p, full);
      }
      break;
    default:
      STPUTC(c, expdest);
    }
  }
breakloop:;
}

/*
 * Expand stuff in backwards quotes.
 */
STATIC void expbackq(union node *cmd, int quoted, int full)
{
  struct backcmd in;
  int i;
  char buf[128];
  char *p;
  char *dest = expdest;
  struct ifsregion saveifs, *savelastp;
  struct nodelist *saveargbackq;
  char lastc;
  int startloc = dest - stackblock();
  char const *syntax = quoted? DQSYNTAX : BASESYNTAX;
  int saveherefd;

  INTOFF;
  saveifs = ifsfirst;
  savelastp = ifslastp;
  saveargbackq = argbackq;
  saveherefd = herefd;
  herefd = -1;
  p = grabstackstr(dest);
  evalbackcmd(cmd, &in);
  ungrabstackstr(p, dest);
  ifsfirst = saveifs;
  ifslastp = savelastp;
  argbackq = saveargbackq;
  herefd = saveherefd;

  p = in.buf;
  lastc = '\0';
  for (;;) {
    if (--in.nleft < 0) {
      if (in.fd < 0)
        break;
      while ((i = read(in.fd, buf, sizeof buf)) < 0 && errno == EINTR);
      TRACE(("expbackq: read returns %d\n", i));
      if (i <= 0)
        break;
      p = buf;
      in.nleft = i - 1;
    }
    lastc = *p++;
    if (lastc != '\0') {
      if (full && syntax[lastc] == CCTL)
        STPUTC(CTLESC, dest);
      STPUTC(lastc, dest);
    }
  }
  if (lastc == '\n') {
    STUNPUTC(dest);
  }
  if (in.fd >= 0)
    close(in.fd);
  if (in.buf)
    ckfree(in.buf);
  if (in.jp)
    exitstatus = waitforjob(in.jp);
  if (quoted == 0)
    recordregion(startloc, dest - stackblock(), 0);
  TRACE(("evalbackq: size=%d: \"%.*s\"\n",
    (dest - stackblock()) - startloc,
    (dest - stackblock()) - startloc,
    stackblock() + startloc));
  expdest = dest;
  INTON;
}

STATIC int
subevalvar(char *p, char *str, int strloc, int subtype, int startloc, int varflags)
{
	char *startp;
	char *loc = NULL;
	char *q;
	int c = 0;
	int saveherefd = herefd;
	struct nodelist *saveargbackq = argbackq;
	int amount, how;

	herefd = -1;
	switch (subtype) {
	case VSTRIMLEFT:
	case VSTRIMLEFTMAX:
	case VSTRIMRIGHT:
	case VSTRIMRIGHTMAX:
		how = (varflags & VSQUOTE) ? 0 : EXP_CASE;
		break;
	default:
		how = 0;
		break;
	}
	argstr(p, how);
	STACKSTRNUL(expdest);
	herefd = saveherefd;
	argbackq = saveargbackq;
	startp = stackblock() + startloc;
	if (str == NULL)
	    str = stackblock() + strloc;

	switch (subtype) {
	case VSASSIGN:
		setvar(str, startp, 0);
		amount = startp - expdest;
		STADJUST(amount, expdest);
		varflags &= ~VSNUL;
		return 1;

	case VSQUESTION:
		if (*p != CTLENDVAR) {
			outfmt(&errout, "%s\n", startp);
			error(NULL);
		}
		error("%.*s: parameter %snot set",
		      (int)(p - str - 1),
		      str, (varflags & VSNUL) ? "null or "
					      : nullstr);
		/* NOTREACHED */

	case VSTRIMLEFT:
		for (loc = startp; loc < str; loc++) {
			c = *loc;
			*loc = '\0';
			if (patmatch(str, startp/*, varflags & VSQUOTE*/))
				goto recordleft;
			*loc = c;
			if ((varflags & VSQUOTE) && *loc == CTLESC)
			        loc++;
		}
		return 0;

	case VSTRIMLEFTMAX:
		for (loc = str - 1; loc >= startp;) {
			c = *loc;
			*loc = '\0';
			if (patmatch(str, startp/*, varflags & VSQUOTE*/))
				goto recordleft;
			*loc = c;
			loc--;
			if ((varflags & VSQUOTE) && loc > startp &&
			    *(loc - 1) == CTLESC) {
				for (q = startp; q < loc; q++)
					if (*q == CTLESC)
						q++;
				if (q > loc)
					loc--;
			}
		}
		return 0;

	case VSTRIMRIGHT:
	        for (loc = str - 1; loc >= startp;) {
			if (patmatch(str, loc/*, varflags & VSQUOTE*/))
				goto recordright;
			loc--;
			if ((varflags & VSQUOTE) && loc > startp &&
			    *(loc - 1) == CTLESC) { 
				for (q = startp; q < loc; q++)
					if (*q == CTLESC)
						q++;
				if (q > loc)
					loc--;
			}
		}
		return 0;

	case VSTRIMRIGHTMAX:
		for (loc = startp; loc < str - 1; loc++) {
			if (patmatch(str, loc/*, varflags & VSQUOTE*/))
				goto recordright;
			if ((varflags & VSQUOTE) && *loc == CTLESC)
			        loc++;
		}
		return 0;

	default:
		abort();
	}

recordleft:
	*loc = c;
	amount = ((str - 1) - (loc - startp)) - expdest;
	STADJUST(amount, expdest);
	while (loc != str - 1)
		*startp++ = *loc++;
	return 1;

recordright:
	amount = loc - expdest;
	STADJUST(amount, expdest);
	STPUTC('\0', expdest);
	STADJUST(-1, expdest);
	return 1;
}

STATIC char *
cvtnum(int num, char *buf)
{
  char temp[32];
  int neg = num < 0;
  char *p = temp + 31;

  temp[31] = '\0';

  do {
    *--p = num % 10 + '0';
  } while ((num /= 10) != 0);

  if (neg)
    *--p = '-';

  while (*p)
    STPUTC(*p++, buf);
  return buf;
}
/*
 * Expand a variable, and return a pointer to the next character in the
 * input string.
 */

STATIC char *
evalvar(char *p, int flag)
{
	int subtype;
	int varflags;
	char *var;
	char *val;
	int patloc;
	int c;
	int set;
	int special;
	int startloc;
	int varlen;
	int apply_ifs;
	int quotes = flag & (EXP_FULL | EXP_CASE);

	varflags = (unsigned char)*p++;
	subtype = varflags & VSTYPE;
	var = p;
	special = !is_name(*p);
	p = strchr(p, '=') + 1;

again: /* jump here after setting a variable with ${var=text} */
	if (special) {
		set = varisset(*var/*, varflags & VSNUL*/);
		val = NULL;
	} else {
		val = lookupvar(var);
		if (val == NULL || ((varflags & VSNUL) && val[0] == '\0')) {
			val = NULL;
			set = 0;
		} else
			set = 1;
	}

	varlen = 0;
	startloc = expdest - stackblock();

	if (!set && uflag) {
		switch (subtype) {
		case VSNORMAL:
		case VSTRIMLEFT:
		case VSTRIMLEFTMAX:
		case VSTRIMRIGHT:
		case VSTRIMRIGHTMAX:
		case VSLENGTH:
			error("%.*s: parameter not set",
			    (int)(p - var - 1), var);
			/* NOTREACHED */
		}
	}

	if (set && subtype != VSPLUS) {
		/* insert the value of the variable */
		if (special) {
			varvalue(*var, varflags & VSQUOTE,/* subtype,*/ flag);
			if (subtype == VSLENGTH) {
				varlen = expdest - stackblock() - startloc;
				STADJUST(-varlen, expdest);
			}
		} else {
			char const *syntax = (varflags & VSQUOTE) ? DQSYNTAX
								  : BASESYNTAX;

			if (subtype == VSLENGTH) {
				for (;*val; val++)
					varlen++;
			} else {
				while (*val) {
					if (quotes && syntax[(int)*val] == CCTL)
						STPUTC(CTLESC, expdest);
					STPUTC(*val++, expdest);
				}

			}
		}
	}


	apply_ifs = ((varflags & VSQUOTE) == 0 ||
		(*var == '@' && shellparam.nparam != 1));

	switch (subtype) {
	case VSLENGTH:
		expdest = cvtnum(varlen, expdest);
		break;

	case VSNORMAL:
		break;

	case VSPLUS:
		set = !set;
		/* FALLTHROUGH */
	case VSMINUS:
		if (!set) {
		        argstr(p, flag | (apply_ifs ? EXP_IFS_SPLIT : 0));
			/*
			 * ${x-a b c} doesn't get split, but removing the
			 * 'apply_ifs = 0' apparently breaks ${1+"$@"}..
			 * ${x-'a b' c} should generate 2 args.
			 */
			/* We should have marked stuff already */
			apply_ifs = 0;
		}
		break;

	case VSTRIMLEFT:
	case VSTRIMLEFTMAX:
	case VSTRIMRIGHT:
	case VSTRIMRIGHTMAX:
		if (!set)
			break;
		/*
		 * Terminate the string and start recording the pattern
		 * right after it
		 */
		STPUTC('\0', expdest);
		patloc = expdest - stackblock();
		if (subevalvar(p, NULL, patloc, subtype,
			       startloc, varflags) == 0) {
			int amount = (expdest - stackblock() - patloc) + 1;
			STADJUST(-amount, expdest);
		}
		/* Remove any recorded regions beyond start of variable */
		//removerecordregions(startloc);
		apply_ifs = 1;
		break;

	case VSASSIGN:
	case VSQUESTION:
		if (set)
			break;
		if (subevalvar(p, var, 0, subtype, startloc, varflags)) {
			varflags &= ~VSNUL;
			/* 
			 * Remove any recorded regions beyond 
			 * start of variable 
			 */
		//	removerecordregions(startloc);
			goto again;
		}
		apply_ifs = 0;
		break;

	default:
		abort();
	}

	if (apply_ifs)
		recordregion(startloc, expdest - stackblock(),
			     varflags & VSQUOTE);

	if (subtype != VSNORMAL) {	/* skip to end of alternative */
		int nesting = 1;
		for (;;) {
			if ((c = *p++) == CTLESC)
				p++;
			else if (c == CTLBACKQ || c == (CTLBACKQ|CTLQUOTE)) {
				if (set)
					argbackq = argbackq->next;
			} else if (c == CTLVAR) {
				if ((*p++ & VSTYPE) != VSNORMAL)
					nesting++;
			} else if (c == CTLENDVAR) {
				if (--nesting == 0)
					break;
			}
		}
	}
	return p;
}


/*
 * Test whether a specialized variable is set.
 */
STATIC int varisset(int name)
{
  char **ap;

  if (name == '!') {
    if (backgndpid == -1)
      return 0;
  } else if (name == '@' || name == '*') {
    if (*shellparam.p == NULL)
      return 0;
  } else if ((unsigned)(name -= '1') <= '9' - '1') {
    ap = shellparam.p;
    do {
      if (*ap++ == NULL)
        return 0;
    } while (--name >= 0);
  }
  return 1;
}

/*
 * Add the value of a specialized variable to the stack string.
 */
STATIC void varvalue(int name, int quoted, int allow_split)
{
  int num;
  char temp[32];
  char *p;
  int i;
  extern int exitstatus;
  char sep;
  char **ap;
  char const *syntax;

  switch (name) {
  case '$':
    num = rootpid;
    goto numvar;
  case '?':
    num = exitstatus;
    goto numvar;
  case '#':
    num = shellparam.nparam;
    goto numvar;
  case '!':
    num = backgndpid;
numvar:
    p = temp + 31;
    temp[31] = '\0';
    do {
      *--p = num % 10 + '0';
    } while ((num /= 10) != 0);
    while (*p)
      STPUTC(*p++, expdest);
    break;
  case '-':
    for (i = 0 ; optchar[i] ; i++) {
      if (optval[i])
        STPUTC(optchar[i], expdest);
    }
    break;
  case '@':
    if (allow_split) {
      sep = '\0';
      goto allargs;
    }
    /* fall through */
  case '*':
    sep = ' ';
allargs:
    /* Only emit CTLESC if we will do further processing,
       i.e. if allow_split is set.  */
    syntax = quoted && allow_split ? DQSYNTAX : BASESYNTAX;
    for (ap = shellparam.p ; (p = *ap++) != NULL ; ) {
      /* should insert CTLESC characters */
      while (*p) {
        if (syntax[*p] == CCTL)
          STPUTC(CTLESC, expdest);
        STPUTC(*p++, expdest);
      }
      if (*ap)
        STPUTC(sep, expdest);
    }
    break;
  case '0':
    p = arg0;
string:
    /* Only emit CTLESC if we will do further processing,
       i.e. if allow_split is set.  */
    syntax = quoted && allow_split ? DQSYNTAX : BASESYNTAX;
    while (*p) {
      if (syntax[*p] == CCTL)
        STPUTC(CTLESC, expdest);
      STPUTC(*p++, expdest);
    }
    break;
  default:
    if ((unsigned)(name -= '1') <= '9' - '1') {
      p = shellparam.p[name];
      goto string;
    }
    break;
  }
}

/*
 * Record the the fact that we have to scan this region of the
 * string for IFS characters.
 */
STATIC void recordregion(int start, int end, int nulonly)
{
  register struct ifsregion *ifsp;

  if (ifslastp == NULL) {
    ifsp = &ifsfirst;
  } else {
    ifsp = (struct ifsregion *)ckmalloc(sizeof (struct ifsregion));
    ifslastp->next = ifsp;
  }
  ifslastp = ifsp;
  ifslastp->next = NULL;
  ifslastp->begoff = start;
  ifslastp->endoff = end;
  ifslastp->nulonly = nulonly;
}

/*
 * Break the argument string into pieces based upon IFS and add the
 * strings to the argument list.  The regions of the string to be
 * searched for IFS characters have been stored by recordregion.
 */
STATIC void ifsbreakup(char *string, struct arglist *arglist)
{
  struct ifsregion *ifsp;
  struct strlist *sp;
  char *start;
  register char *p;
  char *q;
  char *ifs;

  start = string;
  if (ifslastp != NULL) {
    ifsp = &ifsfirst;
    do {
      p = string + ifsp->begoff;
      ifs = ifsp->nulonly? nullstr : ifsval();
      while (p < string + ifsp->endoff) {
        q = p;
        if (*p == CTLESC)
          p++;
        if (strchr(ifs, *p++)) {
          if (q > start || *ifs != ' ') {
            *q = '\0';
            sp = (struct strlist *)stalloc(sizeof *sp);
            sp->text = start;
            *arglist->lastp = sp;
            arglist->lastp = &sp->next;
          }
          if (*ifs == ' ') {
            for (;;) {
              if (p >= string + ifsp->endoff)
                break;
              q = p;
              if (*p == CTLESC)
                p++;
              if (strchr(ifs, *p++) == NULL) {
                p = q;
                break;
              }
            }
          }
          start = p;
        }
      }
    } while ((ifsp = ifsp->next) != NULL);
    if (*start || (*ifs != ' ' && start > string)) {
      sp = (struct strlist *)stalloc(sizeof *sp);
      sp->text = start;
      *arglist->lastp = sp;
      arglist->lastp = &sp->next;
    }
  } else {
    sp = (struct strlist *)stalloc(sizeof *sp);
    sp->text = start;
    *arglist->lastp = sp;
    arglist->lastp = &sp->next;
  }
}

/*
 * Expand shell metacharacters.  At this point, the only control characters
 * should be escapes.  The results are stored in the list exparg.
 */
STATIC void expandmeta(struct strlist *str)
{
  char *p;
  struct strlist **savelastp;
  struct strlist *sp;
  char c;

  while (str) {
    if (fflag)
      goto nometa;
    p = str->text;
#if UDIR
    if (p[0] == '/' && p[1] == 'u' && p[2] == '/')
      str->text = p = expudir(p);
#endif
    for (;;) {      /* fast check for meta chars */
      if ((c = *p++) == '\0')
        goto nometa;
      if (c == '*' || c == '?' || c == '[' || c == '!')
        break;
    }
    savelastp = exparg.lastp;
    INTOFF;
    if (expdir == NULL)
      expdir = ckmalloc(4096); /* I hope this is big enough */
    expmeta(expdir, str->text);
    ckfree(expdir);
    expdir = NULL;
    INTON;
    if (exparg.lastp == savelastp) {
      if (! zflag) {
nometa:
        *exparg.lastp = str;
        rmescapes(str->text);
        exparg.lastp = &str->next;
      }
    } else {
      *exparg.lastp = NULL;
      *savelastp = sp = expsort(*savelastp);
      while (sp->next != NULL)
        sp = sp->next;
      exparg.lastp = &sp->next;
    }
    str = str->next;
  }
}


#if UDIR
/*
 * Expand /u/username into the home directory for the specified user.
 * We could use the getpw stuff here, but then we would have to load
 * in stdio and who knows what else.
 */

#define MAXLOGNAME 32
#define MAXPWLINE 128

char *pfgets();

STATIC char *expudir(char *path)
{
  register char *p, *q, *r;
  char name[MAXLOGNAME];
  char line[MAXPWLINE];
  int i;

  r = path;        /* result on failure */
  p = r + 3;      /* the 3 skips "/u/" */
  q = name;
  while (*p && *p != '/') {
    if (q >= name + MAXLOGNAME - 1)
      return r;    /* fail, name too long */
    *q++ = *p++;
  }
  *q = '\0';
  setinputfile("/etc/passwd", 1);
  q = line + strlen(name);
  while (pfgets(line, MAXPWLINE) != NULL) {
    if (line[0] == name[0] && prefix(name, line) && *q == ':') {
      /* skip to start of home directory */
      i = 4;
      do {
        while (*++q && *q != ':');
      } while (--i > 0);
      if (*q == '\0')
        break;    /* fail, corrupted /etc/passwd */
      q++;
      for (r = q ; *r && *r != '\n' && *r != ':' ; r++);
      *r = '\0';    /* nul terminate home directory */
      i = r - q;    /* i = strlen(q) */
      r = stalloc(i + strlen(p) + 1);
      scopy(q, r);
      scopy(p, r + i);
      TRACE(("expudir converts %s to %s\n", path, r));
      didudir = 1;
      path = r;    /* succeed */
      break;
    }
  }
  popfile();
  return r;
}
#endif


/*
 * Do metacharacter (i.e. *, ?, [...]) expansion.
 */
STATIC void expmeta(char *enddir, char *name)
{
  register char *p;
  char *q;
  char *start;
  char *endname;
  int metaflag;
  struct stat statb;
  DIR *dirp;
  struct dirent *dp;
  int atend;
  int matchdot;

  metaflag = 0;
  start = name;
  for (p = name ; ; p++) {
    if (*p == '*' || *p == '?')
      metaflag = 1;
    else if (*p == '[') {
      q = p + 1;
      if (*q == '!')
        q++;
      for (;;) {
        if (*q == CTLESC)
          q++;
        if (*q == '/' || *q == '\0')
          break;
        if (*++q == ']') {
          metaflag = 1;
          break;
        }
      }
    } else if (*p == '!' && p[1] == '!'  && (p == name || p[-1] == '/')) {
      metaflag = 1;
    } else if (*p == '\0')
      break;
    else if (*p == CTLESC)
      p++;
    if (*p == '/') {
      if (metaflag)
        break;
      start = p + 1;
    }
  }
  if (metaflag == 0) {  /* we've reached the end of the file name */
    if (enddir != expdir)
      metaflag++;
    for (p = name ; ; p++) {
      if (*p == CTLESC)
        p++;
      *enddir++ = *p;
      if (*p == '\0')
        break;
    }
    if (metaflag == 0 || stat(expdir, &statb) >= 0)
      addfname(expdir);
    return;
  }
  endname = p;
  if (start != name) {
    p = name;
    while (p < start) {
      if (*p == CTLESC)
        p++;
      *enddir++ = *p++;
    }
  }
  if (enddir == expdir) {
    p = ".";
  } else if (enddir == expdir + 1 && *expdir == '/') {
    p = "/";
  } else {
    p = expdir;
    enddir[-1] = '\0';
  }
  if ((dirp = opendir(p)) == NULL)
    return;
  if (enddir != expdir)
    enddir[-1] = '/';
  if (*endname == 0) {
    atend = 1;
  } else {
    atend = 0;
    *endname++ = '\0';
  }
  matchdot = 0;
  if( (start[0] == '.') || (start[0] == CTLESC && start[1] == '.'))
    matchdot++;
  while (! int_pending() && (dp = readdir(dirp)) != NULL) {
    if (dp->d_name[0] == '.' && ! matchdot)
      continue;
    if (patmatch(start, dp->d_name)) {
      if (atend) {
        scopy(dp->d_name, enddir);
        addfname(expdir);
      } else {
        char *q;
        for (p = enddir, q = dp->d_name ; (*p++ = *q++) ;);
        p[-1] = '/';
        expmeta(p, endname);
      }
    }
  }
  closedir(dirp);
  if (! atend)
    endname[-1] = '/';
}

/*
 * Add a file name to the list.
 */
STATIC void addfname(char *name)
{
  char *p;
  struct strlist *sp;

  p = stalloc(strlen(name) + 1);
  scopy(name, p);
  sp = (struct strlist *)stalloc(sizeof *sp);
  sp->text = p;
  *exparg.lastp = sp;
  exparg.lastp = &sp->next;
}

/*
 * Sort the results of file name expansion.  It calculates the number of
 * strings to sort and then calls msort (short for merge sort) to do the
 * work.
 */
STATIC struct strlist *expsort(struct strlist *str)
{
  int len;
  struct strlist *sp;

  len = 0;
  for (sp = str ; sp ; sp = sp->next)
    len++;
  return msort(str, len);
}

STATIC struct strlist *msort(struct strlist *list, int len)
{
  struct strlist *p, *q = NULL;
  struct strlist **lpp;
  int half;
  int n;

  if (len <= 1)
    return list;
  half = len >> 1;
  p = list;
  for (n = half ; --n >= 0 ; ) {
    q = p;
    p = p->next;
  }
  q->next = NULL;      /* terminate first half of list */
  q = msort(list, half);    /* sort first half of list */
  p = msort(p, len - half);    /* sort second half */
  lpp = &list;
  for (;;) {
    if (strcmp(p->text, q->text) < 0) {
      *lpp = p;
      lpp = &p->next;
      if ((p = *lpp) == NULL) {
        *lpp = q;
        break;
      }
    } else {
      *lpp = q;
      lpp = &q->next;
      if ((q = *lpp) == NULL) {
        *lpp = p;
        break;
      }
    }
  }
  return list;
}

/*
 * Returns true if the pattern matches the string.
 */
int patmatch(char *pattern, char *string)
{
  if (pattern[0] == '!' && pattern[1] == '!')
    return 1 - pmatch(pattern + 2, string);
  else
    return pmatch(pattern, string);
}

STATIC int pmatch(char *pattern, char *string)
{
  register char *p, *q;
  register char c;

  p = pattern;
  q = string;
  for (;;) {
    switch (c = *p++) {
    case '\0':
      goto breakloop;
    case CTLESC:
      if (*q++ != *p++)
        return 0;
      break;
    case '?':
      if (*q++ == '\0')
        return 0;
      break;
    case '*':
      c = *p;
      if (c != CTLESC && c != '?' && c != '*' && c != '[') {
        while (*q != c) {
          if (*q == '\0')
            return 0;
          q++;
        }
      }
      do {
        if (pmatch(p, q))
          return 1;
      } while (*q++ != '\0');
      return 0;
    case '[': {
      char *endp;
      int invert, found;
      char chr;

      endp = p;
      if (*endp == '!')
        endp++;
      for (;;) {
        if (*endp == '\0')
          goto dft;    /* no matching ] */
        if (*endp == CTLESC)
          endp++;
        if (*++endp == ']')
          break;
      }
      invert = 0;
      if (*p == '!') {
        invert++;
        p++;
      }
      found = 0;
      chr = *q++;
      c = *p++;
      do {
        if (c == CTLESC)
          c = *p++;
        if (*p == '-' && p[1] != ']') {
          p++;
          if (*p == CTLESC)
            p++;
          if (chr >= c && chr <= *p)
            found = 1;
          p++;
        } else {
          if (chr == c)
            found = 1;
        }
      } while ((c = *p++) != ']');
      if (found == invert)
        return 0;
      break;
    }
dft:    default:
      if (*q++ != c)
        return 0;
      break;
    }
  }
breakloop:
  if (*q != '\0')
    return 0;
  return 1;
}

/*
 * Remove any CTLESC characters from a string.
 */
void rmescapes(char *str)
{
  register char *p, *q;

  p = str;
  while (*p != CTLESC) {
    if (*p++ == '\0')
      return;
  }
  q = p;
  while (*p) {
    if (*p == CTLESC)
      p++;
    *q++ = *p++;
  }
  *q = '\0';
}

/*
 * See if a pattern matches in a case statement.
 */
int casematch(union node *pattern, char *val)
{
  struct stackmark smark;
  int result;
  char *p;

  setstackmark(&smark);
  argbackq = pattern->narg.backquote;
  STARTSTACKSTR(expdest);
  ifslastp = NULL;
  /* Preserve any CTLESC characters inserted previously, so that
     we won't expand reg exps which are inside strings.  */
  argstr(pattern->narg.text, EXP_TILDE | EXP_CASE);
  STPUTC('\0', expdest);
  p = grabstackstr(expdest);
  result = patmatch(p, val);
  popstackmark(&smark);
  return result;
}

//==================================================================================
//exec - execute shell commands.
#ifdef  BSD
#undef BSD
/*
 * When commands are first encountered, they are entered in a hash table.
 * This ensures that a full path search will not have to be done for them
 * on each invocation.
 *
 * We should investigate converting to a linear search, even though that
 * would make the command name "hash" a misnomer.
 */

/*
 * Exec a program.  Never returns.  If you change this routine, you may
 * have to change the find_command routine as well.
 */
void shellexec(char **argv, char **envp, char *path, int index)
{
  char *cmdname;
  int e;

  if (strchr(argv[0], '/') != NULL) {
    tryexec(argv[0], argv, envp);
    e = errno;
  } else {
    e = ENOENT;
    while ((cmdname = padvance(&path, argv[0])) != NULL) {
      if (--index < 0 && pathopt == NULL) {
        tryexec(cmdname, argv, envp);
        if (errno != ENOENT && errno != ENOTDIR)
          e = errno;
      }
      stunalloc(cmdname);
    }
  }
  error2(argv[0], errmsg(e, E_EXEC));
}

STATIC void tryexec(char *cmd, char **argv, char **envp)
{
  int e;
  char *p;

#ifdef SYSV
  do {
    execve(cmd, argv, envp);
  } while (errno == EINTR);
#else
  execve(cmd, argv, envp);
#endif
  e = errno;
  if (e == ENOEXEC) {
    initshellproc();
    setinputfile(cmd, 0);
    commandname = arg0 = savestr(argv[0]);
#ifndef BSD
    pgetc(); pungetc();    /* fill up input buffer */
    p = parsenextc;
    if (parsenleft > 2 && p[0] == '#' && p[1] == '!') {
      argv[0] = cmd;
      execinterp(argv, envp);
    }
#endif
    setparam(argv + 1);
    exraise(EXSHELLPROC);
    /*NOTREACHED*/
  }
  errno = e;
}

#ifndef BSD
/*
 * Execute an interpreter introduced by "#!", for systems where this
 * feature has not been built into the kernel.  If the interpreter is
 * the shell, return (effectively ignoring the "#!").  If the execution
 * of the interpreter fails, exit.
 *
 * This code peeks inside the input buffer in order to avoid actually
 * reading any input.  It would benefit from a rewrite.
 */
#define NEWARGS 5

STATIC void execinterp(char **argv, char **envp)
{
  int n;
  char *inp;
  char *outp;
  char c = '\0';
  char *p;
  char **ap;
  char *newargs[NEWARGS];
  int i;
  char **ap2;
  char **new;

  n = parsenleft - 2;
  inp = parsenextc + 2;
  ap = newargs;
  for (;;) {
    while (--n >= 0 && (*inp == ' ' || *inp == '\t'))
      inp++;
    if (n < 0)
      goto bad;
    if ((c = *inp++) == '\n')
      break;
    if (ap == &newargs[NEWARGS])
bad:      sh_error("Bad #! line");
    STARTSTACKSTR(outp);
    do {
      STPUTC(c, outp);
    } while (--n >= 0 && (c = *inp++) != ' ' && c != '\t' && c != '\n');
    STPUTC('\0', outp);
    n++, inp--;
    *ap++ = grabstackstr(outp);
  }
  if (ap == newargs + 1) {  /* if no args, maybe no exec is needed */
    p = newargs[0];
    for (;;) {
      if (equal(p, "sh") || equal(p, "ash")) {
        return;
      }
      while (*p != '/') {
        if (*p == '\0')
          goto break2;
        p++;
      }
      p++;
    }
break2:;
  }
  i = (char *)ap - (char *)newargs;    /* size in bytes */
  if (i == 0)
    sh_error("Bad #! line");
  for (ap2 = argv ; *ap2++ != NULL ; );
  new = ckmalloc(i + ((char *)ap2 - (char *)argv));
  ap = newargs, ap2 = new;
  while ((i -= sizeof (char **)) >= 0)
    *ap2++ = *ap++;
  ap = argv;
  while ((*ap2++ = *ap++));
  shellexec(new, envp, pathval(), 0);
}
#endif

/*
 * Do a path search.  The variable path (passed by reference) should be
 * set to the start of the path before the first call; padvance will update
 * this value as it proceeds.  Successive calls to padvance will return
 * the possible path expansions in sequence.  If an option (indicated by
 * a percent sign) appears in the path entry then the global variable
 * pathopt will be set to point to it; otherwise pathopt will be set to
 * NULL.
 */
char *padvance(char **path, char *name)
{
  register char *p, *q;
  char *start;
  int len;

  if (*path == NULL)
    return NULL;
  start = *path;
  for (p = start ; *p && *p != ':' && *p != '%' ; p++);
  len = p - start + strlen(name) + 2;  /* "2" is for '/' and '\0' */
  while (stackblocksize() < len)
    growstackblock();
  q = stackblock();
  if (p != start) {
    bcopy(start, q, p - start);
    q += p - start;
    *q++ = '/';
  }
  strcpy(q, name);
  pathopt = NULL;
  if (*p == '%') {
    pathopt = ++p;
    while (*p && *p != ':')  p++;
  }
  if (*p == ':')
    *path = p + 1;
  else
    *path = NULL;
  return stalloc(len);
}

/*** Command hashing code ***/
int hashcmd(int argc, char **argv)
{
  struct tblentry **pp;
  struct tblentry *cmdp;
  int c;
  int verbose;
  struct cmdentry entry;
  char *name;

  if (argc <= 1) {
    for (pp = cmdtable ; pp < &cmdtable[CMDTABLESIZE] ; pp++) {
      for (cmdp = *pp ; cmdp ; cmdp = cmdp->next) {
        if(cmdp->cmdtype != CMDBUILTIN) printentry(cmdp);
      }
    }
    return 0;
  }
  verbose = 0;
  while ((c = nextopt("r")) != '\0') {
    if (c == 'r') {
      clearcmdentry(0);
      return 0;
    }
  }
  while ((name = *argptr) != NULL) {
    if ((cmdp = cmdlookup(name, 0)) != NULL
     && (cmdp->cmdtype == CMDNORMAL
      || (cmdp->cmdtype == CMDBUILTIN && builtinloc >= 0)))
      delete_cmd_entry();
    find_command(name, &entry, 1);
    if (verbose) {
      if (entry.cmdtype != CMDUNKNOWN) {  /* if no error msg */
        cmdp = cmdlookup(name, 0);
        printentry(cmdp);
      }
      flushall();
    }
    argptr++;
  }
  return 0;
}

STATIC void printentry(struct tblentry *cmdp)
{
  int index;
  char *path;
  char *name;

  if (cmdp->cmdtype == CMDNORMAL) {
    index = cmdp->param.index;
    path = pathval();
    do {
      name = padvance(&path, cmdp->cmdname);
      stunalloc(name);
    } while (--index >= 0);
    out1str(name);
  } else if (cmdp->cmdtype == CMDBUILTIN) {
    out1fmt("builtin %s", cmdp->cmdname);
  } else if (cmdp->cmdtype == CMDFUNCTION) {
    out1fmt("function %s", cmdp->cmdname);
#ifdef DEBUG
  } else {
    sh_error("internal error: cmdtype %d", cmdp->cmdtype);
#endif
  }
  if (cmdp->rehash)
    out1c('*');
  out1c('\n');
}

/*
 * Resolve a command name.  If you change this routine, you may have to
 * change the shellexec routine as well.
 */
void find_command(char *name, struct cmdentry *entry, int printerr)
{
  struct tblentry *cmdp;
  int index;
  int prev;
  char *path;
  char *fullname;
  struct stat statb;
  int e;
  int i;

  /* If name contains a slash, don't use the hash table */
  if (strchr(name, '/') != NULL) {
    entry->cmdtype = CMDNORMAL;
    entry->u.index = 0;
    return;
  }

  /* If name is in the table, and not invalidated by cd, we're done */
  if ((cmdp = cmdlookup(name, 0)) != NULL && cmdp->rehash == 0)
    goto success;

  /* If %builtin not in path, check for builtin next */
  if (builtinloc < 0 && (i = find_builtin(name)) >= 0) {
    INTOFF;
    cmdp = cmdlookup(name, 1);
    cmdp->cmdtype = CMDBUILTIN;
    cmdp->param.index = i;
    INTON;
    goto success;
  }

  /* We have to search path. */
  prev = -1;    /* where to start */
  if (cmdp) {    /* doing a rehash */
    if (cmdp->cmdtype == CMDBUILTIN)
      prev = builtinloc;
    else
      prev = cmdp->param.index;
  }

  path = pathval();
  e = ENOENT;
  index = -1;
loop:
  while ((fullname = padvance(&path, name)) != NULL) {
    stunalloc(fullname);
    index++;
    if (pathopt) {
      if (prefix("builtin", pathopt)) {
        if ((i = find_builtin(name)) < 0)
          goto loop;
        INTOFF;
        cmdp = cmdlookup(name, 1);
        cmdp->cmdtype = CMDBUILTIN;
        cmdp->param.index = i;
        INTON;
        goto success;
      } else if (prefix("func", pathopt)) {
        /* handled below */
      } else {
        goto loop;  /* ignore unimplemented options */
      }
    }
    /* if rehash, don't redo absolute path names */
    if (fullname[0] == '/' && index <= prev) {
      if (index < prev)
        goto loop;
      TRACE(("searchexec \"%s\": no change\n", name));
      goto success;
    }
    while (stat(fullname, &statb) < 0) {
#ifdef SYSV
      if (errno == EINTR)
        continue;
#endif
      if (errno != ENOENT && errno != ENOTDIR)
        e = errno;
      goto loop;
    }
    e = EACCES;  /* if we fail, this will be the error */
    if ((statb.st_mode & S_IFMT) != S_IFREG)
      goto loop;
    if (pathopt) {    /* this is a %func directory */
      stalloc(strlen(fullname) + 1);
      readcmdfile(fullname);
      if ((cmdp = cmdlookup(name, 0)) == NULL || cmdp->cmdtype != CMDFUNCTION)
        sh_error("%s not defined in %s", name, fullname);
      stunalloc(fullname);
      goto success;
    }
    if (statb.st_uid == geteuid()) {
      if ((statb.st_mode & 0100) == 0)
        goto loop;
    } else if (statb.st_gid == getegid()) {
      if ((statb.st_mode & 010) == 0)
        goto loop;
    } else {
      if ((statb.st_mode & 01) == 0) {
#ifdef  BSD
        if ((statb.st_mode & 010) == 0)
          goto loop;
        /* Are you in this group too? */
        {
          int group_list[NGROUPS];
          int ngroups, i;

          ngroups = getgroups(NGROUPS, group_list);
          for (i = 0; i < ngroups; i++)
            if (statb.st_gid == group_list[i])
              goto Found;
        }
#endif
        goto loop;
      }
    }
#ifdef  BSD
  Found:
#endif
    TRACE(("searchexec \"%s\" returns \"%s\"\n", name, fullname));
    INTOFF;
    cmdp = cmdlookup(name, 1);
    cmdp->cmdtype = CMDNORMAL;
    cmdp->param.index = index;
    INTON;
    goto success;
  }

  /* We failed.  If there was an entry for this command, delete it */
  if (cmdp)
    delete_cmd_entry();
  if (printerr)
    outfmt(out2, "%s: %s\n", name, errmsg(e, E_EXEC));
  entry->cmdtype = CMDUNKNOWN;
  return;

success:
  cmdp->rehash = 0;
  entry->cmdtype = cmdp->cmdtype;
  entry->u = cmdp->param;
}

/*
 * Search the table of builtin commands.
 */
int find_builtin(char *name)
{
  const register struct builtincmd *bp;

  for (bp = builtincmd ; bp->name ; bp++) {
    if (*bp->name == *name && equal(bp->name, name))
      return bp->code;
  }
  return -1;
}

/*
 * Called when a cd is done.  Marks all commands so the next time they
 * are executed they will be rehashed.
 */
void hashcd()
{
  struct tblentry **pp;
  struct tblentry *cmdp;

  for (pp = cmdtable ; pp < &cmdtable[CMDTABLESIZE] ; pp++) {
    for (cmdp = *pp ; cmdp ; cmdp = cmdp->next) {
      if (cmdp->cmdtype == CMDNORMAL
       || (cmdp->cmdtype == CMDBUILTIN && builtinloc >= 0))
        cmdp->rehash = 1;
    }
  }
}

/*
 * Called before PATH is changed.  The argument is the new value of PATH;
 * pathval() still returns the old value at this point.  Called with
 * interrupts off.
 */
void changepath(char *newval)
{
  char *old, *new;
  int index;
  int firstchange;
  int bltin;

  old = pathval();
  new = newval;
  firstchange = 9999;  /* assume no change */
  index = 0;
  bltin = -1;
  for (;;) {
    if (*old != *new) {
      firstchange = index;
      if ((*old == '\0' && *new == ':')
       || (*old == ':' && *new == '\0'))
        firstchange++;
      old = new;  /* ignore subsequent differences */
    }
    if (*new == '\0')
      break;
    if (*new == '%' && bltin < 0 && prefix("builtin", new + 1))
      bltin = index;
    if (*new == ':') {
      index++;
    }
    new++, old++;
  }
  if (builtinloc < 0 && bltin >= 0)
    builtinloc = bltin;    /* zap builtins */
  if (builtinloc >= 0 && bltin < 0)
    firstchange = 0;
  clearcmdentry(firstchange);
  builtinloc = bltin;
}

/*
 * Clear out command entries.  The argument specifies the first entry in
 * PATH which has changed.
 */
STATIC void clearcmdentry(int firstchange)
{
  struct tblentry **tblp;
  struct tblentry **pp;
  struct tblentry *cmdp;

  INTOFF;
  for (tblp = cmdtable ; tblp < &cmdtable[CMDTABLESIZE] ; tblp++) {
    pp = tblp;
    while ((cmdp = *pp) != NULL) {
      if ((cmdp->cmdtype == CMDNORMAL && cmdp->param.index >= firstchange)
       || (cmdp->cmdtype == CMDBUILTIN && builtinloc >= firstchange)) {
        *pp = cmdp->next;
        ckfree(cmdp);
      } else {
        pp = &cmdp->next;
      }
    }
  }
  INTON;
}

/*
 * Delete all functions.
 */
void deletefuncs()
{
  struct tblentry **tblp;
  struct tblentry **pp;
  struct tblentry *cmdp;

  INTOFF;
  for (tblp = cmdtable ; tblp < &cmdtable[CMDTABLESIZE] ; tblp++) {
    pp = tblp;
    while ((cmdp = *pp) != NULL) {
      if (cmdp->cmdtype == CMDFUNCTION) {
        *pp = cmdp->next;
        freefunc(cmdp->param.func);
        ckfree(cmdp);
      } else {
        pp = &cmdp->next;
      }
    }
  }
  INTON;
}

/*
 * Locate a command in the command hash table.  If "add" is nonzero,
 * add the command to the table if it is not already present.  The
 * variable "lastcmdentry" is set to point to the address of the link
 * pointing to the entry, so that delete_cmd_entry can delete the
 * entry.
 */
STATIC struct tblentry *cmdlookup(char *name, int add)
{
  int hashval;
  register char *p;
  struct tblentry *cmdp;
  struct tblentry **pp;

  p = name;
  hashval = *p << 4;
  while (*p)
    hashval += *p++;
  hashval &= 0x7FFF;
  pp = &cmdtable[hashval % CMDTABLESIZE];
  for (cmdp = *pp ; cmdp ; cmdp = cmdp->next) {
    if (equal(cmdp->cmdname, name))
      break;
    pp = &cmdp->next;
  }
  if (add && cmdp == NULL) {
    INTOFF;
    cmdp = *pp = ckmalloc(sizeof (struct tblentry) - ARB
          + strlen(name) + 1);
    cmdp->next = NULL;
    cmdp->cmdtype = CMDUNKNOWN;
    cmdp->rehash = 0;
    strcpy(cmdp->cmdname, name);
    INTON;
  }
  lastcmdentry = pp;
  return cmdp;
}

/*
 * Delete the command entry returned on the last lookup.
 */
STATIC void delete_cmd_entry()
{
  struct tblentry *cmdp;

  INTOFF;
  cmdp = *lastcmdentry;
  *lastcmdentry = cmdp->next;
  ckfree(cmdp);
  INTON;
}

#ifdef notdef
void getcmdentry(char *name, struct cmdentry *entry)
{
  struct tblentry *cmdp = cmdlookup(name, 0);

  if (cmdp) {
    entry->u = cmdp->param;
    entry->cmdtype = cmdp->cmdtype;
  } else {
    entry->cmdtype = CMDUNKNOWN;
    entry->u.index = 0;
  }
}
#endif

/*
 * Add a new command entry, replacing any existing command entry for
 * the same name.
 */
void addcmdentry(char *name, struct cmdentry *entry)
{
  struct tblentry *cmdp;

  INTOFF;
  cmdp = cmdlookup(name, 1);
  if (cmdp->cmdtype == CMDFUNCTION) {
    freefunc(cmdp->param.func);
  }
  cmdp->cmdtype = entry->cmdtype;
  cmdp->param = entry->u;
  INTON;
}

/*
 * Define a shell function.
 */
void defun(char *name, union node *func)
{
  struct cmdentry entry;

  INTOFF;
  entry.cmdtype = CMDFUNCTION;
  entry.u.func = copyfunc(func);
  addcmdentry(name, &entry);
  INTON;
}

/*
 * Delete a function if it exists.
 */
void unsetfunc(char *name)
{
  struct tblentry *cmdp;

  if ((cmdp = cmdlookup(name, 0)) != NULL && cmdp->cmdtype == CMDFUNCTION) {
    freefunc(cmdp->param.func);
    delete_cmd_entry();
  }
}
#endif

#ifndef  BSD
  #define BSD
#endif

//==================================================================================
//eval - Evaluate commands.
/*
 * Called to reset things after an exception.
 */

/*
 * The eval commmand.
 */
int evalcmd(int argc, char **argv)
{
  char *p;
  char *concat;
  char **ap;

  if (argc > 1) {
    p = argv[1];
    if (argc > 2) {
      STARTSTACKSTR(concat);
      ap = argv + 2;
      for (;;) {
        while (*p)
          STPUTC(*p++, concat);
        if ((p = *ap++) == NULL)
          break;
        STPUTC(' ', concat);
      }
      STPUTC('\0', concat);
      p = grabstackstr(concat);
    }
    evalstring(p);
  }
  return exitstatus;
}

/*
 * Execute a command or commands contained in a string.
 */
void evalstring(char *s)
{
  union node *n;
  struct stackmark smark;

  setstackmark(&smark);
  setinputstring(s, 1);
  while ((n = parsecmd(0)) != NEOF) {
    evaltree(n, 0);
    popstackmark(&smark);
  }
  popfile();
  popstackmark(&smark);
}

/*
 * Evaluate a parse tree.  The value is left in the global variable
 * exitstatus.
 */
void evaltree(union node *n, int flags)
{
  if (n == NULL) {
    TRACE(("evaltree(NULL) called\n"));
    return;
  }
  TRACE(("evaltree(0x%x: %d) called\n", (int)n, n->type));
  switch (n->type) {
  case NSEMI:
    evaltree(n->nbinary.ch1, 0);
    if (evalskip)
      goto out;
    evaltree(n->nbinary.ch2, flags);
    break;
  case NAND:
    evaltree(n->nbinary.ch1, EV_TESTED);
    if (evalskip || exitstatus != 0)
      goto out;
    evaltree(n->nbinary.ch2, flags);
    break;
  case NOR:
    evaltree(n->nbinary.ch1, EV_TESTED);
    if (evalskip || exitstatus == 0)
      goto out;
    evaltree(n->nbinary.ch2, flags);
    break;
  case NREDIR:
    expredir(n->nredir.redirect);
    redirect(n->nredir.redirect, REDIR_PUSH);
    evaltree(n->nredir.n, flags);
    popredir();
    break;
  case NSUBSHELL:
    evalsubshell(n, flags);
    break;
  case NBACKGND:
    evalsubshell(n, flags);
    break;
  case NIF: {
    int status = 0;

    evaltree(n->nif.test, EV_TESTED);
    if (evalskip)
      goto out;
    if (exitstatus == 0) {
      evaltree(n->nif.ifpart, flags);
      status = exitstatus;
    } else if (n->nif.elsepart) {
      evaltree(n->nif.elsepart, flags);
      status = exitstatus;
    }
    exitstatus = status;
    break;
  }
  case NWHILE:
  case NUNTIL:
    evalloop(n);
    break;
  case NFOR:
    evalfor(n);
    break;
  case NCASE:
    evalcase(n, flags);
    break;
  case NDEFUN:
    defun(n->narg.text, n->narg.next);
    exitstatus = 0;
    break;
  case NNOT:
    evaltree(n->nnot.com, EV_TESTED);
    exitstatus = !exitstatus;
    break;
  case NPIPE:
    evalpipe(n);
    break;
  case NCMD:
    evalcommand(n, flags, (struct backcmd *)NULL);
    break;
  default:
    out1fmt("Node type = %d\n", n->type);
    flushout(&output);
    break;
  }
out:
  if (pendingsigs)
    dotrap();
  if ((flags & EV_EXIT) || (eflag && exitstatus && !(flags & EV_TESTED)))
    exitshell(exitstatus);
}

STATIC void evalloop(union node *n)
{
  int status;

  loopnest++;
  status = 0;
  for (;;) {
    evaltree(n->nbinary.ch1, EV_TESTED);
    if (evalskip) {
skipping:    if (evalskip == SKIPCONT && --skipcount <= 0) {
        evalskip = 0;
        continue;
      }
      if (evalskip == SKIPBREAK && --skipcount <= 0)
        evalskip = 0;
      break;
    }
    if (n->type == NWHILE) {
      if (exitstatus != 0)
        break;
    } else {
      if (exitstatus == 0)
        break;
    }
    evaltree(n->nbinary.ch2, 0);
    status = exitstatus;
    if (evalskip)
      goto skipping;
  }
  loopnest--;
  exitstatus = status;
}

STATIC void evalfor(union node *n)
{
  struct arglist arglist;
  union node *argp;
  struct strlist *sp;
  struct stackmark smark;

  setstackmark(&smark);
  arglist.lastp = &arglist.list;
  for (argp = n->nfor.args ; argp ; argp = argp->narg.next) {
    expandarg(argp, &arglist, EXP_FULL | EXP_TILDE);
    if (evalskip)
      goto out;
  }
  *arglist.lastp = NULL;

  exitstatus = 0;
  loopnest++;
  for (sp = arglist.list ; sp ; sp = sp->next) {
    setvar(n->nfor.var, sp->text, 0);
    evaltree(n->nfor.body, 0);
    if (evalskip) {
      if (evalskip == SKIPCONT && --skipcount <= 0) {
        evalskip = 0;
        continue;
      }
      if (evalskip == SKIPBREAK && --skipcount <= 0)
        evalskip = 0;
      break;
    }
  }
  loopnest--;
out:
  popstackmark(&smark);
}

STATIC void evalcase(union node *n, int flags)
{
  union node *cp;
  union node *patp;
  struct arglist arglist;
  struct stackmark smark;

  setstackmark(&smark);
  arglist.lastp = &arglist.list;
  expandarg(n->ncase.expr, &arglist, EXP_TILDE);
  for (cp = n->ncase.cases ; cp && evalskip == 0 ; cp = cp->nclist.next) {
    for (patp = cp->nclist.pattern ; patp ; patp = patp->narg.next) {
      if (casematch(patp, arglist.list->text)) {
        if (evalskip == 0) {
          evaltree(cp->nclist.body, flags);
        }
        goto out;
      }
    }
  }
out:
  popstackmark(&smark);
}

/*
 * Kick off a subshell to evaluate a tree.
 */
STATIC void evalsubshell(union node *n, int flags)
{
  struct job *jp;
  int backgnd = (n->type == NBACKGND);

  expredir(n->nredir.redirect);
  jp = makejob(n, 1);
  if (forkshell(jp, n, backgnd) == 0) {
    if (backgnd)
      flags &=~ EV_TESTED;
    redirect(n->nredir.redirect, 0);
    evaltree(n->nredir.n, flags | EV_EXIT);  /* never returns */
  }
  if (! backgnd) {
    INTOFF;
    exitstatus = waitforjob(jp);
    INTON;
  }
}

/*
 * Compute the names of the files in a redirection list.
 */
STATIC void expredir(union node *n)
{
  register union node *redir;

  for (redir = n ; redir ; redir = redir->nfile.next) {
    if (redir->type == NFROM
     || redir->type == NTO
     || redir->type == NAPPEND) {
      struct arglist fn;
      fn.lastp = &fn.list;
      expandarg(redir->nfile.fname, &fn, EXP_TILDE | EXP_REDIR);
      redir->nfile.expfname = fn.list->text;
    }
  }
}

/*
 * Evaluate a pipeline.  All the processes in the pipeline are children
 * of the process creating the pipeline.  (This differs from some versions
 * of the shell, which make the last process in a pipeline the parent
 * of all the rest.)
 */
STATIC void evalpipe(union node *n)
{
  struct job *jp;
  struct nodelist *lp;
  int pipelen;
  int prevfd;
  int pip[2];

  TRACE(("evalpipe(0x%x) called\n", (int)n));
  pipelen = 0;
  for (lp = n->npipe.cmdlist ; lp ; lp = lp->next)
    pipelen++;
  INTOFF;
  jp = makejob(n, pipelen);
  prevfd = -1;
  for (lp = n->npipe.cmdlist ; lp ; lp = lp->next) {
    prehash(lp->n);
    pip[1] = -1;
    if (lp->next) {
      if (pipe(pip) < 0) {
        close(prevfd);
        sh_error("Pipe call failed");
      }
    }
    if (forkshell(jp, lp->n, n->npipe.backgnd) == 0) {
      INTON;
      if (prevfd > 0) {
        close(0);
        copyfd(prevfd, 0);
        close(prevfd);
      }
      if (pip[1] >= 0) {
        close(pip[0]);
        if (pip[1] != 1) {
          close(1);
          copyfd(pip[1], 1);
          close(pip[1]);
        }
      }
      evaltree(lp->n, EV_EXIT);
    }
    if (prevfd >= 0)
      close(prevfd);
    prevfd = pip[0];
    close(pip[1]);
  }
  INTON;
  if (n->npipe.backgnd == 0) {
    INTOFF;
    exitstatus = waitforjob(jp);
    TRACE(("evalpipe:  job done exit status %d\n", exitstatus));
    INTON;
  }
}

/*
 * Execute a command inside back quotes.  If it's a builtin command, we
 * want to save its output in a block obtained from malloc.  Otherwise
 * we fork off a subprocess and get the output of the command via a pipe.
 * Should be called with interrupts off.
 */
void evalbackcmd(union node *n, struct backcmd *result)
{
  int pip[2];
  struct job *jp;
  struct stackmark smark;    /* unnecessary */

  setstackmark(&smark);
  result->fd = -1;
  result->buf = NULL;
  result->nleft = 0;
  result->jp = NULL;
  if(n == NULL)
    return;
  if (n->type == NCMD) {
    evalcommand(n, EV_BACKCMD, result);
  } else {
    if (pipe(pip) < 0)
      sh_error("Pipe call failed");
    jp = makejob(n, 1);
    if (forkshell(jp, n, FORK_NOJOB) == 0) {
      FORCEINTON;
      close(pip[0]);
      if (pip[1] != 1) {
        close(1);
        copyfd(pip[1], 1);
        close(pip[1]);
      }
      evaltree(n, EV_EXIT);
    }
    close(pip[1]);
    result->fd = pip[0];
    result->jp = jp;
  }
  popstackmark(&smark);
  TRACE(("evalbackcmd done: fd=%d buf=0x%x nleft=%d jp=0x%x\n",
    result->fd, result->buf, result->nleft, result->jp));
}

/*
 * Execute a simple command.
 */
STATIC void evalcommand(union node *cmd, int flags, struct backcmd *backcmd)
{
  struct stackmark smark;
  union node *argp;
  struct arglist arglist;
  struct arglist varlist;
  char **argv;
  int argc;
  char **envp;
  int varflag;
  struct strlist *sp;
  register char *p;
  int mode;
  int pip[2];
  struct cmdentry cmdentry;
  struct job *jp;
  struct jmploc jmploc;
  struct jmploc *volatile savehandler = NULL;
  char *volatile savecmdname;
  volatile struct shparam saveparam;
  struct localvar *volatile savelocalvars;
  volatile int e;
  char *lastarg;

  /* First expand the arguments. */
  TRACE(("evalcommand(0x%x, %d) called\n", (int)cmd, flags));
  setstackmark(&smark);
  arglist.lastp = &arglist.list;
  varlist.lastp = &varlist.list;
  varflag = 1;
  for (argp = cmd->ncmd.args ; argp ; argp = argp->narg.next) {
    p = argp->narg.text;
    if (varflag && is_name(*p)) {
      do {
        p++;
      } while (is_in_name(*p));
      if (*p == '=') {
        expandarg(argp, &varlist, 0);
        continue;
      }
    }
    expandarg(argp, &arglist, EXP_FULL | EXP_TILDE | EXP_VARTILDE);
    varflag = 0;
  }
  *arglist.lastp = NULL;
  *varlist.lastp = NULL;
  expredir(cmd->ncmd.redirect);
  argc = 0;
  for (sp = arglist.list ; sp ; sp = sp->next)
    argc++;
  argv = stalloc(sizeof (char *) * (argc + 1));
  for (sp = arglist.list ; sp ; sp = sp->next)
    *argv++ = sp->text;

  *argv = NULL;
  lastarg = NULL;
  if (iflag && funcnest == 0 && argc > 0)
    lastarg = argv[-1];
  argv -= argc;

  /* Print the command if xflag is set. */
  if (xflag) {
    outc('+', &errout);
    for (sp = varlist.list ; sp ; sp = sp->next) {
      outc(' ', &errout);
      out2str(sp->text);
    }
    for (sp = arglist.list ; sp ; sp = sp->next) {
      outc(' ', &errout);
      out2str(sp->text);
    }
    outc('\n', &errout);
    flushout(&errout);
  }

  /* Now locate the command. */
  if (argc == 0) {
    cmdentry.cmdtype = CMDBUILTIN;
    cmdentry.u.index = BLTINCMD;
  } else {
    find_command(argv[0], &cmdentry, 1);
    if (cmdentry.cmdtype == CMDUNKNOWN) {  /* command not found */
      exitstatus = 2;
      redirect(cmd->ncmd.redirect, REDIR_PUSH);
      flushout(&errout);
      popredir();
      return;
    }
    /* implement the bltin builtin here */
    if (cmdentry.cmdtype == CMDBUILTIN && cmdentry.u.index == BLTINCMD) {
      for (;;) {
        argv++;
        if (--argc == 0)
          break;
        if ((cmdentry.u.index = find_builtin(*argv)) < 0) {
          outfmt(&errout, "%s: not found\n", *argv);
          exitstatus = 2;
          flushout(&errout);
          return;
        }
        if (cmdentry.u.index != BLTINCMD)
          break;
      }
    }
  }

  /* Fork off a child process if necessary. */
  if (cmd->ncmd.backgnd
   || (cmdentry.cmdtype == CMDNORMAL && (flags & EV_EXIT) == 0)
   || ((flags & EV_BACKCMD) != 0 && (cmdentry.cmdtype != CMDBUILTIN
   || cmdentry.u.index == DOTCMD || cmdentry.u.index == EVALCMD)))

  {
    jp = makejob(cmd, 1);
    mode = cmd->ncmd.backgnd;
    if (flags & EV_BACKCMD) {
      mode = FORK_NOJOB;
      if (pipe(pip) < 0)
        sh_error("Pipe call failed");
    }
    if (forkshell(jp, cmd, mode) != 0)
      goto parent;  /* at end of routine */
    if (flags & EV_BACKCMD) {
      FORCEINTON;
      close(pip[0]);
      if (pip[1] != 1) {
        close(1);
        copyfd(pip[1], 1);
        close(pip[1]);
      }
    }
    flags |= EV_EXIT;
  }

  /* This is the child process if a fork occurred. */
  /* Execute the command. */
  if (cmdentry.cmdtype == CMDFUNCTION) {
    trputs("Shell function:  ");  trargs(argv);
    redirect(cmd->ncmd.redirect, REDIR_PUSH);
    saveparam = shellparam;
    shellparam.malloc = 0;
    shellparam.nparam = argc - 1;
    shellparam.p = argv + 1;
    shellparam.optnext = NULL;
    INTOFF;
    savelocalvars = localvars;
    localvars = NULL;
    INTON;
    if (setjmp(jmploc.loc)) {
      if (exception == EXSHELLPROC)
        freeparam((struct shparam *)&saveparam);
      else {
        freeparam(&shellparam);
        shellparam = saveparam;
      }
      poplocalvars();
      localvars = savelocalvars;
      handler = savehandler;
      longjmp(handler->loc, 1);
    }
    savehandler = handler;
    handler = &jmploc;
    for (sp = varlist.list ; sp ; sp = sp->next)
      mklocal(sp->text);
    funcnest++;
    evaltree(cmdentry.u.func, 0);
    funcnest--;
    INTOFF;
    poplocalvars();
    localvars = savelocalvars;
    freeparam(&shellparam);
    shellparam = saveparam;
    handler = savehandler;
    popredir();
    INTON;
    if (evalskip == SKIPFUNC) {
      evalskip = 0;
      skipcount = 0;
    }
    if (flags & EV_EXIT)
      exitshell(exitstatus);
  } else if (cmdentry.cmdtype == CMDBUILTIN) {
    trputs("builtin command:  ");  trargs(argv);
    mode = (cmdentry.u.index == EXECCMD)? 0 : REDIR_PUSH;
    if (flags == EV_BACKCMD) {
      memout.nleft = 0;
      memout.nextc = memout.buf;
      memout.bufsize = 64;
      mode |= REDIR_BACKQ;
    }
    redirect(cmd->ncmd.redirect, mode);
    savecmdname = commandname;
    cmdenviron = varlist.list;
    e = -1;
    if (setjmp(jmploc.loc)) {
      e = exception;
      exitstatus = (e == EXINT)? SIGINT+128 : 2;
      goto cmddone;
    }
    savehandler = handler;
    handler = &jmploc;
    commandname = argv[0];
    argptr = argv + 1;
    optptr = NULL;      /* initialize nextopt */
    exitstatus = (*builtinfunc[cmdentry.u.index])(argc, argv);
    flushall();
cmddone:
    out1 = &output;
    out2 = &errout;
    freestdout();
    if (e != EXSHELLPROC) {
      commandname = savecmdname;
      if (flags & EV_EXIT) {
        exitshell(exitstatus);
      }
    }
    handler = savehandler;
    if (e != -1) {
      if (e != EXERROR || cmdentry.u.index == BLTINCMD
               || cmdentry.u.index == DOTCMD
               || cmdentry.u.index == EVALCMD
               || cmdentry.u.index == EXECCMD)
        exraise(e);
      FORCEINTON;
    }
    if (cmdentry.u.index != EXECCMD)
      popredir();
    if (flags == EV_BACKCMD) {
      backcmd->buf = memout.buf;
      backcmd->nleft = memout.nextc - memout.buf;
      memout.buf = NULL;
    }
  } else {
    trputs("normal command:  ");  trargs(argv);
    clearredir();
    redirect(cmd->ncmd.redirect, 0);
    if (varlist.list) {
      p = stalloc(strlen(pathval()) + 1);
      scopy(pathval(), p);
    } else {
      p = pathval();
    }
    for (sp = varlist.list ; sp ; sp = sp->next)
      setvareq(sp->text, VEXPORT|VSTACK);
    envp = environment();
    shellexec(argv, envp, p, cmdentry.u.index);
    /*NOTREACHED*/
  }
  goto out;

parent:  /* parent process gets here (if we forked) */
  if (mode == 0) {  /* argument to fork */
    INTOFF;
    exitstatus = waitforjob(jp);
    INTON;
  } else if (mode == 2) {
    backcmd->fd = pip[0];
    close(pip[1]);
    backcmd->jp = jp;
  }

out:
  if (lastarg)
    setvar("_", lastarg, 0);
  popstackmark(&smark);
}

/*
 * Search for a command.  This is called before we fork so that the
 * location of the command will be available in the parent as well as
 * the child.  The check for "goodname" is an overly conservative
 * check that the name will not be subject to expansion.
 */
STATIC void prehash(union node *n)
{
  struct cmdentry entry;

  if (n->type == NCMD && n->ncmd.args && goodname(n->ncmd.args->narg.text))
    find_command(n->ncmd.args->narg.text, &entry, 0);
}

/*
 * Builtin commands.  Builtin commands whose functions are closely
 * tied to evaluation are implemented here.
 */
/*
 * No command given, or a bltin command with no arguments.  Set the
 * specified variables.
 */
int bltincmd(int argc, char **argv)
{
  listsetvar(cmdenviron);
  return exitstatus;
}

/*
 * Handle break and continue commands.  Break, continue, and return are
 * all handled by setting the evalskip flag.  The evaluation routines
 * above all check this flag, and if it is set they start skipping
 * commands rather than executing them.  The variable skipcount is
 * the number of loops to break/continue, or the number of function
 * levels to return.  (The latter is always 1.)  It should probably
 * be an error to break out of more loops than exist, but it isn't
 * in the standard shell so we don't make it one here.
 */
int breakcmd(int argc, char **argv)
{
  int n;

  n = 1;
  if (argc > 1)
    n = number(argv[1]);
  if (n > loopnest)
    n = loopnest;
  if (n > 0) {
    evalskip = (**argv == 'c')? SKIPCONT : SKIPBREAK;
    skipcount = n;
  }
  return 0;
}

/*
 * The return command.
 */
int returncmd(int argc, char **argv)
{
  int ret;
  ret = exitstatus;

  if (argc > 1)
    ret = number(argv[1]);
  if (funcnest) {
    evalskip = SKIPFUNC;
    skipcount = 1;
  }
  return ret;
}

int falsecmd(int argc, char **argv)
{
  return 1;
}

int truecmd(int argc, char **argv)
{
  return 0;
}

int execcmd(int argc, char **argv)
{
  if (argc > 1) {
    iflag = 0;    /* exit on error */
    setinteractive(0);
#if JOBS
    jflag = 0;
    setjobctl(0);
#endif
    shellexec(argv + 1, environment(), pathval(), 0);

  }
  return 0;
}

//==================================================================================
//error - Handle Errors and exceptions.
/*
 * Called to raise an exception.  Since C doesn't include exceptions, we
 * just do a longjmp to the exception handler.  The type of exception is
 * stored in the global variable "exception".
 */
void exraise(int e)
{
  if (handler == NULL)
    abort();
  exception = e;
  longjmp(handler->loc, 1);
}

/*
 * Called from trap.c when a SIGINT is received.  (If the user specifies
 * that SIGINT is to be trapped or ignored using the trap builtin, then
 * this routine is not called.)  Suppressint is nonzero when interrupts
 * are held using the INTOFF macro.  The call to _exit is necessary because
 * there is a short period after a fork before the signal handlers are
 * set to the appropriate value for the child.  (The test for iflag is
 * just defensive programming.)
 */
static void onint()
{
  if (suppressint) {
    intpending++;
    return;
  }
  intpending = 0;
#ifdef BSD
  {
    sigset_t sigmask;
    sigemptyset(&sigmask);
    sigprocmask (SIG_SETMASK, &sigmask, NULL);

  }
#endif
  if (rootshell && iflag)
    exraise(EXINT);
  else
    exit(128 + SIGINT);
}

void error2(char *a, char *b)
{
  sh_error("%s: %s", a, b);
}

/*
 * Error is called to raise the error exception.  If the first argument
 * is not NULL then error prints an error message using printf style
 * formatting.  It then raises the error exception.
 */
#ifdef __STDC__

void sh_error(char *msg, ...)
{
#else
void sh_error(va_alist)
  va_dcl
{
  char *msg;
#endif
  va_list ap;

  CLEAR_PENDING_INT;
  INTOFF;
#ifdef __STDC__
  va_start(ap, msg);
#else
  va_start(ap);
  msg = va_arg(ap, char *);
#endif
#ifdef DEBUG
  if (msg)
    TRACE(("error(\"%s\") pid=%d\n", msg, getpid()));
  else
    TRACE(("error(NULL) pid=%d\n", getpid()));
#endif
  if (msg) {
    if (commandname)
      outfmt(&errout, "%s: ", commandname);
    doformat(&errout, msg, ap);
    out2c('\n');
  }
  va_end(ap);
  flushall();
  exraise(EXERROR);
}

/*
 * Return a string describing an error.  The returned string may be a
 * pointer to a static buffer that will be overwritten on the next call.
 * Action describes the operation that got the error.
 */
char *errmsg(int e, int action)
{
  struct errname const *ep;
  static char buf[12];

  for (ep = errormsg ; ep->errcode ; ep++) {
    if (ep->errcode == e && (ep->action & action) != 0)
      return ep->msg;
  }
  fmtstr(buf, sizeof buf, "error %d", e);
  return buf;
}

//==================================================================================
//dirent - operate on directories.
#if ! DIRENT
#ifdef BSD

/*
 * The BSD opendir routine doesn't check that what is being opened is a
 * directory, so we have to include the check in a wrapper routine.
 */
#undef opendir
DIR *myopendir(char *dirname)      /* name of directory */
{
  struct stat statb;

  if (stat(dirname, &statb) != 0 || ! S_ISDIR(statb.st_mode)) {
    errno = ENOTDIR;
    return NULL;    /* not a directory */
  }
  return opendir(dirname);
}

#else /* not BSD */

/*
 * Dirent routines for old style file systems.
 */
#ifdef __STDC__
pointer malloc(unsigned);
void free(pointer);
int open(char *, int, ...);
int close(int);
int fstat(int, struct stat *);
#else
pointer malloc();
void free();
int open();
int close();
int fstat();
#endif

DIR *opendir(char *dirname)  /* name of directory */
{
  register DIR  *dirp;    /* -> malloc'ed storage */
  register int  fd;    /* file descriptor for read */
  struct stat  statb;    /* result of fstat() */

#ifdef O_NDELAY
  fd = open(dirname, O_RDONLY|O_NDELAY);
#else
  fd = open(dirname, O_RDONLY);
#endif
  if (fd < 0)
    return NULL;    /* errno set by open() */

  if (fstat(fd, &statb) != 0 || !S_ISDIR(statb.st_mode)) {
    (void)close(fd);
    errno = ENOTDIR;
    return NULL;    /* not a directory */
  }

  if ((dirp = (DIR *)malloc(sizeof(DIR))) == NULL) {
    (void)close(fd);
    errno = ENOMEM;
    return NULL;    /* not enough memory */
  }

  dirp->dd_fd = fd;
  dirp->dd_nleft = 0;    /* refill needed */

  return dirp;
}

int closedir(register DIR *dirp)    /* stream from opendir() */
{
  register int fd;

  if (dirp == NULL) {
    errno = EFAULT;
    return -1;      /* invalid pointer */
  }

  fd = dirp->dd_fd;
  free((pointer)dirp);
  return close(fd);
}

struct dirent *readdir(register DIR *dirp)    /* stream from opendir() */
{
  register struct direct *dp;
  register char *p, *q;
  register int i;

  do {
    if ((dirp->dd_nleft -= sizeof (struct direct)) < 0) {
      if ((i = read(dirp->dd_fd,
             (char *)dirp->dd_buf,
             DIRBUFENT*sizeof(struct direct))) <= 0) {
        if (i == 0)
          errno = 0;  /* unnecessary */
        return NULL;    /* EOF or error */
      }
      dirp->dd_loc = dirp->dd_buf;
      dirp->dd_nleft = i - sizeof (struct direct);
    }
    dp = dirp->dd_loc++;
  } while (dp->d_ino == 0);
  dirp->dd_entry.d_ino = dp->d_ino;

  /* now copy the name, nul terminating it */
  p = dp->d_name;
  q = dirp->dd_entry.d_name;
  i = DIRSIZ;
  while (--i >= 0 && *p != '\0')
    *q++ = *p++;
  *q = '\0';
  return &dirp->dd_entry;
}

#endif /* BSD */
#endif /* DIRENT */

//==================================================================================
//cd - cd command.
/*
 * The cd and pwd commands.
 */
int cdcmd(int argc, char **argv)
{
#define IS_ALONE_DASH(s) ((s)[0] == '-' && !(s)[1])
#define IS_ALONE_TILDE(s) ((s)[0] == '~' && !(s)[1])
  char *dest;
  char *path;
  char *p;
  struct stat statb;
  char *padvance();

  nextopt(nullstr);

  dest = *argptr;
  if(!dest || IS_ALONE_TILDE(dest)) {
    dest = bltinlookup("HOME", 1);
  }
  else if(IS_ALONE_DASH(dest)) {
    dest = bltinlookup("OLDPWD", 1);
  }
  if(!dest)
    dest = nullstr;
  if (((*dest == '.') && (dest[1] == '\0')) ||
      ((*dest == '.') && (dest[1] == '/') && (dest[2] == '\0')) ) {
    path = nullstr;
    if((p = padvance(&path, dest)) != NULL) {
      getpwd();
      if(docd(curdir, strcmp(curdir, dest)) < 0)
        sh_error("can't cd to %s", dest);
    }
    return 0;
  }
  if (*dest == '/' || (path = bltinlookup("CDPATH", 1)) == NULL)
    path = nullstr;
  while ((p = padvance(&path, dest)) != NULL) {
    if (stat(p, &statb) >= 0
     && (statb.st_mode & S_IFMT) == S_IFDIR
     && docd(p, strcmp(p, dest)) >= 0)
      return 0;
  }
  sh_error("can't cd to %s", dest);
#undef IS_ALONE_DASH
#undef IS_ALONE_TILDE
  return 0;
}

static void setpwd(char *new_val)
{
  char *oldcur = bltinlookup("PWD", 1);
  setvar("OLDPWD", oldcur, VEXPORT);
  setvar("PWD", new_val, VEXPORT);
  return;
}

/*
 * Actually do the chdir.  If the name refers to symbolic links, we
 * compute the actual directory name before doing the cd.  In an
 * interactive shell, print the directory name if "print" is nonzero
 * or if the name refers to a symbolic link.  We also print the name
 * if "/u/logname" was expanded in it, since this is similar to a
 * symbolic link.  (The check for this breaks if the user gives the
 * cd command some additional, unused arguments.)
 */
#if SYMLINKS == 0

STATIC int docd(char *dest, int print)
{
#if UDIR
  if (didudir)
    print = 1;
#endif
  INTOFF;
  if (chdir(dest) < 0) {
    INTON;
    return -1;
  }
  updatepwd(dest);
  INTON;
#ifdef not
  if (print && iflag)
    out1fmt("%s\n", stackblock());
#endif
  return 0;
}

#else

STATIC int docd(char *dest, int print)
{
  register char *p;
  register char *q;
  char *symlink;
  char *component;
  struct stat statb;
  int first;
  int i;

  TRACE(("docd(\"%s\", %d) called\n", dest, print));
#if UDIR
  if (didudir)
    print = 1;
#endif

top:
  cdcomppath = dest;
  STARTSTACKSTR(p);
  if (*dest == '/') {
    STPUTC('/', p);
    cdcomppath++;
  }
  first = 1;
  while ((q = getcomponent()) != NULL) {
    if ((q[0] == '\0') || (q[0] == '.' && q[1] == '\0'))
      continue;
    if (! first)
      STPUTC('/', p);
    first = 0;
    component = q;
    while (*q)
      STPUTC(*q++, p);
    if (equal(component, ".."))
      continue;
    STACKSTRNUL(p);
    if (lstat(stackblock(), &statb) < 0)
      sh_error("lstat %s failed", stackblock());
    if ((statb.st_mode & S_IFMT) != S_IFLNK)
      continue;

    /* Hit a symbolic link.  We have to start all over again. */
    print = 1;
    STPUTC('\0', p);
    symlink = grabstackstr(p);
    i = (int)statb.st_size + 2;    /* 2 for '/' and '\0' */
    if (cdcomppath != NULL)
      i += strlen(cdcomppath);
    p = stalloc(i);
    if (readlink(symlink, p, (int)statb.st_size) < 0) {
      sh_error("readlink %s failed", stackblock());
    }
    if (cdcomppath != NULL) {
      p[(int)statb.st_size] = '/';
      scopy(cdcomppath, p + (int)statb.st_size + 1);
    } else {
      p[(int)statb.st_size] = '\0';
    }
    if (p[0] != '/') {  /* relative path name */
      char *r;
      q = r = symlink;
      while (*q) {
        if (*q++ == '/')
          r = q;
      }
      *r = '\0';
      dest = stalloc(strlen(symlink) + strlen(p) + 1);
      scopy(symlink, dest);
      strcat(dest, p);
    } else {
      dest = p;
    }
    goto top;
  }
  STPUTC('\0', p);
  p = grabstackstr(p);
  INTOFF;
  if (chdir(p) < 0) {
    INTON;
    return -1;
  }
  updatepwd(p);
  setpwd(curdir);
  INTON;
#ifdef not
  if (print && iflag)
    out1fmt("%s\n", p);
#endif
  return 0;
}
#endif /* SYMLINKS */

/*
 * Get the next component of the path name pointed to by cdcomppath.
 * This routine overwrites the string pointed to by cdcomppath.
 */
STATIC char *getcomponent()
{
  register char *p;
  char *start;

  if ((p = cdcomppath) == NULL)
    return NULL;
  start = cdcomppath;
  while (*p != '/' && *p != '\0')
    p++;
  if (*p == '\0') {
    cdcomppath = NULL;
  } else {
    *p++ = '\0';
    cdcomppath = p;
  }
  return start;
}

/*
 * Update curdir (the name of the current directory) in response to a
 * cd command.  We also call hashcd to let the routines in exec.c know
 * that the current directory has changed.
 */
void hashcd();

STATIC void updatepwd(char *dir)
{
  char *new;
  char *p;

  hashcd();        /* update command hash table */
  cdcomppath = stalloc(strlen(dir) + 1);
  scopy(dir, cdcomppath);
  STARTSTACKSTR(new);
  if (*dir != '/') {
    if (curdir == NULL) {
      getpwd();
      return;
    }
    p = curdir;
    while (*p)
      STPUTC(*p++, new);
    if (p[-1] == '/')
      STUNPUTC(new);
  }
  while ((p = getcomponent()) != NULL) {
    if (equal(p, "..")) {
      while (new > stackblock() && (STUNPUTC(new), *new) != '/');
    } else if (*p != '\0' && ! equal(p, ".")) {
      STPUTC('/', new);
      while (*p)
        STPUTC(*p++, new);
    }
  }
  if (new == stackblock())
    STPUTC('/', new);
  STACKSTRNUL(new);
  if (curdir)
    ckfree(curdir);
  curdir = savestr(stackblock());
}

int pwdcmd(int argc, char **argv)
{
  getpwd();
  out1str(curdir);
  out1c('\n');
  return 0;
}

/*
 * Run /bin/pwd to find out what the current directory is.  We suppress
 * interrupts throughout most of this, but the user can still break out
 * of it by killing the pwd program.  If we already know the current
 * directory, this routine returns immediately.
 */
STATIC void getpwd()
{
  char buf[MAXPWD];
  char *p;
  int i;
  int status;
  struct job *jp;
  int pip[2];

  if (curdir)
    return;
  INTOFF;
  if (pipe(pip) < 0)
    sh_error("Pipe call failed");
  jp = makejob((union node *)NULL, 1);
  if (forkshell(jp, (union node *)NULL, FORK_NOJOB) == 0) {
    close(pip[0]);
    if (pip[1] != 1) {
      close(1);
      copyfd(pip[1], 1);
      close(pip[1]);
    }
    execl("/bin/pwd", "pwd", (char *)0);
    /* sh_error("Cannot exec /bin/pwd");*/
    out2str("Cannot exec /bin/pwd\n");
    flushall();
    _exit(1);
  }
  close(pip[1]);
  pip[1] = -1;
  p = buf;
  while( ((i = read(pip[0], p, buf + MAXPWD - p)) > 0) || (i == -1 && errno == EINTR)) {
    if (i > 0)
      p += i;
  }
  close(pip[0]);
  pip[0] = -1;
  status = waitforjob(jp);
  if (status != 0)
    sh_error((char *)0);
  if (i < 0 || p == buf || p[-1] != '\n')
    sh_error("pwd command failed");
  p[-1] = '\0';
  curdir = savestr(buf);
  INTON;
}

//==================================================================================
/*
 * Main routine.  We initialize things, parse the arguments, execute
 * profiles if we're a login shell, and then call cmdloop to execute
 * commands.  The setjmp call sets up the location to jump to when an
 * exception occurs.  When an exception occurs the variable "state"
 * is used to figure out how far we had gotten.
 */

void ash_main(void)
{
  int argc = 0;
  char **argv = toys.argv;
  struct jmploc jmploc;
  struct stackmark smark;
  volatile int state;
  char *shinit;

  while(*argv++)
    argc++;
   argv = toys.argv;

   if(argv[1] && (strcmp(argv[1], "--help") == 0)) {
     show_help();
     exit(1);
   }

#if PROFILE
  monitor(4, etext, profile_buf, sizeof profile_buf, 50);
#endif
  state = 0;
  if (setjmp(jmploc.loc)) {
    /*
     * When a shell procedure is executed, we raise the
     * exception EXSHELLPROC to clean up before executing
     * the shell procedure.
     */
    if (exception == EXSHELLPROC) {
      rootpid = getpid();
      rootshell = 1;
      minusc = NULL;
      state = 3;
    } else if (state == 0 || iflag == 0 || ! rootshell)
      exitshell(2);
    reset();
#if ATTY
    if (exception == EXINT
     && (! attyset() || equal(termval(), "emacs"))) {
#else
    if (exception == EXINT) {
#endif
      out2c('\n');
      flushout(&errout);
    }
    popstackmark(&smark);
    FORCEINTON;        /* enable interrupts */
    if (state == 1)
      goto state1;
    else if (state == 2)
      goto state2;
    else
      goto state3;
  }
  handler = &jmploc;
#ifdef DEBUG
  opentrace();
  trputs("Shell args:  ");  trargs(argv);
#endif
  rootpid = getpid();
  rootshell = 1;
  init();
  setstackmark(&smark);
  procargs(argc, argv);

  get_history();
  if(hist_list)
    key_list_ptr = hist_list->prev;
  //if (argv[0] && argv[0][0] == '-') { // toybox dependency to pass the complete arg name
  if (toys.optflags & FLAG_l) {
    state = 1;
    read_profile("/etc/profile");
state1:
    state = 2;
    read_profile(".profile");
  } else if ((sflag || minusc) && (shinit = getenv("SHINIT")) != NULL) {
    state = 2;
    evalstring(shinit);
  }
state2:
  state = 3;
  if (minusc) {
    evalstring(minusc);
  }
  if (sflag || minusc == NULL) {
state3:
    cmdloop(1);
  }
#if PROFILE
  monitor(0);
#endif
  exitshell(exitstatus);
}

/*
 * Read and execute commands.  "Top" is nonzero for the top level command
 * loop; it turns on prompting if the shell is interactive.
 */
void cmdloop(int top)
{
  union node *n;
  struct stackmark smark;
  int inter;
  int numeof;

  TRACE(("cmdloop(%d) called\n", top));
  setstackmark(&smark);
  numeof = 0;
  for (;;) {
    if (pendingsigs)
      dotrap();
    inter = 0;
    if (iflag && top) {
      inter++;
      showjobs(1);
      chkmail(0);
      flushout(&output);
    }
    n = parsecmd(inter);
#ifdef DEBUG
    /* showtree(n); */
#endif
    if (n == NEOF) {
      if (Iflag == 0 || numeof >= 50)
        break;
      out2str("\nUse \"exit\" to leave shell.\n");
      numeof++;
    } else if (n != NULL && nflag == 0) {
      if (inter) {
        INTOFF;
        if (prevcmd)
          freefunc(prevcmd);
        prevcmd = curcmd;
        curcmd = copyfunc(n);
        INTON;
      }
      evaltree(n, 0);
#ifdef notdef
      if (exitstatus)            /*DEBUG*/
        outfmt(&errout, "Exit status 0x%X\n", exitstatus);
#endif
    }
    popstackmark(&smark);
  }
  popstackmark(&smark);    /* unnecessary */
}

/*
 * Read /etc/profile or .profile.  Return on error.
 */
STATIC void read_profile(char *name)
{
  int fd;

  INTOFF;
  if ((fd = open(name, O_RDONLY)) >= 0)
    setinputfd(fd, 1);
  INTON;
  if (fd < 0)
    return;
  cmdloop(0);
  popfile();
}

/*
 * Read a file containing shell functions.
 */
void readcmdfile(char *name)
{
  int fd;

  INTOFF;
  if ((fd = open(name, O_RDONLY)) >= 0)
    setinputfd(fd, 1);
  else
    sh_error("Can't open %s", name);
  INTON;
  cmdloop(0);
  popfile();
}

/*
 * Take commands from a file.  To be compatable we should do a path
 * search for the file, but a path search doesn't make any sense.
 */
int dotcmd(int argc, char **argv)
{
  exitstatus = 0;
  if (argc >= 2) {    /* That's what SVR2 does */
    setinputfile(argv[1], 1);
    commandname = argv[1];
    cmdloop(0);
    popfile();
  }
  return exitstatus;
}

int exitcmd(int argc, char **argv)
{
  if (argc > 1)
    exitstatus = number(argv[1]);
  exitshell(exitstatus);
  return 0;
}

#ifdef notdef
/*
 * Should never be called.
 */
void exit(exitstatus)
{
  _exit(exitstatus);
}
#endif


//==================================================================================
/*
 * formating times output.
 */
void print_times(long c_tck, clock_t xtime)
{
  unsigned long s_time;
  s_time = xtime / c_tck;
  xtime = xtime % c_tck;
  out1fmt("%lum%lu.%lus ", (s_time / 60), (s_time % 60), (xtime * 1000) / c_tck);
  return;
}

/*
 * times command.
 */
int timescmd(int argc, char **argv)
{
  long c_tck = sysconf(_SC_CLK_TCK);
  struct tms tms_buf;
  if(c_tck == -1) {
    sh_error("sysconf failed to get clock ticks");
    return errno;
  }
  if(times(&tms_buf) == (clock_t)-1) {
    sh_error("Error in getting times");
    return errno;
  }
  print_times(c_tck, tms_buf.tms_utime);
  print_times(c_tck, tms_buf.tms_stime);
  out1fmt("%c",'\n');
  print_times(c_tck, tms_buf.tms_cutime);
  print_times(c_tck, tms_buf.tms_cstime);
  out1fmt("%c",'\n');
  return 0;
}

/*
 * ash help command.
 */
int helpcmd(int argc, char **argv)
{
  const register struct builtincmd *bp;
  int i = 1;
  for(bp = builtincmd; bp->name ; bp++) {
    if((i % 5) == 0) {
      out1fmt("\n");
    }
    out1fmt("%s, ", bp->name);
    i++;
  }
  out1fmt("%c", '\n');
  return 0;
}

/*
 * Kill commmand.
 */
#if CFG_ASH_KILL
int killcmd(int argc, char **argv)
{
  int status = 0;
  pid_t pid = fork();
  if(pid == 0)
    toy_exec(argv);
  else {
    waitpid(pid, &status, 0);
    return WEXITSTATUS(status);
  }
  return status;
}
#endif

/*
 * test command.
 */
#if CFG_ASH_TEST
int testcmd(int argc, char **argv)
{
  int status;
  pid_t pid = fork();
  if(pid == 0)
    toy_exec(argv);
  else {
	waitpid(pid, &status, 0);
	return WEXITSTATUS(status);
  }
  return status;
}
#endif

/*
 * printf command.
 */
#if CFG_ASH_PRINTF
int printfcmd(int argc, char **argv)
{
  pid_t pid = fork();
  if(pid == 0)
    toy_exec(argv);
  else {
    int status;
    waitpid(pid, &status, 0);
    return WEXITSTATUS(status);
  }
  return 0;
}
#endif
//==================================================================================
//alias command
static void setalias(char *name, char *val)
{
  struct alias *ap, **app;

  app = hashalias(name);
  for (ap = *app; ap; ap = ap->next) {
    if (equal(name, ap->name)) {
      INTOFF;
      ckfree(ap->val);
      ap->val  = savestr(val);
      INTON;
      return;
    }
  }
  /* not found */
  INTOFF;
  ap = ckmalloc(sizeof (struct alias));
  ap->name = savestr(name);
  ap->flag = 0;
  /*
   * XXX - HACK: in order that the parser will not finish reading the
   * alias value off the input before processing the next alias, we
   * dummy up an extra space at the end of the alias.  This is a crock
   * and should be re-thought.  The idea (if you feel inclined to help)
   * is to avoid alias recursions.  The mechanism used is: when
   * expanding an alias, the value of the alias is pushed back on the
   * input as a string and a pointer to the alias is stored with the
   * string.  The alias is marked as being in use.  When the input
   * routine finishes reading the string, it markes the alias not
   * in use.  The problem is synchronization with the parser.  Since
   * it reads ahead, the alias is marked not in use before the
   * resulting token(s) is next checked for further alias sub.  The
   * H A C K is that we add a little fluff after the alias value
   * so that the string will not be exhausted.  This is a good
   * idea ------- ***NOT***
   */
#ifdef notyet
  ap->val = savestr(val);
#else /* hack */
  {
  int len = strlen(val);
  ap->val = ckmalloc(len + 2);
  memcpy(ap->val, val, len);
  ap->val[len] = ' ';  /* fluff */
  ap->val[len+1] = '\0';
  }
#endif
  ap->next = *app;
  *app = ap;
  INTON;
}
/*
 * Serach for aliases.
 */
struct alias *lookupalias(char *name, int check)
{
  struct alias *ap = *hashalias(name);

  for (; ap; ap = ap->next) {
    if (equal(name, ap->name)) {
      if (check && (ap->flag & ALIASINUSE))
        return (NULL);
      return (ap);
    }
  }

  return (NULL);
}
/*
 * get alias name in text format.
 */
char *get_alias_text(char *name)
{
  struct alias *ap;

  ap = lookupalias(name, 0);
  if (ap == NULL)
    return NULL;
  return ap->val;
}

/*
 * Command to list all variables which are set.  Currently this command
 * is invoked from the set command when the set command is called without
 * any variables.
 */
void print_quoted(const char *p)
{
  const char *q;

  if (strcspn(p, "|&;<>()$`\\\"' \t\n*?[]#~=%") == strlen(p)) {
    out1fmt("%s", p);
    return;
  }
  while (*p) {
    if (*p == '\'') {
      out1fmt("\\'");
      p++;
      continue;
    }
    q = index(p, '\'');
    if (!q) {
      out1fmt("'%s'", p );
      return;
    }
    out1fmt("'%.*s'", (int)(q - p), p );
    p = q;
  }
}

/*
 * alias command.
 */
int aliascmd(int argc, char **argv)
{
  char *n, *v;
  int ret = 0;
  struct alias *ap;

  if (argc == 1) {
    int i;

    for (i = 0; i < ATABSIZE; i++)
      for (ap = atab[i]; ap; ap = ap->next) {
        if (*ap->name != '\0') {
          out1fmt("alias %s=", ap->name);
          print_quoted(ap->val);
          out1c('\n');
        }
      }
    return (0);
  }
  while ((n = *++argv) != NULL) {
    if ((v = strchr(n+1, '=')) == NULL) { /* n+1: funny ksh stuff */
      if ((ap = lookupalias(n, 0)) == NULL) {
        outfmt(out2, "alias: %s not found\n", n);
        ret = 1;
      } else {
        out1fmt("alias %s=", n);
        print_quoted(ap->val);
        out1c('\n');
      }
    } else {
      *v++ = '\0';
      setalias(n, v);
    }
  }
  return (ret);
}
/*
 * prepare hash alias table.
 */
static struct alias **hashalias(char *p)
{
  unsigned int hashval;

  hashval = *p << 4;
  while (*p)
    hashval+= *p++;
  return &atab[hashval % ATABSIZE];
}
/*
 * Remove aliases.
 */
void rmaliases(void)
{
  struct alias *ap, *tmp;
  int i;

  INTOFF;
  for (i = 0; i < ATABSIZE; i++) {
    ap = atab[i];
    atab[i] = NULL;
    while (ap) {
      ckfree(ap->name);
      ckfree(ap->val);
      tmp = ap;
      ap = ap->next;
      ckfree(tmp);
    }
  }
  INTON;
}
/*
 * remove alias.
 */
static int unalias(char *name)
{
  struct alias *ap, **app;

  app = hashalias(name);

  for (ap = *app; ap; app = &(ap->next), ap = ap->next) {
    if (equal(name, ap->name)) {
      /*
       * if the alias is currently in use (i.e. its
       * buffer is being used by the input routine) we
       * just null out the name instead of freeing it.
       * We could clear it out later, but this situation
       * is so rare that it hardly seems worth it.
       */
      if (ap->flag & ALIASINUSE)
        *ap->name = '\0';
      else {
        INTOFF;
        *app = ap->next;
        ckfree(ap->name);
        ckfree(ap->val);
        ckfree(ap);
        INTON;
      }
      return (0);
    }
  }

  return (1);
}
/*
 * unalias command.
 */
int unaliascmd(int argc, char **argv)
{
  int i;

  while ((i = nextopt("a")) != '\0') {
    if (i == 'a') {
      rmaliases();
      return (0);
    }
  }
  for (i = 0; *argptr; argptr++)
    i = unalias(*argptr);

  return (i);
}
/*
 * type command.
 */
int typecmd(int argc, char **argv)
{
  struct cmdentry entry;
  struct tblentry *cmdp;
  register char *const *pp;
  struct alias *ap;
  int err = 0;
  char *arg;
  int c;
  int V_flag = 0;
  int v_flag = 0;
  int p_flag = 0;

  while ((c = nextopt("vVp")) != 0) {
    switch (c) {
      case 'v': v_flag = 1; break;
      case 'V': V_flag = 1; break;
      case 'p': p_flag = 1; break;
    }
  }
  if (p_flag && (v_flag || V_flag))
    sh_error("cannot specify -p with -v or -V");

  while ((arg = *argptr++)) {
    if (!v_flag)
      out1str(arg);
    /* First look at the keywords */
    for (pp = parsekwd; *pp; pp++)
      if (**pp == *arg && equal(*pp, arg))
        break;

    if (*pp) {
      if (v_flag)
        err = 1;
      else
        out1str(" is a shell keyword\n");
      continue;
    }

    /* Then look at the aliases */
    if ((ap = lookupalias(arg, 1)) != NULL) {
      if (!v_flag)
        out1fmt(" is an alias for \n");
      out1fmt("%s\n", ap->val);
      continue;
    }

    /* Then check if it is a tracked alias */
    if ((cmdp = cmdlookup(arg, 0)) != NULL) {
      entry.cmdtype = cmdp->cmdtype;
      entry.u = cmdp->param;
    }
    else {
      /* Finally use brute force */
      find_command(arg, &entry, 2);
    }

    switch (entry.cmdtype) {
      case CMDNORMAL: {
        if (strchr(arg, '/') == NULL) {
          char *path = pathval();
          char *name;
          int j = entry.u.index;
          do {
            name = padvance(&path, arg);
            stunalloc(name);
          } while (--j >= 0);
          if (!v_flag)
            out1fmt(" is%s ", cmdp ? " a tracked alias for" : "");
            out1fmt("%s\n", name);
          }
          else {
            if (access(arg, X_OK) == 0) {
              if (!v_flag)
                out1fmt(" is ");
              out1fmt("%s\n", arg);
            }
            else {
              if (!v_flag)
                out1fmt(": %s\n", strerror(errno));
              else
                err = 126;
            }
          }
        break;
      }
      case CMDFUNCTION:
        if (!v_flag)
          out1str(" is a shell function\n");
        else
          out1fmt("%s\n", arg);
        break;

      case CMDBUILTIN:
        if (!v_flag)
          out1str(" is a shell builtin\n");
        else
          out1fmt("%s\n", arg);
        break;

      case CMDSPLBLTIN:
        if (!v_flag)
          out1str(" is a special shell builtin\n");
        else
          out1fmt("%s\n", arg);
        break;

      default:
           out1str("\n");
        err = 127;
        break;
      }
  }
  return err;
}
/*
 * ulimit command.
 */
int ulimitcmd(int argc, char **argv)
{
  quad_t val = 0;
  register int c;
  enum { SOFT = 0x1, HARD = 0x2 }
          how = SOFT | HARD;
  const struct limits *l;
  struct rlimit limit;
  int set, all = 0;
  int optc, what;

  what = 'f';
  while ((optc = nextopt("HSatfdsmcnpl")) != '\0')
    switch (optc) {
    case 'H':
      how = HARD;
      break;
    case 'S':
      how = SOFT;
      break;
    case 'a':
      all = 1;
      break;
    default:
      what = optc;
      break;
    }

  for (l = limits; l->name && l->option != what; l++) {/*do nothing*/;}
  if (!l->name)
    sh_error("ulimit: internal error (%c)\n", what);

  set = *argptr ? 1 : 0;
  if (set) {
    char *p = *argptr;

    if (strcmp(p, "unlimited") == 0)
      val = RLIM_INFINITY;
    else {
      val = (quad_t) 0;

      while ((c = *p++) >= '0' && c <= '9') {
        val = (val * 10) + (long)(c - '0');
        if (val < (quad_t) 0)
          break;
      }
      if (c)
        sh_error("ulimit: bad number\n");
      val *= l->factor;
    }
  }
  if (all) {
    for (l = limits; l->name; l++) {
      getrlimit(l->cmd, &limit);
      if (how & SOFT)
        val = limit.rlim_cur;
      else if (how & HARD)
        val = limit.rlim_max;

      out1fmt("%-20s ", l->name);
      if (val == RLIM_INFINITY)
        out1fmt("unlimited\n");
      else {
        val /= l->factor;
        out1fmt("%ld\n", (long) val);
      }
    }
    return 0;
  }

  getrlimit(l->cmd, &limit);
  if (set) {
    if (how & SOFT)
      limit.rlim_cur = val;
    if (how & HARD)
      limit.rlim_max = val;
    if (setrlimit(l->cmd, &limit) < 0)
      sh_error("ulimit: bad limit\n");
  } else {
    if (how & SOFT)
      val = limit.rlim_cur;
    else if (how & HARD)
      val = limit.rlim_max;
  }

  if (!set) {
    if (val == RLIM_INFINITY)
      out1fmt("unlimited\n");
    else {
      val /= l->factor;
      out1fmt("%ld\n", (long) val);
    }
  }
  return 0;
}
//==================================================================================
//For History
/*
 * get history from ash_history file.
 */
void get_history()
{
  FILE *fp;
  char *file_path;
  char buf[BUFSIZ] = {0,};

  struct passwd *pw;
  uid_t uid = getuid();
  pw = getpwuid(uid);
  file_path = xmsprintf("%s/%s", pw->pw_dir, "ash_history");

  fp = fopen(file_path, "a+");
  if(!fp) {
    free(file_path);
    return;
  }
  while(fgets(buf, BUFSIZ, fp)) {
    char *history_cmd = xstrdup(buf);
    dlist_add(&hist_list, history_cmd);
    history_items++;
  }
  free(file_path);
  fclose(fp);
  return;
}
/*
 *seach history.
 */
char *search_in_history(int key)
{
  char *str;
  if(key_list_ptr == NULL)
    return NULL;

  if(hist_list == NULL)
    return NULL;

  hist_list->prev->next = NULL;
  hist_list->prev = NULL;

  if(key == KEY_UP) {
    if(key_list_ptr->prev == NULL) //we are at top.
      return key_list_ptr->data;
    if((key_list_ptr->next == NULL) && !up_key_pressed && enter_flag) {//we are at bottom
      str = key_list_ptr->data;
      up_key_pressed = 1;
      enter_flag = 0;
      return str;
    }
    key_list_ptr = key_list_ptr->prev;
    str = key_list_ptr->data;
    up_key_pressed = 0;
    return str;
  }
  else {
    if(key_list_ptr->next == NULL) {//we are at bottom.
      enter_flag = 1;
      up_key_pressed = 0;
      return NULL;
    }

    key_list_ptr = key_list_ptr->next;
    str = key_list_ptr->data;
    if(key_list_ptr->next == NULL)
      up_key_pressed = 0;
    enter_flag = 0;
    return str;
  }
}
/*
 * write history to ash_history file.
 */
void save_history()
{
  FILE *fp;
  struct passwd *pw;

  if(!hist_list)
    return;
  uid_t uid = getuid();
  pw = getpwuid(uid);
  char *file_path;
  file_path = xmsprintf("%s/%s", pw->pw_dir, "ash_history");
  fp = xfopen(file_path, "w");

  struct double_list *temp_list = hist_list;
  temp_list->prev->next = NULL;
  while(temp_list) {
    struct double_list *tmp_ptr = temp_list;
    fputs(temp_list->data, fp);
    temp_list = temp_list->next;
    free(tmp_ptr->data);
    free(tmp_ptr);
  }
  free(file_path);
  fclose(fp);
  return;
}
/*
 * add command to history list.
 */
void add_to_history(const char *p)
{
  int i = 0;
  char *history_cmd = (char *)p;

  while(history_cmd[i] != '\n')
    i++;
  history_cmd = xzalloc(i+2);
  memcpy(history_cmd, p, i+1);

  enter_flag = 1;
  up_key_pressed = 0;

  if(history_items > 0) {
    if(strcmp(hist_list->prev->data, history_cmd) == 0) {
      free(history_cmd);
      key_list_ptr = hist_list->prev;
      up_key_pressed = 0;
      return;
    }
  }
  if(history_items == NUM_OF_HISTORY_ITEMS) {
    struct double_list *temp = hist_list;
    hist_list->prev->next = hist_list->next;
    hist_list->next->prev = hist_list->prev;
    hist_list = hist_list->next;

    free(temp->data);
    free(temp);
    dlist_add(&hist_list, history_cmd);
    key_list_ptr = hist_list->prev;
    return;
  }

  dlist_add(&hist_list, history_cmd);
  key_list_ptr = hist_list->prev;
  history_items++;
  return;
}
/*
 * history command.
 */
int historycmd(int argc, char **argv)
{
  struct double_list *tmp_ptr = hist_list;
  int list_items = 0;

  if(!tmp_ptr)
    return -1;

  do {
    xprintf("%5d   %s", list_items, tmp_ptr->data);
    tmp_ptr = tmp_ptr->next;
    list_items++;
  }while(tmp_ptr != hist_list);
  return 0;
}

//==================================================================================
//for let command:
#ifdef ECHO
# undef ECHO
#include "lib/ash_lexyyc"
#include "lib/ash_ytabc"
#endif
//==================================================================================
