/* fsync.c - Synchronize a file's in-core state with storage device.
 *
 * Copyright 2013 Ranjan Kumar <ranjankumar.bth@gmail.com>

USE_FSYNC(NEWTOY(fsync, "<1d", TOYFLAG_BIN))

config FSYNC
  bool "fsync"
  default y
  help
    usage: fsync [d] [FILE...]

    Synchronize a file's in-core state with storage device.

    -d Avoid syncing metadata.
*/

#define FOR_fsync

#include "toys.h"

/*
 * fsync main function.
 */
void fsync_main(void)
{
  for(; *toys.optargs; toys.optargs++) {
    int fd;
#ifdef O_NOATIME
    if( (fd = open(*toys.optargs, O_RDONLY | O_NOATIME | O_NOCTTY )) == -1) {
#else
    if( (fd = open(*toys.optargs, O_RDONLY | O_NOCTTY )) == -1) {
#endif
      perror_msg("can't open '%s'", *toys.optargs);
      continue;
    }
    if( ((toys.optflags & FLAG_d) ? fdatasync(fd) : fsync(fd)) == -1)
      perror_msg("can't Sync '%s'", *toys.optargs);
	  xclose(fd);
  }
  return;
}
