/* mesg.c - mesg implementation.
 *
 * Copyright 2013 Madhur Verma <mad.flexi@gmail.com>
 *

USE_MESG(NEWTOY(mesg, ">1", TOYFLAG_USR|TOYFLAG_BIN))

config MESG
  bool "mesg"
  default y
  help
    Usage: mesg [y|n]

    Control write access to your terminal
    y Allow write access to your terminal
    n Disallow write access to your terminal

config MESG_ONLY_GROUP
  bool "Enable writing to tty only by group, not by everybody"
  default y
  depends on MESG
*/

#define FOR_mesg
#include "toys.h"

#if CFG_MESG_ONLY_GROUP==1
#define S_GRP S_IWGRP
#else
#define S_GRP (S_IWGRP | S_IWOTH)
#endif

void mesg_main(void)
{
  struct stat std;
  mode_t mode;

  if (!isatty(STDIN_FILENO)) error_exit("not a tty");
  if(fstat(STDIN_FILENO, &std)) error_exit("can't stat %s", STDIN_FILENO);

  if(toys.optc == 1){
    if(strlen(toys.optargs[0]) > 1 || !strchr("yn", **toys.optargs)){
    toys.exithelp++;
    error_exit("Only y/n");
    }else{
      mode = (**toys.optargs == 'y') ? std.st_mode | S_GRP : std.st_mode & ~(S_IWGRP|S_IWOTH);
      if (fchmod(STDIN_FILENO, mode) != 0) perror_exit("chmod failed");
    }
  }else{
    puts((std.st_mode & (S_IWGRP|S_IWOTH)) ? "is y" : "is n");
    return;
  }
}
