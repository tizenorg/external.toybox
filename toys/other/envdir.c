/* envdir.c - A envdir used to setup environmental variables for a service.
 *
 * Copyright 2012 Ashwini Kumar <ak.ashwini@gmail.com>
 *
 * Not in SUSv4.

USE_ENVDIR(NEWTOY(envdir, "<2", TOYFLAG_USR|TOYFLAG_BIN))

config ENVDIR
  bool "envdir"
  default y
  help
    usage: envdir [d] [child]
    
    d is a single argument. child consists of one or more arguments.
    
    envdir sets various environment variables as specified by files
    in the directory named d. It then runs child.
*/
#define _GNU_SOURCE
#define FOR_envdir
#include "toys.h"

void envdir_main(void) {
  struct dirent *pDirent;
  DIR *pDir;
  FILE *fr;
  char *str = NULL;
  char **aargv;
  int sz = 0;
  pDir = opendir(toys.argv[1]);
  if (pDir == NULL) {
    error_msg("Cannot open directory '%s'\n", toys.argv[1]);
    return;
  }

  while ((pDirent = readdir(pDir)) != NULL) {
    str = (char *) xmalloc(
        strlen(toys.argv[1]) + strlen(pDirent->d_name) + 2);
    if(str && (pDirent->d_name[0] != '.')) { //ignoring [.] & [..]
      strcpy(str, toys.argv[1]);
      strcat(str, "/");
      strcat(str, pDirent->d_name);
      if ((fr = fopen(str, "r")) != NULL) {
        if ((sz = fread(toybuf, 1, sizeof(toybuf), fr)) != 0)
        {
          *strchrnul(toybuf, '\n') = '\0';
          setenv(pDirent->d_name, toybuf, 1);
        }
        else
          unsetenv(pDirent->d_name);
        memset(toybuf, 0, sizeof(toybuf));
        fclose(fr);
        fr = NULL;
      } else
        perror_exit("%s : Unable to open the file", pDirent->d_name);
    }
    free(str);
    str = NULL;
  }
  
  closedir(pDir);
  aargv = &toys.argv[2];
  xexec(aargv);
}
