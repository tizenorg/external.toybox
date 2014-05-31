/* ftpget.c - Get a remote file from FTP.
 *
 * Copyright 2012 Ranjan Kumar <ranjankumar.bth@gmail.com>
 *
 * Not in SUSv4.
 * 
USE_FTPGET(NEWTOY(ftpget, "<2cvu:p:P:", TOYFLAG_BIN))
USE_FTPGET(OLDTOY(ftpput,ftpget, "<2vu:p:P:", TOYFLAG_BIN))

config FTPGET
  bool "ftpget/ftpput"
  default y
  help
    usage: ftpget [-c -v -u username -p password P PortNumber] HOST_NAME [LOCAL_FILENAME] REMOTE_FILENAME
    usage: ftpput [-v -u username -p password P PortNumber] HOST_NAME [REMOTE_FILENAME] LOCAL_FILENAME

    ftpget - Get a remote file from FTP.
    ftpput - Upload a local file on remote machine through FTP.

    -c Continue previous transfer.
    -v Verbose.
    -u User name.
    -p Password.
    -P Port Number.
*/
#define FOR_ftpget
#include "toys.h"

GLOBALS(
  char *port;
  char *password;
  char *username;
  FILE *sockfd;
  int c;
  int isget;
)

#define DATACONNECTION_OPENED   125
#define FTPFILE_STATUSOKAY      150
#define FTP_COMMAND_OKAY        200
#define FTPFILE_STATUS          213
#define FTPSERVER_READY         220
#define CLOSE_DATACONECTION     226
#define PASSIVE_MODE            227
#define USERLOGGED_SUCCESS      230
#define PASSWORD_REQUEST        331
#define REQUESTED_PENDINGACTION 350

#define HTTP_DEFAULT_PORT 80
#define FTP_DEFAULT_PORT  21

#ifndef MAX_PORT_VALUE
#define MAX_PORT_VALUE 65535
#endif

typedef struct sockaddr_with_len {
  union {
    struct sockaddr sock;
    struct sockaddr_in sock_in;
    struct sockaddr_in6 sock_in6;
  }sock_u;
  socklen_t socklen;
} sockaddr_with_len;

sockaddr_with_len *socwl;

static void get_file(const char *l_filename, char *r_filename);
static void put_file(const char *r_filename, char *l_filename);
static void close_stream(const char *msg_str);

/*
 * copy string from src to dest -> only number of bytes.
 */
static char *safe_strncpy(char *dst, const char *src, size_t size)
{
  if(!size) return dst;
  dst[--size] = '\0';
  return strncpy(dst, src, size);
}
/*
 * used to converts string into int and validate the input str for invalid int value or out-of-range.
 */
static unsigned get_strtou(const char *str, char **endp, int base)
{
  unsigned long uli;
  char *endptr;

  if(!isalnum(str[0])) {
    errno = ERANGE;
    return UINT_MAX;
  }
  errno = 0;
  uli = strtoul(str, &endptr, base);
  if(uli > UINT_MAX) {
    errno = ERANGE;
    return UINT_MAX;
  }

  if(endp) *endp = endptr;
  if(endptr[0]) {
    if(isalnum(endptr[0]) || errno) { //"123abc" or out-of-range
      errno = ERANGE;
      return UINT_MAX;
    }
    errno = EINVAL;
  }
  return uli;
}

/*
 * verify the host is local unix path.
 * if so, set the swl input param accordingly.
 */
static int is_host_unix(const char *host, sockaddr_with_len **swl)
{
  if(strncmp(host, "local:", 6) == 0) {
    struct sockaddr_un *sockun;
    *swl = xzalloc(sizeof(struct sockaddr_with_len));
    (*swl)->socklen = sizeof(struct sockaddr_un);
    (*swl)->sock_u.sock.sa_family = AF_UNIX;
    sockun = (struct sockaddr_un *)&(*swl)->sock_u.sock;
    safe_strncpy(sockun->sun_path, host + 6, sizeof(sockun->sun_path));
    return 1;
  }
  return 0;
}

/*
 * validate the input param (host) for valid ipv6 ip and extract port number (if there).
 */
static void get_host_and_port(char **host, int *port)
{
  char *ch_ptr;
  const char *org_host = *host;
  if(*host[0] == '[') {
    (*host)++;
    ch_ptr = strchr(*host, ']');
    if(!ch_ptr || (ch_ptr[1] != ':' && ch_ptr[1] != '\0'))
      error_exit("bad address '%s'", org_host);
  }
  else {
    ch_ptr = strrchr(*host, ':');
    //There is more than one ':' like "::1"
    if(ch_ptr && strchr(*host, ':') != ch_ptr)
      ch_ptr = NULL;
  }
  if(ch_ptr) { //pointer to ":" or "]:"
    int size = ch_ptr - (*host) + 1;
    safe_strncpy(*host, *host, size);
    if(*ch_ptr != ':') {
      ch_ptr++; //skip ']'
      //[nn] without port
      if(*ch_ptr == '\0')
        return;
    }
    ch_ptr++; //skip ':' to get the port number.
    *port = get_strtou(ch_ptr, NULL, 10);
    if(errno || (unsigned)*port > MAX_PORT_VALUE)
      error_exit("bad port spec '%s'", org_host);
   }
  return;
}

/*
 * used to extract the address info from the given host ip
 * and update the swl param accordingly.
 */
static int get_socket_stream(const char *host, sa_family_t af, sockaddr_with_len **swl)
{
  struct addrinfo hints;
  struct addrinfo *result, *rp;
  int status = 0;

  memset(&hints, 0 , sizeof(struct addrinfo));
  hints.ai_family = af;
  hints.ai_socktype = SOCK_STREAM;

  if((status = getaddrinfo(host, NULL, &hints, &result)) != 0) {
    perror_exit("bad address '%s' : %s", host, gai_strerror(status));
    return status;
  }

  for(rp = result; rp != NULL; rp = rp->ai_next) {
    if( (rp->ai_family == AF_INET) || (rp->ai_family == AF_INET6)) {
      *swl = xmalloc(sizeof(struct sockaddr_with_len));
      (*swl)->socklen = rp->ai_addrlen;
      memcpy(&((*swl)->sock_u.sock), rp->ai_addr, rp->ai_addrlen);
      break;
    }
  }
  freeaddrinfo(result);
  return ((!rp)? -1: status);
}

/*
 * used to set the port number for ipv4 / ipv6 addresses.
 */
static void setport(struct sockaddr *sock, unsigned port_num)
{
  //for ipv4
  if(sock->sa_family == AF_INET) {
    struct sockaddr_in *sock_in = (void*)sock;
    sock_in->sin_port = port_num;
  }
  //for ipv6
  else if(sock->sa_family == AF_INET6) {
    struct sockaddr_in6 *sock_in6 = (void*)sock;
    sock_in6->sin6_port = port_num;
  }
  return;
}

/*
 * use to get the socket address with the given host ip.
 */
static sockaddr_with_len *get_sockaddr(const char *host, int port, sa_family_t af)
{
  sockaddr_with_len *swl = NULL;
  int status = 0;

  //for unix
  int is_unix = is_host_unix(host, &swl);
  if(is_unix && swl) return swl;

  //[IPV6_ip]:port_num
  if(host[0] == '[' || strrchr(host, ':')) get_host_and_port((char **)&host, &port);

  //for the socket streams.
  status = get_socket_stream(host, af, &swl);
  if(status) return NULL;

  setport(&swl->sock_u.sock, htons(port));
  return swl;
}

/*
 * get the numeric hostname and service name, for a given socket address.
 */
static char *address_to_name(const struct sockaddr *sock)
{
  //man page of getnameinfo.
  char hbuf[NI_MAXHOST] = {0,}, sbuf[NI_MAXSERV] = {0,};
  int status = 0;
  if(sock->sa_family == AF_INET) {
    socklen_t len = sizeof(struct sockaddr_in);
    if((status = getnameinfo(sock, len, hbuf, sizeof(hbuf), sbuf, sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV)) == 0)
      return xmsprintf("%s:%s", hbuf, sbuf);
    else {
      fprintf(stderr, "getnameinfo: %s\n", gai_strerror(status));
      return NULL;
    }
  }
  else if(sock->sa_family == AF_INET6) {
    socklen_t len = sizeof(struct sockaddr_in6);
    if((status = getnameinfo(sock, len, hbuf, sizeof(hbuf), sbuf, sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV)) == 0) {
      //verification for resolved hostname.
      if(strchr(hbuf, ':')) return xmsprintf("[%s]:%s", hbuf, sbuf);
      else return xmsprintf("%s:%s", hbuf, sbuf);
    }
    else {
      fprintf(stderr, "getnameinfo: %s\n", gai_strerror(status));
      return NULL;
    }
  }
  else if(sock->sa_family == AF_UNIX) {
    struct sockaddr_un *sockun = (void*)sock;
    return xmsprintf("local:%.*s", (int) sizeof(sockun->sun_path), sockun->sun_path);
  }
  return NULL;
}

/*
 * used to get the port number for ftp and http.
 */
static unsigned getport(const char *port, const char *protocol, unsigned defport)
{
#define RANGE_STR "Port Number should be in [1-65535] range"
  long v;
  char *endptr = NULL;

  unsigned portnum = defport;
  if(port) {
    errno = 0;
    v = strtol(port, &endptr, 10);
    if((v > 0) && (!*endptr)) {//for numeric like 123
      portnum = v;
      if(portnum > MAX_PORT_VALUE) error_exit("Invalid Port Number '%s' "RANGE_STR, port);
    }
    else if((v == 0) && (endptr != NULL)) {
      switch(defport) {
        case FTP_DEFAULT_PORT:
          if(strcmp(endptr, "ftp") == 0) portnum = defport; //for "ftp" string.
          else goto ERROR_EXIT;
          break;
        case HTTP_DEFAULT_PORT:
          if(strcmp(endptr, "http") == 0) portnum = defport;//for "HTTP" string.
          else goto ERROR_EXIT;
          break;
        default:
ERROR_EXIT:
          error_exit("Invalid Port");
          break;
      }
    }
    else perror_exit("Invalid Port Number: '%s' "RANGE_STR, port);
  }
#undef RANGE_STR
  return (uint16_t)portnum;
}

/*
 * used to connect with the socket.
 */
static int connect_to_stream(const sockaddr_with_len *swl)
{
  int sockfd;
  if((sockfd = socket(swl->sock_u.sock.sa_family, SOCK_STREAM, 0)) < 0)
    perror_exit("cannot open control socket");
  if(connect(sockfd, &swl->sock_u.sock, swl->socklen) < 0) {
    close(sockfd);
    perror_exit("can't connect to remote host");
  }
  return sockfd;
}

/*
 * send command to ftp and get return status.
 */
static int get_ftp_response(const char *command, const char *param)
{
  unsigned cmd_status = 0;
  char *fmt = "%s %s\r\n";
  if(command) {
    if(!param) fmt += 3;
    fprintf(TT.sockfd, fmt, command, param);
    fflush(TT.sockfd);
    if(toys.optflags & FLAG_v) fprintf(stderr, "FTP Request: %s %s\r\n", command, param);
  }

  do {
    if(fgets(toybuf, sizeof(toybuf)-1, TT.sockfd) == NULL) close_stream(NULL);
  }while(!isdigit(toybuf[0]) || toybuf[3] != ' ');

  toybuf[3] = '\0';
  cmd_status = get_int_value(toybuf, 0, INT_MAX);
  toybuf[3] = ' ';
  return cmd_status;
}

/*
 * send request to ftp  for login.
 */
static void send_requests(void)
{
  int cmd_status = 0;
  //FTP connection request.
  if(get_ftp_response(NULL, NULL) != FTPSERVER_READY) close_stream(NULL);

  //230 User authenticated, password please; 331 Password request.
  cmd_status = get_ftp_response("USER", TT.username);
  if(cmd_status == PASSWORD_REQUEST) {//user logged in. Need Password.
    if(get_ftp_response("PASS", TT.password) != USERLOGGED_SUCCESS) close_stream("PASS");
  }
  else if(cmd_status == USERLOGGED_SUCCESS) {/*do nothing*/;}
  else close_stream("USER");
  //200 Type Binary. Command okay.
  if(get_ftp_response("TYPE I", NULL) != FTP_COMMAND_OKAY)
    close_stream("TYPE I");
  return;
}

/*
 * login to ftp.
 */
static void login_to_ftp(void)
{
  int fd = connect_to_stream(socwl);
  TT.sockfd = fdopen(fd, "r+");
  if(!TT.sockfd) perror_exit("unable to connect with ftp:");
  return;
}

/*
 * ftpget/ftpput main function.
 */
void ftpget_main(void)
{
  const char *port = "ftp";
  char **argv = toys.optargs; //host name + file name.
  unsigned long portnum;
  TT.isget = toys.which->name[3] == 'g';

  //if user name is not specified.
  if(!(toys.optflags & FLAG_u) && (toys.optflags & FLAG_p)) error_exit("Missing username:");
  //if user name and password is not specified in command line.
  if(!(toys.optflags & FLAG_u) && !(toys.optflags & FLAG_p)) {
    TT.username = "anonymous";
    TT.password = "anonymous";
  }
  
  if(!(toys.optflags & FLAG_P)) TT.port = (char *)port; //use default port for ftp i.e. 21.
  
  //if continue is not in the command line argument.
  if(TT.isget) {
    if(!(toys.optflags & FLAG_c)) TT.c = 0;
    else TT.c = 1;
  }

  portnum = getport(TT.port, "tcp", 21); //Get port number.
  socwl = get_sockaddr(argv[0], portnum, AF_UNSPEC);
  if(!socwl) error_exit("error in resolving host name");
  if(toys.optflags & FLAG_v) {
    char *str = address_to_name(&socwl->sock_u.sock);
    if(str != NULL) {
      fprintf(stderr, "Connecting to %s (%s)\n", argv[0], str);
      free(str);
      str = NULL;
    }
  }

  login_to_ftp();
  send_requests();

  if(TT.isget) get_file(argv[1], argv[2] ? argv[2] : argv[1]); //argv[1] = local file name; argv[2] = remote file name
  else put_file(argv[1], argv[2] ? argv[2] : argv[1]); //argv[1] = remote file name; argv[2] = local file name
  return;
}

/*
 * send commands to ftp fo PASV mode.
 */
static void verify_pasv_mode(const char *r_filename)
{
  char *pch;
  unsigned portnum;

  //vsftpd reply like:- "227 Entering Passive Mode (125,19,39,117,43,39)".
  if(get_ftp_response("PASV", NULL) != PASSIVE_MODE) close_stream("PASV");

//Response is "NNN <some text> (N1,N2,N3,N4,P1,P2) garbage.
//Server's IP is N1.N2.N3.N4
//Server's port for data connection is P1*256+P2.
  pch = strrchr(toybuf, ')');
  if(pch) *pch = '\0';

  pch = strrchr(toybuf, ',');
  if(pch) *pch = '\0';
  portnum = get_int_value(pch + 1, 0, 255);

  pch = strrchr(toybuf, ',');
  if(pch) *pch = '\0';
  portnum = portnum + (get_int_value(pch + 1, 0, 255) * 256);
  setport(&socwl->sock_u.sock, htons(portnum));

  if(TT.isget) {
    if(get_ftp_response("SIZE", r_filename) != FTPFILE_STATUS) TT.c = 0;
  }
  return;
}

/*
 * verify the local file presence.
 * if present, get the size of the file.
 */
static void is_localfile_present(const char *l_filename)
{
  struct stat sb;
  if(stat(l_filename, &sb) < 0) perror_exit("stat"); //there is no local file with the given name.
  //if local file present, then request for pending file action.
  if(sb.st_size > 0) {
    sprintf(toybuf, "REST %lu", (unsigned long) sb.st_size);
    if(get_ftp_response(toybuf, NULL) != REQUESTED_PENDINGACTION) TT.c = 0;
  }
  else TT.c = 0;
  return;
}

/*
 * transfer the file from ftp to local or from local to ftp.
 */
static void transfer_file(int local_fd, int remote_fd)
{
  if( (local_fd < 0) || (remote_fd < 0)) {
    toys.exitval = 1;
    perror_exit("Error in file creation:");
  }
  if(TT.isget) {
    for(;;) {
      int len = 0;
      len = xread(remote_fd, toybuf, sizeof(toybuf)-1);
      if(!len) break;
      xwrite(local_fd, toybuf, len);
    }
  }
  else {
    for(;;) {
      int len = 0;
      len = xread(local_fd, toybuf, sizeof(toybuf)-1);
      if(!len) break;
      xwrite(remote_fd, toybuf, len);
    }
  }
  return;
}

/*
 * get the file from ftp.
 */
static void get_file(const char *l_filename, char *r_filename)
{
#define IS_DASH(s) ((s)[0] == '-' && !(s)[1])
  int local_fd = -1;
  int remote_fd;

  verify_pasv_mode(r_filename);
  remote_fd = connect_to_stream(socwl); //Connect to data socket.

  //if local file name will be '-' then local fd will be stdout.
  if(IS_DASH(l_filename)) {
    local_fd = 1; //file descriptor will become stdout.
    TT.c = 0;
  }

  //if continue, check for local file existance.
  if(TT.c) is_localfile_present(l_filename);

  //verify the remote file presence.
  if(get_ftp_response("RETR", r_filename) > FTPFILE_STATUSOKAY) close_stream("RETR");

  //if local fd is not stdout, create a file descriptor.
  if(local_fd == -1) {
    int flags;
    if(TT.c) flags = O_APPEND | O_WRONLY;
    else flags = O_CREAT | O_TRUNC | O_WRONLY;
    local_fd = xcreate((char *)l_filename, flags, 0666);
  }
  transfer_file(local_fd, remote_fd);
  xclose(remote_fd);
  xclose(local_fd);
  if(get_ftp_response(NULL, NULL) != CLOSE_DATACONECTION) close_stream(NULL);
  get_ftp_response("QUIT", NULL);
  toys.exitval = EXIT_SUCCESS;
#undef IS_DASH
  return;
}

/*
 * put file to ftp.
 */
static void put_file(const char *r_filename, char *l_filename)
{
#define IS_DASH(s) ((s)[0] != '-' || (s)[1])
  int local_fd = 0; //stdin.
  int remote_fd;
  unsigned cmd_status = 0;

  verify_pasv_mode(r_filename);
  remote_fd = connect_to_stream(socwl); //Connect to data socket.

  //open the local file for transfer.
  if(IS_DASH(l_filename)) local_fd = xcreate((char *)l_filename, O_RDONLY, 0666);

  //verify for the remote file status, Data Connection already open; transfer starting.
  cmd_status = get_ftp_response("STOR", r_filename);
  if( (cmd_status == DATACONNECTION_OPENED) || (cmd_status == FTPFILE_STATUSOKAY)) {
    transfer_file(local_fd, remote_fd);
    xclose(remote_fd);
    xclose(local_fd);
    if(get_ftp_response(NULL, NULL) != CLOSE_DATACONECTION) close_stream(NULL);
    get_ftp_response("QUIT", NULL);
    toys.exitval = EXIT_SUCCESS;
  }
  else {
    xclose(remote_fd);
    xclose(local_fd);
    toys.exitval = EXIT_FAILURE;
    close_stream("STOR");
  }
#undef IS_DASH
  return;
}

/*
 * close ftp connection and print the message.
 */
static void close_stream(const char *msg_str)
{
  char *str;
  //toybuf holds response data.
  //Remove garbage chars (from ' ' space to '\x7f') DEL remote server response.
  for(str = toybuf; *str >= 0x20 && *str < 0x7f; str++) { /**/;}
  *str = '\0';
  if(TT.sockfd) fclose(TT.sockfd);
  if(socwl != NULL) {
    free(socwl);
    socwl = NULL;
  }
  if(msg_str) error_exit("%s server response: %s", msg_str, toybuf);
  else error_exit("server response: %s", toybuf);
}
