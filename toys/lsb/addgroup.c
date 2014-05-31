/* addgroup.c - create a new group
 *
 * Copyright 2013 Ashwini Kumar <ak.ashwini@gmail.com>
 *
 * See http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/groupadd.html

USE_ADDGROUP(NEWTOY(addgroup, "<1>2g#<0S", TOYFLAG_NEEDROOT|TOYFLAG_SBIN))

config ADDGROUP
  bool "addgroup"
  default y
  help
    usage: addgroup [-g GID] [USER] GROUP

    Add a group or add a user to a group
    
      -g GID  Group id
      -S      Create a system group
*/

#define FOR_addgroup
#include "toys.h"

#define GROUP_PATH        "/etc/group"
#define SECURE_GROUP_PATH "/etc/gshadow"

GLOBALS(
  long gid;
)

/* Add a new group to the system, if GID is given then that is validated
 * to be free, else a free GID is choosen by self.
 * SYSTEM IDs are considered in the range 100 ... 999
 * update_group(), updates the entries in /etc/group, /etc/gshadow files
 */
static void new_group()
{
  struct group grp;
  int max = INT_MAX;

  grp.gr_name = *toys.optargs;
  grp.gr_passwd = (char *)"x";
  
  if(toys.optflags & FLAG_g) {
    if(TT.gid > INT_MAX) error_exit("gid should be less than  '%d' ", INT_MAX);
    if(getgrgid(TT.gid)) error_exit("group '%ld' is in use", TT.gid);
    grp.gr_gid = TT.gid;
  } else {
    if(toys.optflags & FLAG_S) {
      TT.gid = SYS_FIRST_ID;
      max = SYS_LAST_ID;
    } else {
      TT.gid = SYS_LAST_ID + 1; //i.e. starting from 1000
      max = 60000; // as per config file on Linux desktop
    }
    //find unused gid
    while(TT.gid <= max) {
      if(!getgrgid(TT.gid)) break;
      if(TT.gid == max) error_exit("no more free gids left");
      TT.gid++;
    }
    grp.gr_gid = TT.gid;
  }
  grp.gr_mem = NULL;

  update_group(&grp, GROUP_PATH);
  grp.gr_passwd = (char *)"!!";
  update_group(&grp, SECURE_GROUP_PATH); //update the /etc/gshadow file
}

void addgroup_main(void)
{
  struct group *grp = NULL;
  if(toys.optflags && toys.optc == 2) {
    toys.exithelp = 1;
    error_exit("options and user, group can't be together");
  }

  if(toys.optc == 2) {
    //add user to group
    //toys.optargs[0]- user, toys.optargs[1] - group
    if(!getpwnam(toys.optargs[0])) error_exit("user '%s' does not exist", toys.optargs[0]);
    if(!(grp = getgrnam(toys.optargs[1]))) error_exit("group '%s' does not exist", toys.optargs[1]);
    if(!grp->gr_mem) {
      grp->gr_mem = xzalloc(2 * sizeof(char*));
      grp->gr_mem[0] = toys.optargs[0];
    } else {
      int i = 0, j = 0;
      char **mem = NULL;
      while(grp->gr_mem[i]) {
        if(!strcmp(grp->gr_mem[i], toys.optargs[0])) return;
        i++;
      }
      mem = xrealloc(mem, sizeof(char*) * (i+2)); //1 for name, 1 for NULL
      mem[i] = toys.optargs[0];
      mem[i+1] = NULL;
      for(j = 0; j < i; j++) mem[j] = grp->gr_mem[j];

      grp->gr_mem = mem;
    }
    update_group(grp, GROUP_PATH);
    grp->gr_passwd = (char *)"!!"; //by default group passwd is NOT set.
    update_group(grp, SECURE_GROUP_PATH); //update the /etc/gshadow file
  } else {    //new group to be created
    /* investigate the group to be created */
    if((grp = getgrnam(*toys.optargs))) error_exit("group '%s' is in use", *toys.optargs);
    setlocale(LC_ALL, "C");
    is_valid_username(*toys.optargs);
    new_group();
  }
}
