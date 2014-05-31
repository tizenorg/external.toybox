/* fgrep.c - fgrep command
 *
 * Copyright 2012 Ashwini Kumar <ak.ashwini@gmail.com>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/grep.html

USE_FGREP(NEWTOY(fgrep, NULL, TOYFLAG_USR|TOYFLAG_BIN))

config FGREP
  bool "fgrep"
  default y
  depends GREP
  help
    usage: fgrep [-H] [-h] [-n] [-l] [-L] [-c] [-o] [-q] [-v] [-s] [-r] [-i] [-w] [-m NUMBER] [-A NUMBER] [-B NUMBER] [-C NUMBER] [-e PTRN] [-f FILE]

    A grep program to find fix PATTERN - fgrep.

    Mostly used for finding in files.
*/

#define FOR_fgrep
#include "toys.h"

void fgrep_main(void)
{
  int count = 0, i;
  char **myargv = NULL;

  while(toys.argv[count++])
  {
    /*Do Nothing */
  }

  myargv = xzalloc(sizeof(char *) * (count+2)); //2 -- 1 for grep, 1 for -F
  myargv[0] = xstrdup("grep");
  myargv[1] = xstrdup("-F");

  i = 1;
  while(toys.argv[i]) //at 0th index, it is fgrep .. which is omitted
  {
    myargv[i + 1] = toys.argv[i];
    i++;
  }

  toy_exec(myargv); //execute fgre as grep -F
}
