/* telnet.c - Telnet client.
 *
 * Copyright 2012 Madhur Verma <mad.flexi@gmail.com>
 *
 * Not in SUSv4.

USE_TELNET(NEWTOY(telnet, "<1>2", TOYFLAG_BIN))

config TELNET
  bool "telnet"
  default y
  help
    usage: telnet HOST [PORT]
    Connect to telnet server
*/
#define FOR_telnet
#include "toys.h"
#include <arpa/telnet.h>
#include <netinet/in.h>
#include  <sys/poll.h>

GLOBALS(
  char *hostname;
  int port;
  int sfd;
  char buff[128];
  int pbuff;
  char iac[256];
  int piac;
  char *ttype;
  struct termios def_term;
  struct termios raw_term;
  uint8_t term_ok;
  uint8_t term_mode;
  uint8_t flags;
  int win_width;
  int win_height;
  unsigned signalno;
)

#define TELNET_PORT    23
#define DATABUFSIZE    128
#define IACBUFSIZE    256
#define  CM_TRY      0
#define CM_ON      1
#define CM_OFF      2
#define UF_ECHO      0x01
#define UF_SGA      0x02

/*
 * creates a socket of family INET and protocol TCP and connects
 * it to HOST at PORT.
 * if successful then returns SOCK othrwise error
 */
static int xconnect_inet_tcp(char *host, int port)
{
  int ret;
  struct addrinfo *info, *rp;

  rp = xzalloc(sizeof(struct addrinfo));
  rp->ai_family = AF_INET;
  rp->ai_socktype = SOCK_STREAM;
  rp->ai_protocol = IPPROTO_TCP;

  ret = getaddrinfo(host, NULL, rp, &info);
  if(ret || !info) perror_exit("BAD ADDRESS: can't find : %s ", host);
  free(rp);

  ret = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if(ret <= 0) perror_exit("Can't create socket. ");

  for(rp = info; rp != NULL; rp = rp->ai_next){
    ((struct sockaddr_in*)rp->ai_addr)->sin_port = htons(port);
    if(connect(ret, rp->ai_addr, rp->ai_addrlen) != -1) break;
  }
  if(!rp) perror_exit("Connect failed ");
  freeaddrinfo(info);
  return ret;
}

/*
 * sets terminal mode: LINE or CHARACTER based om internal stat.
 */
static char const es[] = "\r\nEscape character is ";
static void set_mode(void)
{
  if (TT.flags & UF_ECHO) {
    if (TT.term_mode == CM_TRY) {
      TT.term_mode = CM_ON;
      printf("\r\nEntering character mode%s'^]'.\r\n", es);
      if (TT.term_ok) tcsetattr(0, TCSADRAIN, &TT.raw_term);
    }
  } else {
    if (TT.term_mode != CM_OFF) {
      TT.term_mode = CM_OFF;
      printf("\r\nEntering line mode%s'^C'.\r\n", es);
      if (TT.term_ok) tcsetattr(0, TCSADRAIN, &TT.def_term);
    }
  }
}

/*
 * flushes all data in IAC buff to server.
 */
static void flush_iac(void)
{
  int wlen = write(TT.sfd, TT.iac, TT.piac);
  if(wlen <= 0) error_msg("IAC : send failed.");
  TT.piac = 0;
}

/*
 * puts DATA in iac buff of length LEN and updates iac buff pointer.
 */
static void put_iac(int len, ...)
{
  if(TT.piac + len >= IACBUFSIZE) flush_iac();
  va_list va; va_start(va, len);
  for(;len > 0; TT.iac[TT.piac++] = (uint8_t)va_arg(va, int), len--);
  va_end(va);
}

/*
 * puts string STR in iac buff and updates iac buff pointer.
 */
static void str_iac(char *str)
{
  int len = strlen(str);
  if(TT.piac + len + 1 >= IACBUFSIZE) flush_iac();
  strcpy(&TT.iac[TT.piac], str);
  TT.piac += len+1;
}

/*
 * Handles escape sequence.
 */
static void handle_esc(void)
{
  char input;
  if(TT.signalno && TT.term_ok) tcsetattr(0, TCSADRAIN, &TT.raw_term);
  write(1,"\r\nConsole escape. Commands are:\r\n\n"
      " l  go to line mode\r\n"
      " c  go to character mode\r\n"
      " z  suspend telnet\r\n"
      " e  exit telnet\r\n", 114);

  if (read(STDIN_FILENO, &input, 1) <= 0){
    if(TT.term_ok) tcsetattr(0, TCSADRAIN, &TT.def_term);
    exit(0);
  }

  switch (input) {
  case 'l':
    if (!TT.signalno) {
      TT.term_mode = CM_TRY;
      TT.flags &= ~(UF_ECHO | UF_SGA);
      set_mode();
      put_iac(6, IAC,DONT,TELOPT_ECHO,IAC,DONT, TELOPT_SGA);
      flush_iac();
      goto ret;
    }
    break;
  case 'c':
    if (TT.signalno) {
      TT.term_mode = CM_TRY;
      TT.flags |= (UF_ECHO | UF_SGA);
      set_mode();
      put_iac(6, IAC,DO,TELOPT_ECHO,IAC,DO,TELOPT_SGA);
      flush_iac();
      goto ret;
    }
    break;
  case 'z':
    if(TT.term_ok) tcsetattr(0, TCSADRAIN, &TT.def_term);
    kill(0, SIGTSTP);
    if(TT.term_ok) tcsetattr(0, TCSADRAIN, &TT.raw_term);
    break;
  case 'e':
    if(TT.term_ok) tcsetattr(0, TCSADRAIN, &TT.def_term);
    exit(0);
  default: break;
  }

  write(1, "continuing...\r\n", 15);
  if (TT.signalno && TT.term_ok) tcsetattr(0, TCSADRAIN, &TT.def_term);

ret:
  TT.signalno = 0;
}

/*
 * calculates current window size and updates in
 * global variables win_height win_width
 * first tries ioctl if fails then tries env variable and
 * if still fails provides default values.
 */
static void get_win_size(void)
{
  struct winsize win;
  char *temp;

  win.ws_row = 0;
  win.ws_col = 0;
  ioctl(0, TIOCGWINSZ, &win);
  TT.win_height = (!win.ws_row)? ((temp = getenv("ROWS"))!=NULL)? atoi(temp):24:win.ws_row;
  TT.win_width = (!win.ws_col)?((temp = getenv("COLUMN"))!=NULL)? atoi(temp):80:win.ws_col;
}

/*
 * handles telnet SUB NEGOTIATIONS
 * only terminal type is supported.
 */
static void handle_negotiations(void)
{
  char opt = TT.buff[TT.pbuff++];
  switch(opt){
  case TELOPT_TTYPE:
    opt =  TT.buff[TT.pbuff++];
    if(opt == TELQUAL_SEND){
      put_iac(4, IAC,SB,TELOPT_TTYPE,TELQUAL_IS);
      str_iac(TT.ttype);
      put_iac(2, IAC,SE);
    }
    break;
  default: break;
  }
}

/*
 * handles server's DO DONT WILL WONT requests.
 * supports ECHO, SGA, TTYPE, NAWS
 */
static void handle_ddww(char ddww)
{
  char opt = TT.buff[TT.pbuff++];
  switch (opt) {
  case TELOPT_ECHO: /* ECHO */
    if (ddww == DO) put_iac(3, IAC,WONT,TELOPT_ECHO);
    if(ddww == DONT) break;
    if (TT.flags & UF_ECHO) {
        if (ddww == WILL) return;
      } else if (ddww == WONT) return;
    if (TT.term_mode != CM_OFF) TT.flags ^= UF_ECHO;
    (TT.flags & UF_ECHO)?put_iac(3, IAC,DO,TELOPT_ECHO):put_iac(3, IAC,DONT,TELOPT_ECHO);
    set_mode();
    printf("\r\n");
    break;

  case TELOPT_SGA: /* Supress GO Ahead */
    if (TT.flags & UF_SGA){ if (ddww == WILL) return;
    } else if (ddww == WONT) return;

    TT.flags ^= UF_SGA;
    (TT.flags & UF_SGA)?put_iac(3, IAC,DO,TELOPT_SGA): put_iac(3, IAC,DONT,TELOPT_SGA);
    break;

  case TELOPT_TTYPE: /* Terminal Type */
    (TT.ttype)?put_iac(3, IAC,WILL,TELOPT_TTYPE):put_iac(3, IAC,WONT,TELOPT_TTYPE);
    break;

  case TELOPT_NAWS: /* Window Size */
    put_iac(3, IAC,WILL,TELOPT_NAWS);
    put_iac(9, IAC,SB,TELOPT_NAWS,(TT.win_width >> 8) & 0xff,TT.win_width & 0xff,(TT.win_height >> 8) & 0xff,TT.win_height & 0xff,IAC,SE);
    break;

  default: /* Default behaviour is to say NO */
    if(ddww == WILL) put_iac(3, IAC,DONT,opt);
    if(ddww == DO) put_iac(3, IAC,WONT,opt);
    break;
  }
}

/*
 * parses data which is read from server of length LEN.
 * and passes it to console.
 */
static int read_server(int len)
{
  int i = 0;
  char curr;
  TT.pbuff = 0;

  do {
    curr = TT.buff[TT.pbuff++];
    if (curr == IAC) {
      curr = TT.buff[TT.pbuff++];
      switch (curr) {
      case DO:    /* FALLTHROUGH */
      case DONT:    /* FALLTHROUGH */
      case WILL:    /* FALLTHROUGH */
      case WONT:
        handle_ddww(curr);
        break;
      case SB:
        handle_negotiations();
        break;
      case SE:
        break;
      default: break;
      }
    } else {
      toybuf[i++] = curr;
      if (curr == '\r') { curr = TT.buff[TT.pbuff++];
        if (curr != '\0') TT.pbuff--;
      }
    }
  } while (TT.pbuff < len);

  if (i) write(STDIN_FILENO, toybuf, i);
  return 0;
}

/*
 * parses data which is read from console og length LEN
 * and passes it to server.
 */
static void write_server(int len)
{
  char *c = (char*)TT.buff;
  int i = 0;

  for (; len > 0; len--, c++) {
    if (*c == 0x1d) {
      handle_esc();
      return;
    }
    toybuf[i++] = *c;
    if (*c == IAC) toybuf[i++] = *c; /* IAC -> IAC IAC */
    else if (*c == '\r') toybuf[i++] = '\0'; /* CR -> CR NUL */
  }
  if(i) write(TT.sfd, toybuf, i);
}

/*
 * SIGINT signal handling.
 * only sets signalno which get handle in main loop.
 */
static void handle_sigint(int signo)
{
  TT.signalno = signo;
}

void telnet_main(void)
{
  int set = 1, len;
  struct pollfd pfds[2];

  TT.hostname = toys.optargs[0];
  TT.port = TELNET_PORT;

  if(toys.optc == 2) TT.port = atoi(toys.optargs[1]);
  if(TT.port <= 0 || TT.port > 65535) error_exit("PORT can only contain non zero positive numerical value upto 65535.");

  TT.ttype = getenv("TERM");
  if(!TT.ttype) TT.ttype = "";
  if(strlen(TT.ttype) > IACBUFSIZE-1) TT.ttype[IACBUFSIZE - 1] = '\0';

  if (tcgetattr(0, &TT.def_term) >= 0) {
    TT.term_ok = 1;
    TT.raw_term = TT.def_term;
    cfmakeraw(&TT.raw_term);
  }
  get_win_size();

  TT.sfd = xconnect_inet_tcp(TT.hostname, TT.port);
  setsockopt(TT.sfd, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set));
  setsockopt(TT.sfd, SOL_SOCKET, SO_KEEPALIVE, &set, sizeof(set));

  pfds[0].fd = STDIN_FILENO;
  pfds[0].events = POLLIN;
  pfds[1].fd = TT.sfd;
  pfds[1].events = POLLIN;

  TT.piac = TT.pbuff = 0;
  TT.signalno = 0;
  signal(SIGINT, handle_sigint);
  while(1){
    if(TT.piac) flush_iac();
    if(poll(pfds, 2, -1) < 0){
      (TT.signalno)?handle_esc():sleep(1);
      continue;
    }
    if(pfds[0].revents){
      len = read(STDIN_FILENO, TT.buff, DATABUFSIZE);
      if(len > 0) write_server(len);
    }
    if(pfds[1].revents){
      len = read(TT.sfd, TT.buff, DATABUFSIZE);
      if(len > 0) read_server(len);
      else{
        printf("Connection closed by foreign host\r\n");
        if(TT.term_ok) tcsetattr(0, TCSADRAIN, &TT.def_term);
        exit(0);
      }
    }
  }
}
