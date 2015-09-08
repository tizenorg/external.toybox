/* logger.c - log messages.
 *
 * Copyright 2012 Ashwini Kumar <ak.ashwini@gmail.com>
 *
 * See http://opengroup.org/onlinepubs/9699919799/utilities/logger.html

USE_LOGGER(NEWTOY(logger, "p:t:si", TOYFLAG_USR|TOYFLAG_BIN))

config LOGGER
  bool "logger"
  default y
  help
    usage: logger [-s] [-t TAG] [-p PRIORITY] [message]

    -i    Log the process id of the logger process with each line.
    -s    Log to stderr as well as the system log
    -t TAG  Log using the specified tag (defaults to user name)
    -p PRIO Priority (numeric or facility.level pair)
*/

#define FOR_logger
#include "toys.h"

GLOBALS(
  char *tag;
  char *priority;
)

#ifndef SYSLOG_NAMES
#define INTERNAL_NOPRI  0x10  /* the "no priority" priority */
/* mark "facility" */
#define INTERNAL_MARK   LOG_MAKEPRI(LOG_NFACILITIES, 0)
typedef struct _code {
  char  *c_name;
  int c_val;
} CODE;

static CODE prioritynames[] =
{  
  { "alert", LOG_ALERT },
  { "crit", LOG_CRIT },
  { "debug", LOG_DEBUG },
  { "emerg", LOG_EMERG },
  { "err", LOG_ERR },
  { "error", LOG_ERR },     /* DEPRECATED */
  { "info", LOG_INFO },                                                                             
  { "none", INTERNAL_NOPRI },   /* INTERNAL */
  { "notice", LOG_NOTICE },
  { "panic", LOG_EMERG },   /* DEPRECATED */
  { "warn", LOG_WARNING },    /* DEPRECATED */
  { "warning", LOG_WARNING },
  { NULL, -1 }
};

static CODE facilitynames[] =
{
  { "auth", LOG_AUTH },
  { "authpriv", LOG_AUTHPRIV },
  { "cron", LOG_CRON },
  { "daemon", LOG_DAEMON },
  { "ftp", LOG_FTP },
  { "kern", LOG_KERN },
  { "lpr", LOG_LPR },
  { "mail", LOG_MAIL },
  { "mark", INTERNAL_MARK },    /* INTERNAL */
  { "news", LOG_NEWS },
  { "security", LOG_AUTH },     /* DEPRECATED */
  { "syslog", LOG_SYSLOG },
  { "user", LOG_USER },
  { "uucp", LOG_UUCP },
  { "local0", LOG_LOCAL0 },
  { "local1", LOG_LOCAL1 },
  { "local2", LOG_LOCAL2 },
  { "local3", LOG_LOCAL3 },
  { "local4", LOG_LOCAL4 },
  { "local5", LOG_LOCAL5 },
  { "local6", LOG_LOCAL6 },
  { "local7", LOG_LOCAL7 },
  { NULL, -1 }
};
#endif

/*
 * get effective username, for setting as tag for syslog
 */
char* get_username()
{
  struct passwd* pw = NULL;
  pw = getpwuid(geteuid());
  return (pw?pw->pw_name : NULL);
}

/*
 * search the given name and return its value
 */
static int dec(char* name, CODE *clist)
{
  const CODE *c;

  if (isdigit(*name)) return atoi(name); //if integer then just return the value 
  for (c = clist; c->c_name; c++) { //find the given parameter in list and return the value.
    if (strcasecmp(name, c->c_name) == 0) return c->c_val;
  }
  return -1;
}

/*
 * Compute priority from "facility.level" pair 
 */
int get_priority(char* prio)
{
  int fac = 0;
  int lev = 0;
  char* dot = NULL;

  dot = strchr(prio, '.');
  if(dot) {
    *dot = '\0';
    fac = dec(prio, facilitynames);
    if(fac < 0) error_exit("Unknown facilityname '%s'",prio);
    *dot++ = '.';
  }
  else dot = prio;
  lev = dec(dot, prioritynames);
  if(lev < 0) error_exit("Unknown priority '%s'",dot);

  return ((lev & LOG_PRIMASK) | (fac & LOG_FACMASK));
}

/* 
 * logger utility main function
 */
void logger_main(void)
{
  int prio = 0, msglen = 0, flags = 0;
  char *message = NULL;
  if(!(toys.optflags & FLAG_t)) TT.tag = get_username();

  if(toys.optflags & FLAG_p) prio = get_priority(TT.priority);
  else prio = LOG_USER | LOG_INFO;

  if(toys.optflags & FLAG_s) flags |= LOG_PERROR;
  if(toys.optflags & FLAG_i) flags |= LOG_PID;

  openlog(TT.tag, flags, 0);
  if(!toys.optc) { // read messages from STDIN and log them using syslog
    while(fgets(toybuf, sizeof(toybuf), stdin)) { 
      if(toybuf[0] && !(toybuf[0] == '\n' && !toybuf[1])) syslog(prio,"%s",toybuf);
    }
  } else {
    while(*toys.optargs) {
      if(message != NULL) msglen = strlen(message);
      message = xrealloc(message, msglen + strlen(*toys.optargs) + 2); // 2 byte: 1 for ' ' and 1 for '\0';
      sprintf(message + msglen, " %s", *toys.optargs++);
    }
    syslog(prio, "%s", message+1); //ignore leading ' '
    free(message);
    message = NULL;
  }
  closelog();
  toys.exitval = 0;
}

/*-
 * Copyright (c) 1983, 1993
 * The Regents of the University of California.  All rights reserved.
 *
 * This is the original license statement for the decode and pencode functions.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *  notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *  notice, this list of conditions and the following disclaimer in the
 *  documentation and/or other materials provided with the distribution.
 *
 * 3. BSD Advertising Clause omitted per the July 22, 1999 licensing change
 *  ftp://ftp.cs.berkeley.edu/pub/4bsd/README.Impt.License.Change
 *
 * 4. Neither the name of the University nor the names of its contributors
 *  may be used to endorse or promote products derived from this software
 *  without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
