/* syslogd.c - a system logging utility.
 *
 * Copyright 2012 Madhur Verma <mad.flexi@gmail.com>
 *
 * Not in SUSv4.

USE_SYSLOGD(NEWTOY(syslogd,">0l#<1>8=8R:b#<0>99=1B#<0>99=0s#<0=2000m#<0>71582787=20O:p:f:a:nSKLD", TOYFLAG_SBIN|TOYFLAG_STAYROOT))

config SYSLOGD
  bool "syslogd"
  default y
  help
  Usage: syslogd  [-a socket][-p socket][-O logfile][-f config file][-m interval]
                  [-n][-p socket][-S][-s SIZE][-b N][-B N][-R HOST][-L][-K][-l N][-D]

  System logging utility

  -a        Extra unix socket for listen
  -O FILE   Default log file <DEFAULT: /var/log/messages>
  -f FILE   Config file <DEFAULT: /etc/syslog.conf>
  -p        Alternative unix domain socket <DEFAULT : /dev/log>
  -n        Avoid auto-backgrounding.
  -S        Smaller output
  -m MARK   interval <DEFAULT: 20 minutes> (RANGE: 0 to 71582787)
  -R HOST   Log to IP or hostname on PORT (default PORT=514/UDP)"
  -L        Log locally and via network (default is network only if -R)"
  -s SIZE   Max size (KB) before rotation (default:2000KB, 0=off)
  -b N      rotated logs to keep (default:1, max=99, 0=purge)
  -B N      Set number of logs to keep logs in memory buffer before write (default:0, max=99, 0=purge)
  -K        Log to kernel printk buffer (use dmesg to read it)
  -l N      Log only messages more urgent than prio(default:8 max:8 min:1)
  -D        Drop duplicates
*/

#define FOR_syslogd
#include "toys.h"
#include <sys/socket.h>
#include <sys/un.h>

GLOBALS(
  char *socket;
  char *config_file;
  char *unix_socket;
  char *logfile;
  long interval;
  long rot_size;
  long buf_count;
  long rot_count;
  char *remote_log;
  long log_prio;

  struct arg_list *lsocks;  // list of listen sockets
  struct arg_list *lfiles;  // list of write logfiles
  fd_set rfds;        // fds for reading
  int sd;            // socket for logging remote messeges.
)

#define flag_get(f,v,d)  ((toys.optflags & f) ? v : d)
#define flag_chk(f)    ((toys.optflags & f) ? 1 : 0)

#ifndef SYSLOG_NAMES
#define  INTERNAL_NOPRI  0x10
#define  INTERNAL_MARK  LOG_MAKEPRI(LOG_NFACILITIES, 0)

typedef struct _code {
  char *c_name;
  int c_val;
} CODE;

static CODE prioritynames[] =
{
  { "alert", LOG_ALERT },
  { "crit", LOG_CRIT },
  { "debug", LOG_DEBUG },
  { "emerg", LOG_EMERG },
  { "err", LOG_ERR },
  { "error", LOG_ERR },    /* DEPRECATED */
  { "info", LOG_INFO },
  { "none", INTERNAL_NOPRI },    /* INTERNAL */
  { "notice", LOG_NOTICE },
  { "panic", LOG_EMERG },    /* DEPRECATED */
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
  { "security", LOG_AUTH },    /* DEPRECATED */
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
 * -- CONFIG ---
 * Misc. configuration.
 * TODO: move to kconfig if possible.
 */
#define DEFLOGFILE    "/var/log/messages"
#define DEFLOGSOCK    "/dev/log"
#define DEFCONFFILE   "/etc/syslog.conf"
#define DEFPORT       514

/* Signal handling */
struct fd_pair { int rd; int wr; };
static struct fd_pair sigfd;

/*
 * UNIX Sockets for listening
 */
typedef struct unsocks_s {
  char *path;
  struct sockaddr_un sdu;
  int sd;
} unsocks_t;

/*
 * Log file entry to log into.
 */
typedef struct logfile_s {
  char *filename;
  char *config;
  uint8_t isNetwork;
  uint32_t facility[8];
  uint8_t level[LOG_NFACILITIES];
  int logfd;
  struct sockaddr_in saddr;
} logfile_t;

/*
 * Log buffer.
 */
typedef struct logbuffer_s {
  int len;
  char buf[1024];
} logbuffer_t;

/*
 * Adds opened socks to rfds for select()
 */
static int addrfds(void)
{
  struct arg_list *node;
  unsocks_t *sock;
  int ret = 0;
  FD_ZERO(&TT.rfds);
  node = TT.lsocks;

  while (node) {
    sock = (unsocks_t*) node->arg;
    if (sock->sd > 2) {
      FD_SET(sock->sd, &TT.rfds);
      ret = sock->sd;
    }
    node = node->next;
  }
  FD_SET(sigfd.rd, &TT.rfds);
//  if (flag_chk(FLAG_r)) FD_SET(TT.sd, &TT.rfds);  TODO: fix for remote log
  return (sigfd.rd > ret)?sigfd.rd:ret;
}

/*
 * initializes unsock_t structure
 * and opens socket for reading
 * and adds to global lsock list.
 */
static int open_unix_socks(void)
{
  struct arg_list *node;
  unsocks_t *sock;
  int ret = 0;

  for(node = TT.lsocks; node; node = node->next) {
    sock = (unsocks_t*) node->arg;
    sock->sdu.sun_family = AF_UNIX;
    strcpy(sock->sdu.sun_path, sock->path);
    sock->sd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sock->sd <= 0) {
      perror_msg("OPEN SOCKS : failed");
      continue;
    }
    unlink(sock->sdu.sun_path);
    if (bind(sock->sd, (struct sockaddr *) &sock->sdu, sizeof(sock->sdu))) {
      perror_msg("BIND SOCKS : failed sock : %s", sock->sdu.sun_path);
      close(sock->sd);
      continue;
    }
    chmod(sock->path, 0777);
    ret++;
  }
  return ret;
}

/*
 * creates a socket of family INET and protocol UDP
 * if successful then returns SOCK othrwise error
 */
static int open_udp_socks(char *host, int port, struct sockaddr_in *sadd)
{
  int ret;
  struct addrinfo *info, *rp;

  rp = xzalloc(sizeof(struct addrinfo));
  rp->ai_family = AF_INET;
  rp->ai_socktype = SOCK_DGRAM;
  rp->ai_protocol = IPPROTO_UDP;

  ret = getaddrinfo(host, NULL, rp, &info);
  if (ret || !info) perror_exit("BAD ADDRESS: can't find : %s ", host);
  free(rp);

  ret = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (ret <= 0) perror_exit("Can't create socket. ");

  for(rp = info; rp != NULL; rp = rp->ai_next){
    ((struct sockaddr_in*)rp->ai_addr)->sin_port = htons(port);
    memcpy(sadd, rp->ai_addr, rp->ai_addrlen);
    break;
  }
  if (!rp) perror_exit("Connect failed ");
  freeaddrinfo(info);
  return ret;
}

/*
 * Returns node having filename
 */
static struct arg_list *get_file_node(char *filename, struct arg_list *list)
{
  while (list) {
    if (strcmp(((logfile_t*) list->arg)->filename, filename) == 0)
      return list;
    list = list->next;
  }
  return list;
}

/*
 * recurses the logfile list and resolves config
 * for evry file and updates facilty and log level bits.
 */
static int resolve_config(logfile_t *file)
{
  char *tk, *fac, *lvl, *tmp, *nfac;
  int count = 0;
  unsigned facval = 0;
  uint8_t set, levval, neg;
  CODE *val = NULL;

  tmp = xstrdup(file->config);
  for (tk = strtok(tmp, "; \0"); tk; tk = strtok(NULL, "; \0")) {
    fac = tk;
    tk = strchr(fac, '.');
    if (tk == NULL) return -1;
    *tk = '\0';
    lvl = tk + 1;

    while(1) {
      count = 0;
      if (*fac == '*') {
        facval = 0xFFFFFFFF;
        fac++;
      }
      nfac = strchr(fac, ',');
      if (nfac) *nfac = '\0';
      while (*fac && ((CODE*) &facilitynames[count])->c_name) {
        val = (CODE*) &facilitynames[count];
        if (strcmp(fac, val->c_name) == 0) {
          facval |= (1<<LOG_FAC(val->c_val));
          break;
        }
        count++;
      }
      if (((CODE*) &facilitynames[count])->c_val == -1)
        return -1;

      if (nfac) fac = nfac+1;
      else break;
    }

    count = 0;
    set = 0;
    levval = 0;
    neg = 0;
    if (*lvl == '!') {
      neg = 1;
      lvl++;
    }
    if (*lvl == '=') {
      set = 1;
      lvl++;
    }
    if (*lvl == '*') {
      levval = 0xFF;
      lvl++;
    }
    while (*lvl && ((CODE*) &prioritynames[count])->c_name) {
      val = (CODE*) &prioritynames[count];
      if (strcmp(lvl, val->c_name) == 0) {
        levval |= set ? LOG_MASK(val->c_val):LOG_UPTO(val->c_val);
        if (neg) levval = ~levval;
        break;
      }
      count++;
    }
    if (((CODE*) &prioritynames[count])->c_val == -1) return -1;

    count = 0;
    set = levval;
    while(set) {
      if (set & 0x1) file->facility[count] |= facval;
      set>>=1;
      count++;
    }
    for (count =0; count < LOG_NFACILITIES; count++) {
      if (facval & 0x1) file->level[count] |= levval;
      facval >>= 1;
    }
  }
  free(tmp);

  return 0;
}

/*
 * Parse config file and update the log file list.
 */
static int parse_config_file(void)
{
  logfile_t *file;
  FILE *fs = NULL;
  char *confline=NULL, *tk=NULL, *tokens[2]={NULL, NULL};
  int len, linelen, tcount, lineno = 0;
  struct arg_list *node;
  /*
   * if -K then open only /dev/kmsg
   * all other log files are neglected
   * thus no need to open config either.
   */
  if (flag_chk(FLAG_K)) {
    node = xzalloc(sizeof(struct arg_list));
    file = xzalloc(sizeof(logfile_t));
    file->filename = "/dev/kmsg";
    file->config = "*.*";
    memset(file->level, 0xFF, sizeof(file->level));
    memset(file->facility, 0xFFFFFFFF, sizeof(file->facility));
    node->arg = (char*) file;
    TT.lfiles = node;
    return 0;
  }
  /*
   * if -R then add remote host to log list
   * if -L is not provided all other log
   * files are neglected thus no need to
   * open config either so just return.
   */
   if (flag_chk(FLAG_R)) {
     node = xzalloc(sizeof(struct arg_list));
     file = xzalloc(sizeof(logfile_t));
     file->filename = xmsprintf("@%s",TT.remote_log);
     file->isNetwork = 1;
     file->config = "*.*";
     memset(file->level, 0xFF, sizeof(file->level));
     memset(file->facility, 0xFFFFFFFF, sizeof(file->facility));
     node->arg = (char*) file;
     TT.lfiles = node;
     if (!flag_chk(FLAG_L))return 0;
   }
  /*
   * Read config file and add logfiles to the list
   * with their configuration.
   */
  fs = fopen(flag_get(FLAG_f, TT.config_file, DEFCONFFILE), "r");
  if (fs==NULL && flag_chk(FLAG_f))
    perror_exit("can't open '%s'", TT.config_file);

  for (len = 0, linelen = 0; fs;) {
    len = getline(&confline, (size_t*) &linelen, fs);
    if (len <= 0) break;
    lineno++;
    for (; *confline == ' '; confline++, len--) ;
    if ((confline[0] == '#') || (confline[0] == '\n')) continue;
    for (tcount = 0, tk = strtok(confline, " \t"); tk && (tcount < 2); tk =
        strtok(NULL, " \t"), tcount++) {
      if (tcount == 2) {
        error_msg("error in '%s' at line %d", flag_get(FLAG_f, TT.config_file, DEFCONFFILE), lineno);
        return -1;
      }
      tokens[tcount] = xstrdup(tk);
    }
    if (tcount <= 1 || tcount > 2) {
      if (tokens[0]) free(tokens[0]);
      error_msg("bad line %d: 1 tokens found, 2 needed", lineno);
      return -1;
    }
    tk = (tokens[1] + (strlen(tokens[1]) - 1));
    if (*tk == '\n') *tk = '\0';
    if (*tokens[1] == '\0') {
      error_msg("bad line %d: 1 tokens found, 2 needed", lineno);
      return -1;
    }
    if (*tokens[1] == '*') goto loop_again;

    node = get_file_node(tokens[1], TT.lfiles);
    if (node == NULL) {
      node = xzalloc(sizeof(struct arg_list));
      file = xzalloc(sizeof(logfile_t));
      file->config = xstrdup(tokens[0]);
      if (resolve_config(file)==-1) {
        error_msg("error in '%s' at line %d", flag_get(FLAG_f, TT.config_file, DEFCONFFILE), lineno);
        return -1;
      }
      file->filename = xstrdup(tokens[1]);
      if (*file->filename == '@') file->isNetwork = 1;
      node->arg = (char*) file;
      node->next = TT.lfiles;
      TT.lfiles = node;
    } else {
      file = (logfile_t*) node->arg;
      int rel = strlen(file->config) + strlen(tokens[0]) + 2;
      file->config = xrealloc(file->config, rel);
      sprintf(file->config, "%s;%s", file->config, tokens[0]);
    }
loop_again:
    if (tokens[0]) free(tokens[0]);
    if (tokens[1]) free(tokens[1]);
    free(confline);
    confline = NULL;
  }
  /*
   * Can't open config file or support is not enabled
   * adding default logfile to the head of list.
   */
  if (fs==NULL){
    node = xzalloc(sizeof(struct arg_list));
    file = xzalloc(sizeof(logfile_t));
    file->filename = flag_get(FLAG_O, TT.logfile, DEFLOGFILE);
    file->isNetwork = 0;
    file->config = "*.*";
    memset(file->level, 0xFF, sizeof(file->level));
    memset(file->facility, 0xFFFFFFFF, sizeof(file->facility));
    node->arg = (char*) file;
    node->next = TT.lfiles;
    TT.lfiles = node;
  }
  if (fs) {
    fclose(fs);
    fs = NULL;
  }
  return 0;
}

/*
 * String STR to UINT32 conversion strored in VAR
 */
static long strtou32(const char *str)
{
  char *endptr = NULL;
  int base = 10;
  errno=0;
  if (str[0]=='0' && (str[1]=='x' || str[1]=='X')) {
    base = 16;
    str+=2;
  }
  long ret_val = strtol(str, &endptr, base);
  if (errno) return -1;
  else if (endptr && (*endptr!='\0'||endptr == str)) return -1;
  return ret_val;
}

/*
 * open every log file in list.
 */
static int open_logfiles(void)
{
  struct arg_list *node;
  logfile_t *tfd;
  int port = -1;
  char *p, *tmpfile;
  node = TT.lfiles;

  while (node) {
    tfd = (logfile_t*) node->arg;
    if (tfd->isNetwork) {
      tmpfile = xstrdup(tfd->filename +1);
      if ((p = strchr(tmpfile, ':'))!=NULL) {
        *p = '\0';
        port = strtou32(p + 1);
        if (port<0 || port>65535) error_exit("wrong port no in %s", tfd->filename);
      }
      tfd->logfd = open_udp_socks(tmpfile, (port>=0)?port:DEFPORT, &tfd->saddr);
      free(tmpfile);
    } else tfd->logfd = open(tfd->filename, O_CREAT | O_WRONLY | O_APPEND, 0666);
    if (tfd->logfd <= 0) {
      tfd->filename = "/dev/console";
      tfd->logfd = open(tfd->filename, O_APPEND);
    }
    node = node->next;
  }
  return 0;
}

/*
 * write to file with rotation
 */
static int write_rotate( logfile_t *tf, int len)
{
  static int buf_idx = 0;
  static logbuffer_t buffer[100];
  int size, isreg, idx;
  struct stat statf;
  isreg = (fstat(tf->logfd, &statf) == 0 && S_ISREG(statf.st_mode));
  size = statf.st_size;

  if (flag_chk(FLAG_s)||flag_chk(FLAG_b)) {
    if (TT.rot_size && isreg && (size+len)>(TT.rot_size*1024)) {
      if (TT.rot_count) { /* always 0..99 */
        int i = strlen(tf->filename) + 3 + 1;
        char old_file[i];
        char new_file[i];
        i = TT.rot_count - 1;
        while (1) {
          sprintf(new_file, "%s.%d", tf->filename, i);
          if (i == 0) break;
          sprintf(old_file, "%s.%d", tf->filename, --i);
          rename(old_file, new_file);
        }
        rename(tf->filename, new_file);
        unlink(tf->filename);
        close(tf->logfd);
        tf->logfd = open(tf->filename, O_CREAT | O_WRONLY | O_APPEND, 0666);
        if (tf->logfd < 0) {
          perror_msg("can't open %s", tf->filename);
          return -1;
        }
      }
      ftruncate(tf->logfd, 0);
    }
  }
  if (TT.buf_count && flag_chk(FLAG_B)) {
    if (buf_idx < TT.buf_count) {
        memcpy(buffer[buf_idx].buf, toybuf, len);
        buffer[buf_idx].buf[len + 1] = '\0';
        buffer[buf_idx].len = len;
        buf_idx++;
        return len;
    } else {
      for (idx = 0; idx < TT.buf_count; idx++) {
         write(tf->logfd, buffer[idx].buf, buffer[idx].len);
      }
      buf_idx = 0;
    }
  }
  return write(tf->logfd, toybuf, len);
}

/*
 * search the given name and return its value
 */
static char* dec(int val, CODE *clist)
{
  const CODE *c;

  for (c = clist; c->c_name; c++) { //find the given parameter in list and return the value.
    if (val == c->c_val) return c->c_name;
  }
  return itoa(val);
}
/*
 * Compute priority from "facility.level" pair
 */
static void priority_to_string(int pri, char **facstr, char **lvlstr)
{
  int fac,lev;

  fac = LOG_FAC(pri);
  lev = LOG_PRI(pri);
  *facstr = dec(fac<<3, facilitynames);
  *lvlstr = dec(lev, prioritynames);
  return;
}

/*
 * Parse messege and write to file.
 */
static int logmsg(char *msg, int len)
{
  time_t now;
  char *ts, *lvlstr, *facstr;
  char *p;
  int pri = 0;
  struct utsname *uts;
  struct arg_list *lnode = TT.lfiles;

  char *omsg = msg;
  int olen = len, fac, lvl;
  /*
   * Extract the priority no.
   */
  if (*msg == '<') {
    pri = (int) strtoul(msg + 1, &p, 10);
    if (*p == '>') msg = p + 1;
  }
  /* Jan 18 00:11:22 msg...
   * 01234567890123456
   */
  if (len < 16 || msg[3] != ' ' || msg[6] != ' ' || msg[9] != ':'
      || msg[12] != ':' || msg[15] != ' ') {
    time(&now);
    ts = ctime(&now) + 4; /* skip day of week */
  } else {
    now = 0;
    ts = msg;
    msg += 16;
  }
  ts[15] = '\0';
  fac = LOG_FAC(pri);
  lvl = LOG_PRI(pri);

  if (flag_chk(FLAG_K)) {
    len = sprintf(toybuf, "<%d> %s\n", pri, msg);
    goto do_log;
  }
  priority_to_string(pri, &facstr, &lvlstr);

  p = "local";
  uts = xzalloc(sizeof(struct utsname));
  if (!uname(uts)) p = uts->nodename;
  if (flag_chk(FLAG_S)) len = sprintf(toybuf, "%s %s\n", ts, msg);
  else len = sprintf(toybuf, "%s %s %s.%s %s\n", ts, p, facstr, lvlstr, msg);
  free(uts);

do_log:
  if (lvl >= TT.log_prio) return 0;

  while (lnode) {
    logfile_t *tf;
    tf = (logfile_t*) lnode->arg;
    if (tf->logfd > 0) {
      if ((tf->facility[lvl] & (1 << fac))&&(tf->level[fac] & (1<<lvl))) {
        int wlen;
        if (tf->isNetwork)
          wlen = sendto(tf->logfd, omsg, olen, 0, (struct sockaddr*)&tf->saddr, sizeof(tf->saddr));
        else wlen = write_rotate(tf, len);
        if (wlen < 0) perror_msg("write failed file : %s ", (tf->isNetwork)?(tf->filename+1):tf->filename);
      }
    }
    lnode = lnode->next;
  }
  return 0;
}

/*
 * closes all read and write fds
 * and frees all nodes and lists
 */
static void cleanup(void)
{
  struct arg_list *fnode;
  logmsg("<46>syslogd exiting", 19);
  while (TT.lsocks) {
    fnode = TT.lsocks;
    if (((unsocks_t*) fnode->arg)->sd >= 0)
      close(((unsocks_t*) fnode->arg)->sd);
    free(fnode->arg);
    TT.lsocks = fnode->next;
    free(fnode);
  }
  unlink("/dev/log");

  while (TT.lfiles) {
    fnode = TT.lfiles;
    if (((logfile_t*) fnode->arg)->logfd >= 0)
      close(((logfile_t*) fnode->arg)->logfd);
    free(fnode->arg);
    TT.lfiles = fnode->next;
    free(fnode);
  }
  return;
}

#ifdef REMOTE_LOG_READ
/*
 * TODO: used in remote logging
 * open UDP port 514 for remote logging
 */
static void remote_log(void)
{
  struct sockaddr_in me;
  const int set = 1;

  TT.sd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (TT.sd < 0) perror_exit("Can't open sock for remote logging ");
  setsockopt(TT.sd, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set));

  memset(&me, 0, sizeof(me));
  me.sin_family = AF_INET;
  me.sin_port = htons(DEFPORT);
  me.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(TT.sd, (struct sockaddr *)&me, sizeof(me)) < 0)
    perror_exit("bind failed on remote log sock");
}
#endif

/********* SIGNAL HANDLING ****************/
static void signal_handler(int sig)
{
  unsigned char ch = sig;
  if (write(sigfd.wr, &ch, 1) != 1) error_msg("can't send signal");
}

static int setup_signal()
{
  if (pipe((int *)&sigfd) < 0){
    error_msg("pipe failed\n");
    return -1;
  }

  fcntl(sigfd.wr , F_SETFD, FD_CLOEXEC);
  fcntl(sigfd.rd , F_SETFD, FD_CLOEXEC);
  int flags = fcntl(sigfd.wr, F_GETFL);
  fcntl(sigfd.wr, F_SETFL, flags | O_NONBLOCK);
  signal(SIGHUP, signal_handler);
  signal(SIGTERM, signal_handler);
  signal(SIGINT, signal_handler);
  signal(SIGQUIT, signal_handler);

  return 0;
}

static int syslog_daemon(void)
{
  int fd;

  fd = open("/dev/null", O_RDWR);
  if (fd < 0) fd = open("/", O_RDONLY, 0666);
  pid_t pid = fork();

  if (pid < 0) {
    perror_msg("DAEMON: failed to fork");
    return -1;
  }
  if (pid) exit(EXIT_SUCCESS);

  setsid();
  dup2(fd, 0);
  dup2(fd, 1);
  dup2(fd, 2);
  close(fd);

  //don't daemonize again if SIGHUP received.
  toys.optflags |= FLAG_n;

  return 0;
}
/********************************************/

/*
 * Main syslogd routine.
 */
void syslogd_main(void)
{
  char *temp;
  unsocks_t *tsd;
  int maxfd, retval, last_len=0;
  struct timeval tv;
  struct arg_list *node;
  char buffer[1024], last_buf[1024];

  if (flag_chk(FLAG_p) && strlen(TT.unix_socket) > 108) {
    error_msg("Socket path should not be more than %d", 108);
    return;
  }

init_jumpin:
  TT.lsocks = xzalloc(sizeof(struct arg_list));
  tsd = xzalloc(sizeof(unsocks_t));

  tsd->path = flag_get(FLAG_p, TT.unix_socket , DEFLOGSOCK);
  TT.lsocks->arg = (char*) tsd;

  if (flag_chk(FLAG_a)) {
    for (temp = strtok(TT.socket, ":"); temp; temp = strtok(NULL, ":")) {
      struct arg_list *ltemp = xzalloc(sizeof(struct arg_list));
      if (strlen(temp) > 107) temp[108] = '\0';
      tsd = xzalloc(sizeof(unsocks_t));
      tsd->path = temp;
      ltemp->arg = (char*) tsd;
      ltemp->next = TT.lsocks;
      TT.lsocks = ltemp;
    }
  }
//  if (flag_chk(FLAG_r)) remote_log();  TODO: fix for remote log
  if (open_unix_socks() == 0) {
    error_msg("Can't open single socket for listenning.");
    goto clean_and_exit;
  }
  setup_signal();
  if (parse_config_file() == -1) goto clean_and_exit;
  open_logfiles();
  if (!flag_chk(FLAG_n)) syslog_daemon();
  {
    int pidfile = open("/var/run/syslogd.pid", O_CREAT | O_WRONLY | O_TRUNC, 0666);
    if (pidfile > 0) {
      unsigned pid = getpid();
      char pidbuf[32];
      int len = sprintf(pidbuf, "%u\n", pid);
      write(pidfile, pidbuf, len);
      close(pidfile);
    }
  }
  logmsg("<46>syslogd started: ToyBox v1.26.0", 35);

  for (;;) {
    maxfd = addrfds();
    tv.tv_usec = 0;
    tv.tv_sec = TT.interval*60;

    retval = select(maxfd + 1, &TT.rfds, NULL, NULL, (TT.interval)?&tv:NULL);
    if (retval < 0) { /* Some error. */
      if (errno == EINTR) continue;
      perror_msg("Error in select ");
      continue;
    }
    if (retval == 0) { /* Timed out */
      logmsg("<46>-- MARK --", 14);
      continue;
    }
    if (FD_ISSET(sigfd.rd, &TT.rfds)) { /* May be a signal */
      unsigned char sig;

      if (read(sigfd.rd, &sig, 1) != 1) {
        error_msg("signal read failed.\n");
        continue;
      }
      switch(sig) {
      case SIGTERM:    /* FALLTHROUGH */
      case SIGINT:     /* FALLTHROUGH */
      case SIGQUIT:
    	  cleanup();
    	  signal(sig, SIG_DFL);
    	  sigset_t ss;
    	  sigemptyset(&ss);
    	  sigaddset(&ss, sig);
    	  sigprocmask(SIG_UNBLOCK, &ss, NULL);
    	  raise(sig);
    	  _exit(1);  /* Should not reach it */
    	  break;
      case SIGHUP:
        cleanup();
        goto init_jumpin;
      default: break;
      }
    }
    if (retval > 0) { /* Some activity on listen sockets. */
      node = TT.lsocks;
      while (node) {
        int sd = ((unsocks_t*) node->arg)->sd;
        if (FD_ISSET(sd, &TT.rfds)) {
          int len = read(sd, buffer, 1023);
          if (len > 0) {
            buffer[len] = '\0';
            if(flag_chk(FLAG_D) && (len == last_len))
              if (memcmp(last_buf, buffer, len) == 0)
                break;

            memcpy(last_buf, buffer, len);
            last_len = len;
            logmsg(buffer, len);
          }
          break;
        }
        node = node->next;
      }
    }
  }
clean_and_exit:
  cleanup();
  return;
}
