/* egrep.c - egrep command
 *
 * Copyright 2012 Ashwini Kumar <ak.ashwini@gmail.com>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/grep.html

USE_EGREP(NEWTOY(egrep, NULL, TOYFLAG_USR|TOYFLAG_BIN))

config EGREP
  bool "egrep"
  default y
  depends GREP
  help
    usage: egrep [-H] [-h] [-n] [-l] [-L] [-c] [-o] [-q] [-v] [-s] [-r] [-i] [-w] [-m NUMBER] [-A NUMBER] [-B NUMBER] [-C NUMBER] [-e PTRN] [-f FILE]

    A grep program to find extended PATTERN - egrep.

    Mostly used for finding in files.
*/

#define FOR_egrep
#include "toys.h"

void egrep_main(void)
{
  int count = 0, i;
  char **myargv = NULL;

  while(toys.argv[count++])
  {
    /*Do Nothing */
  }

  myargv = xzalloc(sizeof(char *) * (count+2)); //2 -- 1 for grep, 1 for -F
  myargv[0] = xstrdup("grep");
  myargv[1] = xstrdup("-E");

  i = 1;
  while(toys.argv[i]) //at 0th index, it is fgrep .. which is omitted
  {
    myargv[i + 1] = toys.argv[i];
    i++;
  }

  toy_exec(myargv); //execute fgre as grep -F
}
