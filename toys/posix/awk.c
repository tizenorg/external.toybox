/* awk.c - awk implementation.
 *
 * Copyright 2012 Madhur Verma <mad.flexi@gmail.com>
 *           Ashwini Kumar <ak.ashwini@gmail.com>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/awk.html

USE_AWK(NEWTOY(awk, "f*F:v*ds", TOYFLAG_USR|TOYFLAG_BIN))

config AWK
  bool "awk"
  default y
  help
    usage: awk [OPTIONS] [AWK_PROGRAM] [FILE]...

    -v VAR=VAL    Set variable
    -F SEP      Use SEP as field separator
    -f FILE     Read program from FILE
*/

/****************************************************************
Copyright (C) Lucent Technologies 1997
All Rights Reserved

Permission to use, copy, modify, and distribute this software and
its documentation for any purpose and without fee is hereby
granted, provided that the above copyright notice appear in all
copies and that both that the copyright notice and this
permission notice and warranty disclaimer appear in supporting
documentation, and that the name Lucent Technologies or any of
its entities not be used in advertising or publicity pertaining
to distribution of the software without specific, written prior
permission.

LUCENT DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
IN NO EVENT SHALL LUCENT OR ANY OF ITS ENTITIES BE LIABLE FOR ANY
SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF
THIS SOFTWARE.
****************************************************************/

#define FOR_awk
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <locale.h>
#include <signal.h>
#include <errno.h>
#include <stdarg.h>
#include <math.h>
#include <setjmp.h>
#include <limits.h>
#include <time.h>

#include "toys.h"
#include "lib/awk.h"
#include "lib/awk_ytabc"

#define  DEBUG
#define  MAX_PFILE    20          /* max number of -f's */
#define  HAT        (NCHARS+2)      /* matches ^ in regular expr */
#define  NFA        20          /* cache this many dynamic fa's */
#define MAXLIN       22
#define type(v)      (v)->nobj      /* badly overloaded here */
#define info(v)      (v)->ntype      /* badly overloaded here */
#define left(v)      (v)->narg[0]
#define right(v)    (v)->narg[1]
#define parent(v)    (v)->nnext
#define LEAF      case CCL: case NCCL: case CHAR: case DOT: case FINAL: case ALL:
#define ELEAF      case EMPTYRE:    /* empty string in regexp */
#define UNARY      case STAR: case PLUS: case QUEST:
#define  MAXFLD      2
#define  FULLTAB      2          /* rehash when table gets this x full */
#define  GROWTAB      4          /* grow table by this factor */
#define  RET(x)      { if(dbg)printf("lex %s\n", tokname(x)); return(x); }
#define tempfree(x)    if (istemp(x)) tfree(x); else
#define  NARGS      50          /* max args in a call */
#define  MAXNUMSIZE    50
#define PA2NUM      50          /* max number of pat,pat patterns allowed */
#define isoctdigit(c)  ((c) >= '0' && (c) <= '7')  /* multiple use of arg */
#define  NFA        20          /* cache this many dynamic fa's */

struct Frame {        /* stack frame for awk function calls */
  int nargs;        /* number of arguments in this call */
  Cell *fcncell;      /* pointer to Cell for function */
  Cell **args;      /* pointer to array of arguments after execute */
  Cell *retval;      /* return value */
};

typedef struct Keyword {
  const char *word;
  int  sub;
  int  type;
} Keyword;

struct files {
  FILE  *fp;
  const char  *fname;
  int  mode;        /* '|', 'a', 'w' => LE/LT, GT */
} *files;

GLOBALS(
  struct arg_list *vars;
  char *sep;
  struct arg_list *prog_files;
)

struct charclass {
  const char *cc_name;
  int cc_namelen;
  int (*cc_func)(int);
} charclasses[] = {
  { "alnum",  5,  isalnum },
  { "alpha",  5,  isalpha },
#ifndef HAS_ISBLANK
  { "blank",  5,  isspace }, /* was isblank */
#else
  { "blank",  5,  isblank },
#endif
  { "cntrl",  5,  iscntrl },
  { "digit",  5,  isdigit },
  { "graph",  5,  isgraph },
  { "lower",  5,  islower },
  { "print",  5,  isprint },
  { "punct",  5,  ispunct },
  { "space",  5,  isspace },
  { "upper",  5,  isupper },
  { "xdigit",  6,  isxdigit },
  { NULL,    0,  NULL },
};

extern int  infunc;
extern char  **environ;

jmp_buf env;
Node  *winner = NULL;    /* root of parse tree */
Cell  *tmps;        /* free temporary cells for execution */
static Cell  truecell  ={ OBOOL, BTRUE, 0, 0, 1.0, NUM };
Cell  *True  = &truecell;
static Cell  falsecell  ={ OBOOL, BFALSE, 0, 0, 0.0, NUM };
Cell  *False  = &falsecell;
static Cell  breakcell  ={ OJUMP, JBREAK, 0, 0, 0.0, NUM };
Cell  *jbreak  = &breakcell;
static Cell  contcell  ={ OJUMP, JCONT, 0, 0, 0.0, NUM };
Cell  *jcont  = &contcell;
static Cell  nextcell  ={ OJUMP, JNEXT, 0, 0, 0.0, NUM };
Cell  *jnext  = &nextcell;
static Cell  nextfilecell  ={ OJUMP, JNEXTFILE, 0, 0, 0.0, NUM };
Cell  *jnextfile  = &nextfilecell;
static Cell  exitcell  ={ OJUMP, JEXIT, 0, 0, 0.0, NUM };
Cell  *jexit  = &exitcell;
static Cell  retcell    ={ OJUMP, JRET, 0, 0, 0.0, NUM };
Cell  *jret  = &retcell;
static Cell  tempcell  ={ OCELL, CTEMP, 0, "", 0.0, NUM|STR|DONTFREE };
Node  *curnode = NULL;  /* the node being executed, for debugging */

int  lineno  = 1;
int  bracecnt = 0;
int  brackcnt  = 0;
int  parencnt = 0;
int  *setvec;
int  *tmpset;
int  maxsetvec = 0;
int  rtok;          /* next token in current re */
int  rlxval;
int  recsize  = RECSIZE;
int  fieldssize = RECSIZE;
int  patlen;
int  nfields  = MAXFLD;    /* last allocated slot for $i */
int  donefld;        /* 1 = implies rec broken into fields */
int  donerec;        /* 1 = record is valid (no flds have changed) */
int  lastfld  = 0;      /* last used field */
int  argno  = 1;      /* current input argument number */
int  nfatab  = 0;      /* entries in fatab */
int nfiles;
int  sc  = 0;        /* 1 => return a } right now */
int  reg  = 0;        /* 1 => return a REGEXPR now */
int  compile_time = 2;    /* for error printing: 2 = cmdline, 1 = compile, 0 = running */
int  npfile = 0;        /* number of filenames */
int  curpfile = 0;      /* current filename */
int  safe  = 0;      /* 1 => "safe" mode */
int  nframe = 0;        /* number of frames allocated */
int  paircnt;        /* number of them in use */
int  pairstack[PA2NUM];    /* state of each pat,pat */
int  dbg  = 0;

Array  *symtab;      /* main symbol table */
Array  *ARGVtab;      /* symbol table containing ARGV[...] */
Array  *ENVtab;      /* symbol table containing ENVIRON[...] */

char  **FS;        /* initial field sep */
char  **RS;        /* initial record sep */
char  **OFS;        /* output field sep */
char  **ORS;        /* output record sep */
char  **OFMT;        /* output format for numbers */
char  **CONVFMT;      /* format for conversions in getsval */
char  **SUBSEP;      /* subscript separator for a[i,j,k]; default \034 */
char  **FILENAME;      /* current filename argument */
char  *file  = "";
char  *record;
char  *fields;
char  *patbeg;
char  inputFS[100] = " ";
char  *pfile[MAX_PFILE];  /* program filenames from -f's */
char  ebuf[300];
char  *ep = ebuf;
char  yysbuf[100];    /* pushback buffer */
char  *yysptr = yysbuf;
char  *cmdname;      /* gets argv[0] for error messages */
char  *lexprog;      /* points to program argument if it exists */

Awkfloat *NF;        /* number of fields in current record */
Awkfloat *ERRNO;      /* global error no. */
Awkfloat *NR;        /* number of current record */
Awkfloat *FNR;        /* number of current record in current file */
Awkfloat *ARGC;        /* number of arguments from command line */
Awkfloat *RSTART;      /* start of re matched with ~; origin 1 (!) */
Awkfloat *RLENGTH;      /* length of same */
Awkfloat srand_seed = 1;

Cell  *fsloc;        /* FS */
Cell  *nrloc;        /* NR */
Cell  *nfloc;        /* NF */
Cell  *errloc;      /* ERRNO */
Cell  *fnrloc;      /* FNR */
Cell  *rstartloc;      /* RSTART */
Cell  *rlengthloc;    /* RLENGTH */
Cell  *symtabloc;      /* SYMTAB */
Cell  *nullloc;      /* a guaranteed empty cell */
Cell  *literal0;
Cell  **fldtab;      /* pointers to Cells */

Node  *nullnode;      /* zero&null, converted into a node for comparisons */

static int     setcnt;
static int     poscnt;
static int     firsttime = 1;
static uschar  *rlxstr;
static uschar  *prestr;  /* current position in current re */
static uschar  *lastre;  /* origin of last re */
static Cell    dollar0 = { OCELL, CFLD, NULL, "", 0.0, REC|STR|DONTFREE };
static Cell    dollar1 = { OCELL, CFLD, NULL, "", 0.0, FLD|STR|DONTFREE };

fa    *fatab[NFA];
FILE  *infile  = NULL;
FILE  *yyin = 0;

const char  *version = "version 2012_09_14";

struct Frame *fp = NULL;  /* frame pointer. bottom level unused */
struct Frame *frame = NULL;  /* base of stack frames; dynamically allocated */

int  word(char *);
int  string(void);
int  regexpr(void);

Keyword keywords[] ={  /* keep sorted: binary searched */
  { "BEGIN",  XBEGIN,    XBEGIN },
  { "END",  XEND,    XEND },
  { "ERRNO",  VARERR,   VARERR },
  { "NF",    VARNF,    VARNF },
  { "atan2",  FATAN,    BLTIN },
  { "and",  BITAND,   BITAND },
  { "break",  BREAK,    BREAK },
  { "close",  CLOSE,    CLOSE },
  { "compl",  BITCOMPL,   BITCOMPL },
  { "continue",  CONTINUE,  CONTINUE },
  { "cos",  FCOS,    BLTIN },
  { "delete",  DELETE,    DELETE },
  { "do",    DO,    DO },
  { "else",  ELSE,    ELSE },
  { "exit",  EXIT,    EXIT },
  { "exp",  FEXP,    BLTIN },
  { "fflush",  FFLUSH,    BLTIN },
  { "for",  FOR,    FOR },
  { "func",  FUNC,    FUNC },
  { "function",  FUNC,    FUNC },
  { "getline",  GETLINE,  GETLINE },
  { "gsub",  GSUB,    GSUB },
  { "if",    IF,    IF },
  { "in",    IN,    IN },
  { "index",  INDEX,    INDEX },
  { "int",  FINT,    BLTIN },
  { "length",  FLENGTH,  BLTIN },
  { "log",  FLOG,    BLTIN },
  { "lshift", BITLSHIFT,  BITLSHIFT },
  { "match",  MATCHFCN,  MATCHFCN },
  { "mktime", FMKTIME,   BLTIN   },
  { "next",  NEXT,    NEXT },
  { "nextfile",  NEXTFILE,  NEXTFILE },
  { "or",   BITOR,    BITOR },
  { "print",  PRINT,    PRINT },
  { "printf",  PRINTF,    PRINTF },
  { "rand",  FRAND,    BLTIN },
  { "return",  RETURN,    RETURN },
  { "rshift", BITRSHIFT,  BITRSHIFT },
  { "sin",  FSIN,    BLTIN },
  { "split",  SPLIT,    SPLIT },
  { "sprintf",  SPRINTF,  SPRINTF },
  { "sqrt",  FSQRT,    BLTIN },
  { "srand",  FSRAND,    BLTIN },
  { "strftime", FSTRFTIME, BLTIN },
  { "sub",  SUB,    SUB },
  { "substr",  SUBSTR,    SUBSTR },
  { "system",  FSYSTEM,  BLTIN },
  { "systime", FSYSTIME,  BLTIN },
  { "tolower",  FTOLOWER,  BLTIN },
  { "toupper",  FTOUPPER,  BLTIN },
  { "while",  WHILE,    WHILE },
  { "xor",  BITXOR,   BITXOR },
};

/* buffer memory management */

/* pbuf:  address of pointer to buffer being managed
 * psiz:  address of buffer size variable
 * minlen:  minimum length of buffer needed
 * quantum: buffer size quantum
 * pbptr:   address of movable pointer into buffer, or 0 if none
 * whatrtn: name of the calling routine if failure should cause fatal error
 *
 * return   0 for realloc failure, !=0 for success
 */

int adjbuf(char **pbuf, int *psiz, int minlen, int quantum, char **pbptr,
  const char *whatrtn)
{
  if (minlen > *psiz) {
    char *tbuf;
    int rminlen = quantum ? minlen % quantum : 0;
    int boff = pbptr ? *pbptr - *pbuf : 0;

    if (rminlen) minlen += quantum - rminlen; /* round up to next multiple of quantum */
    tbuf = (char *) realloc(*pbuf, minlen);
    dprintf(("adjbuf %s: %d %d (pbuf=%p, tbuf=%p)\n", whatrtn, *psiz, minlen, *pbuf, tbuf));
    if (tbuf == NULL) {
      if (whatrtn) FATAL("out of memory in %s", whatrtn);
      return 0;
    }
    *pbuf = tbuf;
    *psiz = minlen;
    if (pbptr) *pbptr = tbuf + boff;
  }
  return 1;
}

void run(Node *a)  /* execution of parse tree starts here */
{
  extern void stdinit(void);
  stdinit();
  execute(a);
  closeall();
}

Cell *execute(Node *u)  /* execute a node of the parse tree */
{
  Cell *(*proc)(Node **, int);
  Cell *x;
  Node *a;

  if (u == NULL) return(True);
  for (a = u; ; a = a->nnext) {
    curnode = a;
    if (isvalue(a)) {
      x = (Cell *) (a->narg[0]);
      if (isfld(x) && !donefld) fldbld();
      else if (isrec(x) && !donerec) recbld();
      return(x);
    }
    if (notlegal(a->nobj)) FATAL("illegal statement"); /* probably a Cell* but too risky to print */
    proc = proctab[a->nobj-FIRSTTOKEN];
    x = (*proc)(a->narg, a->nobj);
    if (isfld(x) && !donefld) fldbld();
    else if (isrec(x) && !donerec) recbld();
    if (isexpr(a)) return(x);
    if (isjump(x)) return(x);
    if (a->nnext == NULL) return(x);
    tempfree(x);
  }
}

Cell *program(Node **a, int n)  /* execute an awk program */
{                /* a[0] = BEGIN, a[1] = body, a[2] = END */
  Cell *x;

  if (setjmp(env) != 0) goto ex;
  if (a[0]) {    /* BEGIN */
    x = execute(a[0]);
    if (isexit(x)) return(True);
    if (isjump(x))
      FATAL("illegal break, continue, next or nextfile from BEGIN");
    tempfree(x);
  }
  if (a[1] || a[2])
    while (getrec(&record, &recsize, 1) > 0) {
      x = execute(a[1]);
      if (isexit(x)) break;
      tempfree(x);
    }
  ex:
  if (setjmp(env) != 0) goto ex1; /* handles exit within END */
  if (a[2]) {    /* END */
    x = execute(a[2]);
    if (isbreak(x) || isnext(x) || iscont(x))
      FATAL("illegal break, continue, next or nextfile from END");
    tempfree(x);
  }
  ex1:
  return(True);
}

Cell *call(Node **a, int n)  /* function call.  very kludgy and fragile */
{
  static Cell newcopycell = { OCELL, CCOPY, 0, "", 0.0, NUM|STR|DONTFREE };
  int i, ncall, ndef;
  int freed = 0; /* handles potential double freeing when fcn & param share a tempcell */
  Node *x;
  Cell *args[NARGS], *oargs[NARGS];  /* BUG: fixed size arrays */
  Cell *y, *z, *fcn;
  char *s;

  fcn = execute(a[0]);  /* the function itself */
  s = fcn->nval;
  if (!isfcn(fcn))
    FATAL("calling undefined function %s", s);
  if (frame == NULL) {
    fp = frame = (struct Frame *) calloc(nframe += 100, sizeof(struct Frame));
    if (frame == NULL)
      FATAL("out of space for stack frames calling %s", s);
  }
  for (ncall = 0, x = a[1]; x != NULL; x = x->nnext) ncall++; /* args in call */
  ndef = (int) fcn->fval;      /* args in defn */
  dprintf( ("calling %s, %d args (%d in defn), fp=%d\n", s, ncall, ndef, (int) (fp-frame)) );
  if (ncall > ndef)
    WARNING("function %s called with %d args, uses only %d", s, ncall, ndef);
  if (ncall + ndef > NARGS)
    FATAL("function %s has %d arguments, limit %d", s, ncall+ndef, NARGS);
  for (i = 0, x = a[1]; x != NULL; i++, x = x->nnext) {  /* get call args */
    dprintf( ("evaluate args[%d], fp=%d:\n", i, (int) (fp-frame)) );
    y = execute(x);
    oargs[i] = y;
    dprintf( ("args[%d]: %s %f <%s>, t=%o\n",
         i, NN(y->nval), y->fval, isarr(y) ? "(array)" : NN(y->sval), y->tval) );
    if (isfcn(y))
      FATAL("can't use function %s as argument in %s", y->nval, s);
    if (isarr(y)) args[i] = y;  /* arrays by ref */
    else args[i] = copycell(y);
    tempfree(y);
  }
  for ( ; i < ndef; i++) {  /* add null args for ones not provided */
    args[i] = gettemp();
    *args[i] = newcopycell;
  }
  fp++;  /* now ok to up frame */
  if (fp >= frame + nframe) {
    int dfp = fp - frame;  /* old index */
    frame = (struct Frame *)
    realloc((char *) frame, (nframe += 100) * sizeof(struct Frame));
    if (frame == NULL) FATAL("out of space for stack frames in %s", s);
    fp = frame + dfp;
  }
  fp->fcncell = fcn;
  fp->args = args;
  fp->nargs = ndef;  /* number defined with (excess are locals) */
  fp->retval = gettemp();

  dprintf( ("start exec of %s, fp=%d\n", s, (int) (fp-frame)) );
  y = execute((Node *)(fcn->sval));  /* execute body */
  dprintf( ("finished exec of %s, fp=%d\n", s, (int) (fp-frame)) );

  for (i = 0; i < ndef; i++) {
    Cell *t = fp->args[i];
    if (isarr(t)) {
      if (t->csub == CCOPY) {
        if (i >= ncall) {
          freesymtab(t);
          t->csub = CTEMP;
          tempfree(t);
        } else {
          oargs[i]->tval = t->tval;
          oargs[i]->tval &= ~(STR|NUM|DONTFREE);
          oargs[i]->sval = t->sval;
          tempfree(t);
        }
      }
    } else if (t != y) {  /* kludge to prevent freeing twice */
      t->csub = CTEMP;
      tempfree(t);
    } else if (t == y && t->csub == CCOPY) {
      t->csub = CTEMP;
      tempfree(t);
      freed = 1;
    }
  }
  tempfree(fcn);
  if (isexit(y) || isnext(y)) return y;
  if (freed == 0) {tempfree(y);}  /* don't free twice! */
  z = fp->retval;      /* return value */
  dprintf( ("%s returns %g |%s| %o\n", s, getfval(z), getsval(z), z->tval) );
  fp--;
  return(z);
}

Cell *copycell(Cell *x)  /* make a copy of a cell in a temp */
{
  Cell *y;

  y = gettemp();
  y->csub = CCOPY;  /* prevents freeing until call is over */
  y->nval = x->nval;  /* BUG? */
  if (isstr(x)) y->sval = tostring(x->sval);
  y->fval = x->fval;
  y->tval = x->tval & ~(CON|FLD|REC|DONTFREE);  /* copy is not constant or field */
              /* is DONTFREE right? */
  return y;
}

Cell *arg(Node **a, int n)  /* nth argument of a function */
{

  n = ptoi(a[0]);  /* argument number, counting from 0 */
  dprintf( ("arg(%d), fp->nargs=%d\n", n, fp->nargs) );
  if (n+1 > fp->nargs)
    FATAL("argument #%d of function %s was not supplied",
      n+1, fp->fcncell->nval);
  return fp->args[n];
}

Cell *jump(Node **a, int n)  /* break, continue, next, nextfile, return */
{
  Cell *y;

  switch (n) {
  case EXIT:
    if (a[0] != NULL) {
      y = execute(a[0]);
      errorflag = (int) getfval(y);
      tempfree(y);
    }
    longjmp(env, 1);
  case RETURN:
    if (a[0] != NULL) {
      y = execute(a[0]);
      if ((y->tval & (STR|NUM)) == (STR|NUM)) {
        setsval(fp->retval, getsval(y));
        fp->retval->fval = getfval(y);
        fp->retval->tval |= NUM;
      }
      else if (y->tval & STR) setsval(fp->retval, getsval(y));
      else if (y->tval & NUM) setfval(fp->retval, getfval(y));
      else FATAL("bad type variable %d", y->tval); /* can't happen */
      tempfree(y);
    }
    return(jret);
  case NEXT: return(jnext);
  case NEXTFILE:
    nextfile();
    return(jnextfile);
  case BREAK: return(jbreak);
  case CONTINUE: return(jcont);
  default: FATAL("illegal jump type %d", n);   /* can't happen */
  }
  return 0;  /* not reached */
}

Cell *awkgetline(Node **a, int n)  /* get next line from specific input */
{    /* a[0] is variable, a[1] is operator, a[2] is filename */
  Cell *r, *x;
  extern Cell **fldtab;
  FILE *fp;
  char *buf;
  int bufsize = recsize;
  int mode;

  if ((buf = (char *) malloc(bufsize)) == NULL)
    FATAL("out of memory in getline");

  fflush(stdout);  /* in case someone is waiting for a prompt */
  r = gettemp();
  if (a[1] != NULL) {    /* getline < file */
    x = execute(a[2]);    /* filename */
    mode = ptoi(a[1]);
    if (mode == '|')    /* input pipe */
      mode = LE;  /* arbitrary flag */
    fp = openfile(mode, getsval(x));
    tempfree(x);
    if (fp == NULL) n = -1;
    else n = readrec(&buf, &bufsize, fp);
    if (n <= 0) {
      ;
    } else if (a[0] != NULL) {  /* getline var <file */
      x = execute(a[0]);
      setsval(x, buf);
      tempfree(x);
    } else {      /* getline <file */
      setsval(fldtab[0], buf);
      if (is_number(fldtab[0]->sval)) {
        fldtab[0]->fval = atof(fldtab[0]->sval);
        fldtab[0]->tval |= NUM;
      }
    }
  } else {      /* bare getline; use current input */
    if (a[0] == NULL)  /* getline */
      n = getrec(&record, &recsize, 1);
    else {      /* getline var */
      n = getrec(&buf, &bufsize, 0);
      x = execute(a[0]);
      setsval(x, buf);
      tempfree(x);
    }
  }
  setfval(r, (Awkfloat) n);
  free(buf);
  return r;
}

Cell *getnf(Node **a, int n)  /* get NF */
{
  if (donefld == 0) fldbld();
  return (Cell *) a[0];
}

Cell *geterrno(Node **a, int n)  /* get ERRNO */
{
  setfval(errloc, (Awkfloat) errno);
  return (Cell *) a[0];
}

Cell *array(Node **a, int n)  /* a[0] is symtab, a[1] is list of subscripts */
{
  Cell *x, *y, *z;
  char *s;
  Node *np;
  char *buf;
  int bufsz = recsize;
  int nsub = strlen(*SUBSEP);

  if ((buf = (char *) malloc(bufsz)) == NULL)
    FATAL("out of memory in array");

  x = execute(a[0]);  /* Cell* for symbol table */
  buf[0] = 0;
  for (np = a[1]; np; np = np->nnext) {
    y = execute(np);  /* subscript */
    s = getsval(y);
    if (!adjbuf(&buf, &bufsz, strlen(buf)+strlen(s)+nsub+1, recsize, 0, "array"))
      FATAL("out of memory for %s[%s...]", x->nval, buf);
    strcat(buf, s);
    if (np->nnext) strcat(buf, *SUBSEP);
    tempfree(y);
  }
  if (!isarr(x)) {
       dprintf( ("making %s into an array\n", NN(x->nval)) );
    if (freeable(x)) xfree(x->sval);
    x->tval &= ~(STR|NUM|DONTFREE);
    x->tval |= ARR;
    x->sval = (char *) makesymtab(NSYMTAB);
  }
  z = setsymtab(buf, "", 0.0, STR|NUM, (Array *) x->sval);
  z->ctype = OCELL;
  z->csub = CVAR;
  tempfree(x);
  free(buf);
  return(z);
}

Cell *awkdelete(Node **a, int n)  /* a[0] is symtab, a[1] is list of subscripts */
{
  Cell *x, *y;
  Node *np;
  char *s;
  int nsub = strlen(*SUBSEP);

  x = execute(a[0]);  /* Cell* for symbol table */
  if (!isarr(x)) return True;
  if (a[1] == 0) {  /* delete the elements, not the table */
    freesymtab(x);
    x->tval &= ~STR;
    x->tval |= ARR;
    x->sval = (char *) makesymtab(NSYMTAB);
  } else {
    int bufsz = recsize;
    char *buf;
    if ((buf = (char *) malloc(bufsz)) == NULL)
      FATAL("out of memory in adelete");
    buf[0] = 0;
    for (np = a[1]; np; np = np->nnext) {
      y = execute(np);  /* subscript */
      s = getsval(y);
      if (!adjbuf(&buf, &bufsz, strlen(buf)+strlen(s)+nsub+1, recsize, 0, "awkdelete"))
        FATAL("out of memory deleting %s[%s...]", x->nval, buf);
      strcat(buf, s);
      if (np->nnext) strcat(buf, *SUBSEP);
      tempfree(y);
    }
    freeelem(x, buf);
    free(buf);
  }
  tempfree(x);
  return True;
}

Cell *intest(Node **a, int n)  /* a[0] is index (list), a[1] is symtab */
{
  Cell *x, *ap, *k;
  Node *p;
  char *buf;
  char *s;
  int bufsz = recsize;
  int nsub = strlen(*SUBSEP);

  ap = execute(a[1]);  /* array name */
  if (!isarr(ap)) {
       dprintf( ("making %s into an array\n", ap->nval) );
    if (freeable(ap)) xfree(ap->sval);
    ap->tval &= ~(STR|NUM|DONTFREE);
    ap->tval |= ARR;
    ap->sval = (char *) makesymtab(NSYMTAB);
  }
  if ((buf = (char *) malloc(bufsz)) == NULL) {
    FATAL("out of memory in intest");
  }
  buf[0] = 0;
  for (p = a[0]; p; p = p->nnext) {
    x = execute(p);  /* expr */
    s = getsval(x);
    if (!adjbuf(&buf, &bufsz, strlen(buf)+strlen(s)+nsub+1, recsize, 0, "intest"))
      FATAL("out of memory deleting %s[%s...]", x->nval, buf);
    strcat(buf, s);
    tempfree(x);
    if (p->nnext) strcat(buf, *SUBSEP);
  }
  k = lookup(buf, (Array *) ap->sval);
  tempfree(ap);
  free(buf);
  if (k == NULL) return(False);
  else return(True);
}

Cell *matchop(Node **a, int n)  /* ~ and match() */
{
  Cell *x, *y;
  char *s, *t;
  int i;
  fa *pfa;
  int (*mf)(fa *, const char *) = match, mode = 0;

  if (n == MATCHFCN) {
    mf = pmatch;
    mode = 1;
  }
  x = execute(a[1]);  /* a[1] = target text */
  s = getsval(x);
  if (a[0] == 0)    /* a[1] == 0: already-compiled reg expr */
    i = (*mf)((fa *) a[2], s);
  else {
    y = execute(a[2]);  /* a[2] = regular expr */
    t = getsval(y);
    pfa = makedfa(t, mode);
    i = (*mf)(pfa, s);
    tempfree(y);
  }
  tempfree(x);
  if (n == MATCHFCN) {
    int start = patbeg - s + 1;
    if (patlen < 0) start = 0;
    setfval(rstartloc, (Awkfloat) start);
    setfval(rlengthloc, (Awkfloat) patlen);
    x = gettemp();
    x->tval = NUM;
    x->fval = start;
    return x;
  } else if ((n == MATCH && i == 1) || (n == NOTMATCH && i == 0))
    return(True);
  else return(False);
}

Cell *boolop(Node **a, int n)  /* a[0] || a[1], a[0] && a[1], !a[0] */
{
  Cell *x, *y;
  int i;

  x = execute(a[0]);
  i = istrue(x);
  tempfree(x);
  switch (n) {
  case BOR:
    if (i) return(True);
    y = execute(a[1]);
    i = istrue(y);
    tempfree(y);
    if (i) return(True);
    else return(False);
  case AND:
    if ( !i ) return(False);
    y = execute(a[1]);
    i = istrue(y);
    tempfree(y);
    if (i) return(True);
    else return(False);
  case NOT:
    if (i) return(False);
    else return(True);
  default:  /* can't happen */
    FATAL("unknown boolean operator %d", n);
    break;
  }
  return 0;  /*NOTREACHED*/
}

Cell *bitops(Node **a, int n)  /* or(a[0],a[1]) and(a[0],a[1]) compl(a[0]) */
{                /* lshit(a[0], a[1]) ... etc */
  Cell *x, *y, *z;
  Awkfloat s1, s2, r = 0;
  x = execute(a[0]);
  s1 = getfval(x);
  y = execute(a[1]);
  s2 = getfval(y);

  switch (n) {
    case BITOR:
      r = (Awkfloat)((unsigned long)s1 | (unsigned long)s2);
      break;
    case BITAND:
      r = (Awkfloat)((unsigned long)s1 & (unsigned long)s2);
      break;
    case BITCOMPL:
      r = (Awkfloat)(~ (unsigned long) s1);
      break;
    case BITXOR:
      r = (Awkfloat)((unsigned long)s1 ^ (unsigned long)s2);
      break;
    case BITLSHIFT:
      r = (Awkfloat)((unsigned long)s1 << (unsigned long)s2);
      break;
    case BITRSHIFT:
      r = (Awkfloat)((unsigned long)s1 >> (unsigned long)s2);
      break;
    default:  /* can't happen */
      FATAL("unknown boolean operator %d", n);
      break;
  }

  z = gettemp();
  setfval(z, r);
  tempfree(x);
  tempfree(y);
  return(z);
}

Cell *relop(Node **a, int n)  /* a[0 < a[1], etc. */
{
  int i;
  Cell *x, *y;
  Awkfloat j;

  x = execute(a[0]);
  y = execute(a[1]);
  if (x->tval&NUM && y->tval&NUM) {
    j = x->fval - y->fval;
    i = j<0? -1: (j>0? 1: 0);
  } else i = strcmp(getsval(x), getsval(y));

  tempfree(x);
  tempfree(y);
  switch (n) {
  case LT:  if (i<0) return(True);
      else return(False);
  case LE:  if (i<=0) return(True);
      else return(False);
  case NE:  if (i!=0) return(True);
      else return(False);
  case EQ:  if (i == 0) return(True);
      else return(False);
  case GE:  if (i>=0) return(True);
      else return(False);
  case GT:  if (i>0) return(True);
      else return(False);
  default:  /* can't happen */
    FATAL("unknown relational operator %d", n);
  }
  return 0;  /*NOTREACHED*/
}

void tfree(Cell *a)  /* free a tempcell */
{
  if (freeable(a)) {
    dprintf( ("freeing %s %s %o\n", NN(a->nval), NN(a->sval), a->tval) );
    xfree(a->sval);
  }
  if (a == tmps) FATAL("tempcell list is curdled");
  a->cnext = tmps;
  tmps = a;
}

Cell *gettemp(void)  /* get a tempcell */
{  int i;
  Cell *x;

  if (!tmps) {
    tmps = (Cell *) calloc(100, sizeof(Cell));
    if (!tmps) FATAL("out of space for temporaries");
    for(i = 1; i < 100; i++) tmps[i-1].cnext = &tmps[i];
    tmps[i-1].cnext = 0;
  }
  x = tmps;
  tmps = x->cnext;
  *x = tempcell;
  return(x);
}

Cell *indirect(Node **a, int n)  /* $( a[0] ) */
{
  Awkfloat val;
  Cell *x;
  int m;
  char *s;

  x = execute(a[0]);
  val = getfval(x);  /* freebsd: defend against super large field numbers */
  if ((Awkfloat)INT_MAX < val)
    FATAL("trying to access out of range field %s", x->nval);
  m = (int) val;
  if (m == 0 && !is_number(s = getsval(x)))  /* suspicion! */
    FATAL("illegal field $(%s), name \"%s\"", s, x->nval);
    /* BUG: can x->nval ever be null??? */
  tempfree(x);
  x = fieldadr(m);
  x->ctype = OCELL;  /* BUG?  why are these needed? */
  x->csub = CFLD;
  return(x);
}

Cell *substr(Node **a, int nnn)    /* substr(a[0], a[1], a[2]) */
{
  int k, m, n;
  char *s;
  int temp;
  Cell *x, *y, *z = 0;

  x = execute(a[0]);
  y = execute(a[1]);
  if (a[2] != 0)
    z = execute(a[2]);
  s = getsval(x);
  k = strlen(s) + 1;
  if (k <= 1) {
    tempfree(x);
    tempfree(y);
    if (a[2] != 0){ tempfree(z);}
    x = gettemp();
    setsval(x, "");
    return(x);
  }
  m = (int) getfval(y);
  if (m <= 0) m = 1;
  else if (m > k) m = k;
  tempfree(y);
  if (a[2] != 0) {
    n = (int) getfval(z);
    tempfree(z);
  } else n = k - 1;
  if (n < 0) n = 0;
  else if (n > k - m) n = k - m;
  dprintf( ("substr: m=%d, n=%d, s=%s\n", m, n, s) );
  y = gettemp();
  temp = s[n+m-1];  /* with thanks to John Linderman */
  s[n+m-1] = '\0';
  setsval(y, s + m - 1);
  s[n+m-1] = temp;
  tempfree(x);
  return(y);
}

Cell *sindex(Node **a, int nnn)    /* index(a[0], a[1]) */
{
  Cell *x, *y, *z;
  char *s1, *s2, *p1, *p2, *q;
  Awkfloat v = 0.0;

  x = execute(a[0]);
  s1 = getsval(x);
  y = execute(a[1]);
  s2 = getsval(y);

  z = gettemp();
  for (p1 = s1; *p1 != '\0'; p1++) {
    for (q=p1, p2=s2; *p2 != '\0' && *q == *p2; q++, p2++)
      ;
    if (*p2 == '\0') {
      v = (Awkfloat) (p1 - s1 + 1);  /* origin 1 */
      break;
    }
  }
  tempfree(x);
  tempfree(y);
  setfval(z, v);
  return(z);
}

int format(char **pbuf, int *pbufsize, const char *s, Node *a)  /* printf-like conversions */
{
  char *fmt;
  char *p, *t;
  const char *os;
  Cell *x;
  int flag = 0, n;
  int fmtwd; /* format width */
  int fmtsz = recsize;
  char *buf = *pbuf;
  int bufsize = *pbufsize;

  os = s;
  p = buf;
  if ((fmt = (char *) malloc(fmtsz)) == NULL)
    FATAL("out of memory in format()");
  while (*s) {
    adjbuf(&buf, &bufsize, MAXNUMSIZE+1+p-buf, recsize, &p, "format1");
    if (*s != '%') {
      *p++ = *s++;
      continue;
    }
    if (*(s+1) == '%') {
      *p++ = '%';
      s += 2;
      continue;
    }
    /* have to be real careful in case this is a huge number, eg, %100000d */
    fmtwd = atoi(s+1);
    if (fmtwd < 0) fmtwd = -fmtwd;
    adjbuf(&buf, &bufsize, fmtwd+1+p-buf, recsize, &p, "format2");
    for (t = fmt; (*t++ = *s) != '\0'; s++) {
      if (!adjbuf(&fmt, &fmtsz, MAXNUMSIZE+1+t-fmt, recsize, &t, "format3"))
        FATAL("format item %.30s... ran format() out of memory", os);
      if (isalpha((uschar)*s) && *s != 'l' && *s != 'h' && *s != 'L')
        break;  /* the ansi panoply */
      if (*s == '*') {
        x = execute(a);
        a = a->nnext;
        sprintf(t-1, "%d", fmtwd=(int) getfval(x));
        if (fmtwd < 0) fmtwd = -fmtwd;
        adjbuf(&buf, &bufsize, fmtwd+1+p-buf, recsize, &p, "format");
        t = fmt + strlen(fmt);
        tempfree(x);
      }
    }
    *t = '\0';
    if (fmtwd < 0) fmtwd = -fmtwd;
    adjbuf(&buf, &bufsize, fmtwd+1+p-buf, recsize, &p, "format4");

    switch (*s) {
    case 'f': /*FALL_THROUGH*/
    case 'e': /*FALL_THROUGH*/
    case 'g': /*FALL_THROUGH*/
    case 'E': /*FALL_THROUGH*/
    case 'G':
      flag = 'f';
      break;
    case 'd': /*FALL_THROUGH*/
    case 'i':
      flag = 'd';
      if(*(s-1) == 'l') break;
      *(t-1) = 'l';
      *t = 'd';
      *++t = '\0';
      break;
    case 'o': /*FALL_THROUGH*/
    case 'x': /*FALL_THROUGH*/
    case 'X': /*FALL_THROUGH*/
    case 'u':
      flag = *(s-1) == 'l' ? 'd' : 'u';
      break;
    case 's':
      flag = 's';
      break;
    case 'c':
      flag = 'c';
      break;
    default:
      WARNING("weird printf conversion %s", fmt);
      flag = '?';
      break;
    }
    if (a == NULL) FATAL("not enough args in printf(%s)", os);
    x = execute(a);
    a = a->nnext;
    n = MAXNUMSIZE;
    if (fmtwd > n) n = fmtwd;
    adjbuf(&buf, &bufsize, 1+n+p-buf, recsize, &p, "format5");
    switch (flag) {
    case '?':  sprintf(p, "%s", fmt);  /* unknown, so dump it too */
      t = getsval(x);
      n = strlen(t);
      if (fmtwd > n) n = fmtwd;
      adjbuf(&buf, &bufsize, 1+strlen(p)+n+p-buf, recsize, &p, "format6");
      p += strlen(p);
      sprintf(p, "%s", t);
      break;
    case 'f':  sprintf(p, fmt, getfval(x)); break;
    case 'd':  sprintf(p, fmt, (long) getfval(x)); break;
    case 'u':  sprintf(p, fmt, (int) getfval(x)); break;
    case 's':
      t = getsval(x);
      n = strlen(t);
      if (fmtwd > n) n = fmtwd;
      if (!adjbuf(&buf, &bufsize, 1+n+p-buf, recsize, &p, "format7"))
        FATAL("huge string/format (%d chars) in printf %.30s... ran format() out of memory", n, t);
      sprintf(p, fmt, t);
      break;
    case 'c':
      if (isnum(x)) {
        if (getfval(x)) sprintf(p, fmt, (int) getfval(x));
        else {
          *p++ = '\0'; /* explicit null byte */
          *p = '\0';   /* next output will start here */
        }
      } else sprintf(p, fmt, getsval(x)[0]);
      break;
    default:
      FATAL("can't happen: bad conversion %c in format()", flag);
    }
    tempfree(x);
    p += strlen(p);
    s++;
  }
  *p = '\0';
  free(fmt);
  for ( ; a; a = a->nnext) execute(a);  /* evaluate any remaining args */
  *pbuf = buf;
  *pbufsize = bufsize;
  return p - buf;
}

Cell *awksprintf(Node **a, int n)    /* sprintf(a[0]) */
{
  Cell *x;
  Node *y;
  char *buf;
  int bufsz=3*recsize;

  if ((buf = (char *) malloc(bufsz)) == NULL)
    FATAL("out of memory in awksprintf");
  y = a[0]->nnext;
  x = execute(a[0]);
  if (format(&buf, &bufsz, getsval(x), y) == -1)
    FATAL("sprintf string %.30s... too long.  can't happen.", buf);
  tempfree(x);
  x = gettemp();
  x->sval = buf;
  x->tval = STR;
  return(x);
}

Cell *awkprintf(Node **a, int n)    /* printf */
{  /* a[0] is list of args, starting with format string */
  /* a[1] is redirection operator, a[2] is redirection file */
  FILE *fp;
  Cell *x;
  Node *y;
  char *buf;
  int len;
  int bufsz=3*recsize;

  if ((buf = (char *) malloc(bufsz)) == NULL)
    FATAL("out of memory in awkprintf");
  y = a[0]->nnext;
  x = execute(a[0]);
  if ((len = format(&buf, &bufsz, getsval(x), y)) == -1)
    FATAL("printf string %.30s... too long.  can't happen.", buf);
  tempfree(x);
  if (a[1] == NULL) {
    /* fputs(buf, stdout); */
    fwrite(buf, len, 1, stdout);
    if (ferror(stdout)) FATAL("write error on stdout");
  } else {
    fp = redirect(ptoi(a[1]), a[2]);
    /* fputs(buf, fp); */
    fwrite(buf, len, 1, fp);
    fflush(fp);
    if (ferror(fp)) FATAL("write error on %s", filename(fp));
  }
  free(buf);
  return(True);
}

Cell *arith(Node **a, int n)  /* a[0] + a[1], etc.  also -a[0] */
{
  Awkfloat i, j = 0;
  double v;
  Cell *x, *y, *z;

  x = execute(a[0]);
  i = getfval(x);
  tempfree(x);
  if (n != UMINUS) {
    y = execute(a[1]);
    j = getfval(y);
    tempfree(y);
  }
  z = gettemp();
  switch (n) {
  case ADD:
    i += j;
    break;
  case MINUS:
    i -= j;
    break;
  case MULT:
    i *= j;
    break;
  case DIVIDE:
    if (j == 0) FATAL("division by zero");
    i /= j;
    break;
  case MOD:
    if (j == 0) FATAL("division by zero in mod");
    modf(i/j, &v);
    i = i - j * v;
    break;
  case UMINUS:
    i = -i;
    break;
  case POWER:
    if (j >= 0 && modf(j, &v) == 0.0) i = ipow(i, (int) j); /* pos integer exponent */
    else i = errcheck(pow(i, j), "pow");
    break;
  default: FATAL("illegal arithmetic operator %d", n); /* can't happen */
  }
  setfval(z, i);
  return(z);
}

double ipow(double x, int n)  /* x**n.  ought to be done by pow, but isn't always */
{
  double v;
  if (n <= 0) return 1;
  v = ipow(x, n/2);
  if (n % 2 == 0) return v * v;
  else return x * v * v;
}

Cell *incrdecr(Node **a, int n)    /* a[0]++, etc. */
{
  Cell *x, *z;
  int k;
  Awkfloat xf;

  x = execute(a[0]);
  xf = getfval(x);
  k = (n == PREINCR || n == POSTINCR) ? 1 : -1;
  if (n == PREINCR || n == PREDECR) {
    setfval(x, xf + k);
    return(x);
  }
  z = gettemp();
  setfval(z, xf);
  setfval(x, xf + k);
  tempfree(x);
  return(z);
}

Cell *assign(Node **a, int n)  /* a[0] = a[1], a[0] += a[1], etc. */
{    /* this is subtle; don't muck with it. */
  Cell *x, *y;
  Awkfloat xf, yf;
  double v;

  y = execute(a[1]);
  x = execute(a[0]);
  if (n == ASSIGN) {  /* ordinary assignment */
    if (x == y && !(x->tval & (FLD|REC)))  /* self-assignment: */
      ;    /* leave alone unless it's a field */
    else if ((y->tval & (STR|NUM)) == (STR|NUM)) {
      setsval(x, getsval(y));
      x->fval = getfval(y);
      x->tval |= NUM;
      if(x == errloc) errno = x->fval;
    }
    else if (isstr(y)) setsval(x, getsval(y));
    else if (isnum(y)) setfval(x, getfval(y));
    else funnyvar(y, "read value of");
    tempfree(y);
    return(x);
  }
  xf = getfval(x);
  yf = getfval(y);
  switch (n) {
  case ADDEQ:
    xf += yf;
    break;
  case SUBEQ:
    xf -= yf;
    break;
  case MULTEQ:
    xf *= yf;
    break;
  case DIVEQ:
    if (yf == 0) FATAL("division by zero in /=");
    xf /= yf;
    break;
  case MODEQ:
    if (yf == 0) FATAL("division by zero in %%=");
    modf(xf/yf, &v);
    xf = xf - yf * v;
    break;
  case POWEQ:
    if (yf >= 0 && modf(yf, &v) == 0.0) xf = ipow(xf, (int) yf); /* pos integer exponent */
    else xf = errcheck(pow(xf, yf), "pow");
    break;
  default:
    FATAL("illegal assignment operator %d", n);
    break;
  }
  tempfree(y);
  setfval(x, xf);
  return(x);
}

Cell *cat(Node **a, int q)  /* a[0] cat a[1] */
{
  Cell *x, *y, *z;
  int n1, n2;
  char *s;

  x = execute(a[0]);
  y = execute(a[1]);
  getsval(x);
  getsval(y);
  n1 = strlen(x->sval);
  n2 = strlen(y->sval);
  s = (char *) malloc(n1 + n2 + 1);
  if (s == NULL)
    FATAL("out of space concatenating %.15s... and %.15s...",
      x->sval, y->sval);
  strcpy(s, x->sval);
  strcpy(s+n1, y->sval);
  tempfree(x);
  tempfree(y);
  z = gettemp();
  z->sval = s;
  z->tval = STR;
  return(z);
}

Cell *pastat(Node **a, int n)  /* a[0] { a[1] } */
{
  Cell *x;

  if (a[0] == 0) x = execute(a[1]);
  else {
    x = execute(a[0]);
    if (istrue(x)) {
      tempfree(x);
      x = execute(a[1]);
    }
  }
  return x;
}

Cell *dopa2(Node **a, int n)  /* a[0], a[1] { a[2] } */
{
  Cell *x;
  int pair;

  pair = ptoi(a[3]);
  if (pairstack[pair] == 0) {
    x = execute(a[0]);
    if (istrue(x)) pairstack[pair] = 1;
    tempfree(x);
  }
  if (pairstack[pair] == 1) {
    x = execute(a[1]);
    if (istrue(x)) pairstack[pair] = 0;
    tempfree(x);
    x = execute(a[2]);
    return(x);
  }
  return(False);
}

Cell *split(Node **a, int nnn)  /* split(a[0], a[1], a[2]); a[3] is type */
{
  Cell *x = 0, *y, *ap;
  char *s;
  int sep;
  char *t, temp, num[50], *fs = 0;
  int n, tempstat, arg3type;

  y = execute(a[0]);  /* source string */
  s = getsval(y);
  arg3type = ptoi(a[3]);
  if (a[2] == 0) fs = *FS; /* fs string */
  else if (arg3type == STRING) {  /* split(str,arr,"string") */
    x = execute(a[2]);
    fs = getsval(x);
  } else if (arg3type == REGEXPR) fs = "(regexpr)";  /* split(str,arr,/regexpr/) */
  else FATAL("illegal type of split");
  sep = *fs;
  ap = execute(a[1]);  /* array name */
  freesymtab(ap);
  dprintf( ("split: s=|%s|, a=%s, sep=|%s|\n", s, NN(ap->nval), fs) );
  ap->tval &= ~STR;
  ap->tval |= ARR;
  ap->sval = (char *) makesymtab(NSYMTAB);

  n = 0;
  if (arg3type == REGEXPR && strlen((char*) ((fa*) a[2])->restr) == 0) {
    /* split(s, a, //); have to arrange that it looks like empty sep */
    arg3type = 0;
    fs = "";
    sep = 0;
  }
  if (*s != '\0' && (strlen(fs) > 1 || arg3type == REGEXPR)) {  /* reg expr */
    fa *pfa;
    if (arg3type == REGEXPR) pfa = (fa *) a[2];  /* it's ready already */
    else pfa = makedfa(fs, 1);
    if (nematch(pfa,s)) {
      tempstat = pfa->initstat;
      pfa->initstat = 2;
      do {
        n++;
        sprintf(num, "%d", n);
        temp = *patbeg;
        *patbeg = '\0';
        if (is_number(s)) setsymtab(num, s, atof(s), STR|NUM, (Array *) ap->sval);
        else setsymtab(num, s, 0.0, STR, (Array *) ap->sval);
        *patbeg = temp;
        s = patbeg + patlen;
        if (*(patbeg+patlen-1) == 0 || *s == 0) {
          n++;
          sprintf(num, "%d", n);
          setsymtab(num, "", 0.0, STR, (Array *) ap->sval);
          pfa->initstat = tempstat;
          goto spdone;
        }
      } while (nematch(pfa,s));
      pfa->initstat = tempstat;   /* bwk: has to be here to reset */
              /* cf gsub and refldbld */
    }
    n++;
    sprintf(num, "%d", n);
    if (is_number(s)) setsymtab(num, s, atof(s), STR|NUM, (Array *) ap->sval);
    else setsymtab(num, s, 0.0, STR, (Array *) ap->sval);
  spdone:
    pfa = NULL;
  } else if (sep == ' ') {
    for (n = 0; ; ) {
      while (*s == ' ' || *s == '\t' || *s == '\n') s++;
      if (*s == 0) break;
      n++;
      t = s;
      do s++;
      while (*s!=' ' && *s!='\t' && *s!='\n' && *s!='\0');
      temp = *s;
      *s = '\0';
      sprintf(num, "%d", n);
      if (is_number(t)) setsymtab(num, t, atof(t), STR|NUM, (Array *) ap->sval);
      else setsymtab(num, t, 0.0, STR, (Array *) ap->sval);
      *s = temp;
      if (*s != 0) s++;
    }
  } else if (sep == 0) {  /* new: split(s, a, "") => 1 char/elem */
    for (n = 0; *s != 0; s++) {
      char buf[2];
      n++;
      sprintf(num, "%d", n);
      buf[0] = *s;
      buf[1] = 0;
      if (isdigit((uschar)buf[0])) setsymtab(num, buf, atof(buf), STR|NUM, (Array *) ap->sval);
      else setsymtab(num, buf, 0.0, STR, (Array *) ap->sval);
    }
  } else if (*s != 0) {
    for (;;) {
      n++;
      t = s;
      while (*s != sep && *s != '\n' && *s != '\0')s++;
      temp = *s;
      *s = '\0';
      sprintf(num, "%d", n);
      if (is_number(t)) setsymtab(num, t, atof(t), STR|NUM, (Array *) ap->sval);
      else setsymtab(num, t, 0.0, STR, (Array *) ap->sval);
      *s = temp;
      if (*s++ == 0) break;
    }
  }
  tempfree(ap);
  tempfree(y);
  if (a[2] != 0 && arg3type == STRING) { tempfree(x); }
  x = gettemp();
  x->tval = NUM;
  x->fval = n;
  return(x);
}

Cell *condexpr(Node **a, int n)  /* a[0] ? a[1] : a[2] */
{
  Cell *x;

  x = execute(a[0]);
  if (istrue(x)) {
    tempfree(x);
    x = execute(a[1]);
  } else {
    tempfree(x);
    x = execute(a[2]);
  }
  return(x);
}

Cell *ifstat(Node **a, int n)  /* if (a[0]) a[1]; else a[2] */
{
  Cell *x;

  x = execute(a[0]);
  if (istrue(x)) {
    tempfree(x);
    x = execute(a[1]);
  } else if (a[2] != 0) {
    tempfree(x);
    x = execute(a[2]);
  }
  return(x);
}

Cell *whilestat(Node **a, int n)  /* while (a[0]) a[1] */
{
  Cell *x;

  for (;;) {
    x = execute(a[0]);
    if (!istrue(x)) return(x);
    tempfree(x);
    x = execute(a[1]);
    if (isbreak(x)) {
      x = True;
      return(x);
    }
    if (isnext(x) || isexit(x) || isret(x)) return(x);
    tempfree(x);
  }
}

Cell *dostat(Node **a, int n)  /* do a[0]; while(a[1]) */
{
  Cell *x;

  for (;;) {
    x = execute(a[0]);
    if (isbreak(x)) return True;
    if (isnext(x) || isexit(x) || isret(x)) return(x);
    tempfree(x);
    x = execute(a[1]);
    if (!istrue(x)) return(x);
    tempfree(x);
  }
}

Cell *forstat(Node **a, int n)  /* for (a[0]; a[1]; a[2]) a[3] */
{
  Cell *x;

  x = execute(a[0]);
  tempfree(x);
  for (;;) {
    if (a[1]!=0) {
      x = execute(a[1]);
      if (!istrue(x)) return(x);
      else tempfree(x);
    }
    x = execute(a[3]);
    if (isbreak(x)) return True;     /* turn off break */
    if (isnext(x) || isexit(x) || isret(x)) return(x);
    tempfree(x);
    x = execute(a[2]);
    tempfree(x);
  }
}

Cell *instat(Node **a, int n)  /* for (a[0] in a[1]) a[2] */
{
  Cell *x, *vp, *arrayp, *cp, *ncp;
  Array *tp;
  int i;

  vp = execute(a[0]);
  arrayp = execute(a[1]);
  if (!isarr(arrayp)) return True;
  tp = (Array *) arrayp->sval;
  tempfree(arrayp);
  for (i = 0; i < tp->size; i++) {  /* this routine knows too much */
    for (cp = tp->tab[i]; cp != NULL; cp = ncp) {
      setsval(vp, cp->nval);
      ncp = cp->cnext;
      x = execute(a[2]);
      if (isbreak(x)) {
        tempfree(vp);
        return True;
      }
      if (isnext(x) || isexit(x) || isret(x)) {
        tempfree(vp);
        return(x);
      }
      tempfree(x);
    }
  }
  return True;
}

Cell *bltin(Node **a, int n)  /* builtin functions. a[0] is type, a[1] is arg list */
{
  Cell *x, *y;
  Awkfloat u;
  int t;
  Awkfloat tmp;
  char *p, *buf;
  Node *nextarg;
  FILE *fp;
  struct tm stm;
  void flush_all(void);

  t = ptoi(a[0]);
  x = execute(a[1]);
  nextarg = a[1]->nnext;
  switch (t) {
  case FLENGTH:
    if (isarr(x)) u = ((Array *) x->sval)->nelem;  /* GROT.  should be function*/
    else u = strlen(getsval(x));
    break;
  case FLOG:
    u = errcheck(log(getfval(x)), "log"); break;
  case FINT:
    modf(getfval(x), &u); break;
  case FEXP:
    u = errcheck(exp(getfval(x)), "exp"); break;
  case FSQRT:
    u = errcheck(sqrt(getfval(x)), "sqrt"); break;
  case FSIN:
    u = sin(getfval(x)); break;
  case FCOS:
    u = cos(getfval(x)); break;
  case FATAN:
    if (nextarg == 0) {
      WARNING("atan2 requires two arguments; returning 1.0");
      u = 1.0;
    } else {
      y = execute(a[1]->nnext);
      u = atan2(getfval(x), getfval(y));
      tempfree(y);
      nextarg = nextarg->nnext;
    }
    break;
  case FSYSTEM:
    fflush(stdout);    /* in case something is buffered already */
    u = (Awkfloat) system(getsval(x)) / 256;   /* 256 is unix-dep */
    break;
  case FRAND: /* in principle, rand() returns something in 0..RAND_MAX */
    u = (Awkfloat) (rand() % RAND_MAX) / RAND_MAX;
    break;
  case FSRAND:
    if (isrec(x)) u = time((time_t *)0);   /* no argument provided */
    else u = getfval(x);
    tmp = u;
    srand((unsigned int) u);
    u = srand_seed;
    srand_seed = tmp;
    break;
  case FTOUPPER: /*FALL_THROUGH*/
  case FTOLOWER:
    buf = tostring(getsval(x));
    if (t == FTOUPPER) {
      for (p = buf; *p; p++)
        if (islower((uschar) *p)) *p = toupper((uschar)*p);
    } else {
      for (p = buf; *p; p++)
        if (isupper((uschar) *p)) *p = tolower((uschar)*p);
    }
    tempfree(x);
    x = gettemp();
    setsval(x, buf);
    free(buf);
    return x;
  case FFLUSH:
    if (isrec(x) || strlen(getsval(x)) == 0) {
      flush_all();  /* fflush() or fflush("") -> all */
      u = 0;
    } else if ((fp = openfile(FFLUSH, getsval(x))) == NULL) u = EOF;
    else u = fflush(fp);
    break;
  case FMKTIME:
    sscanf(getsval(x),"%d %d %d %d %d %d %d", &stm.tm_year, &stm.tm_mon, &stm.tm_mday, &stm.tm_hour, &stm.tm_min, &stm.tm_sec, &stm.tm_isdst);
    stm.tm_year -= 1900;
    stm.tm_mon--;
    u = mktime(&stm);
    break;
  case FSYSTIME:
    u = time(NULL);
    break;
  case FSTRFTIME:
    {
      char outstr[200];
      time_t t;
      struct tm *tmp;

      if(nextarg == NULL) t = time(NULL);
      else t = getfval(execute(nextarg));

      tmp = localtime(&t);
      if (tmp == NULL) {
        perror("localtime");
        exit(EXIT_FAILURE);
      }
      strftime(outstr, sizeof(outstr), getsval(x), tmp);
      tempfree(x);
      x = gettemp();
      setsval(x, outstr);
      return(x);
    }
    break;
  default:  /* can't happen */
    FATAL("illegal function type %d", t);
    break;
  }
  tempfree(x);
  x = gettemp();
  setfval(x, u);
  if (nextarg != 0) {
    WARNING("warning: function has too many arguments");
    for ( ; nextarg; nextarg = nextarg->nnext) execute(nextarg);
  }
  return(x);
}

Cell *printstat(Node **a, int n)  /* print a[0] */
{
  Node *x;
  Cell *y;
  FILE *fp;

  if (a[1] == 0) fp = stdout;  /* a[1] is redirection operator, a[2] is file */
  else fp = redirect(ptoi(a[1]), a[2]);
  for (x = a[0]; x != NULL; x = x->nnext) {
    y = execute(x);
    fputs(getpssval(y), fp);
    tempfree(y);
    if (x->nnext == NULL) fputs(*ORS, fp);
    else fputs(*OFS, fp);
  }
  if (a[1] != 0) fflush(fp);
  if (ferror(fp)) FATAL("write error on %s", filename(fp));
  return(True);
}

Cell *nullproc(Node **a, int n)
{
  return 0;
}

FILE *redirect(int a, Node *b)  /* set up all i/o redirections */
{
  FILE *fp;
  Cell *x;
  char *fname;

  x = execute(b);
  fname = getsval(x);
  fp = openfile(a, fname);
  if (fp == NULL) FATAL("can't open file %s", fname);
  tempfree(x);
  return fp;
}

void stdinit(void)  /* in case stdin, etc., are not constants */
{
  nfiles = FOPEN_MAX;
  files = calloc(nfiles, sizeof(*files));
  if (files == NULL) FATAL("can't allocate file memory for %u files", nfiles);
  files[0].fp = stdin;
  files[0].fname = "/dev/stdin";
  files[0].mode = LT;
  files[1].fp = stdout;
  files[1].fname = "/dev/stdout";
  files[1].mode = GT;
  files[2].fp = stderr;
  files[2].fname = "/dev/stderr";
  files[2].mode = GT;
}

FILE *openfile(int a, const char *us)
{
  const char *s = us;
  int i, m;
  FILE *fp = 0;

  if (*s == '\0') FATAL("null file name in print or getline");
  for (i=0; i < nfiles; i++)
    if (files[i].fname && strcmp(s, files[i].fname) == 0) {
      if (a == files[i].mode || (a==APPEND && files[i].mode==GT)) return files[i].fp;
      if (a == FFLUSH) return files[i].fp;
    }
  if (a == FFLUSH) return NULL;  /* didn't find it, so don't create it! */

  for (i=0; i < nfiles; i++) if (files[i].fp == 0) break;
  if (i >= nfiles) {
    struct files *nf;
    int nnf = nfiles + FOPEN_MAX;
    nf = realloc(files, nnf * sizeof(*nf));
    if (nf == NULL) FATAL("cannot grow files for %s and %d files", s, nnf);
    memset(&nf[nfiles], 0, FOPEN_MAX * sizeof(*nf));
    nfiles = nnf;
    files = nf;
  }
  fflush(stdout);  /* force a semblance of order */
  m = a;
  if (a == GT) fp = fopen(s, "w");
  else if (a == APPEND) {
    fp = fopen(s, "a");
    m = GT;  /* so can mix > and >> */
  } else if (a == '|') fp = popen(s, "w");
  else if (a == LE) fp = popen(s, "r"); /* input pipe */
  else if (a == LT) fp = strcmp(s, "-") == 0 ? stdin : fopen(s, "r");  /* "-" is stdin getline <file */
  else FATAL("illegal redirection %d", a); /* can't happen */
  if (fp != NULL) {
    files[i].fname = tostring(s);
    files[i].fp = fp;
    files[i].mode = m;
  }
  return fp;
}

const char *filename(FILE *fp)
{
  int i;

  for (i = 0; i < nfiles; i++) if (fp == files[i].fp) return files[i].fname;
  return "???";
}

Cell *closefile(Node **a, int n)
{
  Cell *x;
  int i, stat;

  x = execute(a[0]);
  getsval(x);
  stat = -1;
  for (i = 0; i < nfiles; i++) {
    if (files[i].fname && strcmp(x->sval, files[i].fname) == 0) {
      if (ferror(files[i].fp))
        WARNING( "i/o error occurred on %s", files[i].fname );
      if (files[i].mode == '|' || files[i].mode == LE) {
        stat = pclose(files[i].fp);
        files[i].fp = NULL;
      }
      else {
        stat = fclose(files[i].fp);
        files[i].fp = NULL;
      }
      if (stat == EOF)
        WARNING( "i/o error occurred closing %s", files[i].fname );
      if (i > 2) xfree(files[i].fname); /* don't do /dev/std... */
      files[i].fname = NULL;  /* watch out for ref thru this */
      files[i].fp = NULL;
    }
  }
  tempfree(x);
  x = gettemp();
  setfval(x, (Awkfloat) stat);
  return(x);
}

void closeall(void)
{
  int i, stat;

  for (i = 0; i < FOPEN_MAX; i++) {
    if (files[i].fp) {
      if (ferror(files[i].fp))
        WARNING( "i/o error occurred on %s", files[i].fname );
      if (files[i].mode == '|' || files[i].mode == LE) {
        stat = pclose(files[i].fp);
        files[i].fp = NULL;
      }
      else {
        stat = fclose(files[i].fp);
        files[i].fp = NULL;
      }
      if (stat == EOF)
        WARNING( "i/o error occurred while closing %s", files[i].fname );
    }
  }
}

void flush_all(void)
{
  int i;
  for (i = 0; i < nfiles; i++)
    if (files[i].fp) fflush(files[i].fp);
}

void backsub(char **pb_ptr, char **sptr_ptr);

Cell *sub(Node **a, int nnn)  /* substitute command */
{
  char *sptr, *pb, *q;
  Cell *x, *y, *result;
  char *t, *buf;
  fa *pfa;
  int bufsz = recsize;

  if ((buf = (char *) malloc(bufsz)) == NULL)
    FATAL("out of memory in sub");
  x = execute(a[3]);  /* target string */
  t = getsval(x);
  if (a[0] == 0)    /* 0 => a[1] is already-compiled regexpr */
    pfa = (fa *) a[1];  /* regular expression */
  else {
    y = execute(a[1]);
    pfa = makedfa(getsval(y), 1);
    tempfree(y);
  }
  y = execute(a[2]);  /* replacement string */
  result = False;
  if (pmatch(pfa, t)) {
    sptr = t;
    adjbuf(&buf, &bufsz, 1+patbeg-sptr, recsize, 0, "sub");
    pb = buf;
    while (sptr < patbeg) *pb++ = *sptr++;
    sptr = getsval(y);
    while (*sptr != 0) {
      adjbuf(&buf, &bufsz, 5+pb-buf, recsize, &pb, "sub");
      if (*sptr == '\\') backsub(&pb, &sptr);
      else if (*sptr == '&') {
        char *counter;
        sptr++;
        adjbuf(&buf, &bufsz, 1+patlen+pb-buf, recsize, &pb, "sub");
        counter = patbeg+patlen;
        for (q = patbeg; q < counter; ) *pb++ = *q++;
      } else *pb++ = *sptr++;
    }
    *pb = '\0';
    if (pb > buf + bufsz)
      FATAL("sub result1 %.30s too big; can't happen", buf);
    sptr = patbeg + patlen;
    if ((patlen == 0 && *patbeg) || (patlen && *(sptr-1))) {
      adjbuf(&buf, &bufsz, 1+strlen(sptr)+pb-buf, 0, &pb, "sub");
      while ((*pb++ = *sptr++) != 0)
        ;
    }
    if (pb > buf + bufsz)
      FATAL("sub result2 %.30s too big; can't happen", buf);
    setsval(x, buf);  /* BUG: should be able to avoid copy */
    result = True;;
  }
  tempfree(x);
  tempfree(y);
  free(buf);
  return result;
}

Cell *gsub(Node **a, int nnn)  /* global substitute */
{
  Cell *x, *y;
  char *rptr, *sptr, *t, *pb, *q;
  char *buf;
  fa *pfa;
  int mflag, tempstat, num;
  int bufsz = recsize;

  if ((buf = (char *) malloc(bufsz)) == NULL)
    FATAL("out of memory in gsub");
  mflag = 0;  /* if mflag == 0, can replace empty string */
  num = 0;
  x = execute(a[3]);  /* target string */
  t = getsval(x);
  if (a[0] == 0)    /* 0 => a[1] is already-compiled regexpr */
    pfa = (fa *) a[1];  /* regular expression */
  else {
    y = execute(a[1]);
    pfa = makedfa(getsval(y), 1);
    tempfree(y);
  }
  y = execute(a[2]);  /* replacement string */
  if (pmatch(pfa, t)) {
    tempstat = pfa->initstat;
    pfa->initstat = 2;
    pb = buf;
    rptr = getsval(y);
    do {
      if (patlen == 0 && *patbeg != 0) {  /* matched empty string */
        if (mflag == 0) {  /* can replace empty */
          num++;
          sptr = rptr;
          while (*sptr != 0) {
            adjbuf(&buf, &bufsz, 5+pb-buf, recsize, &pb, "gsub");
            if (*sptr == '\\') backsub(&pb, &sptr);
            else if (*sptr == '&') {
              char *counter;
              sptr++;
              adjbuf(&buf, &bufsz, 1+patlen+pb-buf, recsize, &pb, "gsub");
              counter = patbeg+patlen;
              for (q = patbeg; q < counter; ) *pb++ = *q++;
            } else *pb++ = *sptr++;
          }
        }
        if (*t == 0) goto done;   /* at end */
        adjbuf(&buf, &bufsz, 2+pb-buf, recsize, &pb, "gsub");
        *pb++ = *t++;
        if (pb > buf + bufsz)  /* BUG: not sure of this test */
          FATAL("gsub result0 %.30s too big; can't happen", buf);
        mflag = 0;
      }
      else {  /* matched nonempty string */
        num++;
        sptr = t;
        adjbuf(&buf, &bufsz, 1+(patbeg-sptr)+pb-buf, recsize, &pb, "gsub");
        while (sptr < patbeg) *pb++ = *sptr++;
        sptr = rptr;
        while (*sptr != 0) {
          adjbuf(&buf, &bufsz, 5+pb-buf, recsize, &pb, "gsub");
          if (*sptr == '\\') backsub(&pb, &sptr);
          else if (*sptr == '&') {
            char *counter; 
            sptr++;
            adjbuf(&buf, &bufsz, 1+patlen+pb-buf, recsize, &pb, "gsub");
            counter = patbeg+patlen; 
            for (q = patbeg; q < counter; ) *pb++ = *q++;
          } else *pb++ = *sptr++;
        }
        t = patbeg + patlen;
        if (patlen == 0 || *t == 0 || *(t-1) == 0) goto done;
        if (pb > buf + bufsz)
          FATAL("gsub result1 %.30s too big; can't happen", buf);
        mflag = 1;
      }
    } while (pmatch(pfa,t));
    sptr = t;
    adjbuf(&buf, &bufsz, 1+strlen(sptr)+pb-buf, 0, &pb, "gsub");
    while ((*pb++ = *sptr++) != 0)
      ;
  done:  if (pb < buf + bufsz) *pb = '\0';
    else if (*(pb-1) != '\0')
      FATAL("gsub result2 %.30s truncated; can't happen", buf);
    setsval(x, buf);  /* BUG: should be able to avoid copy + free */
    pfa->initstat = tempstat;
  }
  tempfree(x);
  tempfree(y);
  x = gettemp();
  x->tval = NUM;
  x->fval = num;
  free(buf);
  return(x);
}

void backsub(char **pb_ptr, char **sptr_ptr)  /* handle \\& variations */
{            /* sptr[0] == '\\' */
  char *pb = *pb_ptr, *sptr = *sptr_ptr;

  if (sptr[1] == '\\') {
    if (sptr[2] == '\\' && sptr[3] == '&') { /* \\\& -> \& */
      *pb++ = '\\';
      *pb++ = '&';
      sptr += 4;
    } else if (sptr[2] == '&') {  /* \\& -> \ + matched */
      *pb++ = '\\';
      sptr += 2;
    } else {      /* \\x -> \\x */
      *pb++ = *sptr++;
      *pb++ = *sptr++;
    }
  } else if (sptr[1] == '&') {  /* literal & */
    sptr++;
    *pb++ = *sptr++;
  } else        /* literal \ */
    *pb++ = *sptr++;

  *pb_ptr = pb;
  *sptr_ptr = sptr;
}

static int awk_peek(void)
{
  int c = input();
  unput(c);
  return c;
}

int gettok(char **pbuf, int *psz)  /* get next input token */
{
  int c, retc;
  char *buf = *pbuf;
  int sz = *psz;
  char *bp = buf;

  c = input();
  if (c == 0) return 0;
  buf[0] = c;
  buf[1] = 0;
  if (!isalnum(c) && c != '.' && c != '_') return c;

  *bp++ = c;
  if (isalpha(c) || c == '_') {  /* it's a varname */
    for ( ; (c = input()) != 0; ) {
      if (bp-buf >= sz)
        if (!adjbuf(&buf, &sz, bp-buf+2, 100, &bp, "gettok"))
          FATAL( "out of space for name %.10s...", buf );
      if (isalnum(c) || c == '_') *bp++ = c;
      else {
        *bp = 0;
        unput(c);
        break;
      }
    }
    *bp = 0;
    retc = 'a';  /* alphanumeric */
  } else {  /* maybe it's a number, but could be . */
    char *rem;
    double unused __attribute__((unused));
    /* read input until can't be a number */
    for ( ; (c = input()) != 0; ) {
      if (bp-buf >= sz)
        if (!adjbuf(&buf, &sz, bp-buf+2, 100, &bp, "gettok"))
          FATAL( "out of space for number %.10s...", buf );
      if (isdigit(c) || c == 'e' || c == 'E'
        || c == '.' || c == '+' || c == '-'
        || c == 'x' || c == 'X' || ((c >= 'a' && c<= 'f') || (c >= 'A' && c<= 'F')))
        *bp++ = c;
      else {
        unput(c);
        break;
      }
    }
    *bp = 0;
    unused = strtod(buf, &rem);  /* parse the number */
    if (rem == buf) {  /* it wasn't a valid number at all */
      buf[1] = 0;  /* return one character as token */
      retc = buf[0];  /* character is its own type */
      unputstr(rem+1); /* put rest back for later */
    } else {  /* some prefix was a number */
      unputstr(rem);  /* put rest back for later */
      rem[0] = 0;  /* truncate buf after number part */
      retc = '0';  /* type is number */
    }
  }
  *pbuf = buf;
  *psz = sz;
  return retc;
}

Awkfloat str_to_double(char *s)
{
  if(*s == '0' && isdigit(s[1])) {
    int i, j, k, c, fra, flag, rem;
    Awkfloat rem1 = 0.0;
    int a[20], b[20];

    c = fra = flag = rem = 0;

    for(i=0,j=0,k=0;i<strlen(s);i++)
    {
      if(s[i]=='.') flag=1;
      else if(flag==0) a[j++]=s[i]-48;
      else if(flag==1) b[k++]=s[i]-48;
    }
    c=j;
    fra=k;
    for(j=0,i=c-1;j<c;j++,i--)
      rem = rem +(a[j] * pow(8,i));
    for(k=0,i=1;k<fra;k++,i++)
      rem1 = rem1 +(b[k] / pow(8,i));
    rem1=rem+rem1;
    return rem1;
  }
  else return atof(s);
}

int yylex(void)
{
  int c;
  static char *buf = 0;
  static int bufsize = 5; /* BUG: setting this small causes core dump! */

  if (buf == 0 && (buf = (char *) malloc(bufsize)) == NULL)
    FATAL( "out of space in yylex" );
  if (sc) {
    sc = 0;
    RET('}');
  }
  if (reg) {
    reg = 0;
    return regexpr();
  }
  for (;;) {
    c = gettok(&buf, &bufsize);
    if (c == 0) return 0;
    if (isalpha(c) || c == '_') return word(buf);
    if (isdigit(c)) {
      yylval.cp = setsymtab(buf, tostring(buf), str_to_double(buf), CON|NUM, symtab);
      /* should this also have STR set? */
      RET(NUMBER);
    }

    yylval.i = c;
    switch (c) {
    case '\n':  RET(NL); /* {EOL} */
    case '\r':  /* assume \n is coming */ /*FALL_THROUGH*/
    case ' ':  /* {WS}+ */ /*FALL_THROUGH*/
    case '\t': break;
    case '#':  /* #.* strip comments */
      while ((c = input()) != '\n' && c != 0)
        ;
      unput(c);
      break;
    case ';': RET(';');
    case '\\':
      if (awk_peek() == '\n') input();
      else if (awk_peek() == '\r') {
        input(); input();  /* \n */
        lineno++;
      } else RET(c);
      break;
    case '&':
      if (awk_peek() == '&') {
        input(); RET(AND);
      } else RET('&');
    case '|':
      if (awk_peek() == '|') {
        input(); RET(BOR);
      } else RET('|');
    case '!':
      if (awk_peek() == '=') {
        input(); yylval.i = NE; RET(NE);
      } else if (awk_peek() == '~') {
        input(); yylval.i = NOTMATCH; RET(MATCHOP);
      } else RET(NOT);
    case '~':
      yylval.i = MATCH;
      RET(MATCHOP);
    case '<':
      if (awk_peek() == '=') {
        input(); yylval.i = LE; RET(LE);
      } else {
        yylval.i = LT; RET(LT);
      }
    case '=':
      if (awk_peek() == '=') {
        input(); yylval.i = EQ; RET(EQ);
      } else {
        yylval.i = ASSIGN; RET(ASGNOP);
      }
    case '>':
      if (awk_peek() == '=') {
        input(); yylval.i = GE; RET(GE);
      } else if (awk_peek() == '>') {
        input(); yylval.i = APPEND; RET(APPEND);
      } else {
        yylval.i = GT; RET(GT);
      }
    case '+':
      if (awk_peek() == '+') {
        input(); yylval.i = INCR; RET(INCR);
      } else if (awk_peek() == '=') {
        input(); yylval.i = ADDEQ; RET(ASGNOP);
      } else
        RET('+');
    case '-':
      if (awk_peek() == '-') {
        input(); yylval.i = DECR; RET(DECR);
      } else if (awk_peek() == '=') {
        input(); yylval.i = SUBEQ; RET(ASGNOP);
      } else
        RET('-');
    case '*':
      if (awk_peek() == '=') {  /* *= */
        input(); yylval.i = MULTEQ; RET(ASGNOP);
      } else if (awk_peek() == '*') {  /* ** or **= */
        input();  /* eat 2nd * */
        if (awk_peek() == '=') {
          input(); yylval.i = POWEQ; RET(ASGNOP);
        } else {
          RET(POWER);
        }
      } else
        RET('*');
    case '/':
      RET('/');
    case '%':
      if (awk_peek() == '=') {
        input(); yylval.i = MODEQ; RET(ASGNOP);
      } else
        RET('%');
    case '^':
      if (awk_peek() == '=') {
        input(); yylval.i = POWEQ; RET(ASGNOP);
      } else
        RET(POWER);

    case '$':/* BUG: awkward, if not wrong */
      c = gettok(&buf, &bufsize);
      if (isalpha(c)) {
        if (strcmp(buf, "NF") == 0) {  /* very special */
          unputstr("(NF)");
          RET(INDIRECT);
        }
        c = awk_peek();
        if (c == '(' || c == '[' || (infunc && isarg(buf) >= 0)) {
          unputstr(buf);
          RET(INDIRECT);
        }
        yylval.cp = setsymtab(buf, "", 0.0, STR|NUM, symtab);
        RET(IVAR);
      } else if (c == 0) {  /*  */
        SYNTAX( "unexpected end of input after $" );
        RET(';');
      } else {
        unputstr(buf);
        RET(INDIRECT);
      }
    case '}':
      if (--bracecnt < 0) SYNTAX( "extra }" );
      sc = 1;
      RET(';');
    case ']':
      if (--brackcnt < 0) SYNTAX( "extra ]" );
      RET(']');
    case ')':
      if (--parencnt < 0) SYNTAX( "extra )" );
      RET(')');
    case '{':
      bracecnt++;
      RET('{');
    case '[':
      brackcnt++;
      RET('[');
    case '(':
      parencnt++;
      RET('(');
    case '"':
      return string();  /* BUG: should be like tran.c ? */

    default:
      RET(c);
    }
  }
}

int string(void)
{
  int c, n;
  char *s, *bp;
  static char *buf = 0;
  static int bufsz = 500;

  if (buf == 0 && (buf = (char *) malloc(bufsz)) == NULL)
    FATAL("out of space for strings");
  for (bp = buf; (c = input()) != '"'; ) {
    if (!adjbuf(&buf, &bufsz, bp-buf+2, 500, &bp, "string"))
      FATAL("out of space for string %.10s...", buf);
    switch (c) {
      case '\n': /*FALL_THROUGH*/
      case '\r': /*FALL_THROUGH*/
      case 0:
        SYNTAX( "non-terminated string %.10s...", buf );
        lineno++;
        if (c == 0)  FATAL( "giving up" );
        break;
      case '\\':
        c = input();
        switch (c) {
          case '"': *bp++ = '"'; break;
          case 'n': *bp++ = '\n'; break;
          case 't': *bp++ = '\t'; break;
          case 'f': *bp++ = '\f'; break;
          case 'r': *bp++ = '\r'; break;
          case 'b': *bp++ = '\b'; break;
          case 'v': *bp++ = '\v'; break;
          case 'a': *bp++ = '\007'; break;
          case '\\': *bp++ = '\\'; break;

           
          case '0': /*FALL_THROUGH*/
          case '1': /*FALL_THROUGH*/
          case '2': /*FALL_THROUGH*/ /* octal: \d \dd \ddd */
          case '3': /*FALL_THROUGH*/
          case '4': /*FALL_THROUGH*/
          case '5': /*FALL_THROUGH*/
          case '6': /*FALL_THROUGH*/
          case '7':
                 n = c - '0';
                 if ((c = awk_peek()) >= '0' && c < '8') {
                   n = 8 * n + input() - '0';
                   if ((c = awk_peek()) >= '0' && c < '8')
                     n = 8 * n + input() - '0';
                 }
                 *bp++ = n;
                 break;

          case 'x':  /* hex  \x0-9a-fA-F + */
                 {
                   char xbuf[100], *px;
                   for (px = xbuf; (c = input()) != 0 && px-xbuf < 100-2; ) {
                     if (isdigit(c)
                         || (c >= 'a' && c <= 'f')
                         || (c >= 'A' && c <= 'F'))
                       *px++ = c;
                     else break;
                   }
                   *px = 0;
                   unput(c);
                   sscanf(xbuf, "%x", (unsigned int *) &n);
                   *bp++ = n;
                   break;
                 }

          default:
                 *bp++ = c;
                 break;
        }
        break;
      default:
        *bp++ = c;
        break;
    }
  }
  *bp = 0;
  s = tostring(buf);
  *bp++ = ' ';
  *bp++ = 0;
  yylval.cp = setsymtab(buf, s, 0.0, CON|STR|DONTFREE, symtab);
  RET(STRING);
}


int binsearch(char *w, Keyword *kp, int n)
{
  int cond, low, mid, high;

  low = 0;
  high = n - 1;
  while (low <= high) {
    mid = (low + high) / 2;
    if ((cond = strcmp(w, kp[mid].word)) < 0)
      high = mid - 1;
    else if (cond > 0) low = mid + 1;
    else return mid;
  }
  return -1;
}

int word(char *w)
{
  Keyword *kp;
  int c, n;

  n = binsearch(w, keywords, sizeof(keywords)/sizeof(keywords[0]));
  /* BUG: this ought to be inside the if; in theory could fault (daniel barrett) */
  kp = keywords + n;
  if (n != -1) {  /* found in table */
    yylval.i = kp->sub;
    switch (kp->type) {  /* special handling */
      case BLTIN:
        if (kp->sub == FSYSTEM && safe)
          SYNTAX( "system is unsafe" );
        RET(kp->type);
      case FUNC:
        if (infunc) SYNTAX( "illegal nested function" );
        RET(kp->type);
      case RETURN:
        if (!infunc) SYNTAX( "return not in function" );
        RET(kp->type);
      case VARNF:
        yylval.cp = setsymtab("NF", "", 0.0, NUM, symtab);
        RET(VARNF);
      case VARERR:
        yylval.cp = setsymtab("ERRNO", "", (Awkfloat)errno, NUM, symtab);
        RET(VARERR);
      default:
        RET(kp->type);
    }
  }
  c = awk_peek();  /* look for '(' */
  if (c != '(' && infunc && (n=isarg(w)) >= 0) {
    yylval.i = n;
    RET(ARG);
  } else {
    yylval.cp = setsymtab(w, "", 0.0, STR|NUM|DONTFREE, symtab);
    if (c == '(') {
      RET(CALL);
    }
    else {
      RET(VAR);
    }
  }
}

void startreg(void)  /* next call to yylex will return a regular expression */
{
  reg = 1;
}

int regexpr(void)
{
  int c;
  static char *buf = 0;
  static int bufsz = 500;
  char *bp;

  if (buf == 0 && (buf = (char *) malloc(bufsz)) == NULL)
    FATAL("out of space for rex expr");
  bp = buf;
  for ( ; (c = input()) != '/' && c != 0; ) {
    if (!adjbuf(&buf, &bufsz, bp-buf+3, 500, &bp, "regexpr"))
      FATAL("out of space for reg expr %.10s...", buf);
    if (c == '\n') {
      SYNTAX( "newline in regular expression %.10s...", buf );
      unput('\n');
      break;
    } else if (c == '\\') {
      *bp++ = '\\';
      *bp++ = input();
    } else *bp++ = c;
  }
  *bp = 0;
  if (c == 0) SYNTAX("non-terminated regular expression %.10s...", buf);
  yylval.s = tostring(buf);
  unput('/');
  RET(REGEXPR);
}

int input(void)  /* get next lexical input character */
{
  int c;
  extern char *lexprog;

  if (yysptr > yysbuf)
    c = (uschar)*--yysptr;
  else if (lexprog != NULL) {  /* awk '...' */
    if ((c = (uschar)*lexprog) != 0)
      lexprog++;
  } else c = pgetc();    /* awk -f ... */
  if (c == '\n') lineno++;
  else if (c == EOF) c = 0;
  if (ep >= ebuf + sizeof ebuf)
    ep = ebuf;
  return *ep++ = c;
}

void unput(int c)  /* put lexical character back on input */
{
  if (c == '\n') lineno--;
  if (yysptr >= yysbuf + sizeof(yysbuf))
    FATAL("pushed back too much: %.20s...", yysbuf);
  *yysptr++ = c;
  if (--ep < ebuf) ep = ebuf + sizeof(ebuf) - 1;
}

void unputstr(const char *s)  /* put a string back on input */
{
  int i;

  for (i = strlen(s)-1; i >= 0; i--)
    unput(s[i]);
}


Node *nodealloc(int n)
{
  Node *x;

  x = (Node *) xmalloc(sizeof(Node) + (n-1)*sizeof(Node *));
  x->nnext = NULL;
  x->lineno = lineno;
  return(x);
}

Node *exptostat(Node *a)
{
  a->ntype = NSTAT;
  return(a);
}

Node *node1(int a, Node *b)
{
  Node *x;

  x = nodealloc(1);
  x->nobj = a;
  x->narg[0]=b;
  return(x);
}

Node *node2(int a, Node *b, Node *c)
{
  Node *x;

  x = nodealloc(2);
  x->nobj = a;
  x->narg[0] = b;
  x->narg[1] = c;
  return(x);
}

Node *node3(int a, Node *b, Node *c, Node *d)
{
  Node *x;

  x = nodealloc(3);
  x->nobj = a;
  x->narg[0] = b;
  x->narg[1] = c;
  x->narg[2] = d;
  return(x);
}

Node *node4(int a, Node *b, Node *c, Node *d, Node *e)
{
  Node *x;

  x = nodealloc(4);
  x->nobj = a;
  x->narg[0] = b;
  x->narg[1] = c;
  x->narg[2] = d;
  x->narg[3] = e;
  return(x);
}

Node *stat1(int a, Node *b)
{
  Node *x;

  x = node1(a, b);
  x->ntype = NSTAT;
  return(x);
}

Node *stat2(int a, Node *b, Node *c)
{
  Node *x;

  x = node2(a, b, c);
  x->ntype = NSTAT;
  return(x);
}

Node *stat3(int a, Node *b, Node *c, Node *d)
{
  Node *x;

  x = node3(a, b, c, d);
  x->ntype = NSTAT;
  return(x);
}

Node *stat4(int a, Node *b, Node *c, Node *d, Node *e)
{
  Node *x;

  x = node4(a, b, c, d, e);
  x->ntype = NSTAT;
  return(x);
}

Node *op1(int a, Node *b)
{
  Node *x;

  x = node1(a, b);
  x->ntype = NEXPR;
  return(x);
}

Node *op2(int a, Node *b, Node *c)
{
  Node *x;

  x = node2(a, b, c);
  x->ntype = NEXPR;
  return(x);
}

Node *op3(int a, Node *b, Node *c, Node *d)
{
  Node *x;

  x = node3(a, b, c, d);
  x->ntype = NEXPR;
  return(x);
}

Node *op4(int a, Node *b, Node *c, Node *d, Node *e)
{
  Node *x;

  x = node4(a, b, c, d, e);
  x->ntype = NEXPR;
  return(x);
}

Node *celltonode(Cell *a, int b)
{
  Node *x;

  a->ctype = OCELL;
  a->csub = b;
  x = node1(0, (Node *) a);
  x->ntype = NVALUE;
  return(x);
}

Node *rectonode(void)  /* make $0 into a Node */
{
  extern Cell *literal0;
  return op1(INDIRECT, celltonode(literal0, CUNK));
}

Node *makearr(Node *p)
{
  Cell *cp;

  if (isvalue(p)) {
    cp = (Cell *) (p->narg[0]);
    if (isfcn(cp))
      SYNTAX( "%s is a function, not an array", cp->nval );
    else if (!isarr(cp)) {
      xfree(cp->sval);
      cp->sval = (char *) makesymtab(NSYMTAB);
      cp->tval = ARR;
    }
  }
  return p;
}

Node *pa2stat(Node *a, Node *b, Node *c)  /* pat, pat {...} */
{
  Node *x;

  x = node4(PASTAT2, a, b, c, itonp(paircnt));
  if (paircnt++ >= PA2NUM)
    SYNTAX( "limited to %d pat,pat statements", PA2NUM );
  x->ntype = NSTAT;
  return(x);
}

Node *linkum(Node *a, Node *b)
{
  Node *c;

  if (errorflag) return a;  /* don't link things that are wrong */

  if (a == NULL) return(b);
  else if (b == NULL) return(a);
  for (c = a; c->nnext != NULL; c = c->nnext)
    ;
  c->nnext = b;
  return(a);
}

void defn(Cell *v, Node *vl, Node *st)  /* turn on FCN bit in definition, */
{          /*   body of function, arglist */
  Node *p;
  int n;

  if (isarr(v)) {
    SYNTAX( "`%s' is an array name and a function name", v->nval );
    return;
  }
  if (isarg(v->nval) != -1) {
    SYNTAX( "`%s' is both function name and argument name", v->nval );
    return;
  }

  v->tval = FCN;
  v->sval = (char *) st;
  n = 0;  /* count arguments */
  for (p = vl; p; p = p->nnext)
    n++;
  v->fval = n;
  dprintf( ("defining func %s (%d args)\n", v->nval, n) );
}

int isarg(const char *s)    /* is s in argument list for current function? */
{      /* return -1 if not, otherwise arg # */
  extern Node *arglist;
  Node *p = arglist;
  int n;

  for (n = 0; p != 0; p = p->nnext, n++)
    if (strcmp(((Cell *)(p->narg[0]))->nval, s) == 0)
      return n;
  return -1;
}

int ptoi(void *p)  /* convert pointer to integer */
{
  return (int) (long) p;  /* swearing that p fits, of course */
}

Node *itonp(int i)  /* and vice versa */
{
  return (Node *) (long) i;
}

void syminit(void)  /* initialize symbol table with builtin vars */
{
  literal0 = setsymtab("0", "0", 0.0, NUM|STR|CON|DONTFREE, symtab);
  /* this is used for if(x)... tests: */
  nullloc = setsymtab("$zero&null", "", 0.0, NUM|STR|CON|DONTFREE, symtab);
  nullnode = celltonode(nullloc, CCON);

  fsloc = setsymtab("FS", " ", 0.0, STR|DONTFREE, symtab);
  FS = &fsloc->sval;
  RS = &setsymtab("RS", "\n", 0.0, STR|DONTFREE, symtab)->sval;
  OFS = &setsymtab("OFS", " ", 0.0, STR|DONTFREE, symtab)->sval;
  ORS = &setsymtab("ORS", "\n", 0.0, STR|DONTFREE, symtab)->sval;
  OFMT = &setsymtab("OFMT", "%.6g", 0.0, STR|DONTFREE, symtab)->sval;
  CONVFMT = &setsymtab("CONVFMT", "%.6g", 0.0, STR|DONTFREE, symtab)->sval;
  FILENAME = &setsymtab("FILENAME", "", 0.0, STR|DONTFREE, symtab)->sval;
  nfloc = setsymtab("NF", "", 0.0, NUM, symtab);
  NF = &nfloc->fval;
  errloc = setsymtab("ERRNO" , "", 0.0, NUM, symtab);
  ERRNO = &errloc->fval;
  nrloc = setsymtab("NR", "", 0.0, NUM, symtab);
  NR = &nrloc->fval;
  fnrloc = setsymtab("FNR", "", 0.0, NUM, symtab);
  FNR = &fnrloc->fval;
  SUBSEP = &setsymtab("SUBSEP", "\034", 0.0, STR|DONTFREE, symtab)->sval;
  rstartloc = setsymtab("RSTART", "", 0.0, NUM, symtab);
  RSTART = &rstartloc->fval;
  rlengthloc = setsymtab("RLENGTH", "", 0.0, NUM, symtab);
  RLENGTH = &rlengthloc->fval;
  symtabloc = setsymtab("SYMTAB", "", 0.0, ARR, symtab);
  symtabloc->sval = (char *) symtab;
}

void arginit(int ac, char **av)  /* set up ARGV and ARGC */
{
  Cell *cp;
  int i;
  char temp[50];

  ARGC = &setsymtab("ARGC", "", (Awkfloat) ac, NUM, symtab)->fval;
  cp = setsymtab("ARGV", "", 0.0, ARR, symtab);
  ARGVtab = makesymtab(NSYMTAB);  /* could be (int) ARGC as well */
  cp->sval = (char *) ARGVtab;
  for (i = 0; i < ac; i++) {
    sprintf(temp, "%d", i);
    if (is_number(*av))
      setsymtab(temp, *av, atof(*av), STR|NUM, ARGVtab);
    else setsymtab(temp, *av, 0.0, STR, ARGVtab);
    av++;
  }
}

void envinit(char **envp)  /* set up ENVIRON variable */
{
  Cell *cp;
  char *p;

  cp = setsymtab("ENVIRON", "", 0.0, ARR, symtab);
  ENVtab = makesymtab(NSYMTAB);
  cp->sval = (char *) ENVtab;
  for ( ; *envp; envp++) {
    if ((p = strchr(*envp, '=')) == NULL)
      continue;
    if( p == *envp ) continue; /* no left hand side name in env string */

    *p++ = 0;  /* split into two strings at = */
    if (is_number(p)) setsymtab(*envp, p, atof(p), STR|NUM, ENVtab);
    else setsymtab(*envp, p, 0.0, STR, ENVtab);
    p[-1] = '=';  /* restore in case env is passed down to a shell */
  }
}

Array *makesymtab(int n)  /* make a new symbol table */
{
  Array *ap;
  Cell **tp;

  ap = (Array *) xmalloc(sizeof(Array));
  tp = (Cell **) calloc(n, sizeof(Cell *));
  if (ap == NULL || tp == NULL)
    FATAL("out of space in makesymtab");
  ap->nelem = 0;
  ap->size = n;
  ap->tab = tp;
  return(ap);
}

void freesymtab(Cell *ap)  /* free a symbol table */
{
  Cell *cp, *temp;
  Array *tp;
  int i;

  if (!isarr(ap))  return;
  tp = (Array *) ap->sval;
  if (tp == NULL)  return;

  for (i = 0; i < tp->size; i++) {
    for (cp = tp->tab[i]; cp != NULL; cp = temp) {
      xfree(cp->nval);
      if (freeable(cp)) xfree(cp->sval);
      temp = cp->cnext;  /* avoids freeing then using */
      free(cp);
      tp->nelem--;
    }
    tp->tab[i] = 0;
  }
  if (tp->nelem != 0)
    WARNING("can't happen: inconsistent element count freeing %s", ap->nval);
  free(tp->tab);
  free(tp);
}

void freeelem(Cell *ap, const char *s)  /* free elem s from ap (i.e., ap["s"] */
{
  Array *tp;
  Cell *p, *prev = NULL;
  int h;

  tp = (Array *) ap->sval;
  h = hash(s, tp->size);
  for (p = tp->tab[h]; p != NULL; prev = p, p = p->cnext)
    if (strcmp(s, p->nval) == 0) {
      if (prev == NULL)  /* 1st one */
        tp->tab[h] = p->cnext;
      else      /* middle somewhere */
        prev->cnext = p->cnext;
      if (freeable(p)) xfree(p->sval);
      free(p->nval);
      free(p);
      tp->nelem--;
      return;
    }
}

Cell *setsymtab(const char *n, const char *s, Awkfloat f, unsigned t, Array *tp)
{
  int h;
  Cell *p;

  if (n != NULL && (p = lookup(n, tp)) != NULL) {
    dprintf( ("setsymtab found %p: n=%s s=\"%s\" f=%g t=%o\n",
          (void*)p, NN(p->nval), NN(p->sval), p->fval, p->tval) );
    return(p);
  }
  p = (Cell *) malloc(sizeof(Cell));
  if (p == NULL)
    FATAL("out of space for symbol table at %s", n);
  p->nval = tostring(n);
  p->sval = s ? tostring(s) : tostring("");
  p->fval = f;
  p->tval = t;
  p->csub = CUNK;
  p->ctype = OCELL;
  tp->nelem++;
  if (tp->nelem > FULLTAB * tp->size)
    rehash(tp);
  h = hash(n, tp->size);
  p->cnext = tp->tab[h];
  tp->tab[h] = p;
  dprintf( ("setsymtab set %p: n=%s s=\"%s\" f=%g t=%o\n",
        (void*)p, p->nval, p->sval, p->fval, p->tval) );
  return(p);
}

int hash(const char *s, int n)  /* form hash value for string s */
{
  unsigned hashval;

  for (hashval = 0; *s != '\0'; s++)
    hashval = (*s + 31 * hashval);
  return hashval % n;
}

void rehash(Array *tp)  /* rehash items in small table into big one */
{
  int i, nh, nsz;
  Cell *cp, *op, **np;

  nsz = GROWTAB * tp->size;
  np = (Cell **) calloc(nsz, sizeof(Cell *));
  if (np == NULL)    /* can't do it, but can keep running. */
    return;    /* someone else will run out later. */
  for (i = 0; i < tp->size; i++) {
    for (cp = tp->tab[i]; cp; cp = op) {
      op = cp->cnext;
      nh = hash(cp->nval, nsz);
      cp->cnext = np[nh];
      np[nh] = cp;
    }
  }
  free(tp->tab);
  tp->tab = np;
  tp->size = nsz;
}

Cell *lookup(const char *s, Array *tp)  /* look for s in tp */
{
  Cell *p;
  int h;

  h = hash(s, tp->size);
  for (p = tp->tab[h]; p != NULL; p = p->cnext)
    if (strcmp(s, p->nval) == 0) return(p);  /* found it */
  return(NULL);      /* not found */
}

Awkfloat setfval(Cell *vp, Awkfloat f)  /* set float val of a Cell */
{
  int fldno;

  if ((vp->tval & (NUM | STR)) == 0)
    funnyvar(vp, "assign to");
  if (isfld(vp)) {
    donerec = 0;  /* mark $0 invalid */
    fldno = atoi(vp->nval);
    if (fldno > *NF)
      newfld(fldno);
    dprintf( ("setting field %d to %g\n", fldno, f) );
  } else if (isrec(vp)) {
    donefld = 0;  /* mark $1... invalid */
    donerec = 1;
  }
  if (freeable(vp)) xfree(vp->sval); /* free any previous string */
  vp->tval &= ~STR;  /* mark string invalid */
  vp->tval |= NUM;  /* mark number ok */
  dprintf( ("setfval %p: %s = %g, t=%o\n", (void*)vp, NN(vp->nval), f, vp->tval) );
  return vp->fval = f;
}

void funnyvar(Cell *vp, const char *rw)
{
  if (isarr(vp))
    FATAL("can't %s %s; it's an array name.", rw, vp->nval);
  if (vp->tval & FCN)
    FATAL("can't %s %s; it's a function.", rw, vp->nval);
  WARNING("funny variable %p: n=%s s=\"%s\" f=%g t=%o",
      vp, vp->nval, vp->sval, vp->fval, vp->tval);
}

char *setsval(Cell *vp, const char *s)  /* set string val of a Cell */
{
  char *t;
  int fldno;

  dprintf( ("starting setsval %p: %s = \"%s\", t=%o, r,f=%d,%d\n",
        (void*)vp, NN(vp->nval), s, vp->tval, donerec, donefld) );
  if ((vp->tval & (NUM | STR)) == 0)
    funnyvar(vp, "assign to");
  if (isfld(vp)) {
    donerec = 0;  /* mark $0 invalid */
    fldno = atoi(vp->nval);
    if (fldno > *NF)
      newfld(fldno);
    dprintf( ("setting field %d to %s (%p)\n", fldno, s, s) );
  } else if (isrec(vp)) {
    donefld = 0;  /* mark $1... invalid */
    donerec = 1;
  }
  t = tostring(s);  /* in case it's self-assign */
  if (freeable(vp)) xfree(vp->sval);
  vp->tval &= ~NUM;
  vp->tval |= STR;
  vp->tval &= ~DONTFREE;
  dprintf( ("setsval %p: %s = \"%s (%p) \", t=%o r,f=%d,%d\n",
        (void*)vp, NN(vp->nval), t,t, vp->tval, donerec, donefld) );
  return(vp->sval = t);
}

Awkfloat getfval(Cell *vp)  /* get float val of a Cell */
{
  if ((vp->tval & (NUM | STR)) == 0)
    funnyvar(vp, "read value of");
  if (isfld(vp) && donefld == 0) fldbld();
  else if (isrec(vp) && donerec == 0)  recbld();
  if (!isnum(vp)) {  /* not a number */
    vp->fval = atof(vp->sval);  /* best guess */
    if (is_number(vp->sval) && !(vp->tval&CON))
      vp->tval |= NUM;  /* make NUM only sparingly */
  }
  dprintf( ("getfval %p: %s = %g, t=%o\n",
        (void*)vp, NN(vp->nval), vp->fval, vp->tval) );
  return(vp->fval);
}

static char *get_str_val(Cell *vp, char **fmt)    /* get string val of a Cell */
{
  char s[100];  /* BUG: unchecked */
  double dtemp;

  if ((vp->tval & (NUM | STR)) == 0)
    funnyvar(vp, "read value of");
  if (isfld(vp) && donefld == 0) fldbld();
  else if (isrec(vp) && donerec == 0)  recbld();
  if (isstr(vp) == 0) {
    if (freeable(vp)) xfree(vp->sval);
    if (modf(vp->fval, &dtemp) == 0)  /* it's integral */
      sprintf(s, "%.30g", vp->fval);
    else sprintf(s, *fmt, vp->fval);
    vp->sval = tostring(s);
    vp->tval &= ~DONTFREE;
    vp->tval |= STR;
  }
  dprintf( ("getsval %p: %s = \"%s (%p)\", t=%o\n",
        (void*)vp, NN(vp->nval), vp->sval, vp->sval, vp->tval) );
  return(vp->sval);
}

char *getsval(Cell *vp)     /* get string val of a Cell */
{
  return get_str_val(vp, CONVFMT);
}

char *getpssval(Cell *vp)   /* get string val of a Cell for print */
{
  return get_str_val(vp, OFMT);
}


char *tostring(const char *s)  /* make a copy of string s */
{
  char *p;

  p = (char *) malloc(strlen(s)+1);
  if (p == NULL)
    FATAL("out of space in tostring on %s", s);
  strcpy(p, s);
  return(p);
}

char *qstring(const char *is, int delim)  /* collect string up to next delim */
{
  const char *os = is;
  int c, n;
  uschar *s = (uschar *) is;
  uschar *buf, *bp;

  if ((buf = (uschar *) malloc(strlen(is)+3)) == NULL)
    FATAL( "out of space in qstring(%s)", s);
  for (bp = buf; (c = *s) != delim; s++) {
    if (c == '\n')
      SYNTAX( "newline in string %.20s...", os );
    else if (c != '\\') *bp++ = c;
    else {  /* \something */
      c = *++s;
      if (c == 0) {  /* \ at end */
        *bp++ = '\\';
        break;  /* for loop */
      }
      switch (c) {
        case '\\':  *bp++ = '\\'; break;
        case 'n':  *bp++ = '\n'; break;
        case 't':  *bp++ = '\t'; break;
        case 'b':  *bp++ = '\b'; break;
        case 'f':  *bp++ = '\f'; break;
        case 'r':  *bp++ = '\r'; break;
        default:
              if (!isdigit(c)) {
                *bp++ = c;
                break;
              }
              n = c - '0';
              if (isdigit(s[1])) {
                n = 8 * n + *++s - '0';
                if (isdigit(s[1]))
                  n = 8 * n + *++s - '0';
              }
              *bp++ = n;
              break;
      }
    }
  }
  *bp++ = 0;
  return (char *) buf;
}

void recinit(unsigned int n)
{
  if ( (record = (char *) malloc(n)) == NULL
    || (fields = (char *) malloc(n+1)) == NULL
    || (fldtab = (Cell **) malloc((nfields+1) * sizeof(Cell *))) == NULL
    || (fldtab[0] = (Cell *) malloc(sizeof(Cell))) == NULL )
    FATAL("out of space for $0 and fields");
  *fldtab[0] = dollar0;
  fldtab[0]->sval = record;
  fldtab[0]->nval = tostring("0");
  makefields(1, nfields);
}

void makefields(int n1, int n2)    /* create $n1..$n2 inclusive */
{
  char temp[50];
  int i;

  for (i = n1; i <= n2; i++) {
    fldtab[i] = (Cell *) xmalloc(sizeof (struct Cell));
    *fldtab[i] = dollar1;
    sprintf(temp, "%d", i);
    fldtab[i]->nval = tostring(temp);
  }
}

void initgetrec(void)
{
  int i;
  char *p;

  for (i = 1; i < *ARGC; i++) {
    p = getargv(i); /* find 1st real filename */
    if (p == NULL || *p == '\0') {  /* deleted or zapped */
      argno++;
      continue;
    }
    if (!isclvar(p)) {
      setsval(lookup("FILENAME", symtab), p);
      return;
    }
    setclvar(p);  /* a commandline assignment before filename */
    argno++;
  }
  infile = stdin;    /* no filenames, so use stdin */
}

int getrec(char **pbuf, int *pbufsize, int isrecord)  /* get next input record */
{      /* note: cares whether buf == record */
  int c;
  char *buf = *pbuf;
  uschar saveb0;
  int bufsize = *pbufsize, savebufsize = bufsize;

  if (firsttime) {
    firsttime = 0;
    initgetrec();
  }
  dprintf( ("RS=<%s>, FS=<%s>, ARGC=%g, FILENAME=%s\n",
        *RS, *FS, *ARGC, *FILENAME) );
  if (isrecord) {
    donefld = 0;
    donerec = 1;
  }
  saveb0 = buf[0];
  buf[0] = 0;
  while (argno < *ARGC || infile == stdin) {
    dprintf( ("argno=%d, file=|%s|\n", argno, file) );
    if (infile == NULL) {  /* have to open a new file */
      file = getargv(argno);
      if (file == NULL || *file == '\0') {  /* deleted or zapped */
        argno++;
        continue;
      }
      if (isclvar(file)) {  /* a var=value arg */
        setclvar(file);
        argno++;
        continue;
      }
      *FILENAME = file;
      dprintf( ("opening file %s\n", file) );
      if (*file == '-' && *(file+1) == '\0')
        infile = stdin;
      else if ((infile = fopen(file, "r")) == NULL)
        FATAL("can't open file %s", file);
      setfval(fnrloc, 0.0);
    }
    c = readrec(&buf, &bufsize, infile);
    if (c != 0 || buf[0] != '\0') {  /* normal record */
      if (isrecord) {
        if (freeable(fldtab[0]))
          xfree(fldtab[0]->sval);
        fldtab[0]->sval = buf;  /* buf == record */
        fldtab[0]->tval = REC | STR | DONTFREE;
        if (is_number(fldtab[0]->sval)) {
          fldtab[0]->fval = atof(fldtab[0]->sval);
          fldtab[0]->tval |= NUM;
        }
      }
      setfval(nrloc, nrloc->fval+1);
      setfval(fnrloc, fnrloc->fval+1);
      *pbuf = buf;
      *pbufsize = bufsize;
      return 1;
    }
    /* EOF arrived on this file; set up next */
    if (infile != stdin) fclose(infile);
    infile = NULL;
    argno++;
  }
  buf[0] = saveb0;
  *pbuf = buf;
  *pbufsize = savebufsize;
  return 0;  /* true end of file */
}

void nextfile(void)
{
  if (infile != NULL && infile != stdin)
    fclose(infile);
  infile = NULL;
  argno++;
}

int readrec(char **pbuf, int *pbufsize, FILE *inf)  /* read one record into buf */
{
  int sep, c;
  char *rr, *buf = *pbuf;
  int bufsize = *pbufsize;

  if (strlen(*FS) >= sizeof(inputFS))
    FATAL("field separator %.10s... is too long", *FS);
  /*fflush(stdout); avoids some buffering problem but makes it 25% slower*/
  strcpy(inputFS, *FS);  /* for subsequent field splitting */
  if ((sep = **RS) == 0) {
    sep = '\n';
    while ((c=getc(inf)) == '\n' && c != EOF)  /* skip leading \n's */
      ;
    if (c != EOF) ungetc(c, inf);
  }
  for (rr = buf; ; ) {
    for (; (c=getc(inf)) != sep && c != EOF; ) {
      if (rr-buf+1 > bufsize)
        if (!adjbuf(&buf, &bufsize, 1+rr-buf, recsize, &rr, "readrec 1"))
          FATAL("input record `%.30s...' too long", buf);
      *rr++ = c;
    }
    if (**RS == sep || c == EOF) break;
    if ((c = getc(inf)) == '\n' || c == EOF) /* 2 in a row */
      break;
    if (!adjbuf(&buf, &bufsize, 2+rr-buf, recsize, &rr, "readrec 2"))
      FATAL("input record `%.30s...' too long", buf);
    *rr++ = '\n';
    *rr++ = c;
  }
  if (!adjbuf(&buf, &bufsize, 1+rr-buf, recsize, &rr, "readrec 3"))
    FATAL("input record `%.30s...' too long", buf);
  *rr = 0;
  dprintf( ("readrec saw <%s>, returns %d\n", buf, c == EOF && rr == buf ? 0 : 1) );
  *pbuf = buf;
  *pbufsize = bufsize;
  return c == EOF && rr == buf ? 0 : 1;
}

char *getargv(int n)  /* get ARGV[n] */
{
  Cell *x;
  char *s, temp[50];
  extern Array *ARGVtab;

  sprintf(temp, "%d", n);
  if (lookup(temp, ARGVtab) == NULL) return NULL;
  x = setsymtab(temp, "", 0.0, STR, ARGVtab);
  s = getsval(x);
  dprintf( ("getargv(%d) returns |%s|\n", n, s) );
  return s;
}

void setclvar(char *s)  /* set var=value from s */
{
  char *p;
  Cell *q;

  for (p=s; *p != '='; p++)
    ;
  *p++ = 0;
  p = qstring(p, '\0');
  q = setsymtab(s, p, 0.0, STR, symtab);
  setsval(q, p);
  if (is_number(q->sval)) {
    q->fval = atof(q->sval);
    q->tval |= NUM;
  }
  dprintf( ("command line set %s to |%s|\n", s, p) );
}


void fldbld(void)  /* create fields from current record */
{
  /* this relies on having fields[] the same length as $0 */
  /* the fields are all stored in this one array with \0's */
  /* possibly with a final trailing \0 not associated with any field */
  char *r, *fr, sep;
  Cell *p;
  int i, j, n;

  if (donefld) return;
  if (!isstr(fldtab[0])) getsval(fldtab[0]);
  r = fldtab[0]->sval;
  n = strlen(r);
  if (n > fieldssize) {
    xfree(fields);
    if ((fields = (char *) malloc(n+2)) == NULL) /* possibly 2 final \0s */
      FATAL("out of space for fields in fldbld %d", n);
    fieldssize = n;
  }
  fr = fields;
  i = 0;  /* number of fields accumulated here */
  strcpy(inputFS, *FS);
  if (strlen(inputFS) > 1) {  /* it's a regular expression */
    i = refldbld(r, inputFS);
  } else if ((sep = *inputFS) == ' ') {  /* default whitespace */
    for (i = 0; ; ) {
      while (*r == ' ' || *r == '\t' || *r == '\n')
        r++;
      if (*r == 0) break;
      i++;
      if (i > nfields) growfldtab(i);
      if (freeable(fldtab[i])) xfree(fldtab[i]->sval);
      fldtab[i]->sval = fr;
      fldtab[i]->tval = FLD | STR | DONTFREE;
      do
        *fr++ = *r++;
      while (*r != ' ' && *r != '\t' && *r != '\n' && *r != '\0');
      *fr++ = 0;
    }
    *fr = 0;
  } else if ((sep = *inputFS) == 0) {    /* new: FS="" => 1 char/field */
    for (i = 0; *r != 0; r++) {
      char buf[2];
      i++;
      if (i > nfields) growfldtab(i);
      if (freeable(fldtab[i])) xfree(fldtab[i]->sval);
      buf[0] = *r;
      buf[1] = 0;
      fldtab[i]->sval = tostring(buf);
      fldtab[i]->tval = FLD | STR;
    }
    *fr = 0;
  } else if (*r != 0) {  /* if 0, it's a null field */
    /* subtlecase : if length(FS) == 1 && length(RS > 0)
     * \n is NOT a field separator (cf awk book 61,84).
     * this variable is tested in the inner while loop.
     */
    int rtest = '\n';  /* normal case */
    if (strlen(*RS) > 0) rtest = '\0';
    for (;;) {
      i++;
      if (i > nfields) growfldtab(i);
      if (freeable(fldtab[i])) xfree(fldtab[i]->sval);
      fldtab[i]->sval = fr;
      fldtab[i]->tval = FLD | STR | DONTFREE;
      while (*r != sep && *r != rtest && *r != '\0')  /* \n is always a separator */
        *fr++ = *r++;
      *fr++ = 0;
      if (*r++ == 0) break;
    }
    *fr = 0;
  }
  if (i > nfields)
    FATAL("record `%.30s...' has too many fields; can't happen", r);
  cleanfld(i+1, lastfld);  /* clean out junk from previous record */
  lastfld = i;
  donefld = 1;
  for (j = 1; j <= lastfld; j++) {
    p = fldtab[j];
    if(is_number(p->sval)) {
      p->fval = atof(p->sval);
      p->tval |= NUM;
    }
  }
  setfval(nfloc, (Awkfloat) lastfld);
  if (dbg) {
    for (j = 0; j <= lastfld; j++) {
      p = fldtab[j];
      printf("field %d (%s): |%s|\n", j, p->nval, p->sval);
    }
  }
}

void cleanfld(int n1, int n2)  /* clean out fields n1 .. n2 inclusive */
{        /* nvals remain intact */
  Cell *p;
  int i;

  for (i = n1; i <= n2; i++) {
    p = fldtab[i];
    if (freeable(p)) xfree(p->sval);
    p->sval = "";
    p->tval = FLD | STR | DONTFREE;
  }
}

void newfld(int n)  /* add field n after end of existing lastfld */
{
  if (n > nfields) growfldtab(n);
  cleanfld(lastfld+1, n);
  lastfld = n;
  setfval(nfloc, (Awkfloat) n);
}

Cell *fieldadr(int n)  /* get nth field */
{
  if (n < 0)
    FATAL("trying to access out of range field %d", n);
  if (n > nfields)  /* fields after NF are empty */
    growfldtab(n);  /* but does not increase NF */
  return(fldtab[n]);
}

void growfldtab(int n)  /* make new fields up to at least $n */
{
  int nf = 2 * nfields;
  size_t s;

  if (n > nf)  nf = n;
  s = (nf+1) * (sizeof (struct Cell *));  /* freebsd: how much do we need? */
  if (s / sizeof(struct Cell *) - 1 == nf) /* didn't overflow */
    fldtab = (Cell **) realloc(fldtab, s);
  else xfree(fldtab);        /* overflow sizeof int */
  if (fldtab == NULL)
    FATAL("out of space creating %d fields", nf);
  makefields(nfields+1, nf);
  nfields = nf;
}

int refldbld(const char *rec, const char *fs)  /* build fields from reg expr in FS */
{
  /* this relies on having fields[] the same length as $0 */
  /* the fields are all stored in this one array with \0's */
  char *fr;
  int i, tempstat, n;
  fa *pfa;

  n = strlen(rec);
  if (n > fieldssize) {
    xfree(fields);
    if ((fields = (char *) malloc(n+1)) == NULL)
      FATAL("out of space for fields in refldbld %d", n);
    fieldssize = n;
  }
  fr = fields;
  *fr = '\0';
  if (*rec == '\0') return 0;
  pfa = makedfa(fs, 1);
  dprintf( ("into refldbld, rec = <%s>, pat = <%s>\n", rec, fs) );
  tempstat = pfa->initstat;
  for (i = 1; ; i++) {
    if (i > nfields) growfldtab(i);
    if (freeable(fldtab[i])) xfree(fldtab[i]->sval);
    fldtab[i]->tval = FLD | STR | DONTFREE;
    fldtab[i]->sval = fr;
    dprintf( ("refldbld: i=%d\n", i) );
    if (nematch(pfa, rec)) {
      pfa->initstat = 2;  /* horrible coupling to b.c */
      dprintf( ("match %s (%d chars)\n", patbeg, patlen) );
      strncpy(fr, rec, patbeg-rec);
      fr += patbeg - rec + 1;
      *(fr-1) = '\0';
      rec = patbeg + patlen;
    } else {
      dprintf( ("no match %s\n", rec) );
      strcpy(fr, rec);
      pfa->initstat = tempstat;
      break;
    }
  }
  return i;
}

void recbld(void)  /* create $0 from $1..$NF if necessary */
{
  int i;
  char *r, *p;

  if (donerec == 1) return;
  r = record;
  for (i = 1; i <= *NF; i++) {
    p = getsval(fldtab[i]);
    if (!adjbuf(&record, &recsize, 1+strlen(p)+r-record, recsize, &r, "recbld 1"))
      FATAL("created $0 `%.30s...' too long", record);
    while ((*r = *p++) != 0)
      r++;
    if (i < *NF) {
      if (!adjbuf(&record, &recsize, 2+strlen(*OFS)+r-record, recsize, &r, "recbld 2"))
        FATAL("created $0 `%.30s...' too long", record);
      for (p = *OFS; (*r = *p++) != 0; )
        r++;
    }
  }
  if (!adjbuf(&record, &recsize, 2+r-record, recsize, &r, "recbld 3"))
    FATAL("built giant record `%.30s...'", record);
  *r = '\0';
  dprintf( ("in recbld inputFS=%s, fldtab[0]=%p\n", inputFS, (void*)fldtab[0]) );

  if (freeable(fldtab[0])) xfree(fldtab[0]->sval);
  fldtab[0]->tval = REC | STR | DONTFREE;
  fldtab[0]->sval = record;

  dprintf( ("in recbld inputFS=%s, fldtab[0]=%p\n", inputFS, (void*)fldtab[0]) );
  dprintf( ("recbld = |%s|\n", record) );
  donerec = 1;
}

int  errorflag  = 0;

void yyerror(const char *s)
{
  SYNTAX("%s", s);
}

void SYNTAX(const char *fmt, ...)
{
  extern char *cmdname, *curfname;
  static int been_here = 0;
  va_list varg;

  if(toys.exitval == 0) toys.exitval++;
  if (been_here++ > 2) return;
  fprintf(stderr, "%s: ", cmdname);
  va_start(varg, fmt);
  vfprintf(stderr, fmt, varg);
  va_end(varg);
  fprintf(stderr, " at source line %d", lineno);
  if (curfname != NULL)
    fprintf(stderr, " in function %s", curfname);
  if (compile_time == 1 && cursource() != NULL)
    fprintf(stderr, " source file %s", cursource());
  fprintf(stderr, "\n");
  errorflag = 2;
  eprint();
}

void fpecatch(int n)
{
  FATAL("floating point exception %d", n);
}

extern int bracecnt, brackcnt, parencnt;

void bracecheck(void)
{
  int c;
  static int beenhere = 0;

  if (beenhere++) return;
  while ((c = input()) != EOF && c != '\0')
    bclass(c);
  bcheck2(bracecnt, '{', '}');
  bcheck2(brackcnt, '[', ']');
  bcheck2(parencnt, '(', ')');
}

void bcheck2(int n, int c1, int c2)
{
  if (n == 1)  fprintf(stderr, "\tmissing %c\n", c2);
  else if (n > 1)  fprintf(stderr, "\t%d missing %c's\n", n, c2);
  else if (n == -1) fprintf(stderr, "\textra %c\n", c2);
  else if (n < -1) fprintf(stderr, "\t%d extra %c's\n", -n, c2);
}

void FATAL(const char *fmt, ...)
{
  extern char *cmdname;
  va_list varg;

  if(toys.exitval == 0) toys.exitval++;
  fflush(stdout);
  fprintf(stderr, "%s: ", cmdname);
  va_start(varg, fmt);
  vfprintf(stderr, fmt, varg);
  va_end(varg);
  error();
  if (dbg > 1)    /* core dump if serious debugging on */
    abort();
  exit(2);
}

void WARNING(const char *fmt, ...)
{
  extern char *cmdname;
  va_list varg;

  fflush(stdout);
  fprintf(stderr, "%s: ", cmdname);
  va_start(varg, fmt);
  vfprintf(stderr, fmt, varg);
  va_end(varg);
  error();
}

void error()
{
  extern Node *curnode;

  fprintf(stderr, "\n");
  if (compile_time != 2 && NR && *NR > 0) {
    fprintf(stderr, " input record number %d", (int) (*FNR));
    if (strcmp(*FILENAME, "-") != 0)
      fprintf(stderr, ", file %s", *FILENAME);
    fprintf(stderr, "\n");
  }
  if (compile_time != 2 && curnode)
    fprintf(stderr, " source line number %d", curnode->lineno);
  else if (compile_time != 2 && lineno)
    fprintf(stderr, " source line number %d", lineno);
  if (compile_time == 1 && cursource() != NULL)
    fprintf(stderr, " source file %s", cursource());
  fprintf(stderr, "\n");
  eprint();
}

void eprint(void)  /* try to print context around error */
{
  char *p, *q;
  int c;
  static int been_here = 0;
  extern char ebuf[], *ep;

  if (compile_time == 2 || compile_time == 0 || been_here++ > 0)
    return;
  p = ep - 1;
  if (p > ebuf && *p == '\n')  p--;
  for ( ; p > ebuf && *p != '\n' && *p != '\0'; p--)
    ;
  while (*p == '\n') p++;
  fprintf(stderr, " context is\n\t");
  for (q=ep-1; q>=p && *q!=' ' && *q!='\t' && *q!='\n'; q--)
    ;
  for ( ; p < q; p++)
    if (*p)  putc(*p, stderr);
  fprintf(stderr, " >>> ");
  for ( ; p < ep; p++)
    if (*p)  putc(*p, stderr);
  fprintf(stderr, " <<< ");
  if (*ep)
    while ((c = input()) != '\n' && c != '\0' && c != EOF) {
      putc(c, stderr);
      bclass(c);
    }
  putc('\n', stderr);
  ep = ebuf;
}

void bclass(int c)
{
  switch (c) {
  case '{': bracecnt++; break;
  case '}': bracecnt--; break;
  case '[': brackcnt++; break;
  case ']': brackcnt--; break;
  case '(': parencnt++; break;
  case ')': parencnt--; break;
  default : break;
  }
}

double errcheck(double x, const char *s)
{

  if (errno == EDOM) {
    errno = 0;
    WARNING("%s argument out of domain", s);
    x = 1;
  } else if (errno == ERANGE) {
    errno = 0;
    WARNING("%s result out of range", s);
    x = 1;
  }
  return x;
}

int isclvar(const char *s)  /* is s of form var=something ? */
{
  const char *os = s;

  if (!isalpha((uschar) *s) && *s != '_')
    return 0;
  for ( ; *s; s++)
    if (!(isalnum((uschar) *s) || *s == '_'))
      break;
  return *s == '=' && s > os && *(s+1) != '=';
}

/* strtod is supposed to be a proper test of what's a valid number */
/* appears to be broken in gcc on linux: thinks 0x123 is a valid FP number */
/* wrong: violates 4.10.1.4 of ansi C standard */

int is_number(const char *s)
{
  double r;
  char *ep;
  errno = 0;
  r = strtod(s, &ep);
  if (ep == s || r == HUGE_VAL || errno == ERANGE)
    return 0;
  while (*ep == ' ' || *ep == '\t' || *ep == '\n')
    ep++;
  if (*ep == '\0') return 1;
  else return 0;
}

/* encoding in tree Nodes:
  leaf (CCL, NCCL, CHAR, DOT, FINAL, ALL, EMPTYRE):
    left is index, right contains value or pointer to value
  unary (STAR, PLUS, QUEST): left is child, right is null
  binary (CAT, OR): left and right are children
  parent contains pointer to parent
*/

fa *makedfa(const char *s, int anchor)  /* returns dfa for reg expr s */
{
  int i, use, nuse;
  fa *pfa;
  static int now = 1;

  if (setvec == 0) {  /* first time through any RE */
    maxsetvec = MAXLIN;
    setvec = (int *) malloc(maxsetvec * sizeof(int));
    tmpset = (int *) malloc(maxsetvec * sizeof(int));
    if (setvec == 0 || tmpset == 0)
      overflo("out of space initializing makedfa");
  }

  if (compile_time)  /* a constant for sure */
    return mkdfa(s, anchor);
  for (i = 0; i < nfatab; i++)  /* is it there already? */
    if (fatab[i]->anchor == anchor
      && strcmp((const char *) fatab[i]->restr, s) == 0) {
      fatab[i]->use = now++;
      return fatab[i];
    }
  pfa = mkdfa(s, anchor);
  if (nfatab < NFA) {  /* room for another */
    fatab[nfatab] = pfa;
    fatab[nfatab]->use = now++;
    nfatab++;
    return pfa;
  }
  use = fatab[0]->use;  /* replace least-recently used */
  nuse = 0;
  for (i = 1; i < nfatab; i++)
    if (fatab[i]->use < use) {
      use = fatab[i]->use;
      nuse = i;
    }
  freefa(fatab[nuse]);
  fatab[nuse] = pfa;
  pfa->use = now++;
  return pfa;
}

fa *mkdfa(const char *s, int anchor)  /* does the real work of making a dfa */
        /* anchor = 1 for anchored matches, else 0 */
{
  Node *p, *p1;
  fa *f;

  p = reparse(s);
  p1 = op2(CAT, op2(STAR, op2(ALL, NIL, NIL), NIL), p);
    /* put ALL STAR in front of reg.  exp. */
  p1 = op2(CAT, p1, op2(FINAL, NIL, NIL));
    /* put FINAL after reg.  exp. */

  poscnt = 0;
  penter(p1);  /* enter parent pointers and leaf indices */
  if ((f = (fa *) calloc(1, sizeof(fa) + poscnt*sizeof(rrow))) == NULL)
    overflo("out of space for fa");
  f->accept = poscnt-1;  /* penter has computed number of positions in re */
  cfoll(f, p1);  /* set up follow sets */
  freetr(p1);
  if ((f->posns[0] = (int *) calloc(1, *(f->re[0].lfollow)*sizeof(int))) == NULL)
      overflo("out of space in makedfa");
  if ((f->posns[1] = (int *) calloc(1, sizeof(int))) == NULL)
    overflo("out of space in makedfa");
  *f->posns[1] = 0;
  f->initstat = makeinit(f, anchor);
  f->anchor = anchor;
  f->restr = (uschar *) tostring(s);
  return f;
}

int makeinit(fa *f, int anchor)
{
  int i, k;

  f->curstat = 2;
  f->out[2] = 0;
  f->reset = 0;
  k = *(f->re[0].lfollow);
  xfree(f->posns[2]);
  if ((f->posns[2] = (int *) calloc(1, (k+1)*sizeof(int))) == NULL)
    overflo("out of space in makeinit");
  for (i=0; i <= k; i++) {
    (f->posns[2])[i] = (f->re[0].lfollow)[i];
  }
  if ((f->posns[2])[1] == f->accept) f->out[2] = 1;
  for (i=0; i < NCHARS; i++)
    f->gototab[2][i] = 0;
  f->curstat = cgoto(f, 2, HAT);
  if (anchor) {
    *f->posns[2] = k-1;  /* leave out position 0 */
    for (i=0; i < k; i++) {
      (f->posns[0])[i] = (f->posns[2])[i];
    }

    f->out[0] = f->out[2];
    if (f->curstat != 2) --(*f->posns[f->curstat]);
  }
  return f->curstat;
}

void penter(Node *p)  /* set up parent pointers and leaf indices */
{
  switch (type(p)) {
  ELEAF /*FALL_THROUGH*/
  LEAF
    info(p) = poscnt;
    poscnt++;
    break;
  UNARY
    penter(left(p));
    parent(left(p)) = p;
    break;
  case CAT: /*FALL_THROUGH*/
  case OR:
    penter(left(p));
    penter(right(p));
    parent(left(p)) = p;
    parent(right(p)) = p;
    break;
  default:  /* can't happen */
    FATAL("can't happen: unknown type %d in penter", type(p));
    break;
  }
}

void freetr(Node *p)  /* free parse tree */
{
  switch (type(p)) {
  ELEAF /*FALL_THROUGH*/
  LEAF
    xfree(p);
    break;
  UNARY
    freetr(left(p));
    xfree(p);
    break;
  case CAT: /*FALL_THROUGH*/
  case OR:
    freetr(left(p));
    freetr(right(p));
    xfree(p);
    break;
  default:  /* can't happen */
    FATAL("can't happen: unknown type %d in freetr", type(p));
    break;
  }
}

/* in the parsing of regular expressions, metacharacters like . have */
/* to be seen literally;  \056 is not a metacharacter. */

int hexstr(uschar **pp)  /* find and eval hex string at pp, return new p */
{      /* only pick up one 8-bit byte (2 chars) */
  uschar *p;
  int n = 0;
  int i;

  for (i = 0, p = (uschar *) *pp; i < 2 && isxdigit(*p); i++, p++) {
    if (isdigit(*p))
      n = 16 * n + *p - '0';
    else if (*p >= 'a' && *p <= 'f')
      n = 16 * n + *p - 'a' + 10;
    else if (*p >= 'A' && *p <= 'F')
      n = 16 * n + *p - 'A' + 10;
  }
  *pp = (uschar *) p;
  return n;
}

int quoted(uschar **pp)  /* pick up next thing after a \\ */
      /* and increment *pp */
{
  uschar *p = *pp;
  int c;

  if ((c = *p++) == 't') c = '\t';
  else if (c == 'n') c = '\n';
  else if (c == 'f') c = '\f';
  else if (c == 'r') c = '\r';
  else if (c == 'b') c = '\b';
  else if (c == '\\')  c = '\\';
  else if (c == 'x') {  /* hexadecimal goo follows */
    c = hexstr(&p);  /* this adds a null if number is invalid */
  } else if (isoctdigit(c)) {  /* \d \dd \ddd */
    int n = c - '0';
    if (isoctdigit(*p)) {
      n = 8 * n + *p++ - '0';
      if (isoctdigit(*p))  n = 8 * n + *p++ - '0';
    }
    c = n;
  } /* else */
  /* c = c; */
  *pp = p;
  return c;
}

char *cclenter(const char *argp)  /* add a character class */
{
  int i, c, c2;
  uschar *p = (uschar *) argp;
  uschar *op, *bp;
  static uschar *buf = 0;
  static int bufsz = 100;

  op = p;
  if (buf == 0 && (buf = (uschar *) malloc(bufsz)) == NULL)
    FATAL("out of space for character class [%.10s...] 1", p);
  bp = buf;
  for (i = 0; (c = *p++) != 0; ) {
    if (c == '\\') c = quoted(&p);
    else if (c == '-' && i > 0 && bp[-1] != 0) {
      if (*p != 0) {
        c = bp[-1];
        c2 = *p++;
        if (c2 == '\\')  c2 = quoted(&p);
        if (c > c2) {  /* empty; ignore */
          bp--;
          i--;
          continue;
        }
        while (c < c2) {
          if (!adjbuf((char **) &buf, &bufsz, bp-buf+2, 100, (char **) &bp, "cclenter1"))
            FATAL("out of space for character class [%.10s...] 2", p);
          *bp++ = ++c;
          i++;
        }
        continue;
      }
    }
    if (!adjbuf((char **) &buf, &bufsz, bp-buf+2, 100, (char **) &bp, "cclenter2"))
      FATAL("out of space for character class [%.10s...] 3", p);
    *bp++ = c;
    i++;
  }
  *bp = 0;
  dprintf( ("cclenter: in = |%s|, out = |%s|\n", op, buf) );
  xfree(op);
  return (char *) tostring((char *) buf);
}

void overflo(const char *s)
{
  FATAL("regular expression too big: %.30s...", s);
}

void cfoll(fa *f, Node *v)  /* enter follow set of each leaf of vertex v into lfollow[leaf] */
{
  int i;
  int *p;

  switch (type(v)) {
  ELEAF
  LEAF
    f->re[info(v)].ltype = type(v);
    f->re[info(v)].lval.np = right(v);
    while (f->accept >= maxsetvec) {  /* guessing here! */
      maxsetvec *= 4;
      setvec = (int *) realloc(setvec, maxsetvec * sizeof(int));
      tmpset = (int *) realloc(tmpset, maxsetvec * sizeof(int));
      if (setvec == 0 || tmpset == 0)
        overflo("out of space in cfoll()");
    }
    for (i = 0; i <= f->accept; i++)
      setvec[i] = 0;
    setcnt = 0;
    follow(v);  /* computes setvec and setcnt */
    if ((p = (int *) calloc(1, (setcnt+1)*sizeof(int))) == NULL)
      overflo("out of space building follow set");
    f->re[info(v)].lfollow = p;
    *p = setcnt;
    for (i = f->accept; i >= 0; i--)
      if (setvec[i] == 1)  *++p = i;
    break;
  UNARY
    cfoll(f,left(v));
    break;
  case CAT: /*FALL_THROUGH*/
  case OR:
    cfoll(f,left(v));
    cfoll(f,right(v));
    break;
  default:  /* can't happen */
    FATAL("can't happen: unknown type %d in cfoll", type(v));
  }
}

int first(Node *p)  /* collects initially active leaves of p into setvec */
      /* returns 0 if p matches empty string */
{
  int b, lp;

  switch (type(p)) {
  ELEAF
  LEAF
    lp = info(p);  /* look for high-water mark of subscripts */
    while (setcnt >= maxsetvec || lp >= maxsetvec) {  /* guessing here! */
      maxsetvec *= 4;
      setvec = (int *) realloc(setvec, maxsetvec * sizeof(int));
      tmpset = (int *) realloc(tmpset, maxsetvec * sizeof(int));
      if (setvec == 0 || tmpset == 0)
        overflo("out of space in first()");
    }
    if (type(p) == EMPTYRE) {
      setvec[lp] = 0;
      return(0);
    }
    if (setvec[lp] != 1) {
      setvec[lp] = 1;
      setcnt++;
    }
    if (type(p) == CCL && (*(char *) right(p)) == '\0')
      return(0);    /* empty CCL */
    else return(1);
  case PLUS:
    if (first(left(p)) == 0) return(0);
    return(1);
  case STAR: /*FALL_THROUGH*/
  case QUEST:
    first(left(p));
    return(0);
  case CAT:
    if (first(left(p)) == 0 && first(right(p)) == 0) return(0);
    return(1);
  case OR:
    b = first(right(p));
    if (first(left(p)) == 0 || b == 0) return(0);
    return(1);
  default:
    FATAL("can't happen: unknown type %d in first", type(p));  /* can't happen */
    return(-1);
  }
}

void follow(Node *v)  /* collects leaves that can follow v into setvec */
{
  Node *p;

  if (type(v) == FINAL) return;
  p = parent(v);
  switch (type(p)) {
  case STAR: /*FALL_THROUGH*/
  case PLUS:
    first(v);
    follow(p);
    return;

  case OR: /*FALL_THROUGH*/
  case QUEST:
    follow(p);
    return;

  case CAT:
    if (v == left(p)) {  /* v is left child of p */
      if (first(right(p)) == 0) {
        follow(p);
        return;
      }
    } else follow(p);  /* v is right child */
    return;
  default: break;
  }
}

int member(int c, const char *sarg)  /* is c in s? */
{
  uschar *s = (uschar *) sarg;

  while (*s)
    if (c == *s++) return(1);
  return(0);
}

int match(fa *f, const char *p0)  /* shortest match ? */
{
  int s, ns;
  uschar *p = (uschar *) p0;

  s = f->reset ? makeinit(f,0) : f->initstat;
  if (f->out[s]) return(1);
  do {
    /* assert(*p < NCHARS); */
    if ((ns = f->gototab[s][*p]) != 0) s = ns;
    else s = cgoto(f, s, *p);
    if (f->out[s]) return(1);
  } while (*p++ != 0);
  return(0);
}

int pmatch(fa *f, const char *p0)  /* longest match, for sub */
{
  int s, ns;
  uschar *p = (uschar *) p0;
  uschar *q;
  int i, k;

  /* s = f->reset ? makeinit(f,1) : f->initstat; */
  if (f->reset) f->initstat = s = makeinit(f,1);
  else s = f->initstat;

  patbeg = (char *) p;
  patlen = -1;
  do {
    q = p;
    do {
      if (f->out[s]) patlen = q-p;  /* final state */
      /* assert(*q < NCHARS); */
      if ((ns = f->gototab[s][*q]) != 0) s = ns;
      else s = cgoto(f, s, *q);
      if (s == 1) {  /* no transition */
        if (patlen >= 0) {
          patbeg = (char *) p;
          return(1);
        }
        else goto nextin;  /* no match */
      }
    } while (*q++ != 0);
    if (f->out[s]) patlen = q-p-1;  /* don't count $ */
    if (patlen >= 0) {
      patbeg = (char *) p;
      return(1);
    }
nextin:
    s = 2;
    if (f->reset) {
      for (i = 2; i <= f->curstat; i++)
        xfree(f->posns[i]);
      k = *f->posns[0];
      if ((f->posns[2] = (int *) calloc(1, (k+1)*sizeof(int))) == NULL)
        overflo("out of space in pmatch");
      for (i = 0; i <= k; i++)
        (f->posns[2])[i] = (f->posns[0])[i];
      f->initstat = f->curstat = 2;
      f->out[2] = f->out[0];
      for (i = 0; i < NCHARS; i++)
        f->gototab[2][i] = 0;
    }
  } while (*p++ != 0);
  return (0);
}

int nematch(fa *f, const char *p0)  /* non-empty match, for sub */
{
  int s, ns;
  uschar *p = (uschar *) p0;
  uschar *q;
  int i, k;

  /* s = f->reset ? makeinit(f,1) : f->initstat; */
  if (f->reset) f->initstat = s = makeinit(f,1);
  else s = f->initstat;

  patlen = -1;
  while (*p) {
    q = p;
    do {
      if (f->out[s]) patlen = q-p;  /* final state */
      /* assert(*q < NCHARS); */
      if ((ns = f->gototab[s][*q]) != 0)
        s = ns;
      else s = cgoto(f, s, *q);
      if (s == 1) {  /* no transition */
        if (patlen > 0) {
          patbeg = (char *) p;
          return(1);
        } else goto nnextin;  /* no nonempty match */
      }
    } while (*q++ != 0);
    if (f->out[s])
      patlen = q-p-1;  /* don't count $ */
    if (patlen > 0 ) {
      patbeg = (char *) p;
      return(1);
    }
nnextin:
    s = 2;
    if (f->reset) {
      for (i = 2; i <= f->curstat; i++)
        xfree(f->posns[i]);
      k = *f->posns[0];
      if ((f->posns[2] = (int *) calloc(1, (k+1)*sizeof(int))) == NULL)
        overflo("out of state space");
      for (i = 0; i <= k; i++)
        (f->posns[2])[i] = (f->posns[0])[i];
      f->initstat = f->curstat = 2;
      f->out[2] = f->out[0];
      for (i = 0; i < NCHARS; i++)
        f->gototab[2][i] = 0;
    }
    p++;
  }
  return (0);
}

Node *reparse(const char *p)  /* parses regular expression pointed to by p */
{      /* uses relex() to scan regular expression */
  Node *np;

  dprintf( ("reparse <%s>\n", p) );
  lastre = prestr = (uschar *) p;  /* prestr points to string to be parsed */
  rtok = relex();
  /* GNU compatibility: an empty regexp matches anything */
  if (rtok == '\0') {
    /* FATAL("empty regular expression"); previous */
    return(op2(EMPTYRE, NIL, NIL));
  }
  np = regexp();
  if (rtok != '\0')
    FATAL("syntax error in regular expression %s at %s", lastre, prestr);
  return(np);
}

Node *regexp(void)  /* top-level parse of reg expr */
{
  return (alt(concat(primary())));
}

Node *primary(void)
{
  Node *np;

  switch (rtok) {
  case CHAR:
    np = op2(CHAR, NIL, itonp(rlxval));
    rtok = relex();
    return (unary(np));
  case ALL:
    rtok = relex();
    return (unary(op2(ALL, NIL, NIL)));
  case EMPTYRE:
    rtok = relex();
    return (unary(op2(ALL, NIL, NIL)));
  case DOT:
    rtok = relex();
    return (unary(op2(DOT, NIL, NIL)));
  case CCL:
    np = op2(CCL, NIL, (Node*) cclenter((char *) rlxstr));
    rtok = relex();
    return (unary(np));
  case NCCL:
    np = op2(NCCL, NIL, (Node *) cclenter((char *) rlxstr));
    rtok = relex();
    return (unary(np));
  case '^':
    rtok = relex();
    return (unary(op2(CHAR, NIL, itonp(HAT))));
  case '$':
    rtok = relex();
    return (unary(op2(CHAR, NIL, NIL)));
  case '(':
    rtok = relex();
    if (rtok == ')') {  /* special pleading for () */
      rtok = relex();
      return unary(op2(CCL, NIL, (Node *) tostring("")));
    }
    np = regexp();
    if (rtok == ')') {
      rtok = relex();
      return (unary(np));
    }
    else
      FATAL("syntax error in regular expression %s at %s", lastre, prestr);
  default:
    FATAL("illegal primary in regular expression %s at %s", lastre, prestr);
  }
  return 0;  /*NOTREACHED*/
}

Node *concat(Node *np)
{
  switch (rtok) {
  case CHAR: /*FALL_THROUGH*/
  case DOT: /*FALL_THROUGH*/
  case ALL: /*FALL_THROUGH*/
  case EMPTYRE: /*FALL_THROUGH*/
  case CCL: /*FALL_THROUGH*/
  case NCCL: /*FALL_THROUGH*/
  case '$': /*FALL_THROUGH*/
  case '(':
    return (concat(op2(CAT, np, primary())));
    break; //NOT REACHABLE
  default: return (np);
    break; //NOT REACHABLE
  }
}

Node *alt(Node *np) {
  if (rtok == OR) {
    rtok = relex();
    return (alt(op2(OR, np, concat(primary()))));
  }
  return (np);
}

Node *unary(Node *np)
{
  switch (rtok) {
  case STAR:
    rtok = relex();
    return (unary(op2(STAR, np, NIL)));
  case PLUS:
    rtok = relex();
    return (unary(op2(PLUS, np, NIL)));
  case QUEST:
    rtok = relex();
    return (unary(op2(QUEST, np, NIL)));
  default:
    return (np);
  }
}

/*
 * Character class definitions conformant to the POSIX locale as
 * defined in IEEE P1003.1 draft 7 of June 2001, assuming the source
 * and operating character sets are both ASCII (ISO646) or supersets
 * thereof.
 *
 * Note that to avoid overflowing the temporary buffer used in
 * relex(), the expanded character class (prior to range expansion)
 * must be less than twice the size of their full name.
 */

/* Because isblank doesn't show up in any of the header files on any
 * system i use, it's defined here.  if some other locale has a richer
 * definition of "blank", define HAS_ISBLANK and provide your own
 * version.
 * the parentheses here are an attempt to find a path through the maze
 * of macro definition and/or function and/or version provided.  thanks
 * to nelson beebe for the suggestion; let's see if it works everywhere.
 */
int (xisblank)(int c)
{
  return c==' ' || c=='\t';
}

int relex(void)    /* lexical analyzer for reparse */
{
  int c, n;
  int cflag;
  static uschar *buf = 0;
  static int bufsz = 100;
  uschar *bp;
  struct charclass *cc;
  int i;

  switch (c = *prestr++) {
    case '|': return OR;
    case '*': return STAR;
    case '+': return PLUS;
    case '?': return QUEST;
    case '.': return DOT;
    case '\0': prestr--; return '\0';
    case '^': /*FALL_THROUGH*/
    case '$': /*FALL_THROUGH*/
    case '(': /*FALL_THROUGH*/
    case ')':
           return c;
    case '\\':
           rlxval = quoted(&prestr);
           return CHAR;
    default:
           rlxval = c;
           return CHAR;
    case '[':
           if (buf == 0 && (buf = (uschar *) malloc(bufsz)) == NULL)
             FATAL("out of space in reg expr %.10s..", lastre);
           bp = buf;
           if (*prestr == '^') {
             cflag = 1;
             prestr++;
           }
           else cflag = 0;
           n = 2 * strlen((const char *) prestr)+1;
           if (!adjbuf((char **) &buf, &bufsz, n, n, (char **) &bp, "relex1"))
             FATAL("out of space for reg expr %.10s...", lastre);
           for (; ; ) {
             if ((c = *prestr++) == '\\') {
               *bp++ = '\\';
               if ((c = *prestr++) == '\0')
                 FATAL("nonterminated character class %.20s...", lastre);
               *bp++ = c;
               /* } else if (c == '\n') { */
               /*   FATAL("newline in character class %.20s...", lastre); */
           } else if (c == '[' && *prestr == ':') {
             /* POSIX char class names, Dag-Erling Smorgrav, des@ofug.org */
             for (cc = charclasses; cc->cc_name; cc++)
               if (strncmp((const char *) prestr + 1, (const char *) cc->cc_name, cc->cc_namelen) == 0)
                 break;
             if (cc->cc_name != NULL && prestr[1 + cc->cc_namelen] == ':' &&
                 prestr[2 + cc->cc_namelen] == ']') {
               prestr += cc->cc_namelen + 3;
               for (i = 0; i < NCHARS; i++) {
                 if (!adjbuf((char **) &buf, &bufsz, bp-buf+1, 100, (char **) &bp, "relex2"))
                   FATAL("out of space for reg expr %.10s...", lastre);
                 if (cc->cc_func(i)) {
                   *bp++ = i;
                   n++;
                 }
               }
             } else *bp++ = c;
           } else if (c == '\0') {
             FATAL("nonterminated character class %.20s", lastre);
           } else if (bp == buf) {  /* 1st char is special */
             *bp++ = c;
           } else if (c == ']') {
             *bp++ = 0;
             rlxstr = (uschar *) tostring((char *) buf);
             if (cflag == 0)  return CCL;
             else return NCCL;
           } else *bp++ = c;
           }
  }
}

int cgoto(fa *f, int s, int c)
{
  int i, j, k;
  int *p, *q;

  assert(c == HAT || c < NCHARS);
  while (f->accept >= maxsetvec) {  /* guessing here! */
    maxsetvec *= 4;
    setvec = (int *) realloc(setvec, maxsetvec * sizeof(int));
    tmpset = (int *) realloc(tmpset, maxsetvec * sizeof(int));
    if (setvec == 0 || tmpset == 0)
      overflo("out of space in cgoto()");
  }
  for (i = 0; i <= f->accept; i++)
    setvec[i] = 0;
  setcnt = 0;
  /* compute positions of gototab[s,c] into setvec */
  p = f->posns[s];
  for (i = 1; i <= *p; i++) {
    if ((k = f->re[p[i]].ltype) != FINAL) {
      if ((k == CHAR && c == ptoi(f->re[p[i]].lval.np))
       || (k == DOT && c != 0 && c != HAT)
       || (k == ALL && c != 0)
       || (k == EMPTYRE && c != 0)
       || (k == CCL && member(c, (char *) f->re[p[i]].lval.up))
       || (k == NCCL && !member(c, (char *) f->re[p[i]].lval.up) && c != 0 && c != HAT)) {
        q = f->re[p[i]].lfollow;
        for (j = 1; j <= *q; j++) {
          if (q[j] >= maxsetvec) {
            maxsetvec *= 4;
            setvec = (int *) realloc(setvec, maxsetvec * sizeof(int));
            tmpset = (int *) realloc(tmpset, maxsetvec * sizeof(int));
            if (setvec == 0 || tmpset == 0)
              overflo("cgoto overflow");
          }
          if (setvec[q[j]] == 0) {
            setcnt++;
            setvec[q[j]] = 1;
          }
        }
      }
    }
  }
  /* determine if setvec is a previous state */
  tmpset[0] = setcnt;
  j = 1;
  for (i = f->accept; i >= 0; i--)
    if (setvec[i]) {
      tmpset[j++] = i;
    }
  /* tmpset == previous state? */
  for (i = 1; i <= f->curstat; i++) {
    p = f->posns[i];
    if ((k = tmpset[0]) != p[0])
      goto different;
    for (j = 1; j <= k; j++)
      if (tmpset[j] != p[j])
        goto different;
    /* setvec is state i */
    f->gototab[s][c] = i;
    return i;
    different:;
  }

  /* add tmpset to current set of states */
  if (f->curstat >= NSTATES-1) {
    f->curstat = 2;
    f->reset = 1;
    for (i = 2; i < NSTATES; i++)
      xfree(f->posns[i]);
  } else ++(f->curstat);
  for (i = 0; i < NCHARS; i++)
    f->gototab[f->curstat][i] = 0;
  xfree(f->posns[f->curstat]);
  if ((p = (int *) calloc(1, (setcnt+1)*sizeof(int))) == NULL)
    overflo("out of space in cgoto");

  f->posns[f->curstat] = p;/*
   * end main.c
   */


  f->gototab[s][c] = f->curstat;
  for (i = 0; i <= setcnt; i++)
    p[i] = tmpset[i];
  if (setvec[f->accept]) f->out[f->curstat] = 1;
  else f->out[f->curstat] = 0;
  return f->curstat;
}


void freefa(fa *f)  /* free a finite automaton */
{
  int i;

  if (f == NULL) return;
  for (i = 0; i <= f->curstat; i++)
    xfree(f->posns[i]);
  for (i = 0; i <= f->accept; i++) {
    xfree(f->re[i].lfollow);
    if (f->re[i].ltype == CCL || f->re[i].ltype == NCCL)
      xfree((f->re[i].lval.np));
  }
  xfree(f->restr);
  xfree(f);
}

void awk_main(void)
{
  const char *fs = NULL;
  int i;

  if (!(toys.optflags & FLAG_f) && (toys.optc == 0)) {
    toys.exithelp = 1;
    error_exit("Provide -f or program");
  }

  setlocale(LC_CTYPE, "");
  setlocale(LC_NUMERIC, "C"); /* for parsing cmdline & prog */

  if (toys.optflags & FLAG_d)  dbg = 1;

  if (toys.optflags & FLAG_s)  safe = 1;

  cmdname = toys.argv[0];
  signal(SIGFPE, fpecatch);
  srand_seed = 1;
  srand(srand_seed);

  yyin = NULL;
  symtab = makesymtab(NSYMTAB/NSYMTAB);

  if (toys.optflags & FLAG_F) {  /* -F FS set field separator */
    if (TT.sep[0] == 't' && TT.sep[1] == 0) fs = "\t";
    else fs = TT.sep;
    if (fs == NULL || *fs == '\0') WARNING("field separator FS is empty");
    else dprintf(("seprator : %s", fs));

  }

  if (toys.optflags & FLAG_v) { /* -v a=1 to be done NOW.  one -v for each */
    while(TT.vars) {
      if (isclvar(TT.vars->arg)) setclvar(TT.vars->arg);
      else FATAL("invalid -v option argument: %s", TT.vars->arg);
      TT.vars = TT.vars->next;
    }
  }

  struct arg_list *backprogfiles = TT.prog_files;

  if (toys.optflags & FLAG_f) { /* -f program filename */
    while (TT.prog_files) {
      if (npfile >= MAX_PFILE - 1) FATAL("too many -f options");
      pfile[npfile++] = TT.prog_files->arg;
      TT.prog_files = TT.prog_files->next;
    }
  } else {  /* no -f; first argument is program */
    dprintf( ("program = |%s|\n", toys.optargs[0]) );
    lexprog = toys.optargs[0];
    toys.optargs++;
    toys.optc--;
  }

  TT.prog_files = backprogfiles;
  recinit(recsize);
  syminit();
  compile_time = 1;
  for(i = 1;i <= toys.optc;i++)
    toys.argv[i] = toys.optargs[i -1];

  toys.argv[i] = NULL;
  dprintf( ("argc=%d, argv[0]=%s\n", toys.optc, toys.optargs[0]) );
  arginit(toys.optc +1, toys.argv);
  if (!safe) envinit(environ);
  yyparse();
  setlocale(LC_NUMERIC, ""); /* back to whatever it is locally */
  if (fs)  *FS = qstring(fs, '\0');
  dprintf( ("errorflag=%d\n", errorflag) );
  if (errorflag == 0) {
    compile_time = 0;
    run(winner);
  } else bracecheck();
}

int pgetc(void)    /* get 1 character from awk program */
{
  int c;

  for (;;) {
    if (yyin == NULL) {
      if (curpfile >= npfile)  return EOF;
      if (strcmp(pfile[curpfile], "-") == 0) yyin = stdin;
      else if ((yyin = fopen(pfile[curpfile], "r")) == NULL)
        FATAL("can't open file %s", pfile[curpfile]);
      lineno = 1;
    }
    if ((c = getc(yyin)) != EOF) return c;
    if (yyin != stdin) fclose(yyin);
    yyin = NULL;
    curpfile++;
  }
}

char *cursource(void)  /* current source file name */
{
  if (npfile > 0)  return pfile[curpfile];
  else return NULL;
}

