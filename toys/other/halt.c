/* halt.c - halt utility.
 *
 * Copyright 2012 Madhur Verma <mad.flexi@gmail.com>
 *
 * Not in SUSv4.

USE_HALT(NEWTOY(halt, "d#<0nfw", TOYFLAG_USR|TOYFLAG_BIN))

config HALT
  bool "halt"
  default y
  help
    usage: halt [-d delay] [-n] [-f][-w]

    halt utility to halt the sytem.
    -d DELAY  waits DELAY seconds before halt.
    -n    overrides sync() call before halt.
    -f    Forces system HALT and bypasses init process.
    -w    Logs message in system logs.
*/

#define FOR_halt
#include "toys.h"

#include <sys/reboot.h>
#include <err.h>
#include <signal.h>
#include <syslog.h>
#include <pwd.h>
#include <utmp.h>

GLOBALS(
  long delay_number;
)

/*
 * copy string from src to dest -> only number of bytes.
 */
static char *safe_strncpy(char *dst, const char *src, size_t size)
{
  if(!size) return dst;
  dst[--size] = '\0';
  return strncpy(dst, src, size);
}
/*
 * Put hostname entry in utmp file
 */
static void log_wtmp(void)
{
  struct utmp utmp;
  struct utsname uts;

  memset(&utmp, 0, sizeof(utmp));
  utmp.ut_tv.tv_sec = time(NULL);
  strcpy(utmp.ut_user, "halt");
  utmp.ut_type = RUN_LVL;
  utmp.ut_id[0] = '~'; utmp.ut_id[1] = '~';
  utmp.ut_line[0] = '~'; utmp.ut_line[1] = '~';
  uname(&uts);
  safe_strncpy(utmp.ut_host, uts.release, sizeof(utmp.ut_host));
  updwtmp("/var/log/wtmp", &utmp);
}

void halt_main(void)
{
  if (toys.optflags & FLAG_d)  sleep((unsigned int)TT.delay_number);
  if ((toys.optflags & FLAG_n)?0:1) sync();
  if (toys.optflags & FLAG_w) {
    log_wtmp();
    exit(0); 
  }
  if (toys.optflags & FLAG_f) reboot(RB_HALT_SYSTEM);
  else {
    kill(1,SIGUSR1);
    reboot(RB_POWER_OFF);
  }
}
