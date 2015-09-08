/* dumpkmap.c - dumpkmap implementation.
 *
 * Copyright 2013 Madhur Verma <mad.flexi@gmail.com>
 *

USE_DUMPKMAP(NEWTOY(dumpkmap, ">0", TOYFLAG_USR|TOYFLAG_BIN))

config DUMPKMAP
	bool "dumpkmap"
	default y
	help
	  usage: dumpkmap > keymap

	  Print a binary keyboard translation table to stdout.

config BUSYBOX_CMPTBL
  bool "output similar to busybox. (USES NR_KEYS = 128)"
  default n
  depends on DUMPKMAP
*/

#define FOR_dumpkmap
#include "toys.h"

#if CFG_BUSYBOX_CMPTBL==1
# define NR_KEYS 128
# define MAX_NR_KEYMAPS 256
# define KDGKBENT 0x4B46
# define KDGKBTYPE 0x4B33
struct kbentry {
  unsigned char kb_table;
  unsigned char kb_index;
  unsigned short kb_value;
};
#else
# include <linux/kd.h>
# include <linux/keyboard.h>
#endif

static const char * const console_names[] = {
    "/dev/console",
    "/dev/tty0",
    "/dev/tty"
};

/*
 * get fd for console from various checks.
 */
static int get_console(void)
{
  int fd;
  for (fd = 2; fd >= 0; fd--) {
    int nfd, cfd;
    char ptr;

    nfd = open(console_names[fd], O_RDWR);
    if (nfd < 0 && errno == EACCES)
      nfd = open(console_names[fd], O_RDONLY);

    if (nfd < 0 && errno == EACCES)
      nfd = open(console_names[fd], O_WRONLY);

verify:
    cfd = (nfd >= 0 ? nfd : fd);
    ptr = 0;
    if (ioctl(cfd, KDGKBTYPE, &ptr) == 0)
      return cfd;
    if (nfd >= 0) {
      close(nfd);
      nfd = -1;
      goto verify;
    }
  }
  error_exit("can't open console");
  return fd;
}

void dumpkmap_main(void)
{
  struct kbentry ke;
  int i, j, fd, ret;
  char *maps = xzalloc(MAX_NR_KEYMAPS);

  fd = get_console();
  write(STDOUT_FILENO, "bkeymap", 7);

  memset(maps, 0x01, 13);
  maps[3] = maps[7] = maps[11] = 0;

  write(STDOUT_FILENO, maps, MAX_NR_KEYMAPS);

  for (i = 0; i < MAX_NR_KEYMAPS; i++) {
    if (maps[i] == 1) {
      for (j = 0; j < NR_KEYS; j++) {
        ke.kb_index = j;
        ke.kb_table = i;
        ret = ioctl(fd, KDGKBENT, &ke);
        if (ret < 0) perror_exit("IOCTL: failed with %s, %s, %p", (char*)&ke.kb_index, (char*)&ke.kb_table, &ke.kb_value);
        if (!ret) write(STDOUT_FILENO, (void*)&ke.kb_value, 2);
      }
    }
  }
  close(fd);
  free(maps);
}
