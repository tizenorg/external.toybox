/* grep.c - A grep world program.
 *
 * Copyright 2012 Harvind Singh <harvindsingh1981@gmail.com>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/grep.html

USE_GREP(NEWTOY(grep, "<1aHhnlLcoqvsxriwFEm#A#B#C#e:f:", TOYFLAG_USR|TOYFLAG_BIN))

config GREP
  bool "grep"
  default y
  help
    usage: grep [-aHhnlLcoqvsxriwFE] [-m NUMBER] [-A NUMBER] [-B NUMBER] [-C NUMBER] PATTERN/ [-e PTRN] [-f FILE] [FILE]...

    Search for PATTERN in FILEs (or stdin)

    -a  Process a binary file as text file.
    -H  Add 'filename:' prefix
    -h  Do not add 'filename:' prefix
    -n  Add 'line_no:' prefix
    -l  Show only names of files that match
    -L  Show only names of files that don't match
    -c  Show only count of matching lines
    -o  Show only the matching part of line
    -q  Quiet. Return 0 if PATTERN is found, 1 otherwise
    -v  Select non-matching lines
    -s  Suppress open and read errors
    -r  Recurse
    -i  Ignore case
    -w  Match whole words only
    -x  Match whole lines only
    -F  PATTERN is a literal (not regexp)
    -E  PATTERN is an extended regexp
    -m N  Match up to N times per file
    -A N  Print N lines of trailing context
    -B N  Print N lines of leading context
    -C N  Same as '-A N -B N'
    -e PTRN Pattern to match
    -f FILE Read pattern from file
*/
/*  $NetBSD: grep.c,v 1.3 2006/05/15 21:12:21 rillig Exp $   */                                                        
 
/*
 * Copyright (c) 1999 James Howard and Dag-Erling CoyAna Smgrav
 * All rights reserved.
 * Copyright (c) 1980, 1989, 1993, 1994
 *  The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *  notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *  notice, this list of conditions and the following disclaimer in the
 *  documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
 *
 */

#include "toys.h"
#include <stdbool.h>
#include <sys/cdefs.h>
#include <getopt.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <fnmatch.h>
#undef __USE_FILE_OFFSET64
#include <fts.h>
/***********************************HEADER******************************************************************************/
#include <regex.h>
#include <stdbool.h>
#ifdef WITHOUT_NLS
#define getstr(n)   errstr[n]
#else
#include <nl_types.h>
nl_catd catalog;
#define getstr(n)   catgets(catalog, 1, n, errstr[n])
#endif

#define GREP_FIXED    0
#define GREP_BASIC    1
#define GREP_EXTENDED 2
#define BINFILE_BIN   0
#define BINFILE_SKIP  1
#define BINFILE_TEXT  2
#define DIR_READ      0
#define DIR_SKIP      1
#define DIR_RECURSE   2
#define DEV_READ      0
#define DEV_SKIP      1
#define LINK_READ     0
#define LINK_EXPLICIT 1
#define LINK_SKIP     2
#define EXCL_PAT      0
#define INCL_PAT      1
#define MAX_LINE_MATCHES  32

struct file {
  int  fd;
  bool binary;
};

struct str {
  off_t   off;
  size_t  len;
  char    *dat;
  char    *file;
  int     line_no;
};

struct epat {
  char *pat;
  int  mode;
};

typedef struct {
  size_t         len;
  unsigned char  *pattern;
  int            qsBc[UCHAR_MAX + 1];
  /* flags */
  bool bol, eol, reversed, word;
} fastgrep_t;

/* Flags passed to regcomp() and regexec() */
int cflags = 0;
int eflags = REG_STARTEND;

/*
 * Command-line flags.
 * Aflag -> -A x: print x lines trailing each match.
 * Bflag -> -B x: print x lines leading each match.
 * mcount -> count for -m.
 * Hflag -> -H: always print file name.
 * Lflag -> -L: only show names of files with no matches.
 * bflag -> -b: show block numbers for each match.
 * cflag -> -c: only show a count of matching lines.
 * hflag -> -h: don't print filename headers.
 * iflag -> -i: ignore case.
 * lflag -> -l: only show names of files with matches.
 * mflag -> -m x: stop reading the files after x matches.
 * nflag -> -n: show line numbers in front of matching lines.
 * oflag -> -o: print only matching part.
 * qflag -> -q: quiet mode (don't output anything).
 * sflag -> -s: silent mode (ignore errors).
 * vflag -> -v: only show non-matching lines.
 * wflag -> -w: pattern must start and end on word boundaries.
 * xflag -> -x: pattern must match entire line.
 * lbflag -> --line-buffered.
 * nullflag -> --null.
 * nulldataflag -> --null-data.
 * dexclude -> --exclude-dir.
 * dinclude -> --include-dir.
 * fexclude -> --exclude.
 * finclude -> --include.
 * matchall -> Shortcut for matching all cases like empty regex.
 * notfound -> file not found.
 */
unsigned long long Aflag, Bflag, mcount;
bool Hflag, Lflag, bflag, cflag, hflag, iflag, lflag, mflag, nflag, oflag,
     qflag, sflag, vflag, wflag, xflag, lbflag, nullflag, nulldataflag,
     dexclude, dinclude, fexclude, finclude, matchall, notfound;

unsigned char line_sep = '\n';  // 0 for --null-data.
char *label;// --label.
const char *color; // --color.

/*
 * grepbehave -> -EFGP: type of the regex.
 * binbehave -> -aIU: handling of binary files.
 * filebehave -> -JZ: normal, gzip or bzip2 file.
 * devbehave -> -D: handling of devices.
 * dirbehave -> -dRr: handling of directories.
 * linkbehave -> -OpS: handling of symlinks.
 * tail -> lines left to print.
 */
int grepbehave = GREP_BASIC;
int binbehave = BINFILE_BIN;
int filebehave = 0;
int devbehave = DEV_READ;
int dirbehave = DIR_READ;
int linkbehave = LINK_READ;
int tail;

/*
 * Filename exclusion/inclusion patterns.
 * Searching patterns.
 */
unsigned int fpatterns, fpattern_sz, dpatterns, dpattern_sz, patterns, pattern_sz;
char **pattern;
struct epat *dpattern, *fpattern;
regex_t *r_pattern;
fastgrep_t *fg_pattern;

/* For regex errors  */
#define RE_ERROR_BUF  512
char re_error[RE_ERROR_BUF + 1]; /* Seems big enough */

/*
 * Default messags to use when NLS is disabled or no catalogue
 * is found.
 */
const char *errstr[] = {
  "",
  /*1*/  "(standard input)",
  /*2*/  "cannot read bzip2 compressed file",
  /*3*/  "unknown %s option",
  /*4*/  "usage: %s [-abcDEFGHhIiJLlmnOoPqRSsUVvwxZz] [-A num] [-B num] [-C[num]]\n",
  /*5*/  "\t[-e pattern] [-f file] [--binary-files=value] [--color=when]\n",
  /*6*/  "\t[--context[=num]] [--directories=action] [--label] [--line-buffered]\n",
  /*7*/  "\t[pattern] [file ...]\n",
  /*8*/  "Binary file %s matches\n",
  /*9*/  "%s (BSD grep) %s\n",
};

enum {
  BIN_OPT = CHAR_MAX + 1,
  COLOR_OPT,
  DECOMPRESS_OPT,
  HELP_OPT,
  MMAP_OPT,
  LINEBUF_OPT,
  LABEL_OPT,
  R_EXCLUDE_OPT,
  R_INCLUDE_OPT,
  R_DEXCLUDE_OPT,
  R_DINCLUDE_OPT
};

static const char optstr[] =
"0123456789A:B:C:D:EFGHIJLOPSRUVZabcd:e:f:hilm:nopqrsuvwxyz";

struct option long_options[] =
{
  {"binary-files",        required_argument, NULL, BIN_OPT},
  {"decompress",          no_argument,       NULL, DECOMPRESS_OPT},
  {"help",                no_argument,       NULL, HELP_OPT},
  {"mmap",                no_argument,       NULL, MMAP_OPT},
  {"line-buffered",       no_argument,       NULL, LINEBUF_OPT},
  {"label",               required_argument, NULL, LABEL_OPT},
  {"color",               optional_argument, NULL, COLOR_OPT},
  {"colour",              optional_argument, NULL, COLOR_OPT},
  {"exclude",             required_argument, NULL, R_EXCLUDE_OPT},
  {"include",             required_argument, NULL, R_INCLUDE_OPT},
  {"exclude-dir",         required_argument, NULL, R_DEXCLUDE_OPT},
  {"include-dir",         required_argument, NULL, R_DINCLUDE_OPT},
  {"after-context",       required_argument, NULL, 'A'},
  {"text",                no_argument,       NULL, 'a'},
  {"before-context",      required_argument, NULL, 'B'},
  {"byte-offset",         no_argument,       NULL, 'b'},
  {"context",             optional_argument, NULL, 'C'},
  {"count",               no_argument,       NULL, 'c'},
  {"devices",             required_argument, NULL, 'D'},
  {"directories",         required_argument, NULL, 'd'},
  {"extended-regexp",     no_argument,       NULL, 'E'},
  {"regexp",              required_argument, NULL, 'e'},
  {"fixed-strings",       no_argument,       NULL, 'F'},
  {"file",                required_argument, NULL, 'f'},
  {"basic-regexp",        no_argument,       NULL, 'G'},
  {"no-filename",         no_argument,       NULL, 'h'},
  {"with-filename",       no_argument,       NULL, 'H'},
  {"ignore-case",         no_argument,       NULL, 'i'},
  {"bz2decompress",       no_argument,       NULL, 'J'},
  {"files-with-matches",  no_argument,       NULL, 'l'},
  {"files-without-match", no_argument,       NULL, 'L'},
  {"max-count",           required_argument, NULL, 'm'},
  {"line-number",         no_argument,       NULL, 'n'},
  {"only-matching",       no_argument,       NULL, 'o'},
  {"quiet",               no_argument,       NULL, 'q'},
  {"silent",              no_argument,       NULL, 'q'},
  {"recursive",           no_argument,       NULL, 'r'},
  {"no-messages",         no_argument,       NULL, 's'},
  {"binary",              no_argument,       NULL, 'U'},
  {"unix-byte-offsets",   no_argument,       NULL, 'u'},
  {"invert-match",        no_argument,       NULL, 'v'},
  {"word-regexp",         no_argument,       NULL, 'w'},
  {"line-regexp",         no_argument,       NULL, 'x'},
  {"null",                no_argument,       NULL, 'Z'},
  {"null-data",           no_argument,       NULL, 'z'},
  {NULL,                  no_argument,       NULL, 0}
};

/*******************************UTIL GLOBAL*********************************/
static bool first, first_global = true;
static unsigned long long since_printed;

/*******************************File global*********************************/
#define MAXBUFSIZ (32 * 1024)
#define LNBUFBUMP 80

static unsigned char buffer[MAXBUFSIZ], *bufpos, *lnbuf;
static size_t bufrem, lnbuflen;

/****************queue global***********************************************/
struct qentry {
  STAILQ_ENTRY(qentry) list;
  struct str data;
};
static STAILQ_HEAD(, qentry) queue = STAILQ_HEAD_INITIALIZER(queue);
static unsigned long long  count;

/*******************************************************************************************************************/
static int validate_for_negative(char *arg)
{
  while (*arg == ' ') arg++;
  if (*arg == '-') return -1;
  return 0;
}

static inline const char *init_color(char *d)
{
  char *c = getenv("GREP_COLOR");
  return (c ? c : d);
}

static int get_argc(char **argv)
{
  int i = 0;
  while (argv[i]) i++;
  return i;
}
/*
 * Safe calloc() for internal use.
 */
static void *grep_calloc(size_t nmemb, size_t size)
{
  void *ptr = calloc(nmemb, size);
  if (!ptr) perror_exit("calloc");
  return (ptr);
}
/****************************************************************************************************/
static inline int grep_refill(struct file *f)
{
  bufpos = buffer;
  bufrem = 0;
  ssize_t nr = read(f->fd, buffer, MAXBUFSIZ);
  if (nr < 0) return -1;
  bufrem = nr;
  return 0;
}

static inline int grep_lnbufgrow(size_t newlen)
{
  if (lnbuflen < newlen) {
    lnbuf = xrealloc(lnbuf, newlen);
    lnbuflen = newlen;
  }
  return 0;
}

static char *grep_fgetln(struct file *f, size_t *lenp)
{
  unsigned char *p;
  char *ret;
  size_t len, off;
  ptrdiff_t diff;

  /* Fill the buffer, if necessary */
  if (!bufrem && grep_refill(f)) goto error;
  if (!bufrem) { /* Return zero length to indicate EOF */
    *lenp = 0;
    return ((char *)bufpos);
  }

  /* Look for a newline in the remaining part of the buffer */
  if ((p = memchr(bufpos, line_sep, bufrem))) {
    ++p; /* advance over newline */
    ret = (char *)bufpos;
    len = p - bufpos;
    bufrem -= len;
    bufpos = p;
    *lenp = len;
    return ret;
  }

  /* We have to copy the current buffered data to the line buffer */
  for (len = bufrem, off = 0; ; len += bufrem) {
    /* Make sure there is room for more data */
    if (grep_lnbufgrow(len + LNBUFBUMP)) goto error;
    memcpy(lnbuf + off, bufpos, len - off);
    off = len;
    if (grep_refill(f)) goto error;
    if (!bufrem) break; /* EOF: return partial line */
    if (!(p = memchr(bufpos, line_sep, bufrem))) continue;
    /* got it: finish up the line (like code above) */
    ++p;
    diff = p - bufpos;
    len += diff;
    if (grep_lnbufgrow(len)) goto error;
    memcpy(lnbuf + off, bufpos, diff);
    bufrem -= diff;
    bufpos = p;
    break;
  }
  *lenp = len;
  return (char *)lnbuf;

error:
  *lenp = 0;
  return NULL;
}

static inline struct file *grep_file_init(struct file *f)
{
  /* Fill read buffer, also catches errors early */
  if (grep_refill(f)) goto error;

  /* Check for binary stuff, if necessary */
  if (!nulldataflag && binbehave != BINFILE_TEXT && memchr(bufpos, '\0', bufrem))
    f->binary = true;
  return f;
error:
  xclose(f->fd);
  free(f);
  f = NULL;
  return NULL;
}
/*
 * Opens a file for processing.
 */
static struct file *grep_open(char *path)
{
  int ret;
  struct file *f = xzalloc(sizeof *f);
  if (!path) {/* Processing stdin implies --line-buffered. */
    lbflag = true;
    f->fd = STDIN_FILENO;
  } else {
    ret = f->fd = open(path, O_RDONLY);
    if ( ret < 0) {
      free(f);
      return NULL;
    }
  }
  return (grep_file_init(f));
}
/*
 * Close the file descriptor and Reset read buffer and line buffer.
 */
static void grep_close(struct file *f)
{
  xclose(f->fd);
  bufpos = buffer;
  bufrem = 0;

  free(lnbuf);
  lnbuf = NULL;
  lnbuflen = 0;
}
/*
 * Prints a matching line according to the command line options.
 */
static void printline(struct str *line, int sep, regmatch_t *matches, int m)
{
  size_t a = 0;
  int i, n = 0;

  if (!hflag) {
    if (!nullflag) fputs(line->file, stdout);
    else {
      printf("%s", line->file);
      putchar(0);
    }
    ++n;
  }
  if (nflag) {
    if (n > 0) putchar(sep);
    printf("%d", line->line_no);
    ++n;
  }
  if (bflag) {
    if (n > 0) putchar(sep);
    printf("%lld", (long long)line->off);
    ++n;
  }
  if (n) putchar(sep);
  /* --color and -o */
  if ((oflag || color) && m > 0) {
    for (i = 0; i < m; i++) {
      if (!oflag) fwrite(line->dat + a, matches[i].rm_so - a, 1, stdout);
      if (color) fprintf(stdout, "\33[%sm\33[K", color);
      if (nflag && i && oflag) {
        printf("%d", line->line_no);
        putchar(sep);
      }
      fwrite(line->dat + matches[i].rm_so,
          matches[i].rm_eo - matches[i].rm_so, 1, stdout);
      if (color) fprintf(stdout, "\33[m\33[K");
      a = matches[i].rm_eo;
      if (oflag) putchar('\n');
    }
    if (!oflag) {
      if (line->len - a > 0) fwrite(line->dat + a, line->len - a, 1, stdout);
      putchar(line_sep);
    }
  } else {
    fwrite(line->dat, line->len, 1, stdout);
    putchar(line_sep);
  }
}
/****************************************************************************************************/
static struct qentry *dequeue(void)
{
  struct qentry *item = STAILQ_FIRST(&queue);
  if (!item) return NULL;
  STAILQ_REMOVE_HEAD(&queue, list);
  --count;
  return item;
}

static void enqueue(struct str *x)
{
  struct qentry *item = xzalloc(sizeof(struct qentry));
  item->data.dat = xzalloc(sizeof(char) * x->len);
  item->data.len = x->len;
  item->data.line_no = x->line_no;
  item->data.off = x->off;
  memcpy(item->data.dat, x->dat, x->len);
  item->data.file = x->file;

  STAILQ_INSERT_TAIL(&queue, item, list);

  if (++count > Bflag) {
    item = dequeue();
    if (item && item->data.dat) free(item->data.dat);
    if (item) free(item);
  }
}

static void printqueue(void)
{
  struct qentry *item;
  while ((item = dequeue())) {
    printline(&item->data, '-', NULL, 0);
    if (item->data.dat) free(item->data.dat);
    free(item);
  }
}

static void clearqueue(void)
{
  struct qentry *item;
  while ((item = dequeue())) {
    if (item->data.dat) free(item->data.dat);
    free(item);
  }
}
/****************************************************************************************************/
/*
 * Returns:  i >= 0 on failure (position that it failed)
 *           -1 on success
 */
static inline int grep_cmp(unsigned char *pat, unsigned char *data, size_t len)
{
  size_t size;
  wchar_t *wdata, *wpat;
  unsigned int i;

  if (iflag) {
    if ((size = mbstowcs(NULL, (const char *)data, 0)) == ((size_t) - 1))
      return -1;

    wdata = xmalloc(size * sizeof(wint_t));
    if (mbstowcs(wdata, (const char *)data, size) == ((size_t) - 1)) {
      free(wdata);
      return -1;
    }
    if ((size = mbstowcs(NULL, (const char *)pat, 0)) ==
        ((size_t) - 1))
      return (-1);

    wpat = xmalloc(size * sizeof(wint_t));
    if (mbstowcs(wpat, (const char *)pat, size) == ((size_t) - 1)) {
      free(wdata);
      free(wpat);
      return -1;
    }
    for (i = 0; i < len; i++) {
      if ((towlower(wpat[i]) == towlower(wdata[i])) ||
          ((grepbehave != GREP_FIXED) && wpat[i] == L'.'))
        continue;
      free(wpat);
      free(wdata);
      return i;
    }
  } else {
    for (i = 0; i < len; i++) {
      if ((pat[i] == data[i]) || ((grepbehave != GREP_FIXED) && pat[i] == '.'))
        continue;
      return i;
    }
  }
  return -1;
}

static int grep_search(fastgrep_t *fg, unsigned char *data, size_t len, regmatch_t *pmatch)
{
  unsigned int j;
  int ret = REG_NOMATCH;

  if (pmatch->rm_so == (ssize_t)len) return ret;
  if (fg->bol && pmatch->rm_so != 0) {
    pmatch->rm_so = len;
    pmatch->rm_eo = len;
    return ret;
  }
  /* No point in going farther if we do not have enough data. */
  if (len < fg->len) return ret;

  /* Only try once at the beginning or ending of the line. */
  if (fg->bol || fg->eol) {
    /* Verify data is >= pattern length before searching on it. */
    if (len >= fg->len) {
      /* Determine where in data to start search at. */
      j = fg->eol ? len - fg->len : 0;
      if (!((fg->bol && fg->eol) && (len != fg->len)))
        if (grep_cmp(fg->pattern, data + j, fg->len) == -1) {
          pmatch->rm_so = j;
          pmatch->rm_eo = j + fg->len;
          ret = 0;
        }
    }
  } else if (fg->reversed) {
    /* Quick Search algorithm. */
    j = len;
    do {
      if (grep_cmp(fg->pattern, data + j - fg->len, fg->len) == -1) {
        pmatch->rm_so = j - fg->len;
        pmatch->rm_eo = j;
        ret = 0;
        break;
      }
      /* Shift if within bounds, otherwise, we are done. */
      if (j == fg->len) break;
      j -= fg->qsBc[data[j - fg->len - 1]];
    } while (j >= fg->len);
  } else {
    /* Quick Search algorithm. */
    j = pmatch->rm_so;
    do {
      if (grep_cmp(fg->pattern, data + j, fg->len) == -1) {
        pmatch->rm_so = j;
        pmatch->rm_eo = j + fg->len;
        ret = 0;
        break;
      }
      /* Shift if within bounds, otherwise, we are done. */
      if (j + fg->len == len) break;
      else j += fg->qsBc[data[j + fg->len]];
    } while (j <= (len - fg->len));
  }
  return ret;
}

static inline void grep_revstr(unsigned char *str, int len)
{
  int i, counter = len/2;
  char c;
  for (i = 0; i < counter; i++) {
    c = str[i];
    str[i] = str[len - i - 1];
    str[len - i - 1] = c;
  }
}
/****************************************************************************************************/
/*
 * Adds a searching pattern to the internal array.
 */
static void add_pattern(char *pat, size_t len)
{
  /* Check if we can do a shortcut */
  if (!len || matchall) {
    matchall = true;
    return;
  }
  /* Increase size if necessary */
  if (patterns == pattern_sz) {
    pattern_sz *= 2;
    pattern = xrealloc(pattern, ++pattern_sz * sizeof(*pattern));
  }
  if (len > 0 && pat[len - 1] == '\n') --len;
  /* pat may not be NULL-terminated */
  pattern[patterns] = xmalloc(len + 1);
  memcpy(pattern[patterns], pat, len);
  pattern[patterns][len] = '\0';
  ++patterns;
}
/*
 * Adds a file include/exclude pattern to the internal array.
 */
static void add_fpattern(char *pat, int mode)
{
  /* Increase size if necessary */
  if (fpatterns == fpattern_sz) {
    fpattern_sz *= 2;
    fpattern = xrealloc(fpattern, ++fpattern_sz * sizeof(struct epat));
  }
  fpattern[fpatterns].pat = xstrdup(pat);
  fpattern[fpatterns].mode = mode;
  ++fpatterns;
}
/*
 * Adds a directory include/exclude pattern to the internal array.
 */
static void add_dpattern(char *pat, int mode)
{
  /* Increase size if necessary */
  if (dpatterns == dpattern_sz) {
    dpattern_sz *= 2;
    dpattern = xrealloc(dpattern, ++dpattern_sz * sizeof(struct epat));
  }
  dpattern[dpatterns].pat = xstrdup(pat);
  dpattern[dpatterns].mode = mode;
  ++dpatterns;
}
/*
 * Reads searching patterns from a file and adds them with add_pattern().
 */
static void read_patterns(char *fn)
{
  char *line = NULL;
  size_t len = 0;
  ssize_t rlen;
  FILE *f = fopen(fn, "r");

  if (!f) perror_exit("%s", fn);
  while ((rlen = getline(&line, &len, f)) != -1)
    add_pattern(line, *line == '\n' ? 0 : (size_t)rlen);
  free(line);

  if (ferror(f)) perror_exit("%s", fn);
  fclose(f);
}

/***************************************************************************************/
static bool file_matching(char *fname)
{
  char *fname_base, *fname_copy;
  unsigned int i;
  bool ret = finclude ? false : true;

  fname_copy = xstrdup(fname);
  fname_base = basename(fname_copy);

  for (i = 0; i < fpatterns; ++i) {
    if (!fnmatch(fpattern[i].pat, fname, 0) ||
        !fnmatch(fpattern[i].pat, fname_base, 0)) {
      if (fpattern[i].mode == EXCL_PAT) return false;
      else ret = true;
    }
  }
  free(fname_copy);
  return ret;
}

static inline bool dir_matching(char *dname)
{
  unsigned int i;
  bool ret = dinclude ? false : true;

  for (i = 0; i < dpatterns; ++i) {
    if (dname && !fnmatch(dname, dpattern[i].pat, 0)) {
      if (dpattern[i].mode == EXCL_PAT) return false;
      else ret = true;
    }
  }
  return ret;
}
/*
 * Processes a line comparing it with the specified patterns.  Each pattern
 * is looped to be compared along with the full string, saving each and every
 * match, which is necessary to colorize the output and to count the
 * matches.  The matching lines are passed to printline() to display the
 * appropriate output.
 */
static int procline(struct str *l, int nottext)
{
#define iswword(x)  (iswalnum((x)) || (x) == L'_')
  regmatch_t matches[MAX_LINE_MATCHES], pmatch;
  size_t st = 0;
  unsigned int i;
  int c = 0, m = 0, r = 0;

  if (!matchall) {
    while (st <= l->len) {/* Loop to process the whole line */
      pmatch.rm_so = st;
      pmatch.rm_eo = l->len;
      for (i = 0; i < patterns; i++) {/* Loop to compare with all the patterns */
        if (fg_pattern[i].pattern) {
          r = grep_search(&fg_pattern[i], (unsigned char *)l->dat, l->len, &pmatch);
          r = (r == 0) ? 0 : REG_NOMATCH;
          st = pmatch.rm_eo;
        } else {
          r = regexec(&r_pattern[i], l->dat, 1, &pmatch, eflags);
          r = (r == 0) ? 0 : REG_NOMATCH;
          st = pmatch.rm_eo;
        }
        if (r == REG_NOMATCH) continue;

        /* Check for full match */
        if (!r && xflag)
          if (pmatch.rm_so || (size_t)pmatch.rm_eo != l->len) r = REG_NOMATCH;

        /* Check for whole word match */
        if (!r && fg_pattern[i].word ) {
          wint_t wbegin, wend;

          wbegin = wend = L' ';
          if (pmatch.rm_so) wbegin = l->dat[pmatch.rm_so - 1];
          if ((size_t)pmatch.rm_eo != l->len) wend = l->dat[pmatch.rm_eo];
          if (iswword(wbegin) || iswword(wend)) r = REG_NOMATCH;
        }
        if (!r) {
          if (!m) c++;
          if (m < MAX_LINE_MATCHES) matches[m++] = pmatch;
          /* matches - skip further patterns */
          if ((color && !oflag) || qflag || lflag) break;
        }
      }

      if (vflag) {
        c = !c;
        break;
      }
      /* One pass if we are not recording matches */
      if ((color && !oflag) || qflag || lflag) break;
      if (st == (size_t)pmatch.rm_so) break; /* No matches */
    }
  } else c = !vflag;

  /* Binary file */
  if (c && binbehave == BINFILE_BIN && nottext) return c;

  /* Dealing with the context */
  if ((tail || c) && !cflag && !qflag && !lflag && !Lflag) {
    if (c) {
      if ((Aflag || Bflag) && !first_global && (first || since_printed > Bflag))
        printf("--\n");
      tail = Aflag;
      if (Bflag > 0) printqueue();
      printline(l, ':', matches, m);
    } else {
      printline(l, '-', matches, m);
      tail--;
    }
    first = false;
    first_global = false;
    since_printed = 0;
  } else {
    if (Bflag) enqueue(l);
    since_printed++;
  }
  return c;
#undef iswword
}
/*
 * Opens a file and processes it.  Each file is processed line-by-line
 * passing the lines to procline().
 */
static int procfile(char *fn)
{
  struct file *f;
  struct stat sb;
  struct str ln;
  mode_t s;
  int c, t;

  if (mflag && (mcount <= 0)) return 0;
  if (!strcmp(fn, "-")) {
    fn = (label ? label : getstr(1));
    f = grep_open(NULL);
  } else {
    if (!stat(fn, &sb)) {
      /* Check if we need to process the file */
      s = sb.st_mode & S_IFMT;
      if (s == S_IFDIR || dirbehave == DIR_SKIP ) {
        if (Lflag) printf("%s%c", fn, line_sep);
        return 0;
      }
      if ((s == S_IFIFO || s == S_IFCHR || s == S_IFBLK || s == S_IFSOCK)
          && devbehave == DEV_SKIP)
        return 0;
    }
    f = grep_open(fn);
  }
  if (!f) {
    if (!sflag) perror_msg("%s", fn);
    if (errno == ENOENT) notfound = true;
    return 0;
  }
  ln.file = xmalloc(strlen(fn) + 1);
  strcpy(ln.file, fn);
  ln.line_no = ln.len = tail = 0;
  ln.off = -1;

  for (first = true, c = 0;  !c || !(lflag || qflag ); ) {
    ln.off += ln.len + 1;
    if (!(ln.dat = grep_fgetln(f, &ln.len)) || !(ln.len)) {
      if (!(ln.line_no) && matchall) exit(0);
      else break;
    }
    if (ln.len > 0 && ln.dat[ln.len - 1] == line_sep) --ln.len;
    ln.line_no++;

    /* Return if we need to skip a binary file */
    if (f->binary && binbehave == BINFILE_SKIP) {
      grep_close(f);
      free(ln.file);
      free(f);
      ln.file = NULL;
      f = NULL;
      return 0;
    }
    /* Process the file line-by-line */
    t = procline(&ln, f->binary);
    c += t;

    /* Count the matches if we have a match limit */
    if (mflag) {
      if ((mcount -c) <= 0) break;
    }
  }
  if (Bflag > 0) clearqueue();
  grep_close(f);

  if (cflag) {
    if (!hflag) printf("%s:", ln.file);
    printf("%u%c", c, line_sep);
  }
  if (lflag && !qflag && c) printf("%s%c", fn, line_sep);
  if (Lflag && !qflag && !c) printf("%s%c", fn, line_sep);
  if (c && !cflag && !lflag && !Lflag && binbehave == BINFILE_BIN
      && f->binary && !qflag)
    printf(getstr(8), fn);

  free(ln.file);
  free(f);
  return c;
}
/*
 * Processes a directory when a recursive search is performed with
 * the -R option.  Each appropriate file is passed to procfile().
 */
static int grep_tree(char **argv)
{
  FTS *fts;
  FTSENT *p;
  char *d, *dir = NULL;
  int c = 0, fts_flags = 0;
  bool ok;

  switch(linkbehave) {
    case LINK_EXPLICIT:
      fts_flags = FTS_COMFOLLOW;
      break;
    case LINK_SKIP:
      fts_flags = FTS_PHYSICAL;
      break;
    default:
      fts_flags = FTS_LOGICAL;
      break;
  }

  fts_flags |= FTS_NOSTAT | FTS_NOCHDIR;

  if (!(fts = fts_open(argv, fts_flags, NULL))) perror_exit("fts_open");
  while ((p = fts_read(fts))) {
    switch (p->fts_info) {
      case FTS_DNR:/* FALLTHROUGH */
      case FTS_ERR:
        error_exit("%s: %s", p->fts_path, strerror(p->fts_errno));
        break;
      case FTS_D: /* FALLTHROUGH */
      case FTS_DP:
        break;
      case FTS_DC: /* Print a warning for recursive directory loop */
        error_msg("warning: %s: recursive directory loop", p->fts_path);
        break;
      default: /* Check for file exclusion/inclusion */
        ok = true;
        if (dexclude || dinclude) {
          if ((d = strrchr(p->fts_path, '/'))) {
            dir = xmalloc(sizeof(char) * (d - p->fts_path + 1));
            memcpy(dir, p->fts_path, d - p->fts_path);
            dir[d - p->fts_path] = '\0';
          }
          ok = dir_matching(dir);
          if(dir) free(dir);
        }
        if (fexclude || finclude) ok &= file_matching(p->fts_path);
        if (ok) c += procfile(p->fts_path);
        break;
    }
  }
  fts_close(fts);
  return c;
}

/**************************************************************************************************/
static void fgrepcomp(fastgrep_t *fg, char *pat)
{
  unsigned int i;
  /* Initialize. */
  fg->len = strlen(pat);
  fg->bol = fg->eol = fg->reversed = false;
  fg->pattern = (unsigned char *)xstrdup(pat);

  /* Preprocess pattern. */
  for (i = 0; i <= UCHAR_MAX; i++) fg->qsBc[i] = fg->len;

  wchar_t *wpat;
  size_t size;

  if(iflag) {
    if ((size = mbstowcs(NULL, (const char *)pat, 0)) == ((size_t) - 1))
      goto out;

    wpat = xmalloc(size * sizeof(wint_t));
    if (mbstowcs(wpat, (const char *)pat, size) == ((size_t) - 1)) {
      free(wpat);
      goto out; 
    }            
    for (i = 0; i < fg->len; i++) {
      wchar_t ch = towlower(wpat[i]);
      fg->qsBc[ch] = fg->len - i;
      ch = towupper(wpat[i]);
      fg->qsBc[ch] = fg->len - i;
    }
  }
  else {
out:
    for (i = 1; i < fg->len; i++) fg->qsBc[fg->pattern[i]] = fg->len - i;
  }
}
/*
 * Returns: -1 on failure, 0 on success
 */
static int fastcomp(fastgrep_t *fg, char *pat)
{
  unsigned int i;
  int firstHalfDot, firstLastHalfDot, hasDot, lastHalfDot, shiftPatternLen;

  /* Initialize. */
  firstHalfDot = firstLastHalfDot = -1;
  hasDot = lastHalfDot = 0;

  fg->len = strlen(pat);
  fg->bol = fg->eol = fg->reversed = false;
  fg->word = wflag;

  /* Remove end-of-line character ('$'). */
  if (fg->len > 0 && pat[fg->len - 1] == '$') {
    fg->eol = true;
    fg->len--;
  }

  /* Remove beginning-of-line character ('^'). */
  if (pat[0] == '^') {
    fg->bol = true;
    fg->len--;
    pat++;
  }
  if (fg->eol && fg->bol && !fg->len) return -1;

  if (fg->len >= 14 &&
      !memcmp(pat, "[[:<:]]", 7) &&
      !memcmp(pat + fg->len - 7, "[[:>:]]", 7)) {
    fg->len -= 14;
    pat += 7;
    /* Word boundary is handled separately in util.c */
    fg->word = true;
  }

  /*
   * pat has been adjusted earlier to not include '^', '$' or
   * the word match character classes at the beginning and ending
   * of the string respectively.
   */
  fg->pattern = xmalloc(fg->len + 1);
  memcpy(fg->pattern, pat, fg->len);
  fg->pattern[fg->len] = '\0';

  /* Look for ways to cheat...er...avoid the full regex engine. */
  for (i = 0; i < fg->len; i++) {
    /* Can still cheat? */
    if (fg->pattern[i] == '.') {
      hasDot = i;
      if (i < fg->len / 2) {
        /* Closest dot to the beginning */
        if (firstHalfDot < 0) firstHalfDot = i;
      } else {
        /* Closest dot to the end of the pattern. */
        lastHalfDot = i;
        if (firstLastHalfDot < 0) firstLastHalfDot = i;
      }
    } else {
      /* Free memory and let others know this is empty. */
      free(fg->pattern);
      fg->pattern = NULL;
      return -1;
    }
  }

  // Determine if a reverse search would be faster based on the placement of the dots.
  if ((!(lflag || cflag)) && ((!(fg->bol || fg->eol)) &&
        ((lastHalfDot) && ((firstHalfDot < 0) ||
          ((fg->len - (lastHalfDot + 1)) < (size_t)firstHalfDot)))) &&
      !oflag && !color) {
    fg->reversed = true;
    hasDot = fg->len - (firstHalfDot < 0 ? firstLastHalfDot : firstHalfDot) - 1;
    grep_revstr(fg->pattern, fg->len);
  }

  /*
   * Normal Quick Search would require a shift based on the position the
   * next character after the comparison is within the pattern.  With
   * wildcards, the position of the last dot effects the maximum shift
   * distance.
   * The closer to the end the wild card is the slower the search.  A
   * reverse version of this algorithm would be useful for wildcards near
   * the end of the string.
   *
   * Examples:
   * Pattern  Max shift
   * -------  ---------
   * this    5
   * .his    4
   * t.is    3
   * th.s    2
   * thi.    1
   */

  /* Adjust the shift based on location of the last dot ('.'). */
  shiftPatternLen = fg->len - hasDot;

  /* Preprocess pattern. */
  for (i = 0; i <= (signed)UCHAR_MAX; i++) fg->qsBc[i] = shiftPatternLen;
  for (i = hasDot + 1; i < fg->len; i++) fg->qsBc[fg->pattern[i]] = fg->len - i;

  // Put pattern back to normal after pre-processing to allow for easy comparisons later.
  if (fg->reversed) grep_revstr(fg->pattern, fg->len);
  return 0;
}

/********************************************************************************************************************************/
void grep_main(void)
{
  char **aargv, **eargv, *eopts, *ep;
  unsigned long long l;
  unsigned int aargc, eargc, i, j;
  int c, lastc, needpattern, newarg, prevoptind, argc;

  toys.exitval = 2;
  setlocale(LC_ALL,"");
#ifndef WITHOUT_NLS
  catalog = catopen("grep", NL_CAT_LOCALE);
#endif
  argc = get_argc(toys.argv);

  lastc = '\0';
  newarg = prevoptind = needpattern = 1;

  eopts = getenv("GREP_OPTIONS");
  /* support for extra arguments in GREP_OPTIONS */
  eargc = 0;
  if (eopts) {
    char *str;
    /* make an estimation of how many extra arguments we have */
    for (j = 0; j < strlen(eopts); j++)
      if (eopts[j] == ' ') eargc++;
    eargv = (char **)xmalloc(sizeof(char *) * (eargc + 1));
    eargc = 0;
    /* parse extra arguments */
    while ((str = strsep(&eopts, " "))) eargv[eargc++] = xstrdup(str);
    aargv = (char **)grep_calloc(eargc + argc + 1, sizeof(char *));

    aargv[0] = toys.argv[0];
    for (i = 0; i < eargc; i++) aargv[i + 1] = eargv[i];
    for (j = 1; j < (unsigned int)argc; j++, i++) aargv[i+1] = toys.argv[j];
    aargc = eargc + argc;
  } else {
    aargv = toys.argv;
    aargc = argc;
  }

  while (((c = getopt_long(aargc, aargv, optstr, long_options, NULL)) != -1)) {
    switch (c) {
      case '0': /*FALL_THROUGH*/
      case '1': /*FALL_THROUGH*/
      case '2': /*FALL_THROUGH*/
      case '3': /*FALL_THROUGH*/
      case '4': /*FALL_THROUGH*/
      case '5': /*FALL_THROUGH*/
      case '6': /*FALL_THROUGH*/
      case '7': /*FALL_THROUGH*/
      case '8': /*FALL_THROUGH*/
      case '9':
        if (newarg || !isdigit(lastc)) Aflag = 0;
        else if (Aflag > LLONG_MAX / 10) {
          errno = ERANGE;
          error_exit(NULL);
        }
        Aflag = Bflag = (Aflag * 10) + (c - '0');
        break;
      case 'C': /* FALLTHROUGH */
        if (!optarg) {
          Aflag = Bflag = 2;
          break;
        }
      case 'A': /* FALLTHROUGH */
      case 'B':
        if (validate_for_negative(optarg)) error_exit("Invalid argument %s", optarg);
        errno = 0;
        l = strtoull(optarg, &ep, 10);
        if (((errno == ERANGE) && (l == ULLONG_MAX)) ||
            ((errno == EINVAL) && (l == 0))) error_exit("Invalid argument '%s'", optarg);
        else if (ep[0] != '\0') {
          errno = EINVAL;
          error_exit(NULL);
        }
        if (c == 'A') Aflag = l;
        else if (c == 'B') Bflag = l;
        else Aflag = Bflag = l;
        break;
      case 'a':
        binbehave = BINFILE_TEXT;
        break;
      case 'b':
        bflag = true;
        break;
      case 'c':
        cflag = true;
        break;
      case 'D':
        if (!strcasecmp(optarg, "skip")) devbehave = DEV_SKIP;
        else if (!strcasecmp(optarg, "read")) devbehave = DEV_READ;
        else error_exit("%s %s", getstr(3), "--devices");
        break;
      case 'd':
        if (!strcasecmp("recurse", optarg)) {
          Hflag = true;
          dirbehave = DIR_RECURSE;
        } else if (!strcasecmp("skip", optarg)) dirbehave = DIR_SKIP;
        else if (!strcasecmp("read", optarg)) dirbehave = DIR_READ;
        else error_exit("%s %s", getstr(3), "--directories");
        break;
      case 'E':
        grepbehave = GREP_EXTENDED;
        cflags |= REG_EXTENDED;
        break;
      case 'e':
        add_pattern(optarg, strlen(optarg));
        needpattern = 0;
        break;
      case 'F':
        grepbehave = GREP_FIXED;
        break;
      case 'f':
        read_patterns(optarg);
        needpattern = 0;
        break;
      case 'G':
        grepbehave = GREP_BASIC;
        break;
      case 'H':
        Hflag = true;
        hflag = false;
        break;
      case 'h':
        Hflag = false;
        hflag = true;
        break;
      case 'I':
        binbehave = BINFILE_SKIP;
        break;
      case 'i': /*FALL_THROUGH*/
      case 'y':
        iflag =  true;
        cflags |= REG_ICASE;
        break;
      case 'L':
        lflag = false;
        Lflag = true;
        break;
      case 'l':
        cflag = Lflag = false;
        lflag = true;
        break;
      case 'm':
        mflag = true;
        errno = 0;
        if (validate_for_negative(optarg)) error_exit("Invalid argument %s", optarg);
        mcount = strtoull(optarg, &ep, 10);
        if (((errno == ERANGE) && (mcount == ULLONG_MAX)) ||
            ((errno == EINVAL) && (mcount == 0)))
          error_exit("Invalid argument '%s'", optarg);
        else if (ep[0] != '\0') {
          errno = EINVAL;
          error_exit(NULL);
        }
        break;
      case 'n':
        nflag = true;
        break;
      case 'O':
        linkbehave = LINK_EXPLICIT;
        break;
      case 'o':
        oflag = true;
        break;
      case 'p':
        linkbehave = LINK_SKIP;
        break;
      case 'q':
        qflag = true;
        break;
      case 'S':
        linkbehave = LINK_READ;
        break;
      case 'R': /*FALL_THROUGH*/
      case 'r':
        dirbehave = DIR_RECURSE;
        break;
      case 's':
        sflag = true;
        break;
      case 'U':
        binbehave = BINFILE_BIN;
        break;
      case 'u': /*FALL_THROUGH*/
      case MMAP_OPT:
        /* noop, compatibility */
        break;
      case 'v':
        vflag = true;
        break;
      case 'w':
        wflag = true;
        break;
      case 'x':
        xflag = true;
        break;
      case 'Z':
        nullflag = true;
        break;
      case 'z':
        nulldataflag = true;
        line_sep = '\0';
        break;
      case BIN_OPT:
        if (!strcasecmp("binary", optarg)) binbehave = BINFILE_BIN;
        else if (!strcasecmp("without-match", optarg)) binbehave = BINFILE_SKIP;
        else if (!strcasecmp("text", optarg)) binbehave = BINFILE_TEXT;
        else error_exit("%s %s", getstr(3), "--binary-files");
        break;
      case COLOR_OPT:
        color = NULL;
        if (!optarg || !strcasecmp("auto", optarg) ||
            !strcasecmp("tty", optarg) || !strcasecmp("if-tty", optarg)) {
          char *term = getenv("TERM");
          if (isatty(STDOUT_FILENO) && term && strcasecmp(term, "dumb"))
            color = init_color("01;31");
        } else if (!strcasecmp("always", optarg) ||
            !strcasecmp("yes", optarg) || !strcasecmp("force", optarg)) {
          color = init_color("01;31");
        } else if (strcasecmp("never", optarg) &&
            strcasecmp("none", optarg) && strcasecmp("no", optarg))
          error_exit("%s %s", getstr(3), "--color");
        break;
      case LABEL_OPT:
        label = optarg;
        break;
      case LINEBUF_OPT:
        lbflag = true;
        break;
      case R_INCLUDE_OPT:
        finclude = true;
        add_fpattern(optarg, INCL_PAT);
        break;
      case R_EXCLUDE_OPT:
        fexclude = true;
        add_fpattern(optarg, EXCL_PAT);
        break;
      case R_DINCLUDE_OPT:
        dinclude = true;
        add_dpattern(optarg, INCL_PAT);
        break;
      case R_DEXCLUDE_OPT:
        dexclude = true;
        add_dpattern(optarg, EXCL_PAT);
        break;
      case HELP_OPT:
      default:
        toys.exitval = 2;
        return;
    }
    lastc = c;
    newarg = optind != prevoptind;
    prevoptind = optind;
  }
  aargc -= optind;
  aargv += optind;

  /* Fail if we don't have any pattern */
  if (!aargc && needpattern) {
    toys.exitval = 2;
    return;
  }

  /* Process patterns from command line */
  if (aargc && needpattern) {
    add_pattern(*aargv, strlen(*aargv));
    --aargc;
    ++aargv;
  }

  fg_pattern = grep_calloc(patterns, sizeof(*fg_pattern));
  r_pattern = grep_calloc(patterns, sizeof(*r_pattern));

  if (dirbehave == DIR_RECURSE) {
    struct stat sb;
    if (!stat(*aargv, &sb) && S_ISDIR(sb.st_mode))
      Hflag = true;
  }
  /*
   * XXX: fgrepcomp() and fastcomp() are workarounds for regexec() performance.
   * Optimizations should be done there.
   */
  /* Check if cheating is allowed (always is for fgrep). */
  if (grepbehave == GREP_FIXED)
    for (i = 0; i < patterns; ++i) fgrepcomp(&fg_pattern[i], pattern[i]);
  else {
    for (i = 0; i < patterns; ++i) {
      if (fastcomp(&fg_pattern[i], pattern[i])) {
        /* Fall back to full regex library */
        if ((c = regcomp(&r_pattern[i], pattern[i], cflags))) {
          regerror(c, &r_pattern[i], re_error, RE_ERROR_BUF);
          error_exit("%s", re_error);
        }
      }
    }
  }

  if (lbflag) setlinebuf(stdout);
  if ((aargc == 0 || aargc == 1) && !Hflag) hflag = true;
  if (!aargc) exit(!procfile("-"));
  if (dirbehave == DIR_RECURSE) c = grep_tree(aargv);
  else {
    for (c = 0; aargc--; ++aargv) {
      if ((finclude || fexclude) && !file_matching(*aargv)) continue;
      c+= procfile(*aargv);
    }
  }

#ifndef WITHOUT_NLS
  catclose(catalog);
#endif

  /* Find out the correct return value according to the results and the command line option. */
  toys.exitval = (c ? (notfound ? (qflag ? 0 : 2) : 0) : (notfound ? 2 : 1));
}
#define __USE_FILE_OFFSET64
