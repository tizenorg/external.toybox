/* fsck.c - fsck program to check and repair filesystems.
 *
 * Copyright 2013 Ashwini Kumar <ak.ashwini@gmail.com>
 *
 * Not in SUSv4.
USE_FSCK(NEWTOY(fsck, "?t:ANPRTV", TOYFLAG_SBIN))

config FSCK
  bool "fsck"
  default y
  help
    usage: fsck [-ANPRTV] [-C FD] [-t FSTYPE] [FS_OPTS] [BLOCKDEV]...

    -A  Walk /etc/fstab and check all filesystems
    -N  Don't execute, just show what would be done
    -P  With -A, check filesystems in parallel
    -R  With -A, skip the root filesystem
    -T  Don't show title on startup
    -V  Verbose
    -C n  Write status information to specified filedescriptor
    -t TYPE  List of filesystem types to check
*/

#define FOR_fsck
#include "toys.h"

GLOBALS(
  char *type;
  long prog_fd;
)

/*  $NetBSD: fsck.c,v 1.44 2006/10/16 02:44:46 christos Exp $  */

/*
 * Copyright (c) 1996 Christos Zoulas. All rights reserved.
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
 * From: @(#)mount.c  8.19 (Berkeley) 4/19/94
 * From: NetBSD: mount.c,v 1.24 1995/11/18 03:34:29 cgd Exp 
 *
 */

#include <sys/cdefs.h>

#include <sys/param.h>
#include <sys/queue.h>
#define FSTYPENAMES
#define FSCKNAMES

#include <fstab.h>
#include <paths.h>
#include <signal.h>
#include <mntent.h>

#ifndef MFSNAMELEN /* NetBSD > 4.0 */
# define MFSNAMELEN 16 /* length of fs type name, including nul */
#endif

#define  BADTYPE(mnt)              \
  ((hasmntopt(mnt, FSTAB_RO) == NULL) &&          \
    (hasmntopt(mnt, FSTAB_RW) == NULL) && \
    (hasmntopt(mnt, FSTAB_RQ) == NULL) && \
    strcmp(mnt->mnt_opts, "defaults"))

#define CHECK_PREEN 	256
#define CHECK_VERBOSE   512
#define CHECK_DEBUG     1024
#define CHECK_FORCE     2048
#define CHECK_PROGRESS  4096
#define CHECK_NOFIX     8192
#define _PATH_SBIN  "/sbin"
#define _PATH_USRSBIN   "/usr/sbin"

static enum { IN_LIST, NOT_IN_LIST } which = NOT_IN_LIST;

TAILQ_HEAD(fstypelist, entry) opthead, selhead;

struct entry {
  char *type;
  char *options;
  TAILQ_ENTRY(entry) entries;
};

struct partentry {
  TAILQ_ENTRY(partentry)   p_entries;
  char        *p_devname;  /* device name */
  char      *p_mntpt;  /* mount point */
  char        *p_type;  /* file system type */
  void      *p_auxarg;  /* auxiliary argument */
};

TAILQ_HEAD(part, partentry) badh;

struct diskentry {
  TAILQ_ENTRY(diskentry)     d_entries;
  char         *d_name;  /* disk base name */
  TAILQ_HEAD(prt, partentry)  d_part;  /* list of partitions on disk */
  int        d_pid;  /* 0 or pid of fsck proc */
};

TAILQ_HEAD(diskinfo, diskentry) diskh;

static int maxrun = 0;
static char *options = NULL;
static int flags = 0;
static int hot = 0;
static int nrun = 0, ndisks = 0;

static int selected(const char *type)
{
  struct entry *e;
  TAILQ_FOREACH(e, &selhead, entries)   /* If no type specified, it's always selected. */
  if (!strncmp(e->type, type, MFSNAMELEN)) return which == IN_LIST ? 1 : 0;
  return which == IN_LIST ? 0 : 1;
}

static void *isok(struct mntent *me)
{
  if (!(me->mnt_passno)) return NULL;
  if (BADTYPE(me)) return NULL;
  if (!selected(me->mnt_type)) return NULL;
  return me;
}

static const char *getoptions(const char *type)
{
  struct entry *e;
  TAILQ_FOREACH(e, &opthead, entries)
  if (!strncmp(e->type, type, MFSNAMELEN)) return e->options;
  return "";
}

static void catopt(char **sp, const char *o)
{
  char *s;
  size_t i, j;

  s = *sp;
  if (s) {
    i = strlen(s);
    j = i + 1 + strlen(o) + 1;
    s = xrealloc(s, j);
    (void)snprintf(s + i, j, ",%s", o);
  } else s = strdup(o);
  *sp = s;
}

static void mangle(char *opts, int *argcp, const char ** volatile *argvp, int *maxargcp)
{
  char *p, *s;
  int argc, maxargc;
  const char **argv;

  argc = *argcp;
  argv = *argvp;
  maxargc = *maxargcp;
  for (s = opts; (p = strsep(&s, ","));) {
    /* Always leave space for one more argument and the NULL. */
    if (argc >= maxargc - 3) {
      maxargc <<= 1;
      argv = xrealloc(argv, maxargc * sizeof(char *));
    }
    if (*p != '\0')  {
      if (*p == '-') {
        argv[argc++] = p;
        p = strchr(p, '=');
        if (p) {
          *p = '\0';
          argv[argc++] = p+1;
        }
      } else argv[argc++] = p;
    }
  }
  *argcp = argc;
  *argvp = argv;
  *maxargcp = maxargc;
}


static struct diskentry *finddisk(const char *name)
{
  const char *p;
  size_t len, dlen;
  struct diskentry *d;
  for (dlen = len = strlen(name), p = name + len - 1; p >= name; --p)
    if (isdigit((unsigned char)*p)) {
      len = p - name + 1;
      break;
    }
  if (p < name) len = dlen;

  TAILQ_FOREACH(d, &diskh, d_entries)
    if (!strncmp(d->d_name, name, len) && d->d_name[len] == 0)
      return d;

  d = xmalloc(sizeof(*d));
  d->d_name = strdup(name);
  d->d_name[len] = '\0';
  TAILQ_INIT(&d->d_part);
  d->d_pid = 0;

  TAILQ_INSERT_TAIL(&diskh, d, d_entries);
  ndisks++;

  return d;
}

static void printpart(void)
{
  struct diskentry *d;
  struct partentry *p;

  TAILQ_FOREACH(d, &diskh, d_entries) {
    (void) printf("disk %s:", d->d_name);
    TAILQ_FOREACH(p, &d->d_part, p_entries)
      (void) printf(" %s", p->p_devname);
    (void) printf("\n");
  }
}

static void addpart(const char *type, const char *dev, const char *mntpt, void *auxarg)
{
  struct diskentry *d = finddisk(dev);
  struct partentry *p;

  TAILQ_FOREACH(p, &d->d_part, p_entries)
    if (!strcmp(p->p_devname, dev)) {
      error_msg("%s in fstab more than once!", dev);
      return;
    }

  p = xmalloc(sizeof(*p));
  p->p_devname = strdup(dev);
  p->p_mntpt = strdup(mntpt);
  p->p_type = strdup(type);
  p->p_auxarg = auxarg;

  TAILQ_INSERT_TAIL(&d->d_part, p, p_entries);
}

static int startdisk(struct diskentry *d,
  int (*checkit)(const char *, const char *, const char *, void *, pid_t *))
{
  struct partentry *p = TAILQ_FIRST(&d->d_part);
  int rv;

  while ((rv = (*checkit)(p->p_type, p->p_devname, p->p_mntpt,
    p->p_auxarg, &d->d_pid)) && nrun > 0)
    sleep(10);

  if (!rv) nrun++;
  return rv;
}

const char *unrawname(const char *name)
{
  static char unrawbuf[MAXPATHLEN];
  const char *dp;
  struct stat stb;

  if (!(dp = strrchr(name, '/'))) return (name);
  if (stat(name, &stb) < 0) return (name);
  if (!S_ISCHR(stb.st_mode)) return (name);
  if (dp[1] != 'r') return (name);
  (void)snprintf(unrawbuf, sizeof(unrawbuf), "%.*s/%s",
    (int)(dp - name), name, dp + 2);
  return (unrawbuf);
}

const char *rawname(const char *name)
{
  static char rawbuf[MAXPATHLEN];
  const char *dp;

  if (!(dp = strrchr(name, '/'))) return (0);
  (void)snprintf(rawbuf, sizeof(rawbuf), "%.*s/r%s",
    (int)(dp - name), name, dp + 1);
  return (rawbuf);
}

const char *blockcheck(const char *origname)
{
  struct stat stslash, stblock, stchar;
  const char *newname, *raw;
  struct fstab *fsp;
  int retried = 0;


  hot = 0;
  if (stat("/", &stslash) < 0) {
    printf("Can't stat `/'\n");
    return (origname);
  }
  newname = origname;
retry:
  if (stat(newname, &stblock) < 0) {
    return (origname);
  }
  if (S_ISBLK(stblock.st_mode)) {
    if (stslash.st_dev == stblock.st_rdev)
      hot++;
    raw = rawname(newname);
    if(!raw) return (origname);
    if (stat(raw, &stchar) < 0) return (origname);
    if (S_ISCHR(stchar.st_mode)) return (raw);
    else {
      printf("%s is not a character device\n", raw);
      return (origname);
    }
  } else if (S_ISCHR(stblock.st_mode) && !retried) {
    newname = unrawname(newname);
    retried++;
    goto retry;
  } else if ((fsp = getfsfile(newname)) != 0 && !retried) {
    newname = fsp->fs_spec;
    retried++;
    goto retry;
  }
  /*
   * Not a block or character device, just return name and
   * let the user decide whether to use it.
   */
  return (origname);
}

int checkfstab(int flags, int maxrun, void *(*docheck)(struct mntent *),
  int (*checkit)(const char *, const char *, const char *, void *, pid_t *))
{
  struct diskentry *d, *nextdisk;
  struct partentry *p;
  int ret, pid, retcode, passno, sumstatus, status;
  void *auxarg;
  const char *name;

  FILE *fp;  
  struct mntent *me = (struct mntent*)xmalloc(sizeof(struct mntent));
  char evilbuf[2*PATH_MAX];

  char* path_mounts = "/etc/fstab";
  int error = 0;

  TAILQ_INIT(&badh);
  TAILQ_INIT(&diskh);

  sumstatus = 0;

  for (passno = 1; passno <= 2; passno++) {
    if (!(fp = setmntent(path_mounts, "r"))) {
      error_msg("Can't open checklist file: %s", _PATH_FSTAB);
      if(me) free(me);
      return (8);
    }
    while (getmntent_r(fp, me, evilbuf, sizeof(evilbuf))) {
      if (!(auxarg = (*docheck)(me))) continue;

      name = blockcheck(me->mnt_fsname);
      if (((flags & CHECK_PREEN) == 0 ||
            (passno == 1 && me->mnt_passno == 1)) && !(flags & 8) ){
        if (!name) {
          if (flags & CHECK_PREEN) return 8;
          else continue;
        }
        if(me->mnt_passno == 1 && flags & 4) continue;
        sumstatus = (*checkit)(me->mnt_type,
            name, me->mnt_dir, auxarg, NULL);

        if (sumstatus) {
          if ((flags & CHECK_NOFIX) == 0) return (sumstatus);
          else error |= sumstatus;
        }
      } else if(flags & 8 && 1 == passno && me->mnt_passno == 1 && !(flags & 4)) {
        sumstatus = (*checkit)(me->mnt_type,
            name, me->mnt_dir, auxarg, NULL);

        if (sumstatus) {         
          if ((flags & CHECK_NOFIX) == 0) return (sumstatus);    
          else error |= sumstatus;
        }
      } else if (passno == 2 && me->mnt_passno > 1) {
        if (!name) {
          (void) fprintf(stderr,
              "BAD DISK NAME %s\n", me->mnt_fsname);
          sumstatus |= 8;
          continue;
        }
        if(flags & 16) (*checkit)(me->mnt_type, name, me->mnt_dir, auxarg, NULL); //FLAG_N
        else addpart(me->mnt_type, name, me->mnt_dir, auxarg);
      }
    }
  }
  if(flags & 16) return 0;

  if (flags & CHECK_DEBUG) printpart();

  if (flags & CHECK_PREEN) {
    if (maxrun == 0) maxrun = ndisks;
    if (maxrun > ndisks) maxrun = ndisks;

    nextdisk = TAILQ_FIRST(&diskh);
    for (passno = 0; passno < maxrun; ++passno) {
      if ((ret = startdisk(nextdisk, checkit)) != 0) {
        if ((flags & CHECK_NOFIX) == 0) return ret;
        else error |= ret;
      }
      nextdisk = TAILQ_NEXT(nextdisk, d_entries);
    }

    while ((pid = wait(&status)) != -1) {
      TAILQ_FOREACH(d, &diskh, d_entries)
        if (d->d_pid == pid) break;

      if (!d) {
        error_msg("Unknown pid %d", pid);
        continue;
      }

      if (WIFEXITED(status)) retcode = WEXITSTATUS(status);
      else retcode = 0;

      p = TAILQ_FIRST(&d->d_part);
      if (flags & (CHECK_DEBUG|CHECK_VERBOSE))
        (void) printf("done %s: %s (%s) = 0x%x\n",
            p->p_type, p->p_devname, p->p_mntpt,
            status);

      if (WIFSIGNALED(status)) {
        (void) fprintf(stderr,
            "%s: %s (%s): EXITED WITH SIGNAL %d\n",
            p->p_type, p->p_devname, p->p_mntpt,
            WTERMSIG(status));
        retcode = 8;
      }

      TAILQ_REMOVE(&d->d_part, p, p_entries);

      if (retcode != 0) {
        TAILQ_INSERT_TAIL(&badh, p, p_entries);
        sumstatus |= retcode;
      } else {
        free(p->p_type);
        free(p->p_devname);
        free(p);
      }
      d->d_pid = 0;
      nrun--;

      if (TAILQ_FIRST(&d->d_part) == NULL) ndisks--;

      if (nextdisk == NULL) {
        if (TAILQ_FIRST(&d->d_part) != NULL) {
          if ((ret = startdisk(d, checkit)) != 0) {
            if ((flags & CHECK_NOFIX) == 0) return ret;
            else error |= ret;
          }
        }
      } else if (nrun < maxrun && nrun < ndisks) {
        for ( ;; ) {
          nextdisk = TAILQ_NEXT(nextdisk, d_entries);
          if (nextdisk == NULL) nextdisk = TAILQ_FIRST(&diskh);
          if (TAILQ_FIRST(&nextdisk->d_part)
              != NULL && nextdisk->d_pid == 0)
            break;
        }
        if ((ret = startdisk(nextdisk, checkit)) != 0) {
          if ((flags & CHECK_NOFIX) == 0) return ret;
          else error |= ret;
        }
      }
    }
  }
  if (sumstatus) {
    p = TAILQ_FIRST(&badh);
    if (p == NULL) return (sumstatus);

    (void) fprintf(stderr,
        "THE FOLLOWING FILE SYSTEM%s HAD AN %s\n\t",
        TAILQ_NEXT(p, p_entries) ? "S" : "",
        "UNEXPECTED INCONSISTENCY:");

    TAILQ_FOREACH(p, &badh, p_entries)
      (void) fprintf(stderr,
          "%s: %s (%s)%s", p->p_type, p->p_devname,
          p->p_mntpt, TAILQ_NEXT(p, p_entries) ? ", " : "\n");

    return sumstatus;
  }
  (void) endmntent(fp);
  return error;
}

static int checkfs(const char *vfstype, const char *spec, const char *mntpt, void *auxarg, pid_t *pidp)
{
  /* List of directories containing fsck_xxx subcommands. */
  static const char *edirs[] = {
    _PATH_SBIN,
    _PATH_USRSBIN,
    NULL
  };
  const char ** volatile argv, **edir;
  pid_t pid;
  int argc, i, status, maxargc;
  char *optbuf, execname[MAXPATHLEN + 1], execbase[MAXPATHLEN];
  const char *extra = getoptions("t");

  (void) &optbuf;   /* Avoid vfork clobbering */
  (void) &vfstype;

  optbuf = NULL;
  if (options) catopt(&optbuf, options);
  if (extra) catopt(&optbuf, extra);
  
  maxargc = 64;
  argv = xmalloc(sizeof(char *) * maxargc);

  (void) snprintf(execbase, sizeof(execbase), "fsck.%s", vfstype);
  argc = 0;
  argv[argc++] = execbase;
  if (optbuf) mangle(optbuf, &argc, &argv, &maxargc);
  argv[argc++] = spec;
  argv[argc] = NULL;

  if (flags & FLAG_V || flags & FLAG_N) {
    (void)printf("start %s %swait", mntpt, pidp ? "no" : "");
    for (i = 0; i < argc; i++) (void)printf(" %s ", argv[i]);
    (void)printf("\n");
  }
  if(flags & FLAG_N){
    free(argv);
    return 0;
  }
  switch (pid = vfork()) {
  case -1:        /* Error. */
    perror_msg("vfork");
    if (optbuf) free(optbuf);
    free(argv);
    return (1);
  case 0:          /* Child. */
    /* Go find an executable. */
    edir = edirs;
    do {
      (void)snprintf(execname, sizeof(execname), "%s/%s", *edir, execbase);
      execv(execname, (char * const *)(argv));
      if (errno != ENOENT) {
        if (spec) perror_msg("exec %s for %s", execname, spec);
        else perror_msg("exec %s", execname);
      }
    } while (*++edir != NULL);
    if (errno == ENOENT) {
      if (spec) perror_msg("exec %s for %s", execname, spec);
      else perror_msg("exec %s", execname);
    }
    _exit(1);
    /* NOTREACHED */
  default:        /* Parent. */
    if (optbuf) free(optbuf);
    free(argv);

    if (pidp) {
      *pidp = pid;
      return 0;
    }
    if (waitpid(pid, &status, 0) < 0) {
      perror_msg("waitpid");
      return (1);
    }
    if (WIFEXITED(status)) {
      if (WEXITSTATUS(status) != 0) return (WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
      error_msg("%s: %s", spec, strsignal(WTERMSIG(status)));
      return (1);
    }
    break;
  }
  return (0);
}

static void addentry(struct fstypelist *list, const char *type, const char *opts)
{
  struct entry *e;
  e = (struct entry*)xmalloc(sizeof(struct entry));
  e->type = strdup(type);
  e->options = strdup(opts);
  TAILQ_INSERT_TAIL(list, e, entries);
}

static void addoption(char *newoptions)
{
  struct entry *e;
  char *optstr = "t";
  TAILQ_FOREACH(e, &opthead, entries)
  if (!strncmp(e->type, optstr, MFSNAMELEN)) {
    catopt(&e->options, newoptions);
    return;
  }
  addentry(&opthead, optstr, newoptions);
}

static void maketypelist(char *fslist)
{
  char *ptr;
  if (!fslist || (fslist[0] == '\0'))
    error_exit("empty type list");
  if (fslist[0] == 'n' && fslist[1] == 'o') {
    fslist += 2;
    which = NOT_IN_LIST;
  } else which = IN_LIST;
  while ((ptr = strsep(&fslist, ",")))
    addentry(&selhead, ptr, "");
}

static struct mntent* getfstabent(const char* spec)
{
  struct mntent *mnt;
  char evilbuf[2*PATH_MAX];
  FILE* fp = NULL;
  char *path_mounts = "/etc/fstab";
  mnt = (struct mntent*)xmalloc(sizeof(struct mntent));
  if (!(fp = setmntent(path_mounts, "r"))) printf("cannot open %s\n", path_mounts);
  else {
    while (getmntent_r(fp, mnt, evilbuf, sizeof(evilbuf))) {
      if(strcmp(spec, mnt->mnt_fsname) == 0 || strcmp(spec, mnt->mnt_dir) == 0)
        return mnt;
    }
  }
  free(mnt);
  return NULL;
}

void fsck_main(void)
{
  struct mntent *me;
  int j, rval = 0, all_for_spec = 0;
  const char *vfstype = NULL;
  int dev_count = 0, count;
  char *devices[64]; //64 devices is assumed..... :TODO
  char **args_p, *arg, **dev;

  TAILQ_INIT(&selhead);
  TAILQ_INIT(&opthead);

  flags = toys.optflags;
  flags |= CHECK_PREEN;
  count = toys.optc;
  args_p = toys.optargs;

  for (j = 0; j < count; j++) {
    arg = args_p[j];
    if ((arg[0] == '/' && !all_for_spec) || strchr(arg, '=')) {
      devices[dev_count++] = arg;
      continue;
    }
    if (all_for_spec) { //add to args for specific filesystem
      addoption(arg);
      continue;
    }
    if (arg[0] == '-' && arg[1] == '-' && !arg[2]) all_for_spec = 1;
    addoption(arg);
  }
  if (flags & FLAG_t) {
    if (TAILQ_FIRST(&selhead) != NULL)
      error_exit("only one -t option may be specified.");
    maketypelist(TT.type);
    vfstype = TT.type;
  }
  /* Don't do progress meters if we're debugging. */
  if (flags & CHECK_DEBUG) flags &= ~CHECK_PROGRESS;
  /*
   * If progress meters are being used, force max parallel to 1
   * so the progress meter outputs don't interfere with one another.
   */
  if (flags & CHECK_PROGRESS) maxrun = 1;
  if (!dev_count || flags & FLAG_A ) {
    toys.exitval = checkfstab(flags, 1/*maxrun*/, isok, checkfs);
    return;
  }
  dev = devices;
  for (; dev_count--; dev++) {
    const char *spec, *type, *cp;
    int dup_free = 0;
    char  device[MAXPATHLEN];

    spec = *dev;
    cp = strrchr(spec, '/');
    if (!cp) {
      (void)snprintf(device, sizeof(device), "%s%s", _PATH_DEV, spec);
      spec = device;
    }
    if ((me = getfstabent(spec)) == NULL) {
      if (!vfstype) vfstype = "auto";
      type = xstrdup((char *)vfstype);
      dup_free = 1;
    } else {
      spec = me->mnt_fsname;
      type = me->mnt_type;
      if (BADTYPE(me)) error_exit("%s has unknown file system type.", spec);
    }
    rval |= checkfs(type, blockcheck(spec), *dev, NULL, NULL);
    if(dup_free) free((void*)type);
  }
  toys.exitval = rval;
  return;
}
