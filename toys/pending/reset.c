/* reset.c - A program to reset the terminal.
 *
 * Copyright 2012 VIKASH KUMAR <k.vikash@samsung.com>
 * USE_RESET(NEWTOY(reset, NULL, TOYFLAG_USR|TOYFLAG_BIN))

USE_RESET(NEWTOY(reset, NULL, TOYFLAG_USR|TOYFLAG_BIN))


config RESET
  bool "reset"
  default y
  help
    usage: reset

    A program to reset the terminal.
*/
#define FOR_reset
#include "toys.h"

void reset_main(void)
{
  char *args[3] = {"stty", "sane", NULL};

  /*  	\033c - reset the terminal with default setting
   *  	\033(B - set the G0 character set (B=US)
   *  	\033[2J - clear the whole screen
   *  	\033[0m - Reset all attributes
   */

  printf("\033c\033(B\033[0m\033[2J");
  fflush(stdout);
  /* set the terminal to sane settings */
  xexec(args);
  return;
}
