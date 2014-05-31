/* nslookup.c - query Internet name servers
 *
 * Copyright 2013 Ashwini Kumar <ak.ashwini@gmail.com>
 *

USE_NSLOOKUP(NEWTOY(nslookup, "<1>2", TOYFLAG_USR|TOYFLAG_BIN))

config NSLOOKUP
  bool "nslookup"
  default y
  help
    usage: nslookup [HOST] [SERVER]

    Query the nameserver for the IP address of the given HOST
    optionally using a specified DNS server.

    Note:- Only non-interactive mode is supported.
*/

#define FOR_nslookup
#include "toys.h"
#include <resolv.h>

#define DNS_PORT  53

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

void print_addrs(char *hostname, char *msg)
{
  struct addrinfo hints, *res = NULL, *cur = NULL;
  int ret_ga;
  char *n = xstrdup(hostname), *p, *tmp;
  tmp = n;
  if((*n == '[') && (p = strrchr(n, ']')) != NULL ) {
    n++;
    *p = '\0';
  }

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_socktype = SOCK_STREAM;

  ret_ga = getaddrinfo(n, NULL, &hints, &res);                                                                                                                    
  if (ret_ga) error_exit("Hostname %s", gai_strerror(ret_ga));

  cur = res;
  while(cur) {
    char *colon = NULL;
    char *name = address_to_name(cur->ai_addr);
    if(name) {
      colon = strrchr(name, ':');
      if(colon) *colon = '\0';
      xprintf("%-8s %s\n",msg, hostname);
      xprintf("Address: %s\n", name);
      free(name);
    }
    cur = cur->ai_next;
  }
  if (!res->ai_addr) error_exit("getaddrinfo failed");

  freeaddrinfo(res);
  free(tmp);
}

/* set the default DSN if it ,toys.optargs[1], provided */
void set_print_dns()
{
  struct sockaddr *sock = NULL;
  char *colon = NULL, *name = NULL;

  res_init(); //initialize the _res struct, for DNS name.

  if(toys.optargs[1]) { //set the default DNS
    sockaddr_with_len *swl = get_sockaddr(toys.optargs[1], DNS_PORT, AF_UNSPEC);
    if(!swl) perror_exit("bad DNS name '%s'", toys.optargs[1]);

    if(swl->sock_u.sock.sa_family == AF_INET) {
      _res.nscount = 1;
      _res.nsaddr_list[0] = swl->sock_u.sock_in;
    }
    if(swl->sock_u.sock.sa_family == AF_INET6) {
      _res._u._ext.nscount = 1;
      _res._u._ext.nsaddrs[0] = &swl->sock_u.sock_in6;
    }
  }

  sock = (struct sockaddr*)_res._u._ext.nsaddrs[0];
  if(!sock) sock = (struct sockaddr*)&_res.nsaddr_list[0];

  name = address_to_name(sock);
  if(name) {
    colon = strrchr(name, ':');
    if(colon) *colon = '\0';
    print_addrs(name, "Server:");
    free(name);
  }
  puts("");
}

void nslookup_main(void)
{
  if(toys.optargs[0][0] == '-') {
    toys.exithelp = 1;
    error_exit("");
  }
  set_print_dns();
  print_addrs(toys.optargs[0], "Name:");
}
