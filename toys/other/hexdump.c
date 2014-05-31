/* hexdump.c - A program to dump any file into hexadecimal, octal, decimal or character format.
 *
 * Copyright 2013 Ashwini Kumar <ak.ashwini@gmail.com>
 *

USE_HEXDUMP(NEWTOY(hexdump, "RbCcde:f:n#<0os:vx", TOYFLAG_USR|TOYFLAG_BIN))

config HEXDUMP
	bool "hexdump"
	default y 
	help

	  usage: hexdump [-bCcdovxR] [-e format_string] [-f format_file] [-n length] [-s skip] [file ...]

	  A program to dump any file into hexadecimal, octal, decimal or character format.

	  options:
		-b                One-byte octal display
		-C                Canonical hex+ASCII, 16 bytes per line
		-c                One-byte character display
		-d                Two-byte decimal display
		-e format_string  Specify a format string to be used for displaying data
		-f format_file    Specify a file that contains one or more newline separated format strings
		-n length         Interpret only length bytes of input
		-o                Two-byte octal display
		-s OFFSET         Skip OFFSET bytes from the beginning of the input
		-v                Display all input data
		-x                Two-byte hexadecimal display
		-R                Reverse of 'hexdump -Cv'

*/

/*
 * Copyright (c) 1989, 1993
 *  The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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

#define FOR_hexdump
#include "toys.h"

#define F_IGNORE        0x01            /* %_A */
#define F_SETREP        0x02            /* rep count set, not default */

#define F_ADDRESS       0x001           /* print offset */
#define F_BPAD          0x002           /* blank pad */
#define F_C             0x004           /* %_c */
#define F_CHAR          0x008           /* %c */
#define F_DBL           0x010           /* %[EefGf] */
#define F_INT           0x020           /* %[di] */
#define F_P             0x040           /* %_p */
#define F_STR           0x080           /* %s */
#define F_U             0x100           /* %_u */
#define F_UINT          0x200           /* %[ouXx] */
#define F_TEXT          0x400           /* no conversions */

#ifndef MIN     
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif

typedef struct _PR {
  struct _PR *nextpr;             /* next print unit */
  unsigned flags;                 /* flag values */
  int bcnt;                       /* byte count */
  char *cchar;                    /* conversion character */
  char *fmt;                      /* printf format */
  char *nospace;                  /* no whitespace version */
} PR;

typedef struct _FU {
  struct _FU *nextfu;             /* next format unit */
  struct _PR *nextpr;             /* next print unit */
  unsigned flags;                 /* flag values */
  int reps;                       /* repetition count */
  int bcnt;                       /* byte count */
  char *fmt;                      /* format string */
} FU;

typedef struct _FS {                    /* format strings */
  struct _FS *nextfs;             /* linked list of format strings */
  struct _FU *nextfu;             /* linked list of format units */
  int bcnt;
} FS;

enum dump_vflag { ALL, DUP, FIRST, WAIT };  /* -v values */

FU *endfu;                		/* format at end-of-data */
enum dump_vflag v_flag = FIRST;
static off_t address;                   /* address/offset in stream */
static off_t eaddress;                  /* end address */
static char **_argv;

FS *fshead;                             /* head of format strings */  
int blocksize;                          /* data block size */         
int length = -1;                        /* max bytes to read */
off_t skip;                             /* bytes to skip */

static const char *index_str = ".#-+ 0123456789";

static const char conv_str[]=
	"\0\\0\0"
	"\007\\a\0"	/* \a */
	"\b\\b\0"
	"\f\\f\0"
	"\n\\n\0"
	"\r\\r\0"
	"\t\\t\0"
	"\v\\v\0"
	;

static void add(const char *fmt)
{
  const char *p;
  static FS **nextfs;
  FS *tfs;
  FU *tfu, **nextfu;
  const char *savep;
  char *p1, *p2;

  /* start new linked list of format units */
  tfs = xzalloc(sizeof(FS));
  if (!fshead) fshead = tfs;
  else *nextfs = tfs;
  nextfs = &tfs->nextfs;
  nextfu = &tfs->nextfu;

  /* take the format string and break it up into format units */
  p = fmt;
  for (;;) {
    /* skip leading white space */
    for (; isspace(*p); ++p);
    if (!*p) break;

    /* allocate a new format unit and link it in */
    tfu = xzalloc(sizeof(FU));
    *nextfu = tfu;
    nextfu = &tfu->nextfu;
    tfu->reps = 1;

    /* if leading digit, repetition count */
    if (isdigit(*p)) {
      for (savep = p; isdigit(*p); ++p);
      if (!isspace(*p) && *p != '/')
        error_exit("\"%s\": bad format", fmt);
      /* may overwrite either white space or slash */
      tfu->reps = atoi(savep);
      tfu->flags = F_SETREP;
      /* skip trailing white space */
      for (++p; isspace(*p); ++p);
    }

    /* skip slash and trailing white space */
    if (*p == '/')
      while (isspace(*++p));

    /* byte count */
    if (isdigit(*p)) {
      for (savep = p; isdigit(*p); ++p);
      if (!isspace(*p))	error_exit("\"%s\": bad format", fmt);
      tfu->bcnt = atoi(savep);
      /* skip trailing white space */
      for (++p; isspace(*p); ++p);
    }

    /* format */
    if (*p != '"') error_exit("\"%s\": bad format", fmt);
    for (savep = ++p; *p != '"';)
      if (*p++ == 0) error_exit("\"%s\": bad format", fmt);
    tfu->fmt = xstrndup((char *)savep, p - savep);

    /*escape(tfu->fmt)*/

    p1 = tfu->fmt;

    /* alphabetic escape sequences have to be done in place */
    for (p2 = p1;; ++p1, ++p2) {
      if (*p1 == '\0') {
        *p2 = *p1;
        break;
      }
      if (*p1 == '\\') {
        const char *str = conv_str + 4;
        ++p1;
        *p2 = *p1;
        do {
          if (*p1 == str[2]) {
            *p2 = str[0];
            break;
          }
          str += 4;
        } while (*str);
      }
    }
    p++;
  }
}

static void addfile(char *name)
{
	char *p;
	FILE *fp;
	int ch;
	char buf[2048 + 1];

	fp = xfopen(name, "r");
	while (fgets(buf, sizeof(buf), fp)) {
		if (!(p = strchr(buf, '\n'))) {
			perror_msg("line too long.");
			while ((ch = getchar()) != '\n' && ch != EOF);    /*take input for further process*/
			continue;
		}
		*p = '\0';
		for (p = buf; *p && isspace((unsigned char)*p); ++p);     /*skip whitespaces*/
		if (!*p || *p == '#')
			continue;
		add(p);
	}
	fclose(fp);
}

static int size(FS *fs)
{
	FU *fu;
	int bcnt, cur_size;
	char *fmt;
	int prec;

	/* figure out the data block size needed for each format unit */
	for (cur_size = 0, fu = fs->nextfu; fu; fu = fu->nextfu) {
		if (fu->bcnt) {
			cur_size += fu->bcnt * fu->reps;
			continue;
		}
    for (bcnt = prec = 0, fmt = fu->fmt; *fmt; ++fmt) {
      if (*fmt != '%') continue;
      /*
       * skip any special chars -- save precision in
       * case it's a %s format.
       */
      while (strchr(index_str + 1, *++fmt));
      if (*fmt == '.' && isdigit(*++fmt)) {
        prec = atoi(fmt);
        while (isdigit(*++fmt));
      }
      switch(*fmt) {
        case 'c':
          bcnt += 1;
          break;
        case 'd': case 'i': case 'o': case 'u':
        case 'x': case 'X':
          bcnt += 4;
          break;
        case 'e': case 'E': case 'f': case 'g': case 'G':
          bcnt += 8;
          break;
        case 's':
          bcnt += prec;
          break;
        case '_':
          switch(*++fmt) {
            case 'c': case 'p': case 'u':
              bcnt += 1;
              break;
          }
      }
    }
		cur_size += bcnt * fu->reps;
	}
	return cur_size;
}

static void rewrite(FS *fs)
{
  enum { NOTOKAY, USEBCNT, USEPREC } sokay;
  PR *pr, **nextpr;
  FU *fu;
  char *p1, *p2;
  char savech, *fmtp, cs[sizeof(PRId64)];
  int nconv, prec=0;

  for (fu = fs->nextfu; fu; fu = fu->nextfu) {
    /*
     * Break each format unit into print units; each conversion
     * character gets its own.
     */
    nextpr = &fu->nextpr;
    for (nconv = 0, fmtp = fu->fmt; *fmtp; nextpr = &pr->nextpr) {
      pr = xzalloc(sizeof(*pr));
      *nextpr = pr;

      /* Skip preceding text and up to the next % sign. */
      for (p1 = fmtp; *p1 && *p1 != '%'; ++p1);

      /* Only text in the string. */
      if (!*p1) {
        pr->fmt = fmtp;
        pr->flags = F_TEXT;
        break;
      }

      /*
       * Get precision for %s -- if have a byte count, don't
       * need it.
       */
      if (fu->bcnt) {
        sokay = USEBCNT;
        /* Skip to conversion character. */
        for (++p1; *p1 && strchr(index_str, *p1); ++p1);
      } else {
        /* Skip any special chars, field width. */
        while (*++p1 && strchr(index_str + 1, *p1));
        if (*p1 == '.' && isdigit(*++p1)) {
          sokay = USEPREC;
          prec = atoi(p1);
          while (isdigit(*++p1))
            continue;
        } 
        else sokay = NOTOKAY;
      }

      p2 = *p1 ? p1 + 1 : p1; /* Set end pointer. */
      cs[0] = *p1;            /* Set conversion string. */
      cs[1] = '\0';

      /*
       * Figure out the byte count for each conversion;
       * rewrite the format as necessary, set up blank-
       * padding for end of data.
       */
      switch(cs[0]) {
        case 'c':
          pr->flags = F_CHAR;
          switch(fu->bcnt) {
            case 0: case 1:
              pr->bcnt = 1;
              break;
            default:
              p1[1] = '\0';
              error_exit("\"%s\": bad byte count", p1);
          }
          break;
        case 'd': case 'i':
          pr->flags = F_INT;
          goto isint;
        case 'o': case 'u': case 'x': case 'X':
          pr->flags = F_UINT;
isint:
          /* Regardless of pr->bcnt, all integer
           * values are cast to [u]int64_t before
           * being printed by display().  We
           * therefore need to use PRI?64 as the
           * format, where '?' could actually
           * be any of [diouxX].  We make the
           * assumption (not guaranteed by the
           * C99 standard) that we can derive
           * all the other PRI?64 values from
           * PRId64 simply by changing the last
           * character.  For example, if PRId64 is
           * "lld" or "qd", and cs[0] is 'o', then
           * we end up with "llo" or "qo".
           */
          savech = cs[0];
          strncpy(cs, PRId64, sizeof(PRId64) - 2);
          cs[sizeof(PRId64) - 2] = savech;
          cs[sizeof(PRId64) - 1] = '\0';
          switch(fu->bcnt) {
            case 0: case 4:
              pr->bcnt = 4;
              break;
            case 1:
              pr->bcnt = 1;
              break;
            case 2:
              pr->bcnt = 2;
              break;
            case 8:
              pr->bcnt = 8;
              break;
            default:
              p1[1] = '\0';
              error_exit("\"%s\": bad byte count", p1);
          }
          break;
        case 'e': case 'E': case 'f': case 'g': case 'G':
          pr->flags = F_DBL;
          switch(fu->bcnt) {
            case 0: case 8:
              pr->bcnt = 8;
              break;
            case 4:
              pr->bcnt = 4;
              break;
            default:
              p1[1] = '\0';
              error_exit("\"%s\": bad byte count", p1);
          }
          break;
        case 's':
          pr->flags = F_STR;
          switch(sokay) {
            case NOTOKAY:
              error_exit("%%s: requires a precision or a byte count");
            case USEBCNT:
              pr->bcnt = fu->bcnt;
              break;
            case USEPREC:
              pr->bcnt = prec;
              break;
          }
          break;
        case '_':
          ++p2;
          switch(p1[1]) {
            case 'A':
              endfu = fu;
              fu->flags |= F_IGNORE;
              /* FALLTHROUGH */
            case 'a':
              pr->flags = F_ADDRESS;
              ++p2;
              switch(p1[2]) {
                case 'd': case 'o': case'x':
                  /*
                   * See comments above for
                   * the way we use PRId64.
                   */
                  strncpy(cs, PRId64,
                      sizeof(PRId64) - 2);
                  cs[sizeof(PRId64) - 2] = p1[2];
                  cs[sizeof(PRId64) - 1] = '\0';
                  break;
                default:
                  p1[3] = '\0';
                  error_exit("%%%s: bad conversion character", p1);
              }
              break;
            case 'c':
              pr->flags = F_C;
              /* cs[0] = 'c'; set in conv_c */
              goto isint2;
            case 'p':
              pr->flags = F_P;
              cs[0] = 'c';
              goto isint2;
            case 'u':
              pr->flags = F_U;
              /* cs[0] = 'c'; set in conv_u */
isint2:
              switch(fu->bcnt) {
                case 0: case 1:
                  pr->bcnt = 1;
                  break;
                default:
                  p1[2] = '\0';
                  error_exit("%s: bad byte count", p1);
              }
              break;
            default:
              p1[2] = '\0';
              error_exit("%%%s: bad conversion character", p1);
          }
          break;
        default:
          p1[1] = '\0';
          error_exit("%%%s: bad conversion character", p1);
      }

      /*
       * Copy to PR format string, set conversion character
       * pointer, update original.
       */
      savech = *p2;
      p1[0] = '\0';
      pr->fmt = xmalloc(strlen(fmtp) + strlen(cs) + 1);
      strcpy(pr->fmt, fmtp);
      strcat(pr->fmt, cs);
      *p2 = savech;
      pr->cchar = pr->fmt + (p1 - fmtp);
      fmtp = p2;

      /* Only one conversion character if byte count. */
      if (!(pr->flags & F_ADDRESS) && fu->bcnt && nconv++)
        error_exit("byte count with multiple conversion characters");
    }
    /*
     * If format unit byte count not specified, figure it out
     * so can adjust rep count later.
     */
    if (!fu->bcnt)
      for (pr = fu->nextpr; pr; pr = pr->nextpr)
        fu->bcnt += pr->bcnt;
  }
  /*
   * If the format string interprets any data at all, and it's
   * not the same as the blocksize, and its last format unit
   * interprets any data at all, and has no iteration count,
   * repeat it as necessary.
   *
   * If, rep count is greater than 1, no trailing whitespace
   * gets output from the last iteration of the format unit.
   */
  for (fu = fs->nextfu; fu; fu = fu->nextfu) {
    if (!fu->nextfu && fs->bcnt < blocksize && !(fu->flags&F_SETREP) && fu->bcnt)
      fu->reps += (blocksize - fs->bcnt) / fu->bcnt;
    if (fu->reps > 1) {
      if (!fu->nextpr) break;
      for (pr = fu->nextpr;; pr = pr->nextpr)
        if (!pr->nextpr) break;
      for (p1 = pr->fmt, p2 = NULL; *p1; ++p1)
        p2 = isspace(*p1) ? p1 : NULL;
      if (p2) pr->nospace = p2;
    }
  }
}

static void bpad(PR *pr)
{       
  char *p1, *p2;

  /*
   * Remove all conversion flags; '-' is the only one valid
   * with %s, and it's not useful here.
   */
  pr->flags = F_BPAD;
  pr->cchar[0] = 's';
  pr->cchar[1] = '\0';
  for (p1 = pr->fmt; *p1 != '%'; ++p1);
  for (p2 = ++p1; *p1 && strchr(" -0+#", *p1); ++p1) {
    if (pr->nospace) pr->nospace--;
  }
  while ((*p2++ = *p1++) != '\0');
}

static void conv_c(PR *pr, unsigned char *p)
{
	const char *str = conv_str;
	char buf[10];

	do {
		if (*p == *str) {
			++str;
			goto strpr;
		}
		str += 4;
	}while(*str);

	if (isprint(*p)) {
		*pr->cchar = 'c';
		printf(pr->fmt, *p);
	} else {
		sprintf(buf, "%03o", (int) *p);
		str = buf;
strpr:
    *pr->cchar = 's';
		printf(pr->fmt, str);
	}
}

static void conv_u(PR *pr, u_char *p)
{
  static const char *list[] = {
    "nul", "soh", "stx", "etx", "eot", "enq", "ack", "bel",
    "bs",  "ht",  "lf",  "vt",  "ff",  "cr",  "so",  "si",
    "dle", "dcl", "dc2", "dc3", "dc4", "nak", "syn", "etb",
    "can",  "em", "sub", "esc",  "fs",  "gs",  "rs",  "us",
  };

  if (*p <= 0x1f) {
    *pr->cchar = 's';
    printf(pr->fmt, list[*p]);
  } else if (*p == 0x7f) {
    *pr->cchar = 's';
    printf(pr->fmt, "del");
  } else if (isprint(*p)) {
    *pr->cchar = 'c';
    printf(pr->fmt, *p);
  } else {
    *pr->cchar = 'x';
    printf(pr->fmt, (int)*p);
  }
}

static void doskip(const char *fname, int statok)
{
  int cnt;
  struct stat sbuf;

  memset(&sbuf, 0, sizeof( struct stat));
  if (statok) {
    if (fstat(fileno(stdin), &sbuf))
      perror_exit("fstat %s", fname);
    if (S_ISREG(sbuf.st_mode) && skip > sbuf.st_size) {
      skip -= sbuf.st_size;
      address += sbuf.st_size;
      return;
    }
  }
  if (S_ISREG(sbuf.st_mode)) {
    if (fseek(stdin, skip, SEEK_SET))
      perror_exit("fseek %s", fname);
    address += skip;
    skip = 0;
  } else {
    for (cnt = 0; cnt < skip; ++cnt)
      if (getchar() == EOF)
        break;
    address += cnt;
    skip -= cnt;
  }
}

static int next(char **argv)
{
  static int done;
  int statok;

  if (argv) {
    _argv = argv;
    return(1);
  }
  for (;;) {
    if (*_argv) {
      if (!(freopen(*_argv, "r", stdin))) {
        perror_msg("%s", *_argv);
        ++_argv;
        continue;
      }
      statok = done = 1;
    } else {
      if (done++)
        return(0); /* no next file */
      statok = 0;
    }
    if (skip)
      doskip(statok ? *_argv : "stdin", statok);
    if (*_argv)
      ++_argv;
    if (!skip)
      return(1);
  }
  /* NOTREACHED */
}

static unsigned char *get(void)
{
  static int ateof = 1;
  static unsigned char *curp, *savp;
  int n;
  int need, nread;
  unsigned char *tmp;

  if (!curp) {
    curp = xmalloc(blocksize);
    savp = xzalloc(blocksize);
  } else {
    tmp = curp;
    curp = savp;
    savp = tmp;
    address += blocksize;
  }
  need = blocksize;
  nread = 0;
  for (;;) {
    /*
     * if read the right number of bytes, or at EOF for one file,
     * and no other files are available, zero-pad the rest of the
     * block and set the end flag.
     */
    if (!length || (ateof && !next(NULL))) {
      if (need == blocksize) return NULL;
      if (!need && v_flag != ALL && !memcmp(curp, savp, nread)) {
        if (v_flag != DUP)
          printf("*\n");
        return NULL;
      }
      memset((char *)curp + nread, 0, need);
      eaddress = address + nread;
      return curp;
    }
    n = fread((char *)curp + nread, sizeof(unsigned char),length == -1 ? need : MIN(length, need), stdin);
    if (!n) {
      if (ferror(stdin)) perror_msg("%s", _argv[-1]);
      ateof = 1;
      continue;
    }
    ateof = 0;
    if (length != -1)	length -= n;
    need -= n;
    if (!need) {
      if (v_flag == ALL || v_flag == FIRST || memcmp(curp, savp, blocksize)) {
        if (v_flag == DUP || v_flag == FIRST)
          v_flag = WAIT;
        return curp;
      }
      if (v_flag == WAIT)	printf("*\n");
      v_flag = DUP;
      address += blocksize;
      need = blocksize;
      nread = 0;
    }
    else nread += n;
  }
}

static void display(void)
{
  FS *fs;
  FU *fu;
  PR *pr;
  int cnt;
  unsigned char *bp, *savebp;
  off_t saveaddress;
  unsigned char savech = '\0';

  while ((bp = get())) {
    for (fs = fshead, savebp = bp, saveaddress = address; fs; fs = fs->nextfs, bp = savebp, address = saveaddress) {
      for (fu = fs->nextfu; fu; fu = fu->nextfu) {
        if (fu->flags & F_IGNORE)	break;

        for (cnt = fu->reps; cnt; --cnt) {
          for (pr = fu->nextpr; pr; address += pr->bcnt, bp += pr->bcnt, pr = pr->nextpr) {
            if (eaddress && address >= eaddress && !(pr->flags & (F_TEXT|F_BPAD)))
              bpad(pr);
            if (cnt == 1 && pr->nospace) {
              savech = *pr->nospace;
              *pr->nospace = '\0';
            }
            /* PRINT */
            switch(pr->flags) {
              case F_ADDRESS:
                printf(pr->fmt, (int64_t)address);
                break;
              case F_BPAD:
                printf(pr->fmt, "");
                break;
              case F_C:
                conv_c(pr, bp);
                break;
              case F_CHAR:
                printf(pr->fmt, *bp);
                break;
              case F_DBL:
                {
                  double f8;
                  float f4;
                  switch(pr->bcnt) {
                    case 4:
                      memcpy(&f4, bp, sizeof(f4));
                      printf(pr->fmt, f4);
                      break;
                    case 8:
                      memcpy(&f8, bp, sizeof(f8));
                      printf(pr->fmt, f8);
                      break;
                  }
                  break;
                }
              case F_INT:
                {
                  int16_t s2;
                  int32_t s4;
                  int64_t s8;
                  switch(pr->bcnt) {
                    case 1:
                      printf(pr->fmt, (int64_t)*bp);
                      break;
                    case 2:
                      memcpy(&s2, bp, sizeof(s2));
                      printf(pr->fmt, (int64_t)s2);
                      break;
                    case 4:
                      memcpy(&s4, bp, sizeof(s4));
                      printf(pr->fmt, (int64_t)s4);
                      break;
                    case 8:
                      memcpy(&s8, bp, sizeof(s8));
                      printf(pr->fmt, (int64_t)s8);
                      break;
                  }
                  break;
                }
              case F_P:
                printf(pr->fmt, isprint(*bp) ? *bp : '.');
                break;
              case F_STR:
                printf(pr->fmt, (char *)bp);
                break;
              case F_TEXT:
                printf("%s", pr->fmt);
                break;
              case F_U:
                conv_u(pr, bp);
                break;
              case F_UINT: 
                {
                  uint16_t u2;
                  uint32_t u4;
                  uint64_t u8;
                  switch(pr->bcnt) {
                    case 1:
                      printf(pr->fmt, (uint64_t)*bp);
                      break;
                    case 2:
                      memcpy(&u2, bp, sizeof(u2));
                      printf(pr->fmt, (uint64_t)u2);
                      break;
                    case 4:
                      memcpy(&u4, bp, sizeof(u4));
                      printf(pr->fmt, (uint64_t)u4);
                      break;
                    case 8:
                      memcpy(&u8, bp, sizeof(u8));
                      printf(pr->fmt, (uint64_t)u8);
                      break;
                  }
                  break;
                }
            }

            if (cnt == 1 && pr->nospace)
              *pr->nospace = savech;
          }
        }
      }
    }
  }
  if (endfu) {
    /*
     * If eaddress not set, error or file size was multiple of
     * blocksize, and no partial block ever found.
     */
    if (!eaddress) {
      if (!address) return;
      eaddress = address;
    }
    for (pr = endfu->nextpr; pr; pr = pr->nextpr) {
      switch(pr->flags) {
        case F_ADDRESS:
          printf(pr->fmt, (int64_t)eaddress);
          break;
        case F_TEXT:
          printf("%s", pr->fmt);
          break;
      }
    }
  }
}

static int strtol_range(char *str, long min, long max)
{
  char *endptr = NULL;
  errno = 0;
  long ret_value = strtol(str, &endptr, 10);

  if(errno) perror_exit("Invalid num %s", str);
  else if(endptr && (*endptr != '\0' || endptr == str))
    perror_exit("Invalid num %s", str);
  if(ret_value >= min && ret_value <= max) return ret_value;
  else perror_exit("Number %s is not in valid [%d-%d] Range\n", str, min, max);
}

static const char *const add_formats[] = {                            
	"\"%07.7_ax \" 16/1 \"%03o \" \"\\n\"",   /* b */             
	"\"%07.7_ax \" 16/1 \"%3_c \" \"\\n\"",   /* c */             
	"\"%07.7_ax \" 8/2 \"  %05u \" \"\\n\"",  /* d */             
	"\"%07.7_ax \" 8/2 \" %06o \" \"\\n\"",   /* o */             
	"\"%07.7_ax \" 8/2 \"   %04x \" \"\\n\"", /* x */             
};

/*  
 * free all the list nodes on error OR on exit
 */
static void free_list(FS *head)                                                                                                                                 
{
  FS *tfs = NULL;
  if (!head) return;
  tfs = head;
  while(tfs) {
    FU *tmp = tfs->nextfu, *tmp1 = tfs->nextfu;
    if (!tmp) continue;
    while (tmp1) {
      tmp = tmp->nextfu;
      free(tmp1);
      tmp1 = tmp;
    }
    head = head->nextfs;
    free(tfs);
    tfs = head;
  }
}

static char* get_myline(fd)
{
  char c, *buf = NULL;
  long len = 0;

  for (;;) {
    if (1>read(fd, &c, 1)) break;
    if (!(len & 63)) buf=xrealloc(buf, len+65);
    if (((buf[len++]=c) == '\n') || (c == '\0')) break;
  }    
  if (buf) buf[len]=0;
  if (buf && buf[--len]=='\n') buf[len]=0;

  return buf; 
}

static const char option_str[]="bcdoxCe:f:n:s:vR";    

void hexdump_main(void)
{
  FS *tfs;
  int ch, r_flag = 0;
  char *p, **pt;
  int argc = 0;

  pt = toys.argv;
  while(*pt++) argc++;

  while ((ch = getopt(argc, toys.argv, option_str)) != -1) {
    switch (ch) {
      case 'b': case 'c': case 'd': case 'o': case 'x':
        p = strchr(option_str, ch);
        add("\"%07.7_Ax\n\"");
        add(add_formats[(int)(p-option_str)]);
        break;
      case 'C':
        add("\"%08.8_Ax\n\"");
        add("\"%08.8_ax  \" 8/1 \"%02x \" \"  \" 8/1 \"%02x \" ");
        add("\"  |\" 16/1 \"%_p\" \"|\\n\"");
        break;
      case 'e':
        add(optarg);
        break;
      case 'f':
        addfile(optarg);
        break;
      case 'n':
        length = strtol_range(optarg, 0, INT_MAX);
        break;
      case 's':
        errno = 0;
        if (((skip = strtol(optarg, &p, 0)) < 0) 
            || errno)
          error_exit("invalid number '%s'", optarg);
        if(*p && !*(p+1))
          switch(*p) {
            case 'b':
              skip *= 512;
              break;
            case 'k':
              skip *= 1024;
              break;
            case 'm':
              skip *= 1048576;
              break;
            default:
              error_exit("invalid number '%s'", optarg);
          }
        else if(*p && *(p+1))
          error_exit("invalid number '%s'", optarg);
        if(skip > INT_MAX || skip < 0)
          error_exit("number %s is not in 0..%ld range", optarg, INT_MAX);
        break;
      case 'v':
        v_flag = ALL;
        break;
      case 'R':
        r_flag = 1;
        break;

      default:
        fprintf(stdout, "usage: %s ", toys.which->name);
        fprintf(stdout, "[-bCcdovx] [-e format_string] [-f format_file] [-n length] [-s skip] [file ...]\n");
        exit(EXIT_FAILURE);
    }
  }

  if (!fshead) {
    add("\"%07.7_Ax\n\"");
    add("\"%07.7_ax \" 8/2 \"%04x \" \"\\n\"");
  }

  if (r_flag) {
    /* -R: reverse of 'hexdump -Cv' */
    int fd = -1;
    fd = STDIN_FILENO;
    if (!*toys.optargs) {
      toys.optargs--;
      goto start_print;
    }

    do {
      char *buf;
      fd = xopen(*toys.optargs, O_RDONLY);
start_print:
      while ((buf = get_myline(fd)) != NULL) {
        p = buf;
        while (1) {
          /* skip address or previous byte */
          while (isxdigit(*p)) p++;
          while (*p == ' ') p++;
          /* '|' char will break the line */
          if (!isxdigit(*p) || sscanf(p, "%x ", &ch) != 1)
            break;
          xputc(ch);
        }
        free(buf);
      }
      close(fd);
    } while (*++toys.optargs);
  } else {
    /* figure out the data block size */
    for (blocksize = 0, tfs = fshead; tfs; tfs = tfs->nextfs) {
      tfs->bcnt = size(tfs);
      if (blocksize < tfs->bcnt) blocksize = tfs->bcnt;
    }
    /* rewrite the rules, do syntax checking */
    for (tfs = fshead; tfs; tfs = tfs->nextfs)
      rewrite(tfs);

    next(toys.optargs);
    display();
  }
  if (CFG_TOYBOX_FREE) free_list(fshead);
}
