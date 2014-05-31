/* stty.c - change and print terminal line settings
 *
 * Copyright 2012 Ashwini Kumar <ak.ashwini@gmail.com>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/stty.html

USE_STTY(NEWTOY(stty, "?F:ag", TOYFLAG_BIN))

config STTY
  bool "stty"
  default y
  help
    usage: stty [-F device] [-a] [-g] [...]

    Without arguments, prints baud rate, line discipline,
    and deviations from stty sane

    -F DEVICE   Open device instead of stdin
    -a    Print all current settings in human-readable form
    -g    Print in stty-readable form
*/

/* $NetBSD: stty.c,v 1.19 2003/08/07 09:05:42 agc Exp $ */

/*-
 * Copyright (c) 1989, 1991, 1993, 1994
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
 */

#define FOR_stty
#include "toys.h"

GLOBALS(
  char *device;
  int col;
  const char *label;
)

#include <sys/cdefs.h>
#include <limits.h>
#include <termios.h>

#ifndef _POSIX_VDISABLE
#define _POSIX_VDISABLE '\0'
#endif

#ifndef _PATH_URANDOM
#define _PATH_URANDOM   "/dev/urandom"
#endif

#define  LINELENGTH  80
struct info {
  int fd;          /* file descriptor */
  int ldisc;        /* line discipline */
  int off;        /* turn off */
  int set;        /* need set */
  int wset;        /* need window set */
  char *arg;        /* argument */
  struct termios t;      /* terminal info */
  struct winsize win;      /* window info */
};

struct cchar {
  const char *name;
  int sub;
  u_char def;
};

enum FMT { STTY_NOTSET, STTY_GFLAG, STTY_BSD, STTY_POSIX };


struct modes {
  const char *name;
  tcflag_t set;
  tcflag_t unset;
};

/*
 * The code in optlist() depends on minus options following regular
 * options, i.e. "foo" must immediately precede "-foo".
 */
const struct modes cmodes[] = {
  { "cs5",  CS5, CSIZE },
  { "cs6",  CS6, CSIZE },
  { "cs7",  CS7, CSIZE },
  { "cs8",  CS8, CSIZE },
  { "cstopb",  CSTOPB, 0 },
  { "-cstopb",  0, CSTOPB },
  { "cread",  CREAD, 0 },
  { "-cread",  0, CREAD },
  { "parenb",  PARENB, 0 },
  { "-parenb",  0, PARENB },
  { "parodd",  PARODD, 0 },
  { "-parodd",  0, PARODD },
  { "parity",  PARENB | CS7, PARODD | CSIZE },
  { "-parity",  CS8, PARODD | PARENB | CSIZE },
  { "evenp",  PARENB | CS7, PARODD | CSIZE },
  { "-evenp",  CS8, PARODD | PARENB | CSIZE },
  { "oddp",  PARENB | CS7 | PARODD, CSIZE },
  { "-oddp",  CS8, PARODD | PARENB | CSIZE },
  { "pass8",  CS8, PARODD | PARENB | CSIZE },
  { "-pass8",  PARENB | CS7, PARODD | CSIZE },
  { "hupcl",  HUPCL, 0 },
  { "-hupcl",  0, HUPCL },
  { "hup",  HUPCL, 0 },
  { "-hup",  0, HUPCL },
  { "clocal",  CLOCAL, 0 },
  { "-clocal",  0, CLOCAL },
  { "crtscts",  CRTSCTS, 0 },
  { "-crtscts",  0, CRTSCTS },
  { .name = NULL },
};

const struct modes imodes[] = {
  { "ignbrk",  IGNBRK, 0 },
  { "-ignbrk",  0, IGNBRK },
  { "brkint",  BRKINT, 0 },
  { "-brkint",  0, BRKINT },
  { "ignpar",  IGNPAR, 0 },
  { "-ignpar",  0, IGNPAR },
  { "parmrk",  PARMRK, 0 },
  { "-parmrk",  0, PARMRK },
  { "inpck",  INPCK, 0 },
  { "-inpck",  0, INPCK },
  { "istrip",  ISTRIP, 0 },
  { "-istrip",  0, ISTRIP },
  { "iutf8",  IUTF8, 0 },
  { "-iutf8",  0, IUTF8 },
  { "inlcr",  INLCR, 0 },
  { "-inlcr",  0, INLCR },
  { "igncr",  IGNCR, 0 },
  { "-igncr",  0, IGNCR },
  { "icrnl",  ICRNL, 0 },
  { "-icrnl",  0, ICRNL },
  { "ixon",  IXON, 0 },
  { "-ixon",  0, IXON },
  { "flow",  IXON, 0 },
  { "-flow",  0, IXON },
  { "ixoff",  IXOFF, 0 },
  { "-ixoff",  0, IXOFF },
  { "tandem",  IXOFF, 0 },
  { "-tandem",  0, IXOFF },
  { "iuclc",  IUCLC, 0 },
  { "-iuclc",  0, IUCLC },
  { "ixany",  IXANY, 0 },
  { "-ixany",  0, IXANY },
  { "decctlq",  0, IXANY },
  { "-decctlq",  IXANY, 0 },
  { "imaxbel",  IMAXBEL, 0 },
  { "-imaxbel",  0, IMAXBEL },
  { .name = NULL },
};

const struct modes lmodes[] = {
  { "echo",  ECHO, 0 },
  { "-echo",  0, ECHO },
  { "echoe",  ECHOE, 0 },
  { "-echoe",  0, ECHOE },
  { "crterase",  ECHOE, 0 },
  { "-crterase",  0, ECHOE },
  { "crtbs",  ECHOE, 0 },  /* crtbs not supported, close enough */
  { "-crtbs",  0, ECHOE },
  { "echok",  ECHOK, 0 },
  { "-echok",  0, ECHOK },
  { "echoke",  ECHOKE, 0 },
  { "-echoke",  0, ECHOKE },
  { "crtkill",  ECHOKE, 0 },
  { "-crtkill",  0, ECHOKE },
  { "iexten",  IEXTEN, 0 },
  { "-iexten",  0, IEXTEN },
  { "echonl",  ECHONL, 0 },
  { "-echonl",  0, ECHONL },
  { "echoctl",  ECHOCTL, 0 },
  { "-echoctl",  0, ECHOCTL },
  { "ctlecho",  ECHOCTL, 0 },
  { "-ctlecho",  0, ECHOCTL },
  { "echoprt",  ECHOPRT, 0 },
  { "-echoprt",  0, ECHOPRT },
  { "prterase",  ECHOPRT, 0 },
  { "-prterase",  0, ECHOPRT },
  { "isig",  ISIG, 0 },
  { "-isig",  0, ISIG },
  { "icanon",  ICANON, 0 },
  { "-icanon",  0, ICANON },
  { "noflsh",  NOFLSH, 0 },
  { "-noflsh",  0, NOFLSH },
  { "tostop",  TOSTOP, 0 },
  { "-tostop",  0, TOSTOP },
  { "flusho",  FLUSHO, 0 },
  { "-flusho",  0, FLUSHO },
  { "xcase",  XCASE, 0 },
  { "-xcase",  0, XCASE },
  { "pendin",  PENDIN, 0 },
  { "-pendin",  0, PENDIN },
  { "crt",  ECHOE|ECHOKE|ECHOCTL,  0},
  { "newcrt",  ECHOE|ECHOKE|ECHOCTL, ECHOK|ECHOPRT },
  { "-newcrt",  ECHOK, ECHOE|ECHOKE|ECHOCTL },
  { .name = NULL },
};

const struct modes omodes[] = {
  { "opost",  OPOST, 0 },
  { "-opost",  0, OPOST },
  { "olcuc",  OLCUC, 0 },
  { "-olcuc",  0, OLCUC },
  { "litout",  0, OPOST },
  { "-litout",  OPOST, 0 },
  { "onlcr",  ONLCR, 0 },
  { "-onlcr",  0, ONLCR },
  { "ocrnl",  OCRNL, 0 },
  { "-ocrnl",  0, OCRNL },
  { "tabs",  0, TABDLY },
  { "-tabs",  TABDLY, 0 },
  { "onocr",  ONOCR, 0 },
  { "-onocr",  0, ONOCR },
  { "onlret",  ONLRET, 0 },
  { "-onlret",  0, ONLRET },
  { "onfill",  OFILL, 0 },
  { "-onfill",  0, OFILL },
  { "ofdel",  OFDEL, 0 },
  { "-ofdel",  0, OFDEL },
  { "nl1",  NL1, 0 },
  { "nl0",  NL0, 0 },
  { "cr3",  CR3, 0 },
  { "cr2",  CR2, 0 },
  { "cr1",  CR1, 0 },
  { "cr0",  CR0, 0 },
  { "tab3",  TAB3, 0 },
  { "tab2",  TAB2, 0 },
  { "tab1",  TAB1, 0 },
  { "tab0",  TAB0, 0 },
  { "bs1",  BS1, 0 },
  { "bs0",  BS0, 0 },
  { "vt1",  VT1, 0 },
  { "vt0",  VT0, 0 },
  { "ff1",  FF1, 0 },
  { "ff0",  FF0, 0 },
  { .name = NULL },
};

/*
 * Special control characters.
 *
 * Cchars1 are the standard names, cchars2 are the old aliases.
 * The first are displayed, but both are recognized on the
 * command line.
 */
const struct cchar cchars1[] = {
  { "discard",  VDISCARD,   CDISCARD },
#ifdef VDSUSP
  { "dsusp",   VDSUSP,    CDSUSP },
#endif
  { "eof",  VEOF,    CEOF },
  { "eol",  VEOL,    CEOL },
  { "eol2",  VEOL2,    CEOL },
  { "erase",  VERASE,    CERASE },
  { "intr",  VINTR,    CINTR },
  { "kill",  VKILL,    CKILL },
  { "lnext",  VLNEXT,    CLNEXT },
  { "min",  VMIN,    CMIN },
  { "quit",  VQUIT,    CQUIT },
  { "reprint",  VREPRINT,   CREPRINT },
  { "start",  VSTART,    CSTART },
#ifdef VSTATUS
  { "status",  VSTATUS,   CSTATUS },
#endif
  { "stop",  VSTOP,    CSTOP },
  { "susp",  VSUSP,    CSUSP },
  { "time",  VTIME,    CTIME },
  { "werase",  VWERASE,  CWERASE },
  { .name = NULL },
};

const struct cchar cchars2[] = {
  { "brk",  VEOL,    CEOL },
  { "flush",  VDISCARD,   CDISCARD },
  { "rprnt",  VREPRINT,   CREPRINT },
  { .name = NULL },
};

static int c_cchar(const void *, const void *);
void  f_all(struct info *);
void  f_cbreak(struct info *);
void  f_columns(struct info *);
void  f_dec(struct info *);
void  f_ek(struct info *);
void  f_lcase(struct info *);
void  f_insane(struct info *);
void  f_ispeed(struct info *);
void  f_nl(struct info *);
void  f_ospeed(struct info *);
void  f_raw(struct info *);
void  f_rows(struct info *);
void  f_sane(struct info *);
void  f_cooked(struct info *);
void  stty_f_size(struct info *);
void  f_speed(struct info *);
void  f_ostart(struct info *);
void  f_ostop(struct info *);

static const struct key {
  const char *name;      /* name */
  void (*f)(struct info *);    /* function */
#define  F_NEEDARG  0x01      /* needs an argument */
#define  F_OFFOK    0x02      /* can turn off */
  int flags;
} keys[] = {
  { "all",  f_all,    0 },
  { "cbreak",  f_cbreak,  F_OFFOK },
  { "cols",  f_columns,  F_NEEDARG },
  { "columns",  f_columns,  F_NEEDARG },
  { "cooked",   f_cooked,    F_OFFOK },
  { "dec",  f_dec,    0 },
  { "ek",  f_ek,  0 },
  { "lcase",  f_lcase,  F_OFFOK },
  { "insane",  f_insane,  0 },
  { "ispeed",  f_ispeed,  F_NEEDARG },
  { "nl",    f_nl,    F_OFFOK },
  { "ospeed",  f_ospeed,  F_NEEDARG },
  { "raw",  f_raw,    F_OFFOK },
  { "rows",  f_rows,    F_NEEDARG },
  { "sane",  f_sane,    0 },
  { "size",  stty_f_size,    0 },
  { "speed",  f_speed,  0 },
};

static int c_key(const void *, const void *);

static void binit(const char *);
static void bput(const char *);
static const char *ccval(const struct cchar *, int);

static void usage(void)
{

  (void)fprintf(stderr, "usage: %s [-a|-g] [-F file] [options]\n", "stty");
  exit(1);
  /* NOTREACHED */
}
  
#define ARRAY_SIZE(x)   (sizeof(x) / sizeof(x[0]))
struct baud_value_map {
  unsigned short baud;
  unsigned int value;
};  

static const struct baud_value_map bauds[] = {
  {B0, 0},
  {B50, 50},
  {B75, 75},
  {B110, 110},
  {B134, 134},
  {B150, 150},
  {B200, 200},
  {B300, 300},
  {B600, 600},
  {B1200, 1200},
  {B1800, 1800},
  {B2400, 2400},
  {B4800, 4800},
  {B9600, 9600},
  {B19200, 19200},
  {B38400, 38400},
  {B57600, 57600},
  {B115200, 115200},
  {B230400, 230400},
  {B460800, 460800},
  {B921600, 921600},
};   

enum { NUM_OF_BAUDS = ARRAY_SIZE(bauds) };

/*
 * baud_to_value() converts baud specified in enum values like B600, B38400 etc.. to 
 * actual baud value.
 */
unsigned int baud_to_value(speed_t baud)
{  
  int i = 0;

  for(i = 0; i < NUM_OF_BAUDS; i++) {
    if (baud == bauds[i].baud) return bauds[i].value;
  }

  return 0;
}

/*
 * Converts speed value to baud for terminal settings
 */
speed_t value_to_baud(unsigned int value)                                                             
{
  int i = 0;

  for(i = 0; i < NUM_OF_BAUDS; i++) {
    if (value == baud_to_value(bauds[i].baud)) return bauds[i].baud;
  }

  return (speed_t) - 1;
}

/*Print the terminal settings to stdout, depending upon the argument,
 * if '-a' is specified then all the values are printed.
 * if no arguments are specified then only deviation from SANE settings is printed.
 */
void stty_print(struct termios *tp, struct winsize *wp, int ldisc, enum FMT fmt)
{
  const struct cchar *p;
  long tmp;
  u_char *cc;
  int cnt, ispeed, ospeed;
  char buf1[100];

  cnt = 0;

  /* Line speed and Descipline. */
  ispeed = cfgetispeed(tp);
  ospeed = cfgetospeed(tp);
  if (ispeed != ospeed) cnt += printf("ispeed %d baud; ospeed %d baud;", baud_to_value(ispeed), baud_to_value(ospeed));
  else cnt += printf("speed %d baud;", baud_to_value(ispeed));
  if (fmt >= STTY_BSD) cnt += printf(" %d rows; %d columns;", wp->ws_row, wp->ws_col);
  cnt += printf(" line = %u;",tp->c_line);
  if (cnt) xprintf("\n");

#define SANE_SET  1
#define SANE_UNSET  0
#define  on(f)  ((tmp&f) != 0)
// print all settings if '-a' is specified, else only deviation from 'sane'
#define put(n, f, d) \
  if (fmt >= STTY_BSD || on(f) != d) \
    bput(n + on(f)); 

  /* "local" flags */
  tmp = tp->c_lflag;
  binit("lflags");
  put("-isig", ISIG, SANE_SET);
  put("-icanon", ICANON, SANE_SET);
  put("-iexten", IEXTEN, SANE_SET);
  put("-echo", ECHO, SANE_SET);
  put("-echoe", ECHOE, SANE_SET);
  put("-echok", ECHOK, SANE_SET);
  put("-echonl", ECHONL, SANE_UNSET);
  put("-noflsh", NOFLSH, SANE_UNSET);
  put("-xcase", XCASE, SANE_UNSET);
  put("-tostop", TOSTOP, SANE_UNSET);
  put("-echoprt", ECHOPRT, SANE_UNSET);
  put("-echoctl", ECHOCTL, SANE_SET);
  put("-echoke", ECHOKE, SANE_SET);

  /* input flags */
  tmp = tp->c_iflag;
  binit("iflags");
  put("-ignbrk", IGNBRK, SANE_UNSET);
  put("-brkint", BRKINT, SANE_SET);
  put("-ignpar", IGNPAR, SANE_UNSET);
  put("-parmrk", PARMRK, SANE_UNSET);
  put("-inpck", INPCK, SANE_UNSET);
  put("-istrip", ISTRIP, SANE_UNSET);
  put("-inlcr", INLCR, SANE_UNSET);
  put("-igncr", IGNCR, SANE_UNSET);
  put("-icrnl", ICRNL, SANE_SET);
  put("-iutf8", IUTF8, SANE_UNSET);
  put("-ixon", IXON, SANE_SET);
  put("-ixoff", IXOFF, SANE_UNSET);
  put("-iuclc", IUCLC, SANE_UNSET);
  put("-ixany", IXANY, SANE_UNSET);
  put("-imaxbel", IMAXBEL, SANE_SET);

  /* output flags */
  tmp = tp->c_oflag;
  binit("oflags");
  put("-opost", OPOST, SANE_SET);
  put("-olcuc", OLCUC, SANE_UNSET);
  put("-ocrnl", OCRNL, SANE_UNSET);
  put("-onlcr", ONLCR, SANE_SET);
  put("-onocr", ONOCR, SANE_UNSET);
  put("-onlret", ONLRET, SANE_UNSET);
  put("-ofill", OFILL, SANE_UNSET);
  put("-ofdel", OFDEL, SANE_UNSET);
  switch(tmp & NLDLY) {
    case NL1:
      bput("nl1");
      break;   
    case NL0:
      if (fmt >= STTY_BSD) bput("nl0"); //this is sane setting
      break;
  }
   switch(tmp & CRDLY) {
  case CR3:
    bput("cr3");
    break;
  case CR2:
    bput("cr2");
    break;
  case CR1:
    bput("cr1");
    break;
  case CR0:
    if (fmt >= STTY_BSD) bput("cr0"); //this is sane setting.
    break;
  }

  switch(tmp & TABDLY) {
  case TAB3:
    bput("tab3");
    break;
  case TAB2:
    bput("tab2");
    break;
  case TAB1:
    bput("tab1");
    break;
  case TAB0:
    if (fmt >= STTY_BSD) bput("tab0"); //this is sane setting
    break;
  }
   
  switch(tmp & BSDLY) {
    case BS1:
      bput("bs1");
      break;   
    case BS0:
      if (fmt >= STTY_BSD) bput("bs0"); //this is sane setting
      break;
  }
  switch(tmp & VTDLY) {
    case VT1:
      bput("vt1");
      break;   
    case VT0:
      if (fmt >= STTY_BSD) bput("vt0"); //this is sane setting
      break;
  }
  switch(tmp & FFDLY) {
    case FF1:
      bput("ff1");
      break;   
    case FF0:
      if (fmt >= STTY_BSD) bput("ff0"); //this is sane setting
      break;
  }

  /* control flags (hardware state) */
  tmp = tp->c_cflag;
  binit("cflags");
  put("-parenb", PARENB, SANE_UNSET);
  put("-parodd", PARODD, SANE_UNSET);
  switch(tmp&CSIZE) {
  case CS5:
    bput("cs5");
    break;
  case CS6:
    bput("cs6");
    break;
  case CS7:
    bput("cs7");
    break;
  case CS8:
    if (fmt >= STTY_BSD) bput("cs8"); //this is sane setting
    break;
  }
  put("-hupcl", HUPCL, SANE_SET);
  put("-cstopb", CSTOPB, SANE_UNSET);
  put("-cread", CREAD, SANE_SET);
  put("-clocal", CLOCAL, SANE_UNSET);
  put("-crtscts", CRTSCTS, SANE_UNSET);

  /* special control characters */
  cc = tp->c_cc;
  binit("cchars");
  for (p = cchars1; p->name; ++p) {
    if((fmt < STTY_BSD) && (cc[p->sub] == p->def)) continue;
    (void)snprintf(buf1, sizeof(buf1), "%s = %s;",
        p->name, ccval(p, cc[p->sub]));
    bput(buf1);
  }
  for (p = cchars2; p->name; ++p) {
    if((fmt < STTY_BSD) && (cc[p->sub] == p->def)) continue;
    (void)snprintf(buf1, sizeof(buf1), "%s = %s;",
        p->name, ccval(p, cc[p->sub]));
    bput(buf1);
  }

  binit(NULL);
}

/* 
 * set the printing cursor on a new label 
 */
static void binit(const char *lb)
{

  if (TT.col) {
    xprintf("\n");
    TT.col = 0;
  }
  TT.label = lb;
}

/*
 * print the string, wrapping the line at LINELENGTH, i.e 80
 */
static void bput(const char *s)
{

  if (TT.col == 0) {
    TT.col = printf("%s", s);
    return;
  }
  
  if ((TT.col + strlen(s)) > LINELENGTH) {
    xprintf("\n");
    TT.col = printf("%s", s);
    return;
  }
  TT.col += printf(" %s", s);
}

/*
 * this function gets the control character values.
 */
static const char* ccval(const struct cchar *p, int c)
{
  static char buf[5];
  char *bp;

  if (p->sub == VMIN || p->sub == VTIME) {
    (void)snprintf(buf, sizeof(buf), "%d", c); //VMIN and VTIME values are specified in integer values.
    return (buf);
  }

  if (c == _POSIX_VDISABLE) return ("<undef>");
  bp = buf;
  if (c & 0200) {
    *bp++ = 'M';
    *bp++ = '-';
    c &= 0177;
  }
  if (c == 0177) {
    *bp++ = '^';
    *bp++ = '?';
  }
  else if (c < 040) {
    *bp++ = '^';
    *bp++ = c + '@';
  }
  else *bp++ = c;

  *bp = '\0';
  return (buf);
}




#undef CHK
#define  CHK(s)  (!strcmp(name, s))

/* 
 * search the given keyword in modes and apply the changes accordingly
 */
int msearch(char ***argvp, struct info *ip)
{
  const struct modes *mp;
  char *name;

  name = **argvp;

  for (mp = cmodes; mp->name; ++mp)
    if (CHK(mp->name)) {
      ip->t.c_cflag &= ~mp->unset;
      ip->t.c_cflag |= mp->set;
      ip->set = 1;
      return (1);
    }
  for (mp = imodes; mp->name; ++mp)
    if (CHK(mp->name)) {
      ip->t.c_iflag &= ~mp->unset;
      ip->t.c_iflag |= mp->set;
      ip->set = 1;
      return (1);
    }
  for (mp = lmodes; mp->name; ++mp)
    if (CHK(mp->name)) {
      ip->t.c_lflag &= ~mp->unset;
      ip->t.c_lflag |= mp->set;
      ip->set = 1;
      return (1);
    }
  for (mp = omodes; mp->name; ++mp)
    if (CHK(mp->name)) {
      ip->t.c_oflag &= ~mp->unset;
      ip->t.c_oflag |= mp->set;
      ip->set = 1;
      return (1);
    }
  return (0);
}




static int c_cchar(const void *a, const void *b)
{
    return (strcmp(((const struct cchar *)a)->name,
    ((const struct cchar *)b)->name));
}

/*
 * Search and apply the values for special characters
 */
int csearch(char ***argvp, struct info *ip)
{
  struct cchar *cp, tmp;
  long val;
  char *arg, *ep, *name;
    
  name = **argvp;

  tmp.name = name;
  if (!(cp = (struct cchar *)bsearch(&tmp, cchars1,
    sizeof(cchars1)/sizeof(cchars1[0]) - 1, sizeof(cchars1[0]),
    c_cchar)) &&
    !(cp = (struct cchar *)bsearch(&tmp, cchars2,
    sizeof(cchars2)/sizeof(cchars2[0]) - 1, sizeof(cchars2[0]),
    c_cchar)))
    return (0);

  arg = *++*argvp;
  if (!arg) {
    error_msg("option requires an argument -- %s", name);
    usage();
  }
#undef CHK
#define CHK(s)  (*arg == s[0] && !strcmp(arg, s))
  if (CHK("undef") || CHK("<undef>"))  ip->t.c_cc[cp->sub] = _POSIX_VDISABLE;
  else if (cp->sub == VMIN || cp->sub == VTIME) {
    val = strtol(arg, &ep, 10);
    if (val == _POSIX_VDISABLE) {
      error_msg("value of %ld would disable the option -- %s",
        val, name);
      usage();
    }
    if (val > UCHAR_MAX) {
      error_msg("maximum option value is %d -- %s",
        UCHAR_MAX, name);
      usage();
    }
    if (*ep != '\0') {
      error_msg("option requires a numeric argument -- %s", name);
      usage();
    }
    ip->t.c_cc[cp->sub] = (cc_t)val;
  } else if (arg[0] == '^')
    ip->t.c_cc[cp->sub] = (arg[1] == '?') ? 0177 :
      (arg[1] == '-') ? _POSIX_VDISABLE : arg[1] & 037;
  else
    ip->t.c_cc[cp->sub] = arg[0];
  ip->set = 1;
  return (1);
}

/*
 * print terminal settings in STTY readable format (colon ':' separated)
 */
void gprint(struct termios *tp)
{
  int i;
  xprintf("%x:%x:%x:%x",tp->c_iflag, tp->c_oflag,tp->c_cflag,tp->c_lflag);
  for( i = 0; i < NCCS; i++)
    xprintf(":%x",tp->c_cc[i]);
  xprintf("\n");
}

/*
 * Apply the STTY readable format values to terminal
 */
int gread(struct termios *tp, char *s) 
{
  unsigned long input_flag, outflag, controlflag, localflag;
  unsigned int schar;
  int count, i;
  if(sscanf(s, "%lx:%lx:%lx:%lx%n", &input_flag, &outflag, &controlflag, &localflag, &count) != 4) return 0;

  s += count;
  for(i = 0; i < NCCS; i++) {
    if(sscanf(s, ":%x%n", &schar, &count) != 1) return 0;
    tp->c_cc[i] = schar;
    s += count;
  }

  if(*s != '\0') return 0;

  tp->c_iflag = input_flag;
  tp->c_oflag = outflag;
  tp->c_cflag = controlflag;
  tp->c_lflag = localflag;

  return 1;
}

/*
 * compare key names
 */
static int c_key(const void *a, const void *b)
{

    return (strcmp(((const struct key *)a)->name,
    ((const struct key *)b)->name));
}

/*
 * Search for key mode inputs like, ispeed, ospeed, sane, etc...
 * and call the callback function for the same.
 */
int ksearch(char ***argvp, struct info *ip)
{
  char *name;
  struct key *kp, tmp;

  name = **argvp;
  if (*name == '-') {
    ip->off = 1;
    ++name;
  } else ip->off = 0;

  tmp.name = name;
  if (!(kp = (struct key *)bsearch(&tmp, keys,
    sizeof(keys)/sizeof(struct key), sizeof(struct key), c_key)))
    return (0);
  if (!(kp->flags & F_OFFOK) && ip->off) {
    error_msg("illegal option -- %s", name);
    usage();
  }
  if (kp->flags & F_NEEDARG && !(ip->arg = *++*argvp)) {
    error_msg("option requires an argument -- %s", name);
    usage();
  }
  kp->f(ip); //callback function call with arguments
  return (1);
}

void f_all(struct info *ip)
{
  stty_print(&ip->t, &ip->win, ip->ldisc, STTY_BSD);
}

void f_cbreak(struct info *ip)
{
  if (ip->off) ip->t.c_lflag |= ICANON;
  else ip->t.c_lflag &= ~ICANON;
  ip->set = 1;
}

void f_columns(struct info *ip)
{
  ip->win.ws_col = atoi(ip->arg);
  ip->wset = 1;
}

void f_dec(struct info *ip)
{
  ip->t.c_cc[VERASE] = (u_char)0177;
  ip->t.c_cc[VKILL] = CTRL('u');
  ip->t.c_cc[VINTR] = CTRL('c');
  ip->t.c_lflag &= ~ECHOPRT;
  ip->t.c_lflag |= ECHOE|ECHOKE|ECHOCTL;
  ip->t.c_iflag &= ~IXANY;
  ip->set = 1;
}

void f_ek(struct info *ip)
{
  ip->t.c_cc[VERASE] = CERASE;
  ip->t.c_cc[VKILL] = CKILL;
  ip->set = 1;
}

void f_lcase(struct info *ip)
{
  if(ip->off) {
    ip->t.c_lflag &= ~XCASE;
    ip->t.c_iflag &= ~IUCLC;
    ip->t.c_oflag &= ~OLCUC;
  }
  else {
    ip->t.c_lflag |= XCASE;
    ip->t.c_iflag |= IUCLC;
    ip->t.c_oflag |= OLCUC;
  }
  ip->set = 1;
}

void f_insane(struct info *ip)
{
  int f, r;
  
  r = f = open(_PATH_URANDOM, O_RDONLY, 0);
  if (f >= 0) {
    r = read(f, &(ip->t), sizeof(struct termios));
    close(f);
  }
  if (r < 0) {
    /* XXX not cryptographically secure! */
    
      srandom(time(NULL));
    ip->t.c_iflag = random();
    ip->t.c_oflag = random();
    ip->t.c_cflag = random();
    ip->t.c_lflag = random();
    for (f = 0; f < NCCS; f++) {
      ip->t.c_cc[f] = random() & 0xFF;
    }
    ip->t.c_ispeed = random();
    ip->t.c_ospeed = random();
  }
  
  ip->set = 1;
}

void f_ispeed(struct info *ip)
{
  cfsetispeed(&ip->t, value_to_baud((unsigned int)atolx(ip->arg)));
  ip->set = 1;
}

void f_nl(struct info *ip)
{
  if (ip->off) {
    ip->t.c_iflag |= ICRNL;
    ip->t.c_iflag &= ~(INLCR | IGNCR);
    ip->t.c_oflag |= ONLCR;
    ip->t.c_oflag &= ~(OCRNL | ONLRET);
  } else {
    ip->t.c_iflag &= ~ICRNL;
    ip->t.c_oflag &= ~ONLCR;
  }
  ip->set = 1;
}

void f_ospeed(struct info *ip)
{
  cfsetospeed(&ip->t, value_to_baud((unsigned int)atolx(ip->arg)));
  ip->set = 1;
}

void f_raw(struct info *ip)
{
  if (ip->off) {
    ip->off = 0;
    f_cooked(ip);
  }
  else {
    char *raw_val[] = {"-ignbrk", "-brkint", "-ignpar", "-parmrk", "-inpck", "-istrip",
      "-inlcr", "-igncr", "-icrnl", "-ixon", "-ixoff", "-iuclc", "-ixany",
      "-imaxbel", "-opost", "-isig", "-icanon", "-xcase", NULL}; //raw mode settings
    char **ap;
    for(ap = raw_val; *ap; ap++)                           
      msearch(&ap, ip);
    ip->t.c_cflag &= ~(CSIZE|PARENB);
    ip->t.c_cflag |= CS8;
    ip->t.c_cc[VMIN] = 1;
    ip->t.c_cc[VTIME] = 0;
    ip->set = 1;
  }
}

void f_rows(struct info *ip)
{
  ip->win.ws_row = atoi(ip->arg);
  ip->wset = 1;
}

void f_cooked(struct info *ip)
{  
  char *cooked_val[] = {"brkint", "ignpar", "istrip", 
    "icrnl", "ixon", "opost", "isig", "icanon", NULL }; //cooked mode settings
  char **ap;
  if (ip->off) {
    ip->off = 0;
    f_raw(ip);
  }
  else {
    for(ap = cooked_val; *ap; ap++)
      msearch(&ap, ip);
    if(VEOF == VMIN) ip->t.c_cc[VEOF] = CEOF;
    if(VEOL == VTIME) ip->t.c_cc[VEOL] = CEOL;
  }
  ip->set = 1;

}


void f_sane(struct info *ip)
{
  const struct cchar *p;
  char *sane_val[] = {"cread", "-ignbrk", "brkint", "-inlcr", "-igncr", "icrnl", "-ixoff", 
    "-iuclc", "-ixany", "imaxbel", "opost", "-olcuc", "-ocrnl", "onlcr",
    "-onocr", "-onlret", "-ofill", "-ofdel", "nl0", "cr0", "tab0", "bs0", "vt0",
    "ff0", "isig", "icanon", "iexten", "echo", "echoe", "echok", "-echonl",
    "-noflsh", "-xcase", "-tostop", "-echoprt", "echoctl", "echoke", NULL}; //sane settings
  char **ap;
  memset(ip->t.c_cc, 0, NCCS);
  for (p = cchars1; p->name; ++p) {
    if(p->sub == VMIN || p->sub == VTIME) continue;
    ip->t.c_cc[p->sub] = p->def;
  }
  ip->t.c_cc[VMIN] = 1;
  ip->t.c_cc[VTIME] = 0;

  for(ap = sane_val; *ap; ap++)
    msearch(&ap, ip);

  ip->set = 1;
}

void stty_f_size(struct info *ip)
{
  xprintf("%d %d\n", ip->win.ws_row, ip->win.ws_col);
}

void f_speed(struct info *ip)
{
  xprintf("%d\n", baud_to_value(cfgetospeed(&ip->t)));
}

/*
 * stty main entry point: this function will parse the command line arguments
 * and get/set the properties accordingly.
 */
void stty_main() 
{
  struct info i;
  struct termios mode;
  enum FMT fmt;

  memset(&i, 0, sizeof(struct info));
  fmt = STTY_NOTSET;
  i.fd = STDIN_FILENO;

  if(toys.optflags & FLAG_a) fmt = STTY_POSIX;
  if(toys.optflags & FLAG_g) fmt = STTY_GFLAG;
  if(toys.optflags & FLAG_F) {
    if ((i.fd = open(TT.device, O_RDONLY | O_NONBLOCK)) < 0) // Open the device file specified on cmdline
      perror_exit("%s", TT.device);
  }

  if (ioctl(i.fd, TIOCGETD, &i.ldisc) < 0) perror_exit("%s: TIOCGETD", TT.device); //get the line descipline of terminal
  if (tcgetattr(i.fd, &i.t) < 0) perror_exit("tcgetattr"); //get terminal attributes
  if (ioctl(i.fd, TIOCGWINSZ, &i.win) < 0) perror_msg("TIOCGWINSZ"); //get terminal window size

  switch(fmt) {
  case STTY_NOTSET:
    if (*toys.optargs)
      break;
    /* FALLTHROUGH */
  case STTY_BSD:
  case STTY_POSIX:
    stty_print(&i.t, &i.win, i.ldisc, fmt); //print terminal settings in human-readable format
    break;
  case STTY_GFLAG:
    gprint(&i.t); //print terminal settings in STTY-readable (: separated) format
    break;
  }
  
  for (i.set = i.wset = 0; *toys.optargs; ++toys.optargs) {
    if (ksearch(&toys.optargs, &i))  continue;

    if (csearch(&toys.optargs, &i))  continue;

    if (msearch(&toys.optargs, &i))  continue;

    if(gread(&i.t, *toys.optargs)){ //read and parse the STTY-readable format input.
      i.set = 1;
      continue;
    }

    if (isdigit((unsigned char)**toys.optargs)) { // input in digits is assumed to be speed setting
      unsigned int speed;

      speed = (unsigned int)atolx(*toys.optargs);
      if((speed = value_to_baud(speed)) != (speed_t) -1) {
        cfsetospeed(&i.t, speed);
        cfsetispeed(&i.t, speed);
        i.set = 1;
        continue;
      }
    }

    error_msg("illegal option -- %s", *toys.optargs);
    usage();
  }

  if (i.set && tcsetattr(i.fd, TCSANOW, &i.t) < 0) // set terminal attributes
    perror_exit("tcsetattr");
  if (i.wset && ioctl(i.fd, TIOCSWINSZ, &i.win) < 0) // set terminal window size.
    perror_msg("TIOCSWINSZ");
  memset(&mode, 0, sizeof(struct termios));
  tcgetattr(i.fd, &mode); //get terminal attributes, for checking the proper settings.
  if(memcmp(&mode, &i.t, sizeof(struct termios)))
    error_exit("unable to perform all requested operations");
}
