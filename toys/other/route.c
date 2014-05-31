/* route.c - Display routing table.
 *
 * Copyright 2012 Ranjan Kumar <ranjankumar.bth@gmail.com>
 *
 * Not in SUSv4.
 *
USE_ROUTE(NEWTOY(route, "?neA:", TOYFLAG_BIN))
config ROUTE
  bool "route"
  default y
  help
    usage: route -neA inet{6} / [{add|del}]

    Display/Edit kernel routing tables.

    -n  Don't resolve names
    -e  Display other/more information
    -A  inet{6} Select Address Family
*/

#define FOR_route
#include "toys.h"
#include <net/route.h>
#include <sys/param.h>
#include <net/if.h>

GLOBALS(
  char *family;
)


#ifndef RTF_IRTT
#define RTF_IRTT 0x0100 //Initial round trip time.
#endif

#define INET4 "inet"
#define INET6 "inet6"
#define DEFAULT_PREFIXLEN 128
#define ACTION_ADD  1
#define INVALID_ADDR 0xffffffffUL
#define IPV6_ADDR_LEN 40 //32 + 7 (':') + 1 ('\0')

#define TEST_ARGV(argv) if(!*argv) show_help()

typedef struct _arglist {
  char *arg;
  int action;
}ARGLIST;

//add - add a new route.
//del - delete a route.
static ARGLIST arglist1[] = {
  { "add", 1 },
  { "del", 2 },
  { "delete", 2 },
  { NULL, 0 }
};

//-net - the target is a network.
//-host - the target is a host.
static ARGLIST arglist2[] = {
  { "-net", 1 },
  { "-host", 2 },
  { NULL, 0 }
};

//Function Declaration
void display_routes6(void);
static void show_route_help(void);
static void setroute(char **);
static void setroute_inet6(char **);

/*
 * used to get the host name from the given ip.
 */
int get_hostname(const char *ipstr, struct sockaddr_in *sockin)
{
  struct hostent *host;
  sockin->sin_family = AF_INET;
  sockin->sin_port = 0;

  if(strcmp(ipstr, "default") == 0) {
    sockin->sin_addr.s_addr = INADDR_ANY;
    return 1;
  }

  if(inet_aton(ipstr, &sockin->sin_addr)) return 0;

  host = gethostbyname(ipstr);
  if(host == NULL) return -1;
  memcpy(&sockin->sin_addr, host->h_addr_list[0], sizeof(struct in_addr));
  return 0;
}

/*
 * used to extract the address info from the given ip.
 */
int get_addrinfo(const char *ip, struct sockaddr_in6 *sock_in6)
{
  struct addrinfo hints, *result;
  int status = 0;

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_INET6;
  if((status = getaddrinfo(ip, NULL, &hints, &result)) != 0) {
    perror_msg("getaddrinfo: %s", gai_strerror(status));
    return -1;
  }
  if(result) {
    memcpy(sock_in6, result->ai_addr, sizeof(*sock_in6));
    freeaddrinfo(result);
  }
  return 0;
}

/*
 * used to get the flag values for route command.
 */
static void get_flag_value(char **flagstr, int flags)
{
  int i = 0;
  char *str = *flagstr;
  static const char flagchars[] = "GHRDMDAC";
  static const unsigned flagarray[] = {
    RTF_GATEWAY,
    RTF_HOST,
    RTF_REINSTATE,
    RTF_DYNAMIC,
    RTF_MODIFIED,
    RTF_DEFAULT,
    RTF_ADDRCONF,
    RTF_CACHE
  };
  *str++ = 'U';
  while( (*str = flagchars[i]) != 0) {
    if(flags & flagarray[i++]) ++str;
  }
}

/*
 * extract inet4 route info from /proc/net/route file and display it.
 */
static void display_routes(int is_more_info, int notresolve)
{
#define IPV4_MASK (RTF_GATEWAY|RTF_HOST|RTF_REINSTATE|RTF_DYNAMIC|RTF_MODIFIED)
  unsigned long dest, gate, mask;
  int flags, ref, use, metric, mss, win, irtt;
  char iface[64]={0,};
  char buf[BUFSIZ] = {0,};
  char *flag_val = xzalloc(10); //there are 9 flags "UGHRDMDAC" for route.

  FILE *fp = xfopen("/proc/net/route", "r");

  xprintf("Kernel IP routing table\n"
                   "Destination     Gateway         Genmask         Flags %s Iface\n",
  	                        is_more_info ? "  MSS Window  irtt" : "Metric Ref    Use");
  fgets(buf, BUFSIZ, fp); //skip 1st line.
  while(fgets(buf, BUFSIZ, fp)) {
     int nitems = 0;
     char *destip = NULL, *gateip = NULL, *maskip = NULL;
     memset(flag_val, 0, 10);

     nitems = sscanf(buf, "%63s%lx%lx%X%d%d%d%lx%d%d%d\n",
                 iface, &dest, &gate, &flags, &ref, &use, &metric, &mask, &mss, &win, &irtt);
     if(nitems != 11) {//EOF with no (nonspace) chars read...
       if((nitems < 0) && feof(fp)) break;
      perror_exit("fscanf");
    }
    //skip down interfaces...
    if(!(flags & RTF_UP)) continue;

    //For Destination
    if(dest){
      if(inet_ntop(AF_INET, &dest, buf, BUFSIZ) > 0) destip = xstrdup(buf);
    }
    else {
      if(!notresolve) destip = xstrdup("default");
      else destip = xstrdup("0.0.0.0");
    }
    //For Gateway
    if(gate){
      if(inet_ntop(AF_INET, &gate, buf, BUFSIZ) > 0) gateip = xstrdup(buf);
    }
    else {
      if(!notresolve) gateip = xstrdup("*");
      else gateip = xstrdup("0.0.0.0");
    }
    //For Mask
    if(inet_ntop(AF_INET, &mask, buf, BUFSIZ) > 0) maskip = xstrdup(buf);

    //Get flag Values
    get_flag_value(&flag_val, (flags & IPV4_MASK));
    if(flags & RTF_REJECT) flag_val[0] = '!';
    xprintf("%-15.15s %-15.15s %-16s%-6s", destip, gateip, maskip, flag_val);
    if(destip) free(destip);
    if(gateip) free(gateip);
    if(maskip) free(maskip);
    if(is_more_info) xprintf("%5d %-5d %6d %s\n", mss, win, irtt, iface);
    else xprintf("%-6d %-2d %7d %s\n", metric, ref, use, iface);
  }//end of while...
  fclose(fp);
  if(flag_val) free(flag_val);
#undef IPV4_MASK
  return;
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
 * find the given parameter in list like add/del/net/host.
 * and if match found return the appropriate action.
 */
static int get_action(char ***argv, ARGLIST *list)
{
  if(!**argv) return 0;

  const ARGLIST *alist;
  for(alist = list; alist->arg; alist++) { //find the given parameter in list and return the action.
    if(strcmp(**argv, alist->arg) == 0) {
      *argv += 1;
      return alist->action;
    }
  }
  return 0;
}

/*
 * route utility main function.
 */
void route_main(void)
{
  char **argv = toys.optargs;
  if(!*argv) {
    if((toys.optflags & FLAG_A) && (strcmp(TT.family, INET6) == 0))
      display_routes6();
    else if( (!(toys.optflags & FLAG_A)) ||
        ((toys.optflags & FLAG_A) && (strcmp(TT.family, INET4) == 0)))
      display_routes((toys.optflags & FLAG_e), (toys.optflags & FLAG_n));
    else show_route_help();
    return;
  }//End of if statement.
  
  if((toys.optflags & FLAG_A) && (strcmp(TT.family, INET6) == 0))
    setroute_inet6(argv);
  else setroute(argv);
  return;
}

/*
 * get prefix len (if any) and remove the prefix from target ip.
 * if no prefix then set netmask as default.
 */
static void is_prefix(char **tip, char **netmask, struct rtentry *rt)
{
  char *prefix = strchr(*tip, '/');
  if(prefix) {
    unsigned long plen;
    plen = get_int_value(prefix + 1, 0, 32);
    //used to verify the netmask and route conflict.
    (((struct sockaddr_in *)&((rt)->rt_genmask))->sin_addr.s_addr) = htonl( ~(INVALID_ADDR >> plen));
    *prefix = '\0';
    rt->rt_genmask.sa_family = AF_INET;
  }
  else *netmask = "default"; //default netmask.
  return;
}

/*
 * used to get the params like: metric, netmask, gw, mss, window, irtt, dev and their values.
 * additionally set the flag values for reject, mod, dyn and reinstate.
 */
static void get_next_params(char **argv, struct rtentry *rt, char **netmask)
{

  while(*argv) {
    //set the metric field in the routing table.
    if(strcmp(*argv, "metric") == 0) {
      //+1 for binary compatibility!
      argv += 1;
      TEST_ARGV(argv);
      rt->rt_metric = get_strtou(*argv, NULL, 10) + 1;
      argv += 1;
    }
    //when adding a network route, the netmask to be used.
    else if(strcmp(*argv, "netmask") == 0) {
      struct sockaddr sock;
      unsigned int addr_mask = (((struct sockaddr_in *)&((rt)->rt_genmask))->sin_addr.s_addr);
      if(addr_mask) show_route_help();
      argv += 1;
      TEST_ARGV(argv);
      *netmask = *argv;
      if(get_hostname(*netmask, (struct sockaddr_in *) &sock) < 0)
        perror_exit("resolving '%s'", *netmask);
      rt->rt_genmask = sock;
      argv += 1;
    }
    //route packets via a gateway.
    else if(strcmp(*argv, "gw") == 0) {
      if(!(rt->rt_flags & RTF_GATEWAY)) {
        int ishost;
        argv += 1;
      TEST_ARGV(argv);
        if((ishost = get_hostname(*argv, (struct sockaddr_in *) &rt->rt_gateway)) == 0) {
          rt->rt_flags |= RTF_GATEWAY;
          argv += 1;
        }
        else if(ishost < 0) perror_exit("resolving '%s'", *argv);
        else perror_exit("gateway '%s' is a NETWORK", *argv);
      }
      else show_route_help();
    }
    //set the TCP Maximum Segment Size (MSS) for connections over this route to M bytes.
    else if(strcmp(*argv, "mss") == 0) {
#define MSS_LOW 64
#define MSS_MAX 32768
      argv += 1;
      TEST_ARGV(argv);
      rt->rt_mss = get_int_value(*argv, MSS_LOW, MSS_MAX);
      rt->rt_flags |= RTF_MSS;
#undef MSS_LOW
#undef MSS_MAX
      argv += 1;
    }
    //set the TCP window size for connections over this route to W bytes.
    else if(strcmp(*argv, "window") == 0) {
#define WIN_LOW 128
      argv += 1;
      TEST_ARGV(argv);
      rt->rt_window = get_int_value(*argv, WIN_LOW, INT_MAX);
      rt->rt_flags |= RTF_WINDOW;
#undef WIN_LOW
      argv += 1;
    }
    //set the initial round trip time (irtt) for TCP connections over this route to I milliseconds (1-12000).
    else if(strcmp(*argv, "irtt") == 0) {
      long nclock_ticks = sysconf(_SC_CLK_TCK); //number of clock ticks per second.
      argv += 1;
      TEST_ARGV(argv);
      nclock_ticks /= 100;
      rt->rt_irtt = strtoul(*argv, NULL, 10);
      if(nclock_ticks > 0) rt->rt_irtt *= nclock_ticks;
      rt->rt_flags |= RTF_IRTT;
      argv += 1;
    }
    //force the route to be associated with the specified device, as the kernel will otherwise try to determine the device on its own.
    else if(strcmp(*argv, "dev") == 0) {
      argv += 1;
      TEST_ARGV(argv);
      if((!rt->rt_dev)) rt->rt_dev = *argv;
      argv += 1;
    }
    //install a blocking route, which will force a route lookup to fail. This is NOT for firewalling.
    else if(strcmp(*argv, "reject") == 0) {
      rt->rt_flags |= RTF_REJECT;
      argv += 1;
    }
    //install a dynamic or modified route. These flags are for diagnostic purposes, and are generally only set by routing daemons.
    else if(strcmp(*argv, "mod") == 0) {
      rt->rt_flags |= RTF_MODIFIED;
      argv += 1;
    }
    else if(strcmp(*argv, "dyn") == 0) {
      rt->rt_flags |= RTF_DYNAMIC;
      argv += 1;
    }
    else if(strcmp(*argv, "reinstate") == 0) {
      rt->rt_flags |= RTF_REINSTATE;
      argv += 1;
    }
    //No match found; exit form the application.
    else show_route_help();
  }//end of while loop.
  if(!rt->rt_dev) {
    if(rt->rt_flags & RTF_REJECT) rt->rt_dev = (char *)"lo";
  }
  return;
}

/*
 * used to verify the netmask and conflict in netmask and route address.
 */
static void verify_netmask(struct rtentry *rt, char *netmask)
{
  unsigned int addr_mask = (((struct sockaddr_in *)&((rt)->rt_genmask))->sin_addr.s_addr);
  unsigned int router_addr = ~(unsigned int)(((struct sockaddr_in *)&((rt)->rt_dst))->sin_addr.s_addr);
  if(addr_mask) {
	addr_mask = ~ntohl(addr_mask);
    if((rt->rt_flags & RTF_HOST) && addr_mask != INVALID_ADDR)
      perror_exit("conflicting netmask and host route");
    if(addr_mask & (addr_mask + 1)) perror_exit("wrong netmask '%s'", netmask);
    addr_mask = ((struct sockaddr_in *) &rt->rt_dst)->sin_addr.s_addr;
    if(addr_mask & router_addr) perror_exit("conflicting netmask and route address");
  }
  return;
}

/*
 * used to update the kermel info.
 */
static void do_ioctl(int sokfd, int action, void *rt)
{
  if(action == ACTION_ADD) {
    if(ioctl(sokfd, SIOCADDRT, rt) < 0) perror_exit("SIOCADDRT");
  }
  else {
    if(ioctl(sokfd, SIOCDELRT, rt) < 0) perror_exit("SIOCDELRT");
  }
  return;
}

/*
 * add/del a route.
 */
static void setroute(char **argv)
{
  struct rtentry rt;
  const char *netmask = NULL;
  const char *targetip;
  int is_net_or_host = 0;
  int sokfd;
  
  //verify the arg for add/del.
  int action = get_action(&argv, arglist1);
  if(!action || !*argv) show_route_help();

  //verify the arg for -net or -host.
  int arg2_action = get_action(&argv, arglist2);
  //next arg can be other than -net or -host.
  if(*argv == NULL) show_route_help();

  memset(&rt, 0, sizeof(struct rtentry));
  targetip = *argv++;
  
  is_prefix((char **)&targetip, (char **)&netmask, &rt);

  if((is_net_or_host = get_hostname(targetip, (struct sockaddr_in *) &rt.rt_dst)) < 0)
    perror_exit("resolving '%s'", targetip);

  if(arg2_action) is_net_or_host = arg2_action & 1;

  rt.rt_flags = ((is_net_or_host) ? RTF_UP : (RTF_UP | RTF_HOST));

  get_next_params(argv, &rt, (char **)&netmask);
  verify_netmask(&rt, (char *)netmask);

  if((action == ACTION_ADD) && (rt.rt_flags & RTF_HOST))
    (((struct sockaddr_in *)&((rt).rt_genmask))->sin_addr.s_addr) = INVALID_ADDR;

  sokfd = socket(AF_INET, SOCK_DGRAM, 0);
  if(sokfd < 0) perror_exit("socket");
  do_ioctl(sokfd, action, &rt);
  xclose(sokfd);
  return;
}

/*
 * get prefix len (if any) and remove the prefix from target ip.
 * if no prefix then set default prefix len.
 */
static void is_prefix_inet6(char **tip, struct in6_rtmsg *rt)
{
  unsigned long plen;
  char *prefix = strchr(*tip, '/');
  if(prefix) {
    *prefix = '\0';
    plen = get_int_value(prefix + 1, 0, DEFAULT_PREFIXLEN);
  }
  else plen = DEFAULT_PREFIXLEN;
  rt->rtmsg_flags = (plen == DEFAULT_PREFIXLEN) ? (RTF_UP | RTF_HOST) : RTF_UP;
  rt->rtmsg_dst_len = plen;
  return;
}

/*
 * used to get the params like: metric, gw, dev and their values.
 * additionally set the flag values for mod and dyn.
 */
static void get_next_params_inet6(char **argv, struct sockaddr_in6 *sock_in6, struct in6_rtmsg *rt, char **dev_name)
{
  while(*argv) {
    //set the metric field in the routing table.
    if(strcmp(*argv, "metric") == 0) {
      argv += 1;
      TEST_ARGV(argv);
      rt->rtmsg_metric = get_strtou(*argv, NULL, 10);
      argv += 1;
    }
    //route packets via a gateway.
    else if(strcmp(*argv, "gw") == 0) {
      if(!(rt->rtmsg_flags & RTF_GATEWAY)) {
        argv += 1;
        TEST_ARGV(argv);
        if(get_addrinfo(*argv, (struct sockaddr_in6 *) &sock_in6) == 0) {
          memcpy(&rt->rtmsg_gateway, sock_in6->sin6_addr.s6_addr, sizeof(struct in6_addr));
          rt->rtmsg_flags |= RTF_GATEWAY;
          argv += 1;
        }
        else perror_exit("resolving '%s'", *argv);
      }
      else show_route_help();
    }
    //force the route to be associated with the specified device, as the kernel will otherwise try to determine the device on its own.
    else if(strcmp(*argv, "dev") == 0) {
      argv += 1;
      TEST_ARGV(argv);
      if(!*dev_name) *dev_name = *argv;
      argv += 1;
    }
    //install a dynamic or modified route. These flags are for diagnostic purposes, and are generally only set by routing daemons.
    else if(strcmp(*argv, "mod") == 0) {
      rt->rtmsg_flags |= RTF_MODIFIED;
      argv += 1;
    }
    else if(strcmp(*argv, "dyn") == 0) {
      rt->rtmsg_flags |= RTF_DYNAMIC;
      argv += 1;
    }
    //Nothing matched.
    else show_route_help();
  }//end of while loop.
  return;
}

/*
 * used to verify the interface name, if interface name is specified.
 * otherwise kernel will try to determine the device on its own.
 */
static void verify_iface(char *dev_name, int action, struct in6_rtmsg *rt)
{
#ifndef IFNAMSIZ
  #define IFNAMSIZ 16
#endif
  //Create a socket.
  int sokfd = socket(AF_INET6, SOCK_DGRAM, 0);
  if(sokfd < 0) perror_exit("socket");
  rt->rtmsg_ifindex = 0;
  if(dev_name) {
    char ifre_buf[sizeof(struct ifreq)] = {0,};
    struct ifreq *ifre = (void *)ifre_buf;
    strncpy(ifre->ifr_name, dev_name, IFNAMSIZ-1);
    if(ioctl(sokfd, SIOGIFINDEX, ifre) < 0) {
      xclose(sokfd);
      perror_exit("ioctl %#x failed", SIOGIFINDEX);
    }
    rt->rtmsg_ifindex = ifre->ifr_ifindex;
  }
  do_ioctl(sokfd, action, rt);
  xclose(sokfd);
  return;
}

/*
 * add/del a route.
 */
static void setroute_inet6(char **argv)
{
  struct sockaddr_in6 sock_in6;
  struct in6_rtmsg rt;
  memset(&sock_in6, 0, sizeof(struct sockaddr_in6));
  memset(&rt, 0, sizeof(struct in6_rtmsg));

  //verify the arg for add/del.
  int action = get_action(&argv, arglist1);
  if(!action || !*argv) show_route_help();

  const char *targetip = *argv++;
  if(*argv) {
    unsigned long plen = 0;
    const char *dev_name = NULL;
    if(!strcmp(targetip, "default")) {
    	rt.rtmsg_flags = RTF_UP;
    	rt.rtmsg_dst_len = plen;
    }
    else {
      is_prefix_inet6((char **)&targetip, &rt);

      if(get_addrinfo(targetip, (struct sockaddr_in6 *) &sock_in6) != 0)
        perror_exit("resolving '%s'", targetip);
    }
    rt.rtmsg_metric = 1; //default metric.
    memcpy(&rt.rtmsg_dst, sock_in6.sin6_addr.s6_addr, sizeof(struct in6_addr));
    get_next_params_inet6(argv, &sock_in6, &rt, (char **)&dev_name);
    verify_iface((char *)dev_name, action, &rt);
  }
  //no more arguments.
  else show_route_help();
  return;
}

/*
 * format the dest and src address in ipv6 format.
 * e.g. 2002:6b6d:26c8:d:ea03:9aff:fe65:9d62
 */
static void ipv6_addr_formating(char *ptr, char *addr)
{
  int i = 0;
  while(i <= IPV6_ADDR_LEN) {
    if(!*ptr) {
      if(i == IPV6_ADDR_LEN) {
        addr[IPV6_ADDR_LEN - 1] = 0; //NULL terminating the ':' seperated address.
        break;
      }
      error_exit("IPv6 ip format error");
    }
    addr[i++] = *ptr++;
    //put ':' after 4th bit
    if(!((i+1) % 5)) addr[i++] = ':';
  }
  return;
}

/*
 * display the ipv6 route info
 * [    Dest addr /     plen ]
* fe80000000000000025056fffec00008 80 \
* [ (?subtree) : src addr/plen : 0/0]
* 00000000000000000000000000000000 00 \
* [    next hop        ][ metric ][ref ctn][ use   ]
* 00000000000000000000000000000000 00000000 00000000 00000000 \
* [ flags ][dev name]
* 80200001     lo
*/
void display_routes6(void)
{
#define IPV6_MASK (RTF_GATEWAY|RTF_HOST|RTF_DEFAULT|RTF_ADDRCONF|RTF_CACHE)
  char ipv6_addr[80] = {0,};
  char ipv6_dest_addr[41] = {0,}; //32 bytes for address, 7 for ':' and 2 for '\0'
  char ipv6_src_addr[41] = {0,}; //32 bytes for address, 7 for ':' and 2 for '\0'
  char ipv6_addr6[128] = {0,};
  char *flag_val = xzalloc(10);
  int prefixlen, len, metric, use, refcount, flag;

  FILE *fp = xfopen("/proc/net/ipv6_route", "r");
  char buf[BUFSIZ] = {0,};

  xprintf("Kernel IPv6 routing table\n");
  xprintf("%-44s%-40s" "Flags Metric Ref    Use Iface\n", "Destination", "Next Hop");
  while(fgets(buf, BUFSIZ, fp)) {
    int nitems = 0;
    char iface[16] = {0,};
    memset(flag_val, 0, 10);

    //7 ':' and one '\0' = 8
    nitems = sscanf(buf, "%32s%x%*s%x%32s%x%x%x%x%10s\n",
      ipv6_dest_addr+8, &prefixlen, &len, ipv6_src_addr+8, &metric, &use, &refcount, &flag, iface);

    if(nitems != 9) {//EOF with no (nonspace) chars read.
      if((nitems < 0) && feof(fp)) break;
      perror_exit("sscanf");
    }

    //skipped down interfaces.
    if(!(flag & RTF_UP)) continue;

    //ipv6_dest_addr+8: as the values are filled from the 8th location of the array.
    ipv6_addr_formating(ipv6_dest_addr+8, ipv6_dest_addr);
    ipv6_addr_formating(ipv6_src_addr+8, ipv6_src_addr);

    //merge dest and src array, as we need to get ip for dest as well as source.
    //it will reduce the duplication of code.
    {
      int i = 0, j = 0;
      while(i < IPV6_ADDR_LEN) {
        ipv6_addr[i] = ipv6_dest_addr[i];
        i++;
      }
      while(j < IPV6_ADDR_LEN) {
        ipv6_addr[i+j] = ipv6_src_addr[j];
        j++;
      }
    }
    get_flag_value(&flag_val, (flag & IPV6_MASK));
    nitems = 0;
    do {
      struct sockaddr_in6 skaddr6;
      char *destip = NULL;
      if(inet_pton(AF_INET6, ipv6_addr + nitems,(struct sockaddr *) &skaddr6.sin6_addr) > 0) {
        skaddr6.sin6_family = AF_INET6;
        if(inet_ntop(AF_INET6, &skaddr6.sin6_addr, buf, BUFSIZ) > 0) {
          destip = xstrdup(buf);
          if(!nitems) {
            sprintf(ipv6_addr6, "%s/%d", destip, prefixlen);
            nitems += IPV6_ADDR_LEN;
            free(destip);
            destip = NULL;
          }
          else {
            xprintf("%-43s %-39s %-5s %-6d %-4d %5d %-8s\n",
                ipv6_addr6, destip, flag_val, metric, refcount, use, iface);
            free(destip);
            destip = NULL;
            break;
          }
        }
      }
    }while(nitems <= IPV6_ADDR_LEN);
  }
  fclose(fp);
  fp = NULL;
  if(flag_val) {
    free(flag_val);
    flag_val = NULL;
  }
#undef IPV6_MASK
  return;
}

/*
 * display help info and exit from application.
 */
static void show_route_help(void)
{
  toys.exithelp = 1;
  error_exit("Invalid Argument");
}
