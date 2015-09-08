/* test.c - check file types and compare values (Evaluate Expression).
 *
 * Copyright 2012 Sandeep Sharma <sandeep.jack2756@gmail.com>
 * Copright 2012 Ranjan Kumar <ranjankumar.bth@gmail.com>
 *
 * http://pubs.opengroup.org/onlinepubs/9699919799/utilities/test.html
 *
USE_TEST(NEWTOY(test, NULL, TOYFLAG_USR|TOYFLAG_BIN))
USE_TEST(OLDTOY([, test, NULL, TOYFLAG_USR|TOYFLAG_BIN))
USE_TEST(OLDTOY([[, test, NULL, TOYFLAG_USR|TOYFLAG_BIN))

config TEST
  bool "test"
  default y
  help
    usage: test [EXPRESSION] [EXPRESSION] ...

    check file types and compare values (Evaluate Expression).
*/

/* $NetBSD: test.c,v 1.30 2006/09/24 13:24:08 hubertf Exp $ */

/*
 * test(1); version 7-like  --  author Erik Baalbergen
 * modified by Eric Gisin to be used as built-in.
 * modified by Arnold Robbins to add SVR3 compatibility
 * (-x -c -b -p -u -g -k) plus Korn's -L -nt -ot -ef and new -S (socket).
 * modified by J.T. Conklin for NetBSD.
 *
 * This program is in the Public Domain.
 */

#define FOR_test
#include "toys.h"
#include <sys/cdefs.h>
#include <err.h>

/* test(1) accepts the following grammar:
 *     oexpr  ::= aexpr | aexpr "-o" oexpr ;
 *     aexpr  ::= nexpr | nexpr "-a" aexpr ;
 *     nexpr  ::= primary | "!" primary
 *     primary  ::= unary-operator operand
 *         | operand binary-operator operand
 *         | operand
 *         | "(" oexpr ")"
 *         ;
 *     unary-operator ::= "-r"|"-w"|"-x"|"-f"|"-d"|"-c"|"-b"|"-p"|
 *               "-u"|"-g"|"-k"|"-s"|"-t"|"-z"|"-n"|"-o"|"-O"|"-G"|"-L"|"-S";
 *    binary-operator ::= "="|"!="|"-eq"|"-ne"|"-ge"|"-gt"|"-le"|"-lt"|
 *               "-nt"|"-ot"|"-ef";
 *     operand ::= <any legal UNIX file name>
*/

enum token {
  EOI, 
  //file access...
  FILRD, FILWR, FILEX, FILEXIST,
  //file type...
  FILREG, FILDIR, FILCDEV, FILBDEV, FILFIFO, FILSOCK, FILSYM, FILGZ, FILTT,
  //file bit...
  FILSUID, FILSGID, FILSTCK,
  //file options...
  FILNT, FILOT, FILEQ, FILUID, FILGID,
  //str options...
  STREZ, STRNZ, STREQ, STRNE, STRLT, STRGT,
  //int options...
  INTEQ, INTNE, INTGE, INTGT, INTLE, INTLT, UNOT, BAND, BOR, LPAREN, RPAREN, OPERAND
};

enum token_types {
  UNOP,
  BINOP,
  BUNOP,
  BBINOP,
  PAREN
};

static struct t_op {
  const char *op_text;
  short op_num, op_type;
} const ops [] = {
  {"-r",  FILRD,  UNOP},  //FILE exists and read permission is granted
  {"-w",  FILWR,  UNOP},  //FILE exists and write permission is granted
  {"-x",  FILEX,  UNOP},  //FILE exists and execute (or search) permission is granted
  {"-e",  FILEXIST,UNOP},  //FILE exists
  {"-f",  FILREG,  UNOP},  //FILE exists and is a regular file
  {"-d",  FILDIR,  UNOP},  //FILE exists and is a directory
  {"-c",  FILCDEV,UNOP},  //FILE exists and is character special
  {"-b",  FILBDEV,UNOP},  //FILE exists and is block special
  {"-p",  FILFIFO,UNOP},  //FILE exists and is a named pipe
  {"-u",  FILSUID,UNOP},  //FILE exists and its set-user-ID bit is set
  {"-g",  FILSGID,UNOP},  //FILE exists and is set-group-ID
  {"-k",  FILSTCK,UNOP},  //FILE exists and has its sticky bit set
  {"-s",  FILGZ,  UNOP},  //FILE exists and has a size greater than zero
  {"-t",  FILTT,  UNOP},  //file descriptor FD is opened on a terminal
  {"-z",  STREZ,  UNOP},  //the length of STRING is zero
  {"-n",  STRNZ,  UNOP},  //the length of STRING is nonzero
  {"-h",  FILSYM,  UNOP},  //FILE exists and is a symbolic link (same as -L)
  {"-O",  FILUID,  UNOP},  //FILE exists and is owned by the effective user ID
  {"-G",  FILGID,  UNOP},  //FILE exists and is owned by the effective group ID
  {"-L",  FILSYM,  UNOP},  //FILE exists and is a symbolic link (same as -h)
  {"-S",  FILSOCK,UNOP},  //FILE exists and is a socket
  {"=",  STREQ,  BINOP},  //STRING1 = STRING2 (the strings are equal)
  {"==", STREQ,  BINOP},
  {"!=",  STRNE,  BINOP}, //STRING1 != STRING2 (the strings are not equal)
  {"<",  STRLT,  BINOP},  //STRING1 < STRING2
  {">",  STRGT,  BINOP},  //STRING1 > STRING2
  {"-eq",  INTEQ,  BINOP},  //INTEGER1 -eq INTEGER2 (INTEGER1 is equal to INTEGER2)
  {"-ne",  INTNE,  BINOP}, //INTEGER1 -ne INTEGER2 (INTEGER1 is not equal to INTEGER2)
  {"-ge",  INTGE,  BINOP}, //INTEGER1 -ge INTEGER2 (INTEGER1 is greater than or equal to INTEGER2)
  {"-gt",  INTGT,  BINOP}, //INTEGER1 -gt INTEGER2 (INTEGER1 is greater than INTEGER2)
  {"-le",  INTLE,  BINOP}, //INTEGER1 -le INTEGER2 (INTEGER1 is less than or equal to INTEGER2)
  {"-lt",  INTLT,  BINOP}, //INTEGER1 -lt INTEGER2 (INTEGER1 is less than INTEGER2)
  {"-nt",  FILNT,  BINOP}, //FILE1 -nt FILE2 (FILE1 is newer (modification date) than FILE2)
  {"-ot",  FILOT,  BINOP}, //FILE1 -ot FILE2 (FILE1 is older than FILE2)
  {"-ef",  FILEQ,  BINOP}, //FILE1 -ef FILE2 (FILE1 and FILE2 have the same device and inode numbers)
  {"!",  UNOT,  BUNOP}, //EXPRESSION is false (! EXPRESSION)
  {"-a",  BAND,  BBINOP}, //EXPRESSION1 -a EXPRESSION2 (both EXPRESSION1 and EXPRESSION2 are true)
  {"-o",  BOR,  BBINOP}, //EXPRESSION1 -o EXPRESSION2 (either EXPRESSION1 or EXPRESSION2 is true)
  {"(",  LPAREN,  PAREN}, //( EXPRESSION ) EXPRESSION is true
  {")",  RPAREN,  PAREN}, //( EXPRESSION ) EXPRESSION is true
  {0,  0,  0}
};

static char **t_wp;
static struct t_op const *t_wp_op;

static int primary(enum token n);

//For error message printing...
static void syntax(const char *op, const char *msg)
{
  toys.exitval = 2;
  if (op && *op) error_exit("%s: %s", op, msg);
  else error_exit("%s", msg);
  return;
}

static int isoperand(void) //verify the operand (if there search again in the "ops" list)...
{
  struct t_op const *op;
  char *s, *t;

  op = ops;
  if((s  = *(t_wp+1)) == 0)
    return 1;
  if((t = *(t_wp+2)) == 0)
    return 0;
  while(op->op_text) {
    if (strcmp(s, op->op_text) == 0)
      return op->op_type == BINOP &&
          (t[0] != ')' || t[1] != '\0'); 
    op++;
  }
  return 0;
}

//Find the option in "ops" list (if found return the option number from the list)...
static enum token t_lex(char *option)
{
  struct t_op const *op;
  op = ops;
  if(option == 0) {//if there is no option...
    t_wp_op = NULL;
    return EOI;
  }
  while(op->op_text) {//Search option...
    if (strcmp(option, op->op_text) == 0) {
      if ((op->op_type == UNOP && isoperand()) ||
        (op->op_num == LPAREN && *(t_wp+1) == 0))
        break;
      t_wp_op = op;
      return op->op_num;
    }
    op++;
  }
  t_wp_op = NULL;
  return OPERAND;
}
//Verify the "!" oprator (if there evaluate next expression)...
static int nexpr(enum token num)
{
  if(num == UNOT)
    return !nexpr(t_lex(*++t_wp));
  return primary(num);
}

static int aexpr(enum token num)
{
  int res = nexpr(num);
  if(t_lex(*++t_wp) == BAND)
    return aexpr(t_lex(*++t_wp)) && res;
  t_wp--;
  return res;
}

static int oexpr(enum token num)
{
  int res = aexpr(num);
  if(t_lex(*++t_wp) == BOR)
    return oexpr(t_lex(*++t_wp)) || res;
  t_wp--;
  return res;
}

//atoi with error detection...
static int getn(const char *s)
{
  char *p;
  long r;

  errno = 0;
  r = strtol(s, &p, 10);
  if (errno != 0)
	  syntax(s, "out of range");
  while (isspace((unsigned char)*p)) p++;

  if (*p)
	  syntax(s, "bad number");
  return (int) r;
}

static int newerf(const char *f1, const char *f2)
{
  struct stat b1, b2;

  return (stat(f1, &b1) == 0 &&
    stat(f2, &b2) == 0 &&
    b1.st_mtime > b2.st_mtime);
}

static int olderf(const char *f1, const char *f2)
{
  struct stat b1, b2;

  return (stat(f1, &b1) == 0 &&
    stat(f2, &b2) == 0 &&
    b1.st_mtime < b2.st_mtime);
}

static int equalf(const char *f1, const char *f2)
{
  struct stat b1, b2;

  return (stat(f1, &b1) == 0 &&
    stat(f2, &b2) == 0 &&
    b1.st_dev == b2.st_dev &&
    b1.st_ino == b2.st_ino);
}

static int binop(void)
{
  const char *opnd1, *opnd2;
  struct t_op const *op;

  opnd1 = *t_wp;
  (void) t_lex(*++t_wp);
  op = t_wp_op;

  if ((opnd2 = *++t_wp) == NULL)
    syntax(op->op_text, "argument expected");

  switch (op->op_num) {
  case STREQ:
    return strcmp(opnd1, opnd2) == 0;
  case STRNE:
    return strcmp(opnd1, opnd2) != 0;
  case STRLT:
    return strcmp(opnd1, opnd2) < 0;
  case STRGT:
    return strcmp(opnd1, opnd2) > 0;
  case INTEQ:
    return getn(opnd1) == getn(opnd2);
  case INTNE:
    return getn(opnd1) != getn(opnd2);
  case INTGE:
    return getn(opnd1) >= getn(opnd2);
  case INTGT:
    return getn(opnd1) > getn(opnd2);
  case INTLE:
    return getn(opnd1) <= getn(opnd2);
  case INTLT:
    return getn(opnd1) < getn(opnd2);
  case FILNT: //Time of last modification...
    return newerf(opnd1, opnd2);
  case FILOT:
    return olderf(opnd1, opnd2);
  case FILEQ:
    return equalf(opnd1, opnd2);
  default:
    abort(); break;
    /* NOTREACHED */
  }
  return 0; //verify...
}
//For the file related expressions...
static int filestat(char *nm, enum token mode)
{
  struct stat s;

  if (mode == FILSYM ? lstat(nm, &s) : stat(nm, &s))
    return 0;

  switch (mode) {
  case FILRD:
    return access(nm, R_OK) == 0;
  case FILWR:
    return access(nm, W_OK) == 0;
  case FILEX:
    return access(nm, X_OK) == 0;
  case FILEXIST:
    return access(nm, F_OK) == 0;
  case FILREG:
    return S_ISREG(s.st_mode);
  case FILDIR:
    return S_ISDIR(s.st_mode);
  case FILCDEV:
    return S_ISCHR(s.st_mode);
  case FILBDEV:
    return S_ISBLK(s.st_mode);
  case FILFIFO:
    return S_ISFIFO(s.st_mode);
  case FILSOCK:
    return S_ISSOCK(s.st_mode);
  case FILSYM:
    return S_ISLNK(s.st_mode);
  case FILSUID:
    return (s.st_mode & S_ISUID) != 0;
  case FILSGID:
    return (s.st_mode & S_ISGID) != 0;
  case FILSTCK:
    return (s.st_mode & S_ISVTX) != 0;
  case FILGZ:
    return s.st_size > (off_t)0;
  case FILUID:
    return s.st_uid == geteuid();
  case FILGID:
    return s.st_gid == getegid();
  default:
    return 1;
  }
}

static int primary(enum token num)
{
  enum token nn;
  int res;

  if (num == EOI)
    return 0;//missing expression..
  if (num == LPAREN) {
    if ((nn = t_lex(*++t_wp)) == RPAREN)
      return 0; //missing expression...
    res = oexpr(nn);
    if (t_lex(*++t_wp) != RPAREN)
      syntax(NULL, "closing paren expected");
    return res;
  }
  if (t_wp_op && t_wp_op->op_type == UNOP) { //For the unary operator...
    //unary expression...
    if (*++t_wp == NULL)
      syntax(t_wp_op->op_text, "argument expected");
    switch (num) {
    case STREZ:
      return strlen(*t_wp) == 0;
    case STRNZ:
      return strlen(*t_wp) != 0;
    case FILTT:
      return isatty(getn(*t_wp));
    default:
      return filestat(*t_wp, num);
    }
  }

  if (t_lex(t_wp[1]), t_wp_op && t_wp_op->op_type == BINOP) {
    return binop();
  }
  return strlen(*t_wp) > 0;
}

void test_main(void)
{
  char **argv = toys.argv;
  int res;
  const char *argv0;
  int argc = 0;

  argv0 = argv[0];
  while(*argv) {argc++; argv++;}
  argv = toys.argv;

  if(argv0[0] == '[') {
    --argc;
    if(!argv0[1]) { /* "[" ? */
      char *arg = argv[argc];
      if(arg[0] != ']' || arg[1]) {
    	syntax(NULL, "missing ]");
        return;
      }
    } else { /* assuming "[[" */
        if (strcmp(argv[argc], "]]") != 0) {
          syntax(NULL, "missing ]]");
          return;
        }
    }
    argv[argc] = NULL;
  }
  
  if(!argv[1]) {
	  toys.exitval = 1;
	  return;
  }
  t_wp = &argv[1];
  res = !oexpr(t_lex(*t_wp));

  if (*t_wp != NULL && *++t_wp != NULL)
    syntax(*t_wp, "unexpected operator");

  toys.exitval = res;
  return;
}
