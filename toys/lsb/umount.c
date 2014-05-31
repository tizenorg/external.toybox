/* umount.c -  umount filesystems.
 *
 * Copyright 2012 Ashwini Kumar <ak.ashwini@gmail.com>
 *
 * Not in SUSv4.

USE_UMOUNT(NEWTOY(umount, "varlfdt:", TOYFLAG_BIN))

config UMOUNT
  bool "umount"
  default y
  help
    usage: umount [-varlfd] [-t fstypes] [dir...]

    -a  Unmount all file systems
    -v  Verbose mode
    -r  Try to remount devices as read-only if mount is busy
    -l  Lazy umount (detach filesystem)
    -f  Force umount (i.e., unreachable NFS server)
    -d  Free loop device if it has been used
    -t FSTYPE Filesystem types to be acted upon (comma separated)

    unmount the filesystem.
*/

#define FOR_umount
#include "toys.h"
#include <sys/param.h>
#include <linux/loop.h>

GLOBALS(
    char *types;
    struct mtab_list *mnts;
    int fflag;
)

#define LOOPDEV_MAXLEN 64
typedef enum { MNTANY, MNTON, MNTFROM } mntwhat;

static int checkvfsname(char *vfs_name, char **vfs_list)
{   
  int skipvfs = 0;
  if (!vfs_list) return skipvfs;
  while (*vfs_list) {
    if (*vfs_list[0] == 'n' && *vfs_list[1] == 'o') skipvfs = 1;  
    if (!strcmp(vfs_name, *vfs_list)) return (skipvfs);
    ++vfs_list;
  }
  return (!skipvfs);
}

static char **makevfslist(char *fslist)
{   
  char **av;
  size_t i;
  char *nextcp, *fsl;

  if (!fslist) return NULL;

  fsl = xstrdup(fslist);
  for (i = 0, nextcp = fsl; *nextcp; nextcp++)
    if (*nextcp == ',') i++;

  av = xmalloc((i + 2) * sizeof(char *));
  nextcp = fsl;
  i = 0;
  av[i++] = nextcp;
  while ((nextcp = strchr(nextcp, ','))) {
    *nextcp++ = '\0';
    av[i++] = nextcp;
  }
  av[i++] = NULL;
  return av;
}   

static int is_loop(char *dev)
{        
  struct stat st;

  if (!stat(dev, &st) && S_ISBLK(st.st_mode)
      && (major(st.st_rdev) == 7)) return 1;

  return 0;  
}        

static int is_loop_mount(char* path, char *loopdev)
{        
  struct mtab_list *mnts = NULL;
  struct mtab_list *mntlist;

  mnts = TT.mnts;
  if (!mnts) {
    perror_msg("getmntinfo");
    return 0;
  }
  for (mntlist = mnts; mntlist; mntlist = mntlist->next) {
    if (is_loop(mntlist->device) && !strcmp(path, mntlist->dir)) {
      strncpy(loopdev, mntlist->device, LOOPDEV_MAXLEN);
      loopdev[LOOPDEV_MAXLEN-1] = '\0';
      return 1;
    }
  }
  return 0;
}

static char *getmntname(char *name, mntwhat what)
{
  struct mtab_list *mntlist, *mnts = NULL;

  mnts = TT.mnts;
  if (!mnts) {
    perror_msg("getmntinfo");
    return (NULL);
  }
  for (mntlist = mnts; mntlist; mntlist = mntlist->next) {
    if (!strcmp(mntlist->device, name)
        || !strcmp(mntlist->dir, name))
      return ((what == MNTON)? mntlist->dir: mntlist->device);
  }
  return (NULL);
}

//  delete the setup loop device
static int delete_loopdev(char *loopdev)
{
  int loop_fd = open(loopdev, O_RDONLY);
  if (loop_fd < 0) {
    perror_msg("%s: open loop device failed", loopdev);
    return 1;
  }  
  ioctl(loop_fd, LOOP_CLR_FD, 0);
  xclose(loop_fd);
  return 0;
} 

static int umountfs(char *name, int raw)
{
  char *mntpt, rname[MAXPATHLEN], *dev = NULL, loopdev[LOOPDEV_MAXLEN];
  int loop, status;

  if (raw) mntpt = name;
  else {
    if (realpath(name, rname)) name = rname;
    if (!(mntpt = getmntname(name, MNTON))) {
      error_msg("%s: not currently mounted", name);
      return (1);
    }
  }

  if (toys.optflags & FLAG_v) xprintf("unmount %s\n", mntpt);

  loop = is_loop_mount(mntpt, loopdev);
  status = umount(mntpt);

  if (status && (TT.fflag)) status = umount2(mntpt, TT.fflag);

  if (status) {
    if (errno == EBUSY && toys.optflags & FLAG_r) {
      //remount as read-only
      if ((dev = getmntname(mntpt, MNTFROM))) {
        if (mount(dev, mntpt, NULL, MS_REMOUNT|MS_RDONLY, NULL))
          perror_msg("Can't remount %s read-only",dev);
        else {
          printf("%s busy - remounted read-only\n",dev);
          return 0;
        }
      }
    }
    else perror_msg("can't umount %s",mntpt);
    return 1;
  }
  if ((toys.optflags & FLAG_d) && loop) return delete_loopdev(loopdev);
  return 0;
}


void umount_main()
{
  int errs = 0, raw = 0, i =0;
  struct mtab_list *mntlist, *mnts;
  char **typelist = NULL;

  /* Start disks transferring immediately. */
  sync();

  if (toys.optflags & FLAG_f) TT.fflag = MNT_FORCE;
  if (toys.optflags & FLAG_l) TT.fflag = MNT_DETACH;

  mnts = xgetmountlist();
  TT.mnts = mnts;

  if (toys.optflags & FLAG_t) typelist = makevfslist(TT.types);

  if (toys.optflags & FLAG_a) {
    if (!mnts) {
      perror_msg("getmntinfo");
      errs = 1;
    }
    for (errs = 0, mntlist = mnts; mntlist; mntlist = mntlist->next) {
      if (checkvfsname(mntlist->type, typelist))
        continue;
      errs = umountfs(mntlist->dir, 1);
    }
  } else if (!toys.optc) {
    toys.exithelp = 1;
    error_exit("");
  } else {
    for (errs = 0, i = 0; toys.optargs[i]; i++)
      errs = umountfs(toys.optargs[i], raw);
  }
  toys.exitval = errs;
}
