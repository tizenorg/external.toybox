/* dd.c - A program to convert and copy a file.
 *
 * Copyright 2012 Ashwini Kumar <ak.ashwini@gmail.com>
 *
 * See  http://opengroup.org/onlinepubs/9699919799/utilities/dd.html

USE_DD(NEWTOY(dd, NULL, TOYFLAG_USR|TOYFLAG_BIN))

config DD
  bool "dd"
  default y
  help
    usage: dd [if=FILE] [of=FILE] [ibs=N] [obs=N] [bs=N] [count=N] [skip=N]
        [seek=N] [conv=notrunc|noerror|sync|fsync]

    Options:
    if=FILE   Read from FILE instead of stdin
    of=FILE   Write to FILE instead of stdout
    bs=N    Read and write N bytes at a time
    ibs=N     Read N bytes at a time
    obs=N     Write N bytes at a time
    count=N   Copy only N input blocks
    skip=N    Skip N input blocks
    seek=N    Skip N output blocks
    conv=notrunc  Don't truncate output file
    conv=noerror  Continue after read errors
    conv=sync   Pad blocks with zeros
    conv=fsync  Physically write data out before finishing

    Numbers may be suffixed by c (x1), w (x2), b (x512), kD (x1000), k (x1024),
    MD (x1000000), M (x1048576), GD (x1000000000) or G (x1073741824)
    Copy a file, converting and formatting according to the operands.
*/

/*  $NetBSD: dd.c,v 1.37 2004/01/17 21:00:16 dbj Exp $  */                                                            
 
/*-
 * Copyright (c) 1991, 1993, 1994
 *  The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Keith Muller of the University of California, San Diego and Lance
 * Visser of Convex Computer Corporation.
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

#define FOR_dd
#include "toys.h"

#ifdef MAX
#undef MAX
#endif
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define ISCHR     0x01    /* character device (warn on short) */
#define ISPIPE    0x02    /* pipe (not truncatable) */
#define ISTAPE    0x04    /* tape (not seekable) */
#define NOREAD    0x08    /* not readable */

#define SFX_LIST  10
/* Flags (in ddflags). */
#define C_ASCII     0x00001
#define C_BLOCK     0x00002
#define C_BS        0x00004
#define C_CBS       0x00008
#define C_COUNT     0x00010
#define C_EBCDIC    0x00020
#define C_FILES     0x00040
#define C_IBS       0x00080
#define C_IF        0x00100
#define C_LCASE     0x00200
#define C_NOERROR   0x00400
#define C_NOTRUNC   0x00800
#define C_OBS       0x01000
#define C_OF        0x02000
#define C_SEEK      0x04000
#define C_SKIP      0x08000
#define C_SWAB      0x10000
#define C_SYNC      0x20000
#define C_UCASE     0x40000
#define C_UNBLOCK   0x80000
#define C_OSYNC     0x100000
#define C_SPARSE    0x200000
#define C_FSYNC     0x400000

#define OFLAGS    (O_CREAT | (ddflags & (C_SEEK | C_NOTRUNC) ? 0 : O_TRUNC))
#define tv2mS(tv) ((tv).tv_sec * 1000LL + ((tv).tv_usec + 500) / 1000)

/* Input/output stream state. */
typedef struct {
  u_char    *db;    /* buffer address */
  u_char    *dbp;    /* current buffer I/O address */
  uint64_t  dbcnt;    /* current buffer byte count */
  int64_t    dbrcnt;    /* last read byte count */
  uint64_t  dbsz;    /* buffer size */
  u_int    flags;
  const char    *name;    /* name */
  int    fd;    /* file descriptor */
  uint64_t  offset;    /* # of blocks to skip */
} IO;

typedef struct {
  uint64_t  in_full;  /* # of full input blocks */
  uint64_t  in_part;  /* # of partial input blocks */
  uint64_t  out_full;  /* # of full output blocks */
  uint64_t  out_part;  /* # of partial output blocks */
  uint64_t  trunc;    /* # of truncated records */
  uint64_t  swab;    /* # of odd-length swab blocks */
  uint64_t  sparse;    /* # of sparse output blocks */
  uint64_t  bytes;    /* # of bytes written */
  struct timeval  start;    /* start time of dd */
} STAT;

IO in, out;                 /* input/output state */
STAT st;                    /* statistics */
void (*cfunc)(void);        /* conversion function */
uint64_t cpy_cnt ;          /* # of blocks to copy */
static off_t pending = 0;   /* pending seek if sparse */
u_int ddflags;              /* conversion options */
uint64_t cbsz;              /* conversion block size */
u_int files_cnt = 1;        /* # of files to copy */
const u_char *ctab;         /* conversion table */
sigset_t infoset;           /* a set blocking SIGINFO */

struct suffix {                                                                              
  char name[4];
  unsigned mult;
};

static const struct suffix suffixes[] = {
  { "c", 1 },
  { "w", 2 },
  { "b", 512 },    
  { "kD", 1000 },  
  { "k", 1024 },   
  { "K", 1024 },  /* compat with coreutils dd */
  { "MD", 1000000 }, 
  { "M", 1048576 },  
  { "GD", 1000000000 },
  { "G", 1073741824 }
};

static const struct conv {
  const char *name;
  u_int set, noset;
  const u_char *ctab;
} clist[] = {
  { "fsync",    C_FSYNC,    0, NULL },
  { "noerror",  C_NOERROR,  0, NULL },
  { "notrunc",  C_NOTRUNC,  0, NULL },
  { "sync",     C_SYNC,     0, NULL },
  /* If you add items to this table, be sure to add the
   * conversions to the C_BS check in the jcl routine above.
   */
};

static int find_suffix(char *arg)
{
  int i;
  for(i = 0; i < SFX_LIST; i++)
    if(strcmp(arg, suffixes[i].name) == 0) return i;
  return -1;
}

static int c_conv(const void *a, const void *b)
{
  return (strcmp(((const struct conv *)a)->name,
    ((const struct conv *)b)->name));
}

static long long strsuftoll(const char* name, const char* arg, int def, unsigned long long max)
{
  long long result;
  char *endp;
  int idx = -1;
  errno = 0;

  if(*arg == '-') error_exit("invalid number: '%s'", arg);
  result = strtoll(arg, &endp, 10);
  if(errno == ERANGE || result > max || result < def) perror_exit("invalid number '%s'",arg);
  if(*endp != '\0') {
    idx = find_suffix(endp);
    if(idx == -1) error_exit("dd: invalid number '%s'",arg);
    result = result* suffixes[idx].mult;
  }
  return result;
}

static void f_bs(char *arg)
{
  in.dbsz = out.dbsz = strsuftoll("block size", arg, 1, INT_MAX);
}

static void f_count(char *arg)
{
  cpy_cnt = strsuftoll("block count", arg, 0, LLONG_MAX);
}

static void f_ibs(char *arg)
{
  if (!(ddflags & C_BS)) in.dbsz = strsuftoll("input block size", arg, 1, INT_MAX);
}

static void f_if(char *arg)
{
  in.name = arg;
}

static void f_obs(char *arg)
{
  if (!(ddflags & C_BS)) out.dbsz = strsuftoll("output block size", arg, 1, INT_MAX);
}

static void f_of(char *arg)
{
  out.name = arg;
}

static void f_seek(char *arg)
{
  out.offset = strsuftoll("seek blocks", arg, 0, LLONG_MAX);
}

static void f_skip(char *arg)
{
  in.offset = strsuftoll("skip blocks", arg, 0, LLONG_MAX);
}

static void f_conv(char *arg)
{
  struct conv *cp, tmp;

  while (arg != NULL) {
    tmp.name = strsep(&arg, ",");
    if (!(cp = (struct conv *)bsearch(&tmp, clist,
      sizeof(clist)/sizeof(struct conv), sizeof(struct conv), c_conv)))
      error_exit("unknown conversion %s", tmp.name);
    if (ddflags & cp->noset) error_exit("%s: illegal conversion combination", tmp.name);

    ddflags |= cp->set;
    if (cp->ctab) ctab = cp->ctab;
  }
}

static const struct arg {
  const char *name;
  void (*f)(char *);
  u_int set, noset;
} args[] = {
   /* the array needs to be sorted by the first column so
  bsearch() can be used to find commands quickly */
  { "bs",     f_bs,    C_BS,    C_BS|C_OSYNC  },
  { "conv",   f_conv,  0,       0             },
  { "count",  f_count, C_COUNT, C_COUNT       },
  { "ibs",    f_ibs,   C_IBS,   C_IBS         },
  { "if",     f_if,    C_IF,    C_IF          },
  { "obs",    f_obs,   C_OBS,   C_OBS         },
  { "of",     f_of,    C_OF,    C_OF          },
  { "seek",   f_seek,  C_SEEK,  C_SEEK        },
  { "skip",   f_skip,  C_SKIP,  C_SKIP        },
};

void summary(void)
{
  char buf[100];
  int64_t mS;
  struct timeval tv;

  (void)gettimeofday(&tv, NULL);
  mS = tv2mS(tv) - tv2mS(st.start);
  if (mS == 0) mS = 1;
  /* Use snprintf(3) so that we don't reenter stdio(3). */
  (void)snprintf(buf, sizeof(buf),
    "%llu+%llu records in\n%llu+%llu records out\n",
    (unsigned long long)st.in_full,  (unsigned long long)st.in_part,
    (unsigned long long)st.out_full, (unsigned long long)st.out_part);
  (void)write(STDERR_FILENO, buf, strlen(buf));
  if (st.swab) {
    (void)snprintf(buf, sizeof(buf), "%llu odd length swab %s\n",
      (unsigned long long)st.swab,
      (st.swab == 1) ? "block" : "blocks");
    (void)write(STDERR_FILENO, buf, strlen(buf));
  }
  if (st.trunc) {
    (void)snprintf(buf, sizeof(buf), "%llu truncated %s\n",
      (unsigned long long)st.trunc,
      (st.trunc == 1) ? "block" : "blocks");
    (void)write(STDERR_FILENO, buf, strlen(buf));
  }
  if (st.sparse) {
    (void)snprintf(buf, sizeof(buf), "%llu sparse output %s\n",
      (unsigned long long)st.sparse,
      (st.sparse == 1) ? "block" : "blocks");
    (void)write(STDERR_FILENO, buf, strlen(buf));
  }
  (void)snprintf(buf, sizeof(buf),
    "%llu bytes (%sB) transferred in %lu.%03d secs (%sB/sec)\n",
    (unsigned long long) st.bytes, make_human_readable((unsigned long long)(st.bytes), 0),
    (long) (mS / 1000),
    (int) (mS % 1000),
    (make_human_readable((unsigned long long)(st.bytes * 1000LL/ mS), 0)) );
  (void)write(STDERR_FILENO, buf, strlen(buf));

  chmod(out.name, 0664);
}

void terminate(int notused)
{
  if(notused == SIGINT) exit(1);
  exit(0);
}

/* signal handler for SIGUSR1 */
void summaryx(int notused)
{
  summary();
}

static void getfdtype(IO *io)
{
  struct stat sb;

  if (fstat(io->fd, &sb)) perror_exit("cannot fstat: %s", io->name);
  if (S_ISCHR(sb.st_mode)) io->flags |= ISCHR;
  else if (lseek(io->fd, (off_t)0, SEEK_CUR) == -1 && errno == ESPIPE)
    io->flags |= ISPIPE;    /* XXX fixed in 4.4BSD */
}

/*
 * Move the parameter file descriptor to a descriptor that is outside the
 * stdio descriptor range, if necessary.  This is required to avoid
 * accidentally outputting completion or error messages into the
 * output file that were intended for the tty.
 */
static int redup_clean_fd(int fd)
{
  int newfd;

  if (fd != STDIN_FILENO && fd != STDOUT_FILENO &&
    fd != STDERR_FILENO)
    /* File descriptor is ok, return immediately. */
    return fd;

  /*
   * 3 is the first descriptor greater than STD*_FILENO.  Any
   * free descriptor valued 3 or above is acceptable...
   */
  newfd = fcntl(fd, F_DUPFD, 3);
  if (newfd < 0) perror_exit("dupfd IO");

  close(fd);
  return newfd;
}

/*
 * A protected against SIGINFO write
 */
ssize_t bwrite(int fd, const void *buf, size_t len)
{
  sigset_t oset;
  ssize_t rv;
  int oerrno;

  (void)sigprocmask(SIG_BLOCK, &infoset, &oset);
  rv = write(fd, buf, len);
  oerrno = errno;
  (void)sigprocmask(SIG_SETMASK, &oset, NULL);
  errno = oerrno;
  return (rv);
}

/*
 * Position input/output data streams before starting the copy.  Device type
 * dependent.  Seekable devices use lseek, and the rest position by reading.
 * Seeking past the end of file can cause null blocks to be written to the
 * output.
 */
void pos_in(void)
{
  int bcnt, cnt, nr, warned;
  /* If not a pipe or tape device, try to seek on it. */
  if (!(in.flags & (ISPIPE|ISTAPE))) {
    if (lseek(in.fd,
      (off_t)in.offset * (off_t)in.dbsz, SEEK_CUR) == -1) {
      perror_exit("%s: seek error", in.name);
    }
    return;
    /* NOTREACHED */
  }
  /*
   * Read the data.  If a pipe, read until satisfy the number of bytes
   * being skipped.  No differentiation for reading complete and partial
   * blocks for other devices.
   */
  for (bcnt = in.dbsz, cnt = in.offset, warned = 0; cnt;) {
    if ((nr = read(in.fd, in.db, bcnt)) > 0) {
      if (in.flags & ISPIPE) {
        if (!(bcnt -= nr)) {
          bcnt = in.dbsz;
          --cnt;
        }
      } else --cnt;
      continue;
    }
    if (nr == 0) {
      if (files_cnt > 1) {
        --files_cnt;
        continue;
      }
      error_exit("skip reached end of input");
    }
    /*
     * Input error -- either EOF with no more files, or I/O error.
     * If noerror not set die.  POSIX requires that the warning
     * message be followed by an I/O display.
     */
    if (ddflags & C_NOERROR) {
      if (!warned) {
        error_msg("%s: error occurred", in.name);
        warned = 1;
        summary();
      }
      continue;
    }
    perror_exit("%s: read error", in.name);
  }
}

void pos_out(void)
{
  int cnt, n;
  /*
   * If not a tape, try seeking on the file.  Seeking on a pipe is
   * going to fail, but don't protect the user -- they shouldn't
   * have specified the seek operand.
   */
  if (!(out.flags & ISTAPE)) {
    if (lseek(out.fd,
      (off_t)out.offset * (off_t)out.dbsz, SEEK_SET) == -1) {
      perror_exit("%s: seek error", out.name);
    }
    return;
  }
  /* Read it. */
  for (cnt = 0; cnt < out.offset; ++cnt) {
    if ((n = read(out.fd, out.db, out.dbsz)) > 0)
      continue;

    if (n < 0) perror_exit("%s: cannot position by reading", out.name);
    /*
     * If reach EOF, fill with NUL characters; first, back up over
     * the EOF mark.  Note, cnt has not yet been incremented, so
     * the EOF read does not count as a seek'd block.
     */
    while (cnt++ < out.offset)
      if ((n = bwrite(out.fd, out.db, out.dbsz)) != out.dbsz)
        perror_exit("%s: cannot position by writing", out.name);
    break;
  }
}

static void setup(void)
{
  if (in.name == NULL) {
    in.name = "stdin";
    in.fd = STDIN_FILENO;
  } else {
    in.fd = open(in.name, O_RDONLY, 0);
    if (in.fd < 0) perror_exit("%s: cannot open for read", in.name);
    /* Ensure in.fd is outside the stdio descriptor range */
    in.fd = redup_clean_fd(in.fd);
  }
  getfdtype(&in);

  if (files_cnt > 1 && !(in.flags & ISTAPE))
    error_exit("files is not supported for non-tape devices");

  if (out.name == NULL) {
    /* No way to check for read access here. */
    out.fd = STDOUT_FILENO;
    out.name = "stdout";
  } else {
    out.fd = open(out.name, O_RDWR | OFLAGS /*, DEFFILEMODE */);
    /*
     * May not have read access, so try again with write only.
     * Without read we may have a problem if output also does
     * not support seeks.
     */
    if (out.fd < 0) {
      out.fd = open(out.name, O_WRONLY | OFLAGS /*, DEFFILEMODE */);
      out.flags |= NOREAD;
    }
    if (out.fd < 0) perror_exit("%s: cannot open for write", out.name);

    /* Ensure out.fd is outside the stdio descriptor range */
    out.fd = redup_clean_fd(out.fd);
  }
  getfdtype(&out);
  /*
   * Allocate space for the input and output buffers.  If not doing
   * record oriented I/O, only need a single buffer.
   */
  if (!(ddflags & (C_BLOCK|C_UNBLOCK))) {
    in.db = xmalloc(MAX(out.dbsz, in.dbsz));
    out.db = in.db;
  } else {
    in.db = xmalloc((u_int)(MAX(in.dbsz, cbsz) + cbsz));
    out.db = xmalloc((u_int)(out.dbsz + cbsz));
  }
  in.dbp = in.db;
  out.dbp = out.db;
  /* Position the input/output streams. */
  if (in.offset) pos_in();
  if (out.offset) pos_out();
  /*
   * Truncate the output file; ignore errors because it fails on some
   * kinds of output files, tapes, for example.
   */
  if ((ddflags & (C_OF | C_SEEK | C_NOTRUNC)) == (C_OF | C_SEEK))
    (void)ftruncate(out.fd, (off_t)out.offset * out.dbsz);
  /*
   * If converting case at the same time as another conversion, build a
   * table that does both at once.  If just converting case, use the
   * built-in tables.
   */
  (void)gettimeofday(&st.start, NULL);  /* Statistics timestamp. */
}

void dd_out(int force)
{
  static int warned;
  int64_t cnt, n, nw = 0;
  u_char *outp;
  /*
   * Write one or more blocks out.  The common case is writing a full
   * output block in a single write; increment the full block stats.
   * Otherwise, we're into partial block writes.  If a partial write,
   * and it's a character device, just warn.  If a tape device, quit.
   *
   * The partial writes represent two cases.  1: Where the input block
   * was less than expected so the output block was less than expected.
   * 2: Where the input block was the right size but we were forced to
   * write the block in multiple chunks.  The original versions of dd(1)
   * never wrote a block in more than a single write, so the latter case
   * never happened.
   *
   * One special case is if we're forced to do the write -- in that case
   * we play games with the buffer size, and it's usually a partial write.
   */
  outp = out.db;
  for (n = force ? out.dbcnt : out.dbsz;; n = out.dbsz) {
    for (cnt = n;; cnt -= nw) {
      if (!force && ddflags & C_SPARSE) {
        int sparse, i;
        sparse = 1;  /* Is buffer sparse? */
        for (i = 0; i < cnt; i++)
          if (outp[i] != 0) {
            sparse = 0;
            break;
          }
        if (sparse) {
          pending += cnt;
          outp += cnt;
          nw = 0;
          break;
        }
      }
      if (pending != 0)
        if (lseek(out.fd, pending, SEEK_CUR) == -1)
          perror_exit("%s: seek error creating sparse file", out.name);
      nw = bwrite(out.fd, outp, cnt);
      if (nw <= 0) {
        if (nw == 0) error_exit("%s: end of device", out.name);
        if (errno != EINTR) perror_exit("%s: write error", out.name);
        nw = 0;
      }
      if (pending) {
        st.bytes += pending;
        st.sparse += pending/out.dbsz;
        st.out_full += pending/out.dbsz;
        pending = 0;
      }
      outp += nw;
      st.bytes += nw;
      if (nw == n) {
        if (n != out.dbsz) ++st.out_part;
        else ++st.out_full;
        break;
      }
      ++st.out_part;
      if (nw == cnt) break;
      if (out.flags & ISCHR && !warned) {
        warned = 1;
        error_msg("%s: short write on character device", out.name);
      }
      if (out.flags & ISTAPE)
        error_exit("%s: short write on tape device", out.name);
    }
    if ((out.dbcnt -= n) < out.dbsz) break;
  }
  /* Reassemble the output block. */
  if (out.dbcnt) (void)memmove(out.db, out.dbp - out.dbcnt, out.dbcnt);
  out.dbp = out.db + out.dbcnt;
}

static void dd_in(void)
{
  int flags;
  int64_t n;

  for (flags = ddflags;;) {
    if ((flags & C_COUNT) && (st.in_full + st.in_part) >= cpy_cnt)
      return;
    /*
     * Clear the buffer first if doing "sync" on input.
     * If doing block operations use spaces.  This will
     * affect not only the C_NOERROR case, but also the
     * last partial input block which should be padded
     * with zero and not garbage.
     */
    if (flags & C_SYNC) {
      if (flags & (C_BLOCK|C_UNBLOCK)) memset(in.dbp, ' ', in.dbsz);
      else memset(in.dbp, 0, in.dbsz);
    }
    n = read(in.fd, in.dbp, in.dbsz);
    if (n == 0) {
      in.dbrcnt = 0;
      return;
    }
    /* Read error. */
    if (n < 0) {
      /*
       * If noerror not specified, die.  POSIX requires that
       * the warning message be followed by an I/O display.
       */
      perror_msg("%s: read error", in.name);
      if (!(flags & C_NOERROR)) exit(1);

      summary();
      /*
       * If it's not a tape drive or a pipe, seek past the
       * error.  If your OS doesn't do the right thing for
       * raw disks this section should be modified to re-read
       * in sector size chunks.
       */
      if (!(in.flags & (ISPIPE|ISTAPE)) &&
        lseek(in.fd, (off_t)in.dbsz, SEEK_CUR))
        fprintf(stderr, "%s: seek error: %s\n", in.name, strerror(errno));

      /* If sync not specified, omit block and continue. */
      if (!(ddflags & C_SYNC)) continue;

      /* Read errors count as full blocks. */
      in.dbcnt += in.dbrcnt = in.dbsz;
      ++st.in_full;

    /* Handle full input blocks. */
    } else if (n == in.dbsz) {
      in.dbcnt += in.dbrcnt = n;
      ++st.in_full;

    /* Handle partial input blocks. */
    } else {
      /* If sync, use the entire block. */
      if (ddflags & C_SYNC) in.dbcnt += in.dbrcnt = in.dbsz;
      else in.dbcnt += in.dbrcnt = n;
      ++st.in_part;
    }
    /*
     * POSIX states that if bs is set and no other conversions
     * than noerror, notrunc or sync are specified, the block
     * is output without buffering as it is read.
     */
    if (ddflags & C_BS) {
      out.dbcnt = in.dbcnt;
      dd_out(1);
      in.dbcnt = 0;
      continue;
    }
    in.dbp += in.dbrcnt;
    (*cfunc)();
  }
}

void def_close(void)
{
  /* Just update the count, everything is already in the buffer. */
  if (in.dbcnt) out.dbcnt = in.dbcnt;
}

/*
 * def --
 * Copy input to output.  Input is buffered until reaches obs, and then
 * output until less than obs remains.  Only a single buffer is used.
 * Worst case buffer calculation is (ibs + obs - 1).
 */
void def(void)
{
  uint64_t cnt;
  u_char *inp;
  const u_char *t;

  if ((t = ctab) != NULL)
    for (inp = in.dbp - (cnt = in.dbrcnt); cnt--; ++inp)
      *inp = t[*inp];

  /* Make the output buffer look right. */
  out.dbp = in.dbp;
  out.dbcnt = in.dbcnt;

  if (in.dbcnt >= out.dbsz) {
    /* If the output buffer is full, write it. */
    dd_out(0);
    /*
     * Ddout copies the leftover output to the beginning of
     * the buffer and resets the output buffer.  Reset the
     * input buffer to match it.
      */
    in.dbp = out.dbp;
    in.dbcnt = out.dbcnt;
  }
}

/*
 * Cleanup any remaining I/O and flush output.  If necesssary, output file
 * is truncated.
 */
static void dd_close(void)
{
  if (cfunc == def) def_close();
  /* If there are pending sparse blocks, make sure
   * to write out the final block un-sparse
   */
  if ((out.dbcnt == 0) && pending) {
    memset(out.db, 0, out.dbsz);
    out.dbcnt = out.dbsz;
    out.dbp = out.db + out.dbcnt;
    pending -= out.dbsz;
  }
  if (out.dbcnt) dd_out(1);
  /*
   * Reporting nfs write error may be defered until next
   * write(2) or close(2) system call.  So, we need to do an
   * extra check.  If an output is stdout, the file structure
   * may be shared among with other processes and close(2) just
   * decreases the reference count.
   */
  if (out.fd == STDOUT_FILENO && fsync(out.fd) == -1 && errno != EINVAL) {
    perror_exit("fsync stdout");
    /* NOTREACHED */
  }
  if(ddflags & C_FSYNC)
    if(fsync(out.fd) < 0) perror_exit("fsync failed for %s", out.name);

  if (close(out.fd) == -1) perror_exit("close");
}

static int c_arg(const void *a, const void *b)
{
  return (strcmp(((const struct arg *)a)->name,
    ((const struct arg *)b)->name));
}

/*
 * args -- parse JCL syntax of dd.
 */
void jcl(char **argv)
{
  struct arg *ap, tmp;
  char *oper, *arg;

  in.dbsz = out.dbsz = 512;

  while ((oper = *++argv) != NULL) {
    if ((arg = strchr(oper, '=')) == NULL) 
      error_exit("unknown operand %s", oper);

    *arg++ = '\0';
    if (!*arg) {
      toys.exithelp = 1;
      error_exit("");
    }
    tmp.name = oper;
    if (!(ap = (struct arg *)bsearch(&tmp, args,
            sizeof(args)/sizeof(struct arg), sizeof(struct arg), c_arg)))
      error_exit("unknown operand %s", tmp.name);
    if (ddflags & ap->noset)
      error_exit("%s: illegal argument combination or already set", tmp.name);

    ddflags |= ap->set;
    ap->f(arg);
  }
  /* Final sanity checks. */
  if (ddflags & C_BS) {
    /*
     * Bs is turned off by any conversion -- we assume the user
     * just wanted to set both the input and output block sizes
     * and didn't want the bs semantics, so we don't warn.
     */
    if (ddflags & (C_BLOCK | C_LCASE | C_SWAB | C_UCASE |
          C_UNBLOCK | C_OSYNC | C_ASCII | C_EBCDIC | C_SPARSE)) {
      ddflags &= ~C_BS;
      ddflags |= C_IBS|C_OBS;
    }
    /* Bs supersedes ibs and obs. */
    if ((ddflags & C_BS) && (ddflags & (C_IBS|C_OBS)))
      xprintf("bs supersedes ibs and obs\n");
  }
  cfunc = def;
}

int do_dd(int argc, char *argv[])
{
  int ch;

  while ((ch = getopt(argc, argv, "")) != -1) {
    switch (ch) {
    default:
      error_exit("usage: dd [operand ...]\n");
      /* NOTREACHED */
    }
  }
  argc -= (optind - 1);
  argv += (optind - 1);

  jcl(argv);
  setup();

  (void)signal(SIGUSR1, summaryx);
  (void)signal(SIGINT, terminate);
  (void)sigemptyset(&infoset);
  (void)sigaddset(&infoset, SIGUSR1);
  (void)atexit(summary);

  while (files_cnt--) dd_in();

  dd_close();
  return 0;
}

void dd_main(void)
{
  int count = 0;
  while(toys.argv[count]) count++;

  /*Initialize global vars to 0 */
  memset((void *)&in, 0, sizeof(IO));
  memset((void *)&out, 0, sizeof(IO));
  memset((void *)&st, 0, sizeof(STAT));

  toys.exitval = do_dd(count, toys.argv);
}
