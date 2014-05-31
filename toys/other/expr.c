/* expr.c - expr implementation
 *
 * Copyright 2013 Madhur Verma <mad.flexi@gmail.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/expr.html

USE_EXPR(NEWTOY(expr, "?", TOYFLAG_USR|TOYFLAG_BIN))

config EXPR
  bool "expr"
  default y
  help
    usage: expr EXPRESSION

    Print the value of EXPRESSION to stdout
    EXPRESSION may be:
            ARG1 | ARG2 ARG1 if it is neither null nor 0, otherwise ARG2
            ARG1 & ARG2 ARG1 if neither argument is null or 0, otherwise 0
            ARG1 < ARG2 1 if ARG1 is less than ARG2, else 0. Similarly:
            ARG1 <= ARG2
            ARG1 = ARG2
            ARG1 != ARG2
            ARG1 >= ARG2
            ARG1 > ARG2
            ARG1 + ARG2 Sum of ARG1 and ARG2. Similarly:
            ARG1 - ARG2
            ARG1 * ARG2
            ARG1 / ARG2
            ARG1 % ARG2
            STRING : REGEXP   Anchored pattern match of REGEXP in STRING
            match STRING REGEXP Same as STRING : REGEXP
            substr STRING POS LENGTH Substring of STRING, POS counted from 1
            index STRING CHARS  Index in STRING where any CHARS is found, or 0
            length STRING   Length of STRING
            quote TOKEN   Interpret TOKEN as a string, even if
                          it is a keyword like 'match' or an
                          operator like '/'
            (EXPRESSION)  Value of EXPRESSION

    Beware that many operators need to be escaped or quoted for shells.
    Comparisons are arithmetic if both ARGs are numbers, else
    lexicographical. Pattern matches return the string matched between
    \( and \) or null; if \( and \) are not used, they return the number
    of characters matched or 0.
*/

#define FOR_expr
#include "toys.h"

#ifndef lint
static const char yysccsid[] = "@(#)yaccpar	1.9 (Berkeley) 02/21/93";
#endif

#include <stdlib.h>
#include <string.h>

#define YYBYACC 1
#define YYMAJOR 1
#define YYMINOR 9
#define YYPATCH 20100216

#define YYEMPTY        (-1)
#define yyclearin      (yychar = YYEMPTY)
#define yyerrok        (yyerrflag = 0)
#define YYRECOVERING() (yyerrflag != 0)


#ifndef yyparse
#define yyparse    exprparse
#endif /* yyparse */

#ifndef yylex
#define yylex      exprlex
#endif /* yylex */

#ifndef yyerror
#define yyerror    exprerror
#endif /* yyerror */

#ifndef yychar
#define yychar     exprchar
#endif /* yychar */

#ifndef yyval
#define yyval      exprval
#endif /* yyval */

#ifndef yylval
#define yylval     exprlval
#endif /* yylval */

#ifndef yydebug
#define yydebug    exprdebug
#endif /* yydebug */

#ifndef yynerrs
#define yynerrs    exprnerrs
#endif /* yynerrs */

#ifndef yyerrflag
#define yyerrflag  exprerrflag
#endif /* yyerrflag */

#ifndef yylhs
#define yylhs      exprlhs
#endif /* yylhs */

#ifndef yylen
#define yylen      exprlen
#endif /* yylen */

#ifndef yydefred
#define yydefred   exprdefred
#endif /* yydefred */

#ifndef yydgoto
#define yydgoto    exprdgoto
#endif /* yydgoto */

#ifndef yysindex
#define yysindex   exprsindex
#endif /* yysindex */

#ifndef yyrindex
#define yyrindex   exprrindex
#endif /* yyrindex */

#ifndef yygindex
#define yygindex   exprgindex
#endif /* yygindex */

#ifndef yytable
#define yytable    exprtable
#endif /* yytable */

#ifndef yycheck
#define yycheck    exprcheck
#endif /* yycheck */

#ifndef yyname
#define yyname     exprname
#endif /* yyname */

#ifndef yyrule
#define yyrule     exprrule
#endif /* yyrule */
#define YYPREFIX "expr"

/* compatibility with bison */
#ifdef YYPARSE_PARAM
/* compatibility with FreeBSD */
#ifdef YYPARSE_PARAM_TYPE
#define YYPARSE_DECL() yyparse(YYPARSE_PARAM_TYPE YYPARSE_PARAM)
#else
#define YYPARSE_DECL() yyparse(void *YYPARSE_PARAM)
#endif
#else
#define YYPARSE_DECL() yyparse(void)
#endif /* YYPARSE_PARAM */

extern int YYPARSE_DECL();

#line 2 "expr.y"
#include <sys/types.h>

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <regex.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char **av;

static void yyerror(const char *, ...);
static int yylex(void);
static int is_zero_or_null(const char *);
static int is_integer(const char *);
static int64_t perform_arith_op(const char *, const char *, const char *);

#define YYSTYPE	const char *

#line 139 "expr.c"
#define STRING 257
#define SPEC_OR 258
#define SPEC_AND 259
#define COMPARE 260
#define ADD_SUB_OPERATOR 261
#define MUL_DIV_MOD_OPERATOR 262
#define SPEC_REG 263
#define LENGTH 264
#define SUBSTR 265
#define MATCH 266
#define INDEX 267
#define LEFT_PARENT 268
#define RIGHT_PARENT 269
#define YYERRCODE 256
static const short exprlhs[] = {                         -1,
    0,    1,    1,    1,    1,    1,    1,    1,    1,    1,
    1,    1,    1,    1,    1,    1,    1,    1,
};
static const short exprlen[] = {                          2,
    1,    1,    1,    1,    1,    1,    1,    3,    3,    3,
    3,    3,    3,    3,    2,    4,    3,    3,
};
static const short exprdefred[] = {                       0,
    2,    3,    4,    5,    6,    7,    0,    0,    0,    0,
    0,    0,    0,   15,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
   17,   18,   14,    0,    0,    0,    0,    0,   10,   16,
};
static const short exprdgoto[] = {                       12,
   34,
};
static const short exprsindex[] = {                    -198,
    0,    0,    0,    0,    0,    0, -198, -198, -198, -198,
 -198,    0, -255,    0, -216, -216, -216, -147, -198, -198,
 -198, -198, -198, -198, -184, -170, -158, -139, -133, -216,
    0,    0,    0, -226, -206, -161, -247, -263,    0,    0,
};
static const short exprrindex[] = {                       0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,   20,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,   88,   86,   79,   72,   65,    0,
    0,    0,    0,   53,   40,   27,   14,    1,    0,    0,
};
static const short exprgindex[] = {                       0,
    2,
};
#define YYTABLESIZE 357
static const short exprtable[] = {                       24,
   12,   13,   19,   20,   21,   22,   23,   24,   14,   15,
   16,   17,   18,   11,   23,   24,   30,   31,   32,    1,
    0,   35,   36,   37,   38,   39,   13,   35,   36,   37,
   38,   40,   20,   21,   22,   23,   24,    0,    0,    9,
    1,   25,   26,   27,   28,   29,   24,    7,    8,    9,
   10,   11,    8,   21,   22,   23,   24,    0,    1,    2,
    3,    4,    5,    6,    7,    7,    8,    9,   10,   11,
    0,    6,    1,    0,    3,    4,    5,    6,    5,    7,
    8,    9,   10,   11,    0,    4,    1,    3,    0,    4,
    5,    6,    0,    7,    8,    9,   10,   11,    1,   22,
   23,   24,    5,    6,    0,    7,    8,    9,   10,   11,
   19,   20,   21,   22,   23,   24,    0,    1,    0,    0,
    0,   33,    6,    1,    7,    8,    9,   10,   11,    0,
    7,    8,    9,   10,   11,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,   12,   12,   12,
   12,   12,   12,    0,   12,   12,   12,   12,   12,   12,
   11,   11,   11,   11,   11,    0,    0,   11,   11,   11,
   11,   11,   11,   13,   13,   13,   13,    0,    0,    0,
   13,   13,   13,   13,   13,   13,    9,    9,    9,    0,
    0,    0,    0,    9,    9,    9,    9,    9,    9,    8,
    8,    0,    0,    0,    0,    0,    8,    8,    8,    8,
    8,    8,    7,    7,    7,    7,    7,    7,    0,    6,
    6,    6,    6,    7,    6,    0,    5,    5,    5,    0,
    6,    5,    0,    4,    4,    3,    0,    5,    4,    0,
    3,    0,    0,    0,    4,    0,    3,
};
static const short exprcheck[] = {                      263,
    0,    0,  258,  259,  260,  261,  262,  263,    7,    8,
    9,   10,   11,    0,  262,  263,   15,   16,   17,    0,
   -1,   20,   21,   22,   23,   24,    0,   26,   27,   28,
   29,   30,  259,  260,  261,  262,  263,   -1,   -1,    0,
  257,  258,  259,  260,  261,  262,  263,  264,  265,  266,
  267,  268,    0,  260,  261,  262,  263,   -1,  257,  258,
  259,  260,  261,  262,    0,  264,  265,  266,  267,  268,
   -1,    0,  257,   -1,  259,  260,  261,  262,    0,  264,
  265,  266,  267,  268,   -1,    0,  257,    0,   -1,  260,
  261,  262,   -1,  264,  265,  266,  267,  268,  257,  261,
  262,  263,  261,  262,   -1,  264,  265,  266,  267,  268,
  258,  259,  260,  261,  262,  263,   -1,  257,   -1,   -1,
   -1,  269,  262,  257,  264,  265,  266,  267,  268,   -1,
  264,  265,  266,  267,  268,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,  257,  258,  259,
  260,  261,  262,   -1,  264,  265,  266,  267,  268,  269,
  257,  258,  259,  260,  261,   -1,   -1,  264,  265,  266,
  267,  268,  269,  257,  258,  259,  260,   -1,   -1,   -1,
  264,  265,  266,  267,  268,  269,  257,  258,  259,   -1,
   -1,   -1,   -1,  264,  265,  266,  267,  268,  269,  257,
  258,   -1,   -1,   -1,   -1,   -1,  264,  265,  266,  267,
  268,  269,  258,  259,  260,  261,  262,  263,   -1,  258,
  259,  260,  261,  269,  263,   -1,  258,  259,  260,   -1,
  269,  263,   -1,  258,  259,  258,   -1,  269,  263,   -1,
  263,   -1,   -1,   -1,  269,   -1,  269,
};
#define YYFINAL 12
#ifndef YYDEBUG
#define YYDEBUG 1
#endif
#define YYMAXTOKEN 269
#if YYDEBUG
static const char *yyname[] = {

"end-of-file",0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,"STRING","SPEC_OR","SPEC_AND",
"COMPARE","ADD_SUB_OPERATOR","MUL_DIV_MOD_OPERATOR","SPEC_REG","LENGTH",
"SUBSTR","MATCH","INDEX","LEFT_PARENT","RIGHT_PARENT",
};
static const char *yyrule[] = {
"$accept : exp",
"exp : expr",
"expr : STRING",
"expr : SPEC_OR",
"expr : SPEC_AND",
"expr : COMPARE",
"expr : ADD_SUB_OPERATOR",
"expr : MUL_DIV_MOD_OPERATOR",
"expr : expr SPEC_OR expr",
"expr : expr SPEC_AND expr",
"expr : expr SPEC_REG expr",
"expr : expr ADD_SUB_OPERATOR expr",
"expr : expr MUL_DIV_MOD_OPERATOR expr",
"expr : expr COMPARE expr",
"expr : LEFT_PARENT expr RIGHT_PARENT",
"expr : LENGTH expr",
"expr : SUBSTR expr expr expr",
"expr : MATCH expr expr",
"expr : INDEX expr expr",
 
};
#endif
#ifndef YYSTYPE
typedef int YYSTYPE;
#endif
#if YYDEBUG
#include <stdio.h>
#endif

/* define the initial stack-sizes */
#ifdef YYSTACKSIZE
#undef YYMAXDEPTH
#define YYMAXDEPTH  YYSTACKSIZE
#else
#ifdef YYMAXDEPTH
#define YYSTACKSIZE YYMAXDEPTH
#else
#define YYSTACKSIZE 500
#define YYMAXDEPTH  500
#endif
#endif

#define YYINITSTACKSIZE 500

int      yydebug;
int      yynerrs;

typedef struct {
    unsigned stacksize;
    short    *s_base;
    short    *s_mark;
    short    *s_last;
    YYSTYPE  *l_base;
    YYSTYPE  *l_mark;
} YYSTACKDATA;

#define YYPURE 0

int      yyerrflag;
int      yychar;
YYSTYPE  yyval;
YYSTYPE  yylval;

/* variables for the parser stack */
static YYSTACKDATA yystack;
#line 278 "expr.y"

/*
 * Returns 1 if the string is empty or contains only numeric zero.
 */
static int is_zero_or_null(const char *str)
{
	char *endptr;
	return str[0] == '\0'
		|| ( strtoll(str, &endptr, 10) == 0LL
			&& endptr[0] == '\0');
}

/*
 * Returns 1 if the string is an integer.
 */
static int is_integer(const char *str)
{
	char *endptr;
	(void) strtoll(str, &endptr, 10);
	/* note we treat empty string as valid number */
	return (endptr[0] == '\0');
}

static int64_t perform_arith_op(const char *left, const char *op, const char *right)
{
	int64_t res, sign, l, r;
	u_int64_t temp;

	res = 0;

	if (!is_integer(left)) {
		yyerror("non-integer argument '%s'", left);
		/* NOTREACHED */
	}
	if (!is_integer(right)) {
		yyerror("non-integer argument '%s'", right);
		/* NOTREACHED */
	}

	errno = 0;
	l = strtoll(left, NULL, 10);
	if (errno == ERANGE) {
		yyerror("value '%s' is %s is %lld", left,
		    (l > 0) ? "too big, maximum" : "too small, minimum",
		    (l > 0) ? LLONG_MAX : LLONG_MIN);
		/* NOTREACHED */
	}

	errno = 0;
	r = strtoll(right, NULL, 10);
	if (errno == ERANGE) {
		yyerror("value '%s' is %s is %lld", right,
		    (l > 0) ? "too big, maximum" : "too small, minimum",
	  	    (l > 0) ? LLONG_MAX : LLONG_MIN);
		/* NOTREACHED */
	}

	switch(op[0]) {
	case '+':
		/* 
		 * Do the op into an unsigned to avoid overflow and then cast
		 * back to check the resulting signage. 
		 */
		temp = l + r;
		res = (int64_t) temp;
		/* very simplistic check for over-& underflow */
		if ((res < 0 && l > 0 && r > 0)
	  	    || (res > 0 && l < 0 && r < 0)) 
			yyerror("integer overflow or underflow occurred for "
                            "operation '%s %s %s'", left, op, right);
		break;
	case '-':
		/* 
		 * Do the op into an unsigned to avoid overflow and then cast
		 * back to check the resulting signage. 
		 */
		temp = l - r;
		res = (int64_t) temp;
		/* very simplistic check for over-& underflow */
		if ((res < 0 && l > 0 && l > r)
		    || (res > 0 && l < 0 && l < r) ) 
			yyerror("integer overflow or underflow occurred for "
			    "operation '%s %s %s'", left, op, right);
		break;
	case '/':
		if (r == 0) 
			yyerror("second argument to '%s' must not be zero", op);
		res = l / r;
			
		break;
	case '%':
		if (r == 0)
			yyerror("second argument to '%s' must not be zero", op);
		res = l % r;
		break;
	case '*':
		/* shortcut */
		if ((l == 0) || (r == 0)) {
			res = 0;
			break;
		}
				
		sign = 1;
		if (l < 0)
			sign *= -1;
		if (r < 0)
			sign *= -1;

		res = l * r;
		/*
		 * XXX: not the most portable but works on anything with 2's
		 * complement arithmetic. If the signs don't match or the
		 * result was 0 on 2's complement this overflowed.
		 */
		if ((res < 0 && sign > 0) || (res > 0 && sign < 0) || 
		    (res == 0))
			yyerror("integer overflow or underflow occurred for "
			    "operation '%s %s %s'", left, op, right);
			/* NOTREACHED */
		break;
	}
	return res;
}

static int handle_ddash = 1;
static const char *x = "|&=<>+-*/%:()";
static const int x_token[] = {
	SPEC_OR, SPEC_AND, COMPARE, COMPARE, COMPARE, ADD_SUB_OPERATOR,
	ADD_SUB_OPERATOR, MUL_DIV_MOD_OPERATOR, MUL_DIV_MOD_OPERATOR, 
	MUL_DIV_MOD_OPERATOR, SPEC_REG, LEFT_PARENT, RIGHT_PARENT
};

/*
 * Lexer for expr.
 */
int yylex(void)
{
  const char *p = *av++;
  int retval;

  if (!p) retval = 0;
  else if (!p[0]) retval = STRING;
  else if (p[1] == '\0') {
    const char *w = strchr(x, p[0]);
    if (w)retval = x_token[w-x];
    else retval = STRING;
  } else if (p[1] == '=' && p[2] == '\0' && (p[0] == '>' || p[0] == '<' || p[0] == '!' || p[0] == '='))
    retval = COMPARE;
  else if (handle_ddash && p[0] == '-' && p[1] == '-' && p[2] == '\0') {
    /* ignore "--" if passed as first argument and isn't followed
     * by another STRING */
    retval = yylex();
    if (retval != STRING && retval != LEFT_PARENT && retval != RIGHT_PARENT) {
      retval = STRING; /* is not followed by string or parenthesis, use as STRING */
      av--; /* was increased in call to yylex() above */
      p = "--";
    } else p = yylval; /* "--" is to be ignored */
  } else if (strcmp(p, "quote") == 0) {
      retval = yylex();
      if(!retval) yyerror("Syntax error");
      retval = STRING;
      p = yylval;
  } else if (strcmp(p, "length") == 0) retval = LENGTH;
  else if (strcmp(p, "match") == 0) retval = MATCH;
  else if (strcmp(p, "index") == 0) retval = INDEX;
  else if (strcmp(p, "substr") == 0) retval = SUBSTR;
  else retval = STRING;

  handle_ddash = 0;
  yylval = p;

  return retval;
}

/*
 * Print error message and exit with error 2 (syntax error).
 */
static void yyerror(const char *fmt, ...)
{
	va_list arg;
	va_start(arg, fmt);
	verrx(2, fmt, arg);
	va_end(arg);
}

void expr_main(void)
{
  if(toys.optc < 1) yyerror("too few arguments");
  (void)setlocale(LC_ALL, "");
	av = toys.optargs;
	toys.exitval = yyparse();
	/* NOTREACHED */
}
#line 529 "expr.c"
/* allocate initial stack or double stack size, up to YYMAXDEPTH */
static int yygrowstack(YYSTACKDATA *data)
{
    int i;
    unsigned newsize;
    short *newss;
    YYSTYPE *newvs;

    if ((newsize = data->stacksize) == 0)
        newsize = YYINITSTACKSIZE;
    else if (newsize >= YYMAXDEPTH)
        return -1;
    else if ((newsize *= 2) > YYMAXDEPTH)
        newsize = YYMAXDEPTH;

    i = data->s_mark - data->s_base;
    newss = (data->s_base != 0)
          ? (short *)realloc(data->s_base, newsize * sizeof(*newss))
          : (short *)malloc(newsize * sizeof(*newss));
    if (newss == 0)
        return -1;

    data->s_base  = newss;
    data->s_mark = newss + i;

    newvs = (data->l_base != 0)
          ? (YYSTYPE *)realloc(data->l_base, newsize * sizeof(*newvs))
          : (YYSTYPE *)malloc(newsize * sizeof(*newvs));
    if (newvs == 0)
        return -1;

    data->l_base = newvs;
    data->l_mark = newvs + i;

    data->stacksize = newsize;
    data->s_last = data->s_base + newsize - 1;
    return 0;
}

#if YYPURE || defined(YY_NO_LEAKS)
static void yyfreestack(YYSTACKDATA *data)
{
    free(data->s_base);
    free(data->l_base);
    memset(data, 0, sizeof(*data));
}
#else
#define yyfreestack(data) /* nothing */
#endif

#define YYABORT  goto yyabort
#define YYREJECT goto yyabort
#define YYACCEPT goto yyaccept
#define YYERROR  goto yyerrlab

int
YYPARSE_DECL()
{
    int yym, yyn, yystate;
#if YYDEBUG
    const char *yys;

    if ((yys = getenv("YYDEBUG")) != 0)
    {
        yyn = *yys;
        if (yyn >= '0' && yyn <= '9')
            yydebug = yyn - '0';
    }
#endif

    yynerrs = 0;
    yyerrflag = 0;
    yychar = YYEMPTY;
    yystate = 0;

#if YYPURE
    memset(&yystack, 0, sizeof(yystack));
#endif

    if (yystack.s_base == NULL && yygrowstack(&yystack)) goto yyoverflow;
    yystack.s_mark = yystack.s_base;
    yystack.l_mark = yystack.l_base;
    yystate = 0;
    *yystack.s_mark = 0;

yyloop:
    if ((yyn = yydefred[yystate]) != 0) goto yyreduce;
    if (yychar < 0)
    {
        if ((yychar = yylex()) < 0) yychar = 0;
#if YYDEBUG
        if (yydebug)
        {
            yys = 0;
            if (yychar <= YYMAXTOKEN) yys = yyname[yychar];
            if (!yys) yys = "illegal-symbol";
            printf("%sdebug: state %d, reading %d (%s)\n",
                    YYPREFIX, yystate, yychar, yys);
        }
#endif
    }
    if ((yyn = yysindex[yystate]) && (yyn += yychar) >= 0 &&
            yyn <= YYTABLESIZE && yycheck[yyn] == yychar)
    {
#if YYDEBUG
        if (yydebug)
            printf("%sdebug: state %d, shifting to state %d\n",
                    YYPREFIX, yystate, yytable[yyn]);
#endif
        if (yystack.s_mark >= yystack.s_last && yygrowstack(&yystack))
        {
            goto yyoverflow;
        }
        yystate = yytable[yyn];
        *++yystack.s_mark = yytable[yyn];
        *++yystack.l_mark = yylval;
        yychar = YYEMPTY;
        if (yyerrflag > 0)  --yyerrflag;
        goto yyloop;
    }
    if ((yyn = yyrindex[yystate]) && (yyn += yychar) >= 0 &&
            yyn <= YYTABLESIZE && yycheck[yyn] == yychar)
    {
        yyn = yytable[yyn];
        goto yyreduce;
    }
    if (yyerrflag) goto yyinrecovery;

    yyerror("syntax error");

    goto yyerrlab;

yyerrlab:
    ++yynerrs;

yyinrecovery:
    if (yyerrflag < 3)
    {
        yyerrflag = 3;
        for (;;)
        {
            if ((yyn = yysindex[*yystack.s_mark]) && (yyn += YYERRCODE) >= 0 &&
                    yyn <= YYTABLESIZE && yycheck[yyn] == YYERRCODE)
            {
#if YYDEBUG
                if (yydebug)
                    printf("%sdebug: state %d, error recovery shifting\
 to state %d\n", YYPREFIX, *yystack.s_mark, yytable[yyn]);
#endif
                if (yystack.s_mark >= yystack.s_last && yygrowstack(&yystack))
                {
                    goto yyoverflow;
                }
                yystate = yytable[yyn];
                *++yystack.s_mark = yytable[yyn];
                *++yystack.l_mark = yylval;
                goto yyloop;
            }
            else
            {
#if YYDEBUG
                if (yydebug)
                    printf("%sdebug: error recovery discarding state %d\n",
                            YYPREFIX, *yystack.s_mark);
#endif
                if (yystack.s_mark <= yystack.s_base) goto yyabort;
                --yystack.s_mark;
                --yystack.l_mark;
            }
        }
    }
    else
    {
        if (yychar == 0) goto yyabort;
#if YYDEBUG
        if (yydebug)
        {
            yys = 0;
            if (yychar <= YYMAXTOKEN) yys = yyname[yychar];
            if (!yys) yys = "illegal-symbol";
            printf("%sdebug: state %d, error recovery discards token %d (%s)\n",
                    YYPREFIX, yystate, yychar, yys);
        }
#endif
        yychar = YYEMPTY;
        goto yyloop;
    }

yyreduce:
#if YYDEBUG
    if (yydebug)
        printf("%sdebug: state %d, reducing by rule %d (%s)\n",
                YYPREFIX, yystate, yyn, yyrule[yyn]);
#endif
    yym = yylen[yyn];
    if (yym)
        yyval = yystack.l_mark[1-yym];
    else
        memset(&yyval, 0, sizeof yyval);
    switch (yyn)
    {
case 1:
#line 40 "expr.y"
	{
		 printf("%s\n", yystack.l_mark[0]);
		return (is_zero_or_null(yystack.l_mark[0]));
		}
break;
case 2:
#line 46 "expr.y"
	{ yyval = yystack.l_mark[0]; }
break;
case 3:
#line 46 "expr.y"
	{ yyval = yystack.l_mark[0]; }
break;
case 4:
#line 46 "expr.y"
	{ yyval = yystack.l_mark[0]; }
break;
case 5:
#line 46 "expr.y"
	{ yyval = yystack.l_mark[0]; }
break;
case 6:
#line 46 "expr.y"
	{ yyval = yystack.l_mark[0]; }
break;
case 7:
#line 46 "expr.y"
	{ yyval = yystack.l_mark[0]; }
break;

case 8:
#line 47 "expr.y"
	{
		/*
		 * Return evaluation of first expression if it is neither
		 * an empty string nor zero; otherwise, returns the evaluation
		 * of second expression.
		 */
		if (!is_zero_or_null(yystack.l_mark[-2]))
			yyval = yystack.l_mark[-2];
		else
			yyval = yystack.l_mark[0];
		}
break;
case 9:
#line 58 "expr.y"
	{
		/*
		 * Returns the evaluation of first expr if neither expression
		 * evaluates to an empty string or zero; otherwise, returns
		 * zero.
		 */
		if (!is_zero_or_null(yystack.l_mark[-2]) && !is_zero_or_null(yystack.l_mark[0]))
			yyval = yystack.l_mark[-2];
		else
			yyval = "0";
		}
break;
case 10:
#line 69 "expr.y"
	{
		/*
		 * The ``:'' operator matches first expr against the second,
		 * which must be a regular expression.
		 */
		regex_t rp;
		regmatch_t rm[2];
		int eval;

		/* compile regular expression */
		if ((eval = regcomp(&rp, yystack.l_mark[0], 0)) != 0) {
			char errbuf[256];
			(void)regerror(eval, &rp, errbuf, sizeof(errbuf));
			yyerror("%s", errbuf);
			/* NOT REACHED */
		}
		
		/* compare string against pattern --  remember that patterns 
		   are anchored to the beginning of the line */
		if (regexec(&rp, yystack.l_mark[-2], 2, rm, 0) == 0 && rm[0].rm_so == 0) {
			char *val;
			if (rm[1].rm_so >= 0) {
				 val = xmsprintf("%.*s",
					(int) (rm[1].rm_eo - rm[1].rm_so),
					yystack.l_mark[-2] + rm[1].rm_so);
			} else {
				val = xmsprintf("%d",
					(int)(rm[0].rm_eo - rm[0].rm_so));
			}
			if (val == NULL)
				err(1, NULL);
			yyval = val;
		} else {
			if (rp.re_nsub == 0) {
				yyval = "0";
			} else {
				yyval = "";
			}
		}

		}
break;
case 11:
#line 110 "expr.y"
	{
		/* Returns the results of addition, subtraction */
		char *val;
		int64_t res;
		
		res = perform_arith_op(yystack.l_mark[-2], yystack.l_mark[-1], yystack.l_mark[0]);
		val = xmsprintf("%lld", (long long int) res);
		if (val == NULL)
			err(1, NULL);
		yyval = val;

        }
break;
case 12:
#line 122 "expr.y"
	{
		/* 
		 * Returns the results of multiply, divide or remainder of 
		 * numeric-valued arguments.
		 */
		char *val;
		int64_t res;

		res = perform_arith_op(yystack.l_mark[-2], yystack.l_mark[-1], yystack.l_mark[0]);
		val = xmsprintf("%lld", (long long int) res);
		if (val == NULL)
			err(1, NULL);
		yyval = val;

		}
break;
case 13:
#line 137 "expr.y"
	{
		/*
		 * Returns the results of integer comparison if both arguments
		 * are integers; otherwise, returns the results of string
		 * comparison using the locale-specific collation sequence.
		 * The result of each comparison is 1 if the specified relation
		 * is true, or 0 if the relation is false.
		 */

		int64_t l, r;
		int res;

		res = 0;

		/*
		 * Slight hack to avoid differences in the compare code
		 * between string and numeric compare.
		 */
		if (is_integer(yystack.l_mark[-2]) && is_integer(yystack.l_mark[0])) {
			/* numeric comparison */
			l = strtoll(yystack.l_mark[-2], NULL, 10);
			r = strtoll(yystack.l_mark[0], NULL, 10);
		} else {
			/* string comparison */
			l = strcoll(yystack.l_mark[-2], yystack.l_mark[0]);
			r = 0;
		}

		switch(yystack.l_mark[-1][0]) {	
		case '=': /* equal */
			res = (l == r);
			break;
		case '>': /* greater or greater-equal */
			if (yystack.l_mark[-1][1] == '=')
				res = (l >= r);
			else
				res = (l > r);
			break;
		case '<': /* lower or lower-equal */
			if (yystack.l_mark[-1][1] == '=')
				res = (l <= r);
			else
				res = (l < r);
			break;
		case '!': /* not equal */
			/* the check if this is != was done in yylex() */
			res = (l != r);
		}

		yyval = (res) ? "1" : "0";

		}
break;
case 14:
#line 189 "expr.y"
	{ yyval = yystack.l_mark[-1]; }
break;
case 15:
#line 190 "expr.y"
	{
		/*
		 * Return length of 'expr' in bytes.
		 */
		char *ln;

		ln = xmsprintf("%ld", (long) strlen(yystack.l_mark[0]));
		if (ln == NULL)
			err(1, NULL);
		yyval = ln;
		}
break;
case 16:
#line 201 "expr.y"
  {
    char *ln = NULL;
    if(!is_integer(yystack.l_mark[0]) || (!is_integer(yystack.l_mark[-1])))
      errx(1, "Index or length can only be integer.");
    long long elen = strtoll(yystack.l_mark[0], NULL, 10);
    long long sind = strtoll(yystack.l_mark[-1], NULL, 10);
    if(sind <= 0 || elen <= 0)
      errx(1, "Index or length can't be 0 or neg");
    ln = xmsprintf("%s", yystack.l_mark[-2]);
    if(sind <= strlen(yystack.l_mark[-2]))
      ln += sind-1;
    else ln[0] = '\0';
    if (ln == NULL)
      errx(1, NULL);
    if(elen < strlen(ln))
      ln[elen] = '\0';
    yyval = ln;
  }
break;
case 17:
#line 211 "expr.y"
	{
		/*
		 * The ``:'' operator matches first expr against the second,
		 * which must be a regular expression.
		 */
		regex_t rp;
		regmatch_t rm[2];
		int eval;

		/* compile regular expression */
		if ((eval = regcomp(&rp, yystack.l_mark[0], 0)) != 0) {
			char errbuf[256];
			(void)regerror(eval, &rp, errbuf, sizeof(errbuf));
			yyerror("%s", errbuf);
			/* NOT REACHED */
		}
		
		/* compare string against pattern --  remember that patterns 
		   are anchored to the beginning of the line */
		if (regexec(&rp, yystack.l_mark[-1], 2, rm, 0) == 0 && rm[0].rm_so == 0) {
			char *val;
			if (rm[1].rm_so >= 0) {
				val = xmsprintf("%.*s", (int) (rm[1].rm_eo - rm[1].rm_so), yystack.l_mark[-1] + rm[1].rm_so);
			} else {
				val = xmsprintf("%d", (int)(rm[0].rm_eo - rm[0].rm_so));
			}
			if (val == NULL) errx(1, NULL);
			yyval = val;
		} else {
			if (rp.re_nsub == 0) {
				yyval = "0";
			} else {
				yyval = "";
			}
		}
		}
break;
case 18:
#line 252 "expr.y"
{
  char *ptr = (char*)yystack.l_mark[0];
  char *res, *str = NULL;
  long count = -1;
  str = (char*)yystack.l_mark[-1];
  while(*ptr != '\0'){
    res = strchr(str, (int)(*ptr));
    if(res && (count > res - str || count == -1) ) count = res - str;
    ptr++;
  }
  res = NULL;
  res = xmsprintf("%ld", count+1);
  if (res == NULL)
    errx(1, NULL);
  yyval = res;
}
break;
#line 1004 "expr.c"
    }
    yystack.s_mark -= yym;
    yystate = *yystack.s_mark;
    yystack.l_mark -= yym;
    yym = yylhs[yyn];
    if (yystate == 0 && yym == 0)
    {
#if YYDEBUG
        if (yydebug)
            printf("%sdebug: after reduction, shifting from state 0 to\
 state %d\n", YYPREFIX, YYFINAL);
#endif
        yystate = YYFINAL;
        *++yystack.s_mark = YYFINAL;
        *++yystack.l_mark = yyval;
        if (yychar < 0)
        {
            if ((yychar = yylex()) < 0) yychar = 0;
#if YYDEBUG
            if (yydebug)
            {
                yys = 0;
                if (yychar <= YYMAXTOKEN) yys = yyname[yychar];
                if (!yys) yys = "illegal-symbol";
                printf("%sdebug: state %d, reading %d (%s)\n",
                        YYPREFIX, YYFINAL, yychar, yys);
            }
#endif
        }
        if (yychar == 0) goto yyaccept;
        goto yyloop;
    }
    if ((yyn = yygindex[yym]) && (yyn += yystate) >= 0 &&
            yyn <= YYTABLESIZE && yycheck[yyn] == yystate)
        yystate = yytable[yyn];
    else
        yystate = yydgoto[yym];
#if YYDEBUG
    if (yydebug)
        printf("%sdebug: after reduction, shifting from state %d \
to state %d\n", YYPREFIX, *yystack.s_mark, yystate);
#endif
    if (yystack.s_mark >= yystack.s_last && yygrowstack(&yystack))
    {
        goto yyoverflow;
    }
    *++yystack.s_mark = (short) yystate;
    *++yystack.l_mark = yyval;
    goto yyloop;

yyoverflow:
    yyerror("yacc stack overflow");

yyabort:
    yyfreestack(&yystack);
    return (1);

yyaccept:
    yyfreestack(&yystack);
    return (0);
}

/* $NetBSD: expr.y,v 1.38 2012/03/15 02:02:20 joerg Exp $ */

/*_
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jaromir Dolecek <jdolecek@NetBSD.org> and J.T. Conklin <jtc@NetBSD.org>.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
