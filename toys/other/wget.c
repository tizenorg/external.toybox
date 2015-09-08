/* wget.c - Get files from HTTP or FTP.
 *
 * Copyright 2012 Ranjan Kumar <ranjankumar.bth@gmail.com>
 *
 * Not in SUSv4.
 * 
USE_WGET(NEWTOY(wget, "<1csqO:P:Y:U:T#<1", TOYFLAG_BIN))

config WGET
  bool "wget"
  default y
  help
    usage: wget [-c -s -q -O outputfile -P dirprefix -Y Proxy -U useragent -T timeout] URL.

    wget - Get files from HTTP or FTP.

    -c  resume getting a partially-downloaded file.
    -s  spider mode. Don’t download anything, only check file existence.
    -q  quiet (no output).
    -O FILE  write documents to FILE. ('-' for stdout).
    -P DIR  save files to DIR (default .)
    -Y Use   proxy ('on' or 'off').
    -U AGENT Use AGENT for User-Agent header.
    -T SEC  set all timeout values to SECONDS.
*/

#define FOR_wget
#include "toys.h"
#include <sys/poll.h>

/*
 * To Do list: as toybox parsing is not supporting long options.
 *  "passive-ftp"
 *  "header"
 *  "post-data"
 *  "no-check-certificate"
 *
 */

GLOBALS(
  unsigned timeout;
  char *useragent;
  char *proxyflag;
  char *dirprefix;
  char *outputfile;
  int sockfd;
  int flags;
  off_t filepos;
  off_t contentlen;
  int cleanit;
  int chunked;
  int isftp;
)

typedef struct _hostinfo {
  char *hostname;
  const char *username;
  char *password;
  const char *path;
  char *url;
  int portnum;
} HOSTINFO;

//HTTP Response Code
#define HTTP_CONTINUE         100
#define HTTP_OK               200
#define HTTP_NOCONTENT        204
#define HTTP_PARTIALCONTENT   206
#define HTTP_MULTIPLECHOICES  300
#define HTTP_MOVEDPERMANENTLY 301
#define HTTP_FOUND            302
#define HTTP_SEEOTHER         303

//FTP Response Code
#define FTPFILE_STATUSOKAY      150
#define FTPFILE_STATUS          213
#define FTPSERVER_READY         220
#define CLOSE_DATACONECTION     226
#define PASSIVE_MODE            227
#define USERLOGGED_SUCCESS      230
#define PASSWORD_REQUEST        331
#define REQUESTED_PENDINGACTION 350

#define OFLAG_LIST1 O_WRONLY | O_CREAT | O_TRUNC | O_EXCL
#define OFLAG_LIST2 O_WRONLY | O_CREAT | O_TRUNC

#define IS_DASH(s) ((s)[0] == '-' && !(s)[1])
#define HTTPSTR  "http://"
#define FTPSTR "ftp://"
#define FTPPROXY_VAR "ftp_proxy"
#define HTTPPROXY_VAR "http_proxy"

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

sockaddr_with_len *swl;

static void start_download(const char *);
static void parse_url(const char *, HOSTINFO *);
static char *base64_encodeing(char *);
static void get_ftp_data(FILE *);
static void get_http_data(FILE *);
static int sendcommand_to_ftp(const char *, const char *, FILE *);
static void close_connection(const char *);

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
 * Remove white spaces from the given string.
 */
static char *omit_whitespace(char *s)
{
  while(*s == ' ' || (unsigned char)(*s - 9) <= (13 - 9)) s++;
  return (char *) s;
}
/*
 * Remove non white spaces from the given string.
 */
static char *omitnon_whitespace(char *s)
{
  while (*s != '\0' && *s != ' ' && (unsigned char)(*s - 9) > (13 - 9)) s++;
  return (char *) s;
}
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
/* Get last path component with no strip.
 * e.g.
 * "/"    -> "/"
 * "abc"    -> "abc"
 * "abc/def"  -> "def"
 * "abc/def/" -> ""
 */
static char *get_last_path_component_withnostrip(char *path)
{
  char *slash = strrchr(path, '/');
  if (!slash || (slash == path && !slash[1])) return (char*)path;
  return slash + 1;
}
// Find out if the last character of a string matches with the given one.
// Don't underrun the buffer if the string length is 0.
static char *find_last_char(char *str, int c)
{
  if (str && *str) {
    size_t sz = strlen(str) - 1;
    str += sz;
    if ( (unsigned char)*str == c) return (char*)str;
  }
  return NULL;
}
/*
 * Concat path and the file name.
 */
static char *append_pathandfile(char *path, char *fname)
{
  char *c;
  if (!path) path = "";
  c = find_last_char(path, '/');
  while (*fname == '/') fname++;
  return xmsprintf("%s%s%s", path, (c==NULL ? "/" : ""), fname);
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
 * used to connect to socket.
 */
static FILE *do_login(sockaddr_with_len *swl)
{
  FILE *fp;
  int fd = connect_to_stream(swl);
  fp = fdopen(fd, "r+");
  if(!fp) perror_exit("unable to connect:");
  return fp;
}

/*
 * wget main function.
 */
void wget_main(void)
{
  char **argv = toys.optargs; //url or ftp file name.

  if(!(toys.optflags & FLAG_Y)) TT.proxyflag = "on";
  else {
    if(strcmp(TT.proxyflag, "on") && strcmp(TT.proxyflag, "off"))
     error_exit("Proxy will be either on/off");
  }
  if(!(toys.optflags & FLAG_U)) TT.useragent = "Wget";
  if(!(toys.optflags & FLAG_T)) TT.timeout = 900;
  if(!(toys.optflags & FLAG_c)) TT.filepos = 0;
  
  TT.sockfd = -1;

  TT.flags = OFLAG_LIST1;
  //If outputfile name is in the command line argument.
  if(TT.outputfile) {
    if(IS_DASH(TT.outputfile)) {
      TT.sockfd = 1; //stdout
      toys.optflags &= ~FLAG_c;
    }
    //Modify the OFLAG_LIST.
    TT.flags = OFLAG_LIST2;
  }
  while(*argv) start_download(*argv++);
  
  if(TT.sockfd >= 0) xclose(TT.sockfd);
  return;
}

/*
 * get http response data.
 */
static void get_http_responsedata(FILE *fp, char *ch)
{
  char *ptr;
  if(fgets(toybuf, sizeof(toybuf) - 1, fp) == NULL) {
    fclose(fp);
    fp = NULL;
    perror_exit("error in getting response data");
  }
  //remove '\n' and '\r' characters (if any).
  ptr = strchrnul(toybuf, '\n');
  ch[0] = *ptr;
  ch[1] = 0;
  *ptr = '\0';
  ptr = strchrnul(toybuf, '\r');
  *ptr = '\0';
  return;
}

/*
 * get header info of http.
*/
static char *get_header(FILE *fp)
{
  char *str, *headerval;
  char newline_ch;
  get_http_responsedata(fp, &newline_ch); //get header line.

  //end of the headers.
  if(toybuf[0] == '\0') return NULL;
  //convert the header name to lower case.
  for(str = toybuf; isalnum(*str) || *str == '-' || *str == '.'; ++str) {*str |= 0x20;}
  //verify we are at the end of the header name.
  if(*str != ':') perror_exit("bad header line: ");
  //locate the start of the header value.
  *str++ = '\0';
  headerval = omit_whitespace(str);

  if(newline_ch != '\n') {
    for(newline_ch = getc(fp); newline_ch != EOF; newline_ch = getc(fp)) {
      if(newline_ch == '\n') break;
    }
  }
  return headerval;
}

/*
 * resolve host name.
 */
static sockaddr_with_len *resolve_hostname(HOSTINFO *srchostinfo)
{
  sockaddr_with_len *swl = get_sockaddr(srchostinfo->hostname, srchostinfo->portnum, AF_UNSPEC);
  if(!swl) error_exit("error in resolving host name");
  if(!(toys.optflags & FLAG_q)) {
    char *str = address_to_name(&swl->sock_u.sock);
    if(str != NULL) {
      fprintf(stderr, "Connecting to %s (%s)\n", srchostinfo->hostname, str);
      free(str);
      str = NULL;
    }
  }
  return swl;
}

/*
 * prepare http request.
 */
static void prepare_http_request(int isproxy, FILE *srcfp, HOSTINFO *srchostinfo, HOSTINFO *dsthostinfo)
{
  if(isproxy) fprintf(srcfp, "GET %stp://%s/%s HTTP/1.1\r\n", TT.isftp ? "f" : "ht", dsthostinfo->hostname, dsthostinfo->path);
  else fprintf(srcfp, "GET /%s HTTP/1.1\r\n", dsthostinfo->path);
  fprintf(srcfp, "Host: %s\r\nUser-Agent: %s\r\n", dsthostinfo->hostname, TT.useragent);
  //close the connection as soon as task done.
  fprintf(srcfp, "Connection: close\r\n");
  //Use Authenication:
  //<user>:<passwd>" is base64 encoded.
  //URL-decode "user:password" string before base64-encoding:
  //wget http://username:pass%50word@example.com should send
  //which decodes to "username:pass word"
  if(dsthostinfo->username) {
    char *username = base64_encodeing((char *)dsthostinfo->username);
    fprintf(srcfp, "Proxy-Authorization: Basic %s\r\n", username);
    free(username);
    username = NULL;
  }
  if(isproxy && srchostinfo->username) {
    char *username = base64_encodeing((char *)srchostinfo->username);
    fprintf(srcfp, "Proxy-Authorization: Basic %s\r\n", username);
    free(username);
    username = NULL;    
  }

  if(TT.filepos) fprintf(srcfp, "Range: bytes=%lu-\r\n", (unsigned long) TT.filepos);
  fprintf(srcfp, "\r\n");
  fflush(srcfp);
  return;
}

/*
 * get http response.
 */
static int get_http_response(FILE *srcfp, int *cmd_status)
{
  char *str, ch;
  get_http_responsedata(srcfp, &ch);
  str = toybuf;
  //e.g. "HTTP/1.1 301 Moved Permanently"
  //we need to get 301 value.
  str = omitnon_whitespace(str);
  str = omit_whitespace(str);

  str[3] = '\0';
  if(*str == '\0') perror_exit("Error in getting http response status");

  *cmd_status = get_int_value(str, 0, INT_MAX);

  switch(*cmd_status) {
    case HTTP_CONTINUE:
      while(get_header(srcfp) != NULL); //leave all remaining headers(if any).
      return 0;
    case HTTP_OK: /*FALL THROUGH*/
    case HTTP_NOCONTENT:
      break;
    case HTTP_MULTIPLECHOICES: /*FALL THROUGH*/
    case HTTP_MOVEDPERMANENTLY: /*FALL THROUGH*/
    case HTTP_FOUND: /*FALL THROUGH*/
    case HTTP_SEEOTHER:
      break;
    case HTTP_PARTIALCONTENT:
      if(TT.filepos) break;
    default:
      close_connection("server error:");
      break;
  }
  return 1;
}

/*
 * get http header keyword
 * content length - server need to know the length of the content before it starts transmitting.
 * transfer encoding - data transfer mechanism, where server does not need to know the length of the content before it starts transmitting.
 * location - either to load a different web page or the location of a newly-created resource.
 */
static unsigned int get_http_keyword(char **str)
{
  int index = 0;
  static const char HTTP_keywords[] =
    "content-length\0""transfer-encoding\0""location\0";

  char *key_string = (char *)HTTP_keywords;
  //remove '\t' from the http header(if any).
  char *ptr = strchrnul(*str, '\0') - 1;
  while(ptr >= *str && (*ptr == ' ' || *ptr == '\t')) {
    *ptr = '\0';
    ptr--;
  }

  while(*key_string) {
    if(strcmp(key_string, toybuf) == 0) return (index + 1);
    key_string += strlen(key_string) + 1;
    index++;
  }
  return -1;
}

/*
 * verify content length for garbage data.
 */
static void verify_content_len(char *str)
{
  if(TT.contentlen < 0 || errno) {
    strncpy(toybuf, str, strlen(str));
    perror_exit("content-length garbage: ");
  }
  TT.cleanit = 1;
  return;
}

/*
 * verify location for change location.
 * if new location change source host and dest host info accordingly.
 */
static int verify_location(int isproxy, char *str, HOSTINFO **srchostinfo, HOSTINFO **dsthostinfo)
{
  int is_new_location = 0;
  static int rd_dirlevel = 5; //5 level of directory read limit.
  HOSTINFO *shostinfo = *srchostinfo;
  HOSTINFO *dhostinfo = *dsthostinfo;

  //verify the directory read level.
  if(--rd_dirlevel == 0) perror_exit("max dir level reached");
  //if location is changed.
  if(str[0] == '/') dhostinfo->path = xstrdup(str+1);
  else {
    parse_url(str, dhostinfo);
    if(!isproxy) {
      if(shostinfo->url) {
        free(shostinfo->url);
        shostinfo->url = NULL;
      }
      shostinfo->hostname = dhostinfo->hostname;
      shostinfo->portnum = dhostinfo->portnum;
      is_new_location = 1;
    }
  }
  return is_new_location;
}

/*
 * Convert the input string in the lower.
 */
static char *upper_to_lower(char *str)
{
  char *c;
  for(c = str; *c; ++c) *c = tolower(*c);
  return str;
}

/*
 * create http session and verify the data.
 */
static FILE *create_http_session(int isproxy, HOSTINFO *srchostinfo, HOSTINFO *dsthostinfo)
{
#define CONTENT_KEY 1
#define TRANSFER_KEY 2
#define LOCATION_KEY 3

  sockaddr_with_len *swl;
  FILE *srcfp; //socket to web server.
  char *str;
  int status = 0;
  int isreadall = 0;

NEWADDRESS:
  swl = resolve_hostname(srchostinfo);
NEWSESSION:
  TT.chunked = TT.cleanit = 0;
  srcfp = do_login(swl); //Open socket for http server.

  //Prepare HTTP request.
  prepare_http_request(isproxy, srcfp, srchostinfo, dsthostinfo);

  //Get HTTP response.
  do {
    isreadall = get_http_response(srcfp, &status);
  } while(!isreadall);

  //getting HTTP headers.
  while((str = get_header(srcfp)) != NULL) {
    unsigned key = get_http_keyword(&str);

    if(key == CONTENT_KEY) {
      TT.contentlen = get_strtou(str, NULL, 10);
      verify_content_len(str);
      continue;
    }
    if(key == TRANSFER_KEY) {
      if(strcmp(upper_to_lower(str), "chunked") != 0) {
        strncpy(toybuf, str, strlen(str));
        perror_exit("transfer encoding chunked is not supported: ");
      }
      TT.chunked = 1;
    }
    //HTTP Location header - either to load a different web page or location of a newly-created resource.
    if(key == LOCATION_KEY && status >= HTTP_MULTIPLECHOICES) {
      int is_new_location = verify_location(isproxy, str, &srchostinfo, &dsthostinfo);
      fclose(srcfp);
      if(is_new_location) {
        if(swl) {
          free(swl);
          swl = NULL;
        }
        goto NEWADDRESS;
      }
      else goto NEWSESSION;
    }
  }//end of while loop
  if(swl) {
    free(swl);
    swl = NULL;
  }
#undef CONTENT_KEY
#undef TRANSFER_KEY
#undef LOCATION_KEY
  return srcfp;
}

/*
 * send request to ftp  for login.
 */
static void send_ftp_request(FILE *srcfp, HOSTINFO *dsthostinfo, sockaddr_with_len *swl)
{
  char *pch;
  unsigned portnum;
  //FTP connection request.
  if(sendcommand_to_ftp(NULL, NULL, srcfp) != FTPSERVER_READY) close_connection(NULL);

  //resolve password.
  dsthostinfo->password = strchr(dsthostinfo->username, ':');
  if(dsthostinfo->password) *dsthostinfo->password++ = '\0';

  //230 User authenticated, password please; 331 Password request.
  switch(sendcommand_to_ftp("USER ", dsthostinfo->username, srcfp)) {
    case USERLOGGED_SUCCESS:
      break;
    case PASSWORD_REQUEST: //user logged in... Need Password.
      if(sendcommand_to_ftp("PASS ", dsthostinfo->password, srcfp) != USERLOGGED_SUCCESS) close_connection("PASS");
      break;
    default://login failed.
      close_connection("USER");
      break;
  }
  sendcommand_to_ftp("TYPE I", NULL, srcfp); //200 Type Binary... Command okay.
  if(sendcommand_to_ftp("SIZE ", dsthostinfo->path, srcfp) == FTPFILE_STATUS) {
    //remove '\n' and '\r' char from toybuf buffer to get the size of the file.
    char *ptr = strchrnul(toybuf, '\n');
    *ptr = '\0';
    ptr = strchrnul(toybuf, '\r');
    *ptr = '\0';
    TT.contentlen = get_strtou(toybuf + 4, NULL, 10);
    verify_content_len(toybuf);
  }
  if(sendcommand_to_ftp("PASV", NULL, srcfp) != PASSIVE_MODE) close_connection("PASV");
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
  setport(&swl->sock_u.sock, htons(portnum));
  return;
}

/*
 * create ftp session and verify modes of ftp.
 */
static FILE *create_ftp_session(FILE **datafp, HOSTINFO *srchostinfo, HOSTINFO *dsthostinfo)
{
  FILE *srcfp; //socket to web/ftp server.
  int alloc_flag = 0;
  sockaddr_with_len *swl = resolve_hostname(srchostinfo);

  if(!dsthostinfo->username || (strlen(dsthostinfo->username) == 0)) {
    alloc_flag = 1;
    dsthostinfo->username = xstrdup("anonymous:anonymous");
  }

  srcfp = do_login(swl);
  send_ftp_request(srcfp, dsthostinfo, swl);

  *datafp = do_login(swl);

  if(TT.filepos) {
    sprintf(toybuf, "REST %lu", (unsigned long) TT.filepos);
    if(sendcommand_to_ftp(toybuf, NULL, srcfp) == REQUESTED_PENDINGACTION) TT.contentlen = TT.contentlen - TT.filepos;
  }

  if(sendcommand_to_ftp("RETR ", dsthostinfo->path, srcfp) > FTPFILE_STATUSOKAY) close_connection("RETR");

  if(swl) {
    free(swl);
    swl = NULL;
  }
  if(alloc_flag) {
    free((char *)dsthostinfo->username);
    dsthostinfo->username = NULL;
  }
  return srcfp;
}

/*
 * clean http data (remove '\n' and '\r' and get the content length).
 */
static int clean_http_data(FILE *datafp)
{
  int status = 0;
  char ch;
  get_http_responsedata(datafp, &ch);
  TT.contentlen = get_strtou(toybuf, NULL, 16);
  if(TT.contentlen == 0) status = 1;
  TT.cleanit = 1;
  return status;
}

/*
 * transfer the file content from srouce to destination.
 */
static void transfer_data(FILE *srcfp, FILE *datafp)
{
  if(!(toys.optflags & FLAG_s)) {
    if(TT.sockfd < 0) TT.sockfd = xcreate(TT.outputfile, TT.flags, 0666);
    //for ftp data download.
    if(srcfp != datafp) get_ftp_data(datafp);
    else {//for http data download.
      int iscleaned = 0;
      if(TT.chunked) iscleaned = clean_http_data(datafp);
      if(!iscleaned) get_http_data(datafp);
    }
    if(!(toys.optflags & FLAG_O)) {
      xclose(TT.sockfd);
      TT.sockfd = -1;
    }
  }
  //Close ftp connection.
  if(datafp != srcfp) {
    fclose(datafp);
    if(sendcommand_to_ftp(NULL, NULL, srcfp) != CLOSE_DATACONECTION) close_connection("ftp error: ");
  }
  return;
}

/*
 * get proxy information.
 */
static int get_proxy_info(const char *url, HOSTINFO *dsthostinfo, HOSTINFO *srchostinfo)
{
  int isproxy;
  char *proxy = NULL;
  parse_url(url, dsthostinfo);
  isproxy = (strncmp(TT.proxyflag, "off", 3) != 0);
  if(isproxy) {
    proxy = getenv(TT.isftp ? FTPPROXY_VAR : HTTPPROXY_VAR);
    isproxy = (proxy && proxy[0]);
    if(isproxy) parse_url(proxy, srchostinfo);
  }
  if(!isproxy) {
    srchostinfo->portnum = dsthostinfo->portnum;
    srchostinfo->hostname = srchostinfo->url = xstrdup(dsthostinfo->hostname);
  }
  return isproxy;
}

/*
 * download http/ftp url.
 */ 
static void start_download(const char *url)
{
  HOSTINFO srchostinfo, dsthostinfo;
  int isproxy;
  char *outputfile = NULL;

  FILE *srcfp; //socket to web/ftp server.
  FILE *datafp; //socket to ftp server (data).

  srchostinfo.username = dsthostinfo.username = NULL;
  srchostinfo.url = dsthostinfo.url = NULL;

  isproxy = get_proxy_info(url, &dsthostinfo, &srchostinfo);

  //if -O option is not define then directory prefix can be there.
  if(!(toys.optflags & FLAG_O)) {
    TT.outputfile = get_last_path_component_withnostrip((char *)dsthostinfo.path);
    if(!TT.outputfile[0] || TT.outputfile[0] == '/') TT.outputfile = (char*)"index.html";
    //Keep a local copy of output file
    if(TT.dirprefix) TT.outputfile = outputfile = append_pathandfile(TT.dirprefix, TT.outputfile);
    else TT.outputfile = outputfile = xstrdup(TT.outputfile);
  }

  //Reset the file postion as begining of the file.
  TT.filepos = 0;
  if(toys.optflags & FLAG_c) {
    TT.sockfd = open(TT.outputfile, O_WRONLY);
    //if file is present, move the file cursor.
    if(TT.sockfd >= 0) TT.filepos = xlseek(TT.sockfd, 0, SEEK_END);
  }

  //Create HTTP session.
  if(isproxy || !TT.isftp) {
    srcfp = create_http_session(isproxy, &srchostinfo, &dsthostinfo);
    datafp = srcfp;
  }
  //Create FTP session.
  else srcfp = create_ftp_session(&datafp, &srchostinfo, &dsthostinfo);

  transfer_data(srcfp, datafp);

  if(srcfp) fclose(srcfp);

  if(outputfile) {
    free(outputfile);
    outputfile = NULL;
  }
  if(dsthostinfo.url) {
    free(dsthostinfo.url);
    dsthostinfo.url = NULL;
  }
  if(srchostinfo.url) {
    free(srchostinfo.url);
    srchostinfo.url = NULL;
  }
  return;
}

/*
 * used to verify the timeout.
 */
static int is_timeout(int fd, int event, unsigned int timeinms, int *revents)
{
  struct pollfd pfds[1];
  int ret = 0;

  pfds[0].fd = fd;
  pfds[0].events = event;

  ret = poll(pfds, 1, timeinms);
  *revents = pfds[0].revents;
  return ret;
}

/*
 * get ftp data.
 */
static void get_ftp_data(FILE *datafp)
{
  int srcfd;
  int revents = 0;
  srcfd = fileno(datafp);
  unsigned int timeinms = (TT.timeout *1000);

  fprintf(stderr, "Please wait Downloading...\n");
  for(;;) {
    int len = 0;
    int ret = is_timeout(srcfd, POLLIN, timeinms, &revents);
    if(ret < 0) {
      if(timeinms != 0 && --timeinms == 0) error_exit("download timed out");
      continue;
    }
    if(ret == 0) error_exit("download timed out");

    if(revents) {
      len = xread(srcfd, toybuf, sizeof(toybuf) - 1);
      if(!len) break;
    }
    revents = 0;
    ret = is_timeout(TT.sockfd, POLLOUT, timeinms, &revents);
    if(ret < 0) {
      if(timeinms != 0 && --timeinms == 0) error_exit("download timed out");
      continue;
    }
    if(ret == 0) error_exit("download timed out");

    if(revents) xwrite(TT.sockfd, toybuf, len);
  }
  return;
}

/*
 * get http data.
 */
static void get_http_data(FILE *datafp)
{
  int srcfd = fileno(datafp);
  unsigned int timeinms = (TT.timeout *1000);
  int iscleaned = 0;
  char ch;

  fprintf(stderr, "Please wait Downloading...\n");
  for(;;) {
    for(;;) {
      int nitems = 0, revents = 0;

      //there is no content to read.
      if((int)TT.contentlen <= 0) break;
      clearerr(datafp);
      errno = 0;
      int ret = is_timeout(srcfd, POLLIN, timeinms, &revents);
      if(ret < 0) {
        if(timeinms != 0 && --timeinms == 0) error_exit("download timed out");
        continue;
      }
      if(ret == 0) error_exit("download timed out");

      if(revents) {
        unsigned readsize = sizeof(toybuf);
        //read data upto content length.
        if(TT.contentlen < readsize) readsize = TT.contentlen;
        nitems = fread(toybuf, 1, readsize, datafp);
        if(nitems <= 0) {
          if(errno == EWOULDBLOCK) continue;
          if(ferror(datafp)) perror_exit("read error");
          break;
        }
      }

      revents = 0;
      ret = is_timeout(TT.sockfd, POLLOUT, timeinms, &revents);
      if(ret < 0) {
        if(timeinms != 0 && --timeinms == 0) error_exit("download timed out");
        continue;
      }
      if(ret == 0) error_exit("download timed out");
      
      if(revents) xwrite(TT.sockfd, toybuf, nitems);

      if((TT.contentlen -= nitems) == 0) break;
    }//inner loop.
    clearerr(datafp);
    if(!TT.chunked) break;
    get_http_responsedata(datafp, &ch);
    iscleaned = clean_http_data(datafp);
    if(iscleaned) break;
  }
  return;
}

/*
 * send command to ftp and return the status of ftp command.
 */
static int sendcommand_to_ftp(const char *str1, const char *str2, FILE *fp)
{
  unsigned cmd_status = 0;
  if(str1){
    if(!str2) str2 = "";
    fprintf(fp, "%s%s\r\n", str1, str2);
    fflush(fp);
  }
  do{
    if(fgets(toybuf, sizeof(toybuf) - 1, fp) == NULL) {
      fclose(fp);
      close_connection(NULL);
    }
  }while(!isdigit(toybuf[0]) || toybuf[3] != ' ');

  toybuf[3] = '\0';
  cmd_status = get_int_value(toybuf, 0, INT_MAX);
  toybuf[3] = ' ';
  return cmd_status;
}

/*
 * used to verify the url for http or ftp.
 */
static void isftp_or_http(const char *srcurl, HOSTINFO **hinfo)
{
  HOSTINFO *host_info = *hinfo;
  char *url;
  if(host_info->url) {
    free(host_info->url);
    host_info->url = NULL;
  }
  host_info->url = url = xstrdup((char *)srcurl);
  if(strncmp(url, HTTPSTR, 7) == 0) {
    host_info->portnum = getport("http", "tcp", 80);
    host_info->hostname = url + 7;
    TT.isftp = 0;
  } else if(strncmp(url, FTPSTR, 6) == 0) {
    host_info->portnum = getport("ftp", "tcp", 21);
    host_info->hostname = url + 6;
    TT.isftp = 1;
  } else {
    error_exit("not an http or ftp url: %s", url);
  }
  return;
}

/*
 * get source path from given url.
 * e.g. scheme://username:password@domain:port/path?query_string#fragment_id
 * The fragment-id follows the URL of the whole object from which it is separated
 * by a hash sign (#). If the fragment-id is void, the hash sign may be omitted:
 * A void fragment-id with or without the hash sign means that the URL refers to the whole object.
 */

static void get_srcpath(HOSTINFO **hinfo)
{
  HOSTINFO *host_info = *hinfo;
  char *srcpath, *query, *fragment_id;
  srcpath = strchr(host_info->hostname, '/'); //for the source path. If not then path is "".
  query = strchr(host_info->hostname, '?'); //for the query. abc.com?login=Name1@Name2
  if(!srcpath || (query && srcpath > query)) srcpath = query;

  fragment_id = strchr(host_info->hostname, '#');
  if(!srcpath || (fragment_id && srcpath > fragment_id)) srcpath = fragment_id;
  if(!srcpath) host_info->path = "";
  else if(*srcpath == '/') {
    *srcpath = '\0';
    host_info->path = srcpath + 1;
  } else {// '#' or '?'
    //hostname = scheme://username:password@domain:port/path?query_string#fragment_id
    //after memmove "hostname = scheme:/username:password@domain:port/path?query_string#fragment_id"
    //as http request needs one '/' after GET string. e.g. 'GET /?login=john@doe HTTP/1.0'
    memmove(host_info->hostname - 1, host_info->hostname, srcpath - host_info->hostname);
    host_info->hostname--;
    srcpath[-1] = '\0'; //NULL terminate the request string.
    host_info->path = srcpath;
  }
  return;
}
/*
 * Get user info from given url.
 */
static void get_usrinfo(HOSTINFO **hinfo)
{
  HOSTINFO *host_info = *hinfo;
  char *srcpath = strrchr(host_info->hostname, '@');
  if(srcpath != NULL) {
    host_info->username = host_info->hostname;
    *srcpath = '\0';
    host_info->hostname = srcpath + 1;
  }
  srcpath = host_info->hostname;
  return;
}
/*
 * parse url given by user.
 */
static void parse_url(const char *srcurl, HOSTINFO *hinfo)
{
  isftp_or_http(srcurl, &hinfo);
  get_srcpath(&hinfo);
  get_usrinfo(&hinfo);
  return;
}

static char encoding_table[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
                'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
                'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
                'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
                'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
                'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
                'w', 'x', 'y', 'z', '0', '1', '2', '3',
                '4', '5', '6', '7', '8', '9', '+', '/'
};
/* 
 * The rfc (Request for Comments) states that non base64 chars are to be ignored.
 * str param, a pointer to a base64 encoded string.
 * Base64 encoding converts 3 octets into 4 encoded characters. (Reference:http://en.wikipedia.org/wiki/Base64)
 * e.g.Encoded in ASCII, the characters M, a, and n are stored as the bytes 77, 97, and 110,
 * which are the 8-bit binary values 01001101, 01100001, and 01101110. These three values are
 * joined together into a 24-bit string, producing 010011010110000101101110.
 * Groups of 6 bits are converted into individual numbers from left to right.
 * (in this case, there are four numbers in a 24-bit string),
 * which are then converted into their corresponding Base64 character values.
 * The Base64 index table:
 * char      val
 * A-Z      0-25
 * a-z       26-51
 * 0-1      52-61
 * '+'      62
 * '/'      63
*/
static char *base64_encodeing(char *str)
{
  char *instr, *temp_instr;
  unsigned ch = 0;
  int i = 0;

  int len = strlen(str);
  int padding = len % 3;

  if(padding) {
    if(padding == 1) {
      instr = xstrndup(str, strlen(str) + 3);
      strcat(instr, "00");
    }
    else {
      instr = xstrndup(str, strlen(str) + 2);
      strcat(instr, "0");
    }
    len = ((len / 3) * 4) + 4;
  }
  else {
    instr = str;
    len = (len / 3) * 4;
  }

  char *ptr = xzalloc(len + 1);
  char *temp_ptr = ptr;
  temp_instr = instr;

  while(*instr) {
    int val = *instr++;

    ch = (ch << 8) | val;
    i++;
    if(i == 3) {
      int counter = 4;
      unsigned mask = 0x3F;
      while(counter) {
        temp_ptr[counter - 1] = ch & mask;
        ch = ch >> 6;
        counter--;
      }
      temp_ptr += 4;
      i = 0;
    }
  }

  ptr[len] = '\0';
  i = 0;
  while(ptr[i]) {
    int val = ptr[i];
    ptr[i++] = (char) encoding_table[val];
  }
  if(padding == 1) {
    ptr[len - 2] = '=';
    ptr[len - 1] = '=';
  }
  else ptr[len - 1] = '=';
  if(temp_instr) {
    free(temp_instr);
    temp_instr = NULL;
  }
  return ptr;
}

/*
 * close http/ftp connection and print the message.
 */
static void close_connection(const char *msgstr)
{
  char *str = toybuf; //buf holds peer's response.
  //Remove garbage characters from remote server response.
  while (*str >= 0x20 && *str < 0x7f) // from ' ' space to '\x7f' DEL
    str++;
  *str = '\0';
  if(TT.sockfd >= 0) xclose(TT.sockfd);
  perror_exit("%s %s", (msgstr ? msgstr : ""), toybuf);
}
