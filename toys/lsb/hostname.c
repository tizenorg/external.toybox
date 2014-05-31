/* hostname.c - Get/Set the hostname
 *
 * Copyright 2012 Andre Renaud <andre@bluewatersys.com>
 * Copyright 2012 Sandeep Sharma <sandeep.jack2756@gmail.com> 
 *    Added support for "Fdifs" flags.
 *
 * http://refspecs.linuxfoundation.org/LSB_4.1.0/LSB-Core-generic/LSB-Core-generic/hostname.html

USE_HOSTNAME(NEWTOY(hostname, "F:dfis", TOYFLAG_BIN))

config HOSTNAME
  bool "hostname"
  default y
  help
    usage: hostname [newname]
    -s  Short
    -i  Addresses for the hostname
    -d  DNS domain name
    -f  Fully qualified domain name
    -F FILE Use FILE's content as hostname
    Get/Set the current hostname/DNS Domain name
*/
#define FOR_hostname

#include "toys.h"

GLOBALS(
  char *file_name;
)

#ifndef _GNU_SOURCE
/*
 * locate character in string.
 */
static char *strchrnul(char *s, int c)
{
  while(*s != '\0' && *s != c) s++;
  return (char*)s;
}
#endif

/*
 * Parse the given file to extract hostname,
 * first word of last line with continuation
 * and "#" (comment) taking care of*/
void parse_get_hostname()
{
  char *file_bak = NULL;
  char *ptr, *tmp;
  int fd = xopen(TT.file_name, O_RDONLY);

  while((ptr = get_line(fd)) != NULL) {
      char *myline = NULL;
validate:
      tmp = ptr;

      while(*ptr && *ptr <= ' ') ptr++; //skip space
      if(*ptr == '\0') {
          free(tmp);
          continue;
      }
      if(ptr[strlen(ptr) - 1] == '\\') {
          char *p = strrchr(ptr, '\\');
          *p = '\0';      
          myline = xstrdup(ptr);
          free(tmp);
          tmp = NULL;
          while((ptr = get_line(fd)) != NULL) {
              int  contin = 0;
              if(ptr[strlen(ptr) - 1] == '\\') {
                  p = strrchr(ptr, '\\');
                  *p = '\0';      
                  contin = 1;
              }
              myline = realloc(myline,strlen(ptr) + strlen(myline) + 1);
              strcat(myline, ptr);
              free(ptr);
              if(!contin) break;
          }
          ptr = myline;
          goto validate;
      } else myline = xstrdup(ptr);

      if(*myline == '#') {
        free(myline);
        myline = NULL;
        continue;
      }

      file_bak = xstrdup(myline);
      free(myline);
      if(tmp) free(tmp);

      if(file_bak) {
        char *p = strchr(file_bak, ' ');
        if(p) *p = '\0';
        *(strchrnul(file_bak, '#')) = '\0';
        if(sethostname(file_bak, strlen(file_bak))) 
          perror_exit("set failed");
      }
  }
}
/*
 * main for hostname
 */
void hostname_main(void)
{
  const char *hostname = NULL;
  if(toys.optargs[0]) hostname = toys.optargs[0];
  char *host_name = NULL;
  gethostname(toybuf, sizeof(toybuf));
  host_name = toybuf;
  if((toys.optflags & FLAG_s) || (toys.optflags & FLAG_i) ||(toys.optflags & FLAG_f) || (toys.optflags & FLAG_d)) {
      struct hostent *hostnt;
      char *p;
      h_errno = 0;
      hostnt = gethostbyname(host_name);
      if(!hostnt) perror_exit("%s %s", host_name, hstrerror(h_errno)); //return may be NULL.

      p = strchrnul(hostnt->h_name, '.');
      if (FLAG_f & toys.optflags) {
          puts(hostnt->h_name);
      } else if (toys.optflags & FLAG_s) {
          *p = '\0';
          puts(hostnt->h_name);
      } else if (toys.optflags & FLAG_d) {
          if (*p)
              puts(p + 1);
      } else {
          if (hostnt->h_length == sizeof(struct in_addr)) {
              struct in_addr **hostnt_addr_list = (struct in_addr **)hostnt->h_addr_list;
              while (*hostnt_addr_list) {
                  xprintf("%s ", inet_ntoa(**hostnt_addr_list));
                  hostnt_addr_list++;
              }
              xputc('\n');
          }
      }
  } 
  else if(toys.optflags & FLAG_F) {
      parse_get_hostname();
  }
  else if(hostname) { 
      if(sethostname(hostname, strlen(hostname)))
          perror_exit("set failed '%s'", hostname);
  } else {
      if(gethostname(toybuf, sizeof(toybuf)))
          perror_exit("get failed");
      xputs(toybuf);
  }
}
