/* vi: set sw=4 ts=4 : */
/* e2fs.c - Linux second extended file system.
 *
 * Copyright 2013 Ranjan Kumar <ranjankumar.bth@gmail.com>
 */

#include "toys.h"

/*
 * Get file version on a Linux second extended file system.
 */
int get_e2fs_version(const int fd, unsigned long *version)
{
  return (ioctl(fd, EXT2_IOC_GETVERSION, (void*)version));
}

/*
 * Set file version on a Linux second extended file system.
 */
int set_e2fs_version(const int fd, unsigned long version)
{
  return (ioctl(fd, EXT2_IOC_SETVERSION, (void*)&version));
}

/*
 * Get the file flags on a Linux second extended file system.
 */
int get_e2fs_flag(const int fd, struct stat *sb, unsigned long *flagval)
{
  int status = -1;
  if(!S_ISREG(sb->st_mode) && !S_ISDIR(sb->st_mode)) {
    errno = EOPNOTSUPP;
    return -1;
  }
  status = ioctl(fd, EXT2_IOC_GETFLAGS, (void*)flagval);
  return status;
}

/*
 * Set the file flags on a Linux second extended file system.
 */
int set_e2fs_flag(const int fd, struct stat *sb, unsigned long flagval)
{
  int status = -1;
  if(!S_ISREG(sb->st_mode) && !S_ISDIR(sb->st_mode)) {
    errno = EOPNOTSUPP;
    return -1;
  }
  status = ioctl(fd, EXT2_IOC_SETFLAGS, (void*)&flagval);
  return status;
}
