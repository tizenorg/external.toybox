/* cttyhack.c - Program to get the controlling TTY
 *
 * Copyright 2012 Ashwini Kumar <ak.ashwini@gmail.com>
 *
 * Not in SUSv4.

USE_CTTYHACK(NEWTOY(cttyhack, NULL, TOYFLAG_BIN))

config CTTYHACK
  bool "cttyhack"
  default y
  help
    usage: cttyhack PROG ARGS

    cttyhack will try to get a controlling terminal and execute the PROG.
*/

#define FOR_cttyhack
#include "toys.h"
#include <linux/vt.h>
#include <linux/serial.h>


void cttyhack_main(void)
{
  int fd;
  char terminal[88] = {0,};
  char buf[80];
  struct vt_stat vstate;
  struct serial_struct serial;
  int s = 0;

  if ((fd = open("/dev/tty", O_RDWR)) >= 0) {
      xstrncpy(terminal, "/dev/tty", sizeof("/dev/tty"));
      xclose(fd);
  }
  else {
    do {
      if ((fd = open("/sys/class/tty/console/active", O_RDONLY)) >= 0) {
        if ((s = read(fd, buf, 80)) > 0) {
          buf[s] = '\0'; //cause the name will have \n at the end.
          sprintf(terminal, "/dev/%s", buf);
        }
        xclose(fd);
        break;
      }
      if (ioctl(0, VT_GETSTATE, &vstate) == 0) {
        sprintf(terminal, "/dev/ttyS%d", vstate.v_active);
        break;
      }
      if (ioctl(0, TIOCGSERIAL, &serial) == 0) {
        sprintf(terminal, "/dev/ttyS%d", serial.line);
        break;
      }
      goto execute;
    } while(0);

    if ((fd = open(terminal, O_RDWR)) < 0) goto execute;

    dup2(fd, 0);
    dup2(fd, 1);
    dup2(fd, 2);
    while(fd > 2) {
      close(fd);
      fd--;
    }
    //set the terminal as Controlling terminal
    ioctl(0, TIOCSCTTY, 1);
  }

execute:
  if (toys.optargs[0]) {
    int i = 0;
    errno = 0;
    char **arg = xzalloc((toys.optc+1) * sizeof(char*));
    for(i = 0; i < toys.optc; i++)
      arg[i] = xstrdup(toys.optargs[i]);
    execvp(arg[0], arg);
    if(errno) error_exit("can't execute %s", arg[0]);
  } else {
    if(terminal[0] == '\0') error_exit(NULL);
    xprintf("%s\n", terminal);
  }
}
