/* reboot.c - Reboot program.
 *
 * Copyright 2012 Ashwini Kumar <ak.ashwini@gmail.com>
 *
 * Not in SUSv4.

USE_REBOOT(NEWTOY(reboot, "d#<0nf", TOYFLAG_USR|TOYFLAG_BIN))

config REBOOT
  bool "reboot"
  default y
  help
    usage: reboot [-d delay] [-n] [-f]

    Reboot utility to reboot the sytem.
    -d DELAY  waits DELAY seconds before rebooting.
    -n    overrides sync() call before reboot.
    -f    Forces system REBOOT and bypasses init process.
*/

#define FOR_reboot
#include "toys.h"

#include <sys/reboot.h>
#include <err.h>
#include <signal.h>
#include <syslog.h>

GLOBALS(
  long delay_number;
)

void reboot_main(void)
{
  if (toys.optflags & FLAG_d) sleep((unsigned int)TT.delay_number);
  if ((toys.optflags & FLAG_n)?0:1) sync();
  if (toys.optflags & FLAG_f) reboot(RB_AUTOBOOT); //howto=RB_AUTOBOOT;
  else {
    kill(1,SIGTERM);
    sleep(3);
    reboot(RB_DISABLE_CAD);
  }
}
