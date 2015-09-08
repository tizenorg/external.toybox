/* ping.c - ping program.
 *
 * Copyright 2012 Ashwini Kumar <ak.ashwini@gmail.com>
 *
 * Not in SUSv4.
 
USE_PING(NEWTOY(ping, "<1>1t#<0>255c#<0s#<0>65535I:W#<0w#<0q46", TOYFLAG_STAYROOT|TOYFLAG_USR|TOYFLAG_BIN))
 
config PING
  bool "ping"
  default y
  help
    usage: ping [OPTIONS] HOST

    Send ICMP ECHO_REQUEST packets to network hosts

    Options:
    -4, -6      Force IP or IPv6 name resolution
    -c CNT      Send only CNT pings
    -s SIZE     Send SIZE data bytes in packets (default:56)
    -t TTL      Set TTL
    -I IFACE/IP Use interface or IP address as source
    -W SEC      Seconds to wait for the first response (default:10)
                (after all -c CNT packets are sent)
    -w SEC      Seconds until ping exits (default:infinite)
                (can exit earlier with -c CNT)
    -q          Quiet, only displays output at start
                and when finished
*/
#define FOR_ping 
#include "toys.h"

#include <netinet/in_systm.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <ifaddrs.h>

GLOBALS(
  long wait_exit;
  long wait_resp;
  char *iface;
  long size;
  long count;
  long ttl;
)


#ifndef MAX
#define MAX(x,y) (x > y ? x : y)
#endif

#define  MAXPACKET  (IP_MAXPACKET-60-8)  /* max packet size */

#define F_QUIET    0x0001    /* minimize all output */
#define F_TIMING  0x0002    /* room for a timestamp */
#define F_SOURCE_ADDR  0x0004    /* set source IP address/interface */
#define F_MCAST    0x0008    /* multicast target */

/* MAX_DUP_CHK is the number of bits in received table, the
 *  maximum number of received sequence numbers we can track to check
 *  for duplicates.
 */
#define MAX_DUP_CHK   (8 * 2048)
u_char  rcvd_tbl[MAX_DUP_CHK/8];
int   nrepeats = 0;
#define A(seq)  rcvd_tbl[(seq/8)%sizeof(rcvd_tbl)]  /* byte in array */
#define B(seq)  (1 << (seq & 0x07))  /* bit in byte */
#define SET(seq) (A(seq) |= B(seq))
#define CLR(seq) (A(seq) &= (~B(seq)))
#define TST(seq) (A(seq) & B(seq))

struct tv32 {
  int32_t tv32_sec;
  int32_t tv32_usec;
};


u_char  *packet;
int  packlen;
int  pingflags = 0, ping_options;
char  *fill_pat;

int s;          /* Socket file descriptor */
int sloop;        /* Socket file descriptor/loopback */

#define PHDR_LEN sizeof(struct tv32)  /* size of timestamp header */
struct sockaddr_in whereto, send_addr;  /* Who to ping */
struct sockaddr_in src_addr;    /* from where */
struct sockaddr_in loc_addr;    /* 127.1 */
int datalen = 64 - PHDR_LEN;    /* How much data */

#define MAXHOSTNAMELEN  64
char hostname[MAXHOSTNAMELEN];

static struct {
  struct ip  o_ip;
  char    o_opt[MAX_IPOPTLEN];
  union {
    u_char    u_buf[MAXPACKET+offsetof(struct icmp, icmp_data)];
    struct icmp u_icmp;
  } o_u;
} out_pack;
#define  opack_icmp  out_pack.o_u.u_icmp

char optspace[MAX_IPOPTLEN];    /* record route space */
int optlen;


int npackets = 0;        /* total packets to send */
int ntransmitted;      /* output sequence # = #sent */
int ident;        /* our ID, in network byte order */
int nreceived;        /* # of packets we got back */
double interval = 1.0;      /* interval between packets */
struct timeval interval_tv;
double tmin = 999999999.0;
double tmax = 0.0;
double tsum = 0.0;      /* sum of all times */
double tsumsq = 0.0;
double maxwait = 0.0;
double wait_resp = 10.0;
int bufspace = IP_MAXPACKET;
struct timeval now, clear_cache, last_tx, next_tx, first_tx;
struct timeval last_rx, first_rx;
int lastrcvd = 1;      /* last ping sent has been received */

static void pr_pack_sub(int cc, char *addr, int seqno,
    int dupflag, int ttl, double triptime)
{
  (void)printf("%d bytes from %s: seq=%u", cc, addr, seqno);
  if (dupflag) (void)printf(" DUP!");
  (void)printf(" ttl=%d", ttl);
  if (pingflags & F_TIMING)
    (void)printf(" time=%.3f ms", triptime*1000.0);
}

/* Compute the IP checksum
 *  This assumes the packet is less than 32K long.
 */
static u_int16_t in_cksum(u_int16_t *p, u_int len)
{
  u_int32_t sum = 0;
  int nwords = len >> 1;

  while (nwords-- != 0)
    sum += *p++;

  if (len & 1) {
    union {
      u_int16_t w;
      u_int8_t c[2];
    } u;
    u.c[0] = *(u_char *)p;
    u.c[1] = 0;
    sum += u.w;
  }

  /* end-around-carry */
  sum = (sum >> 16) + (sum & 0xffff);
  sum += (sum >> 16);
  return (~sum);
}

// compute the difference of two timevals in seconds
static double diffsec(struct timeval *timenow, struct timeval *then)
{
  return ((timenow->tv_sec - then->tv_sec)*1.0
      + (timenow->tv_usec - then->tv_usec)/1000000.0);
}

static void timevaladd(struct timeval *t1, struct timeval *t2)
{
  t1->tv_sec += t2->tv_sec;
  if ((t1->tv_usec += t2->tv_usec) >= 1000000) {
    t1->tv_sec++;
    t1->tv_usec -= 1000000;
  }
}

static void sec_to_timeval(const double sec, struct timeval *tp)
{
  tp->tv_sec = sec;
  tp->tv_usec = (sec - tp->tv_sec) * 1000000.0;
}

static double timeval_to_sec(const struct timeval *tp)
{
  return tp->tv_sec + tp->tv_usec / 1000000.0;
}

/*
 * Print statistics.
 * Heavily buffered STDIO is used here, so that all the statistics
 * will be written with 1 sys-write call.  This is nice when more
 * than one copy of the program is running on a terminal;  it prevents
 * the statistics output from becomming intermingled.
 */
static void summary(int header)
{
  if (header)
    (void)printf("\n--- %s PING Statistics ---\n", hostname);
  (void)printf("%d packets transmitted, ", ntransmitted);
  (void)printf("%d packets received, ", nreceived);
  if (nrepeats)
    (void)printf("+%d duplicates, ", nrepeats);
  if (ntransmitted) {
    if (nreceived > ntransmitted)
      (void)printf("-- somebody's duplicating packets!");
    else
      (void)printf("%.1f%% packet loss",
          (((ntransmitted-nreceived)*100.0) /
           ntransmitted));
  }
  xputc('\n');
  if (nreceived && (pingflags & F_TIMING)) {
    double n = nreceived + nrepeats;
    double avg = (tsum / n);

    printf("round-trip min/avg/max = "
        "%.3f/%.3f/%.3f ms\n",
        tmin * 1000.0, avg * 1000.0, tmax * 1000.0);
  }
}

// Print statistics when SIGINFO is received.
static void prtsig(int dummy)
{
  summary(0);
}

// Print statistics and give up.
static void finish(int dummy)
{
  (void)signal(SIGQUIT, SIG_DFL);
  summary(1);
  exit(nreceived > 0 ? 0 : 2);
}

// On the first SIGINT, allow any outstanding packets to dribble in
static void prefinish(int dummy)
{
  if (lastrcvd      /* quit now if caught up */
      || nreceived == 0)    /* or if remote is dead */
    finish(0);

  (void)signal(dummy, finish);  /* do this only the 1st time */

  if (npackets > ntransmitted)  /* let the normal limit work */
    npackets = ntransmitted;
}

/*
 *  Return an ASCII host address
 *  as a dotted quad and optionally with a hostname
 */
static char *pr_addr(struct in_addr *addr)    /* in network order */
{
  struct  hostent  *hp;
  static  char buf[MAXHOSTNAMELEN+4+16+1];

  if (!(hp = gethostbyaddr((char *)addr, sizeof(*addr), AF_INET))) {
    (void)snprintf(buf, sizeof(buf), "%s", inet_ntoa(*addr));
  } else {
    (void)snprintf(buf, sizeof(buf), "%s (%s)", hp->h_name,
        inet_ntoa(*addr));
  }

  return buf;
}

// Print an ASCII host address starting from a string of bytes.
static void pr_saddr(u_char *cp)
{
  n_long l;
  struct in_addr addr;

  l = (u_char)*++cp;
  l = (l<<8) + (u_char)*++cp;
  l = (l<<8) + (u_char)*++cp;
  l = (l<<8) + (u_char)*++cp;
  addr.s_addr = htonl(l);
  (void)printf("\t%s", (l == 0) ? "0.0.0.0" : pr_addr(&addr));
}

// Print a descriptive string about an ICMP header other than an echo reply.
static int pr_icmph(struct icmp *icp, struct sockaddr_in *from, int cc) /* 0=printed nothing */
{
  switch (icp->icmp_type ) {
    case ICMP_UNREACH:
      (void)printf("Destination Unreachable");
      break;
    case ICMP_SOURCEQUENCH:
      (void)printf("Source Quench");
      break;
    case ICMP_REDIRECT:
      (void)printf("Redirect (change route)");
      break;
    case ICMP_ECHO:
      (void)printf("Echo Request");
      break;
    case ICMP_ECHOREPLY:
      /* displaying other's pings is too noisey */
      return 0;
    case ICMP_TIME_EXCEEDED:
      (void)printf("Time Exceeded");
      break;
    case ICMP_PARAMETERPROB: 
      (void)printf("Parameter Problem");
      break;
    case ICMP_TIMESTAMP:
      printf("Timestamp Request");
      break;
    case ICMP_TIMESTAMPREPLY: 
      printf("Timestamp Reply");
      break;
    case ICMP_INFO_REQUEST:
      printf("Information Request");
      break;
    case ICMP_INFO_REPLY:  
      printf("Information Reply");
      break;
    case ICMP_ADDRESS:   
      printf("Address Mask Request");
      break;
    case ICMP_ADDRESSREPLY: 
      printf("Address Mask Reply");
      break;
    default:
      (void)printf("Bad ICMP type: %d", icp->icmp_type);
      break;
  }
  return 1;
}

static void get_ifaddr(char *addrname, const char* name)
{
  struct ifaddrs *ifaddr_list, *ifa_item;
  int family, s;  
  char host[NI_MAXHOST];

  if (getifaddrs(&ifaddr_list) == -1) {
    perror("getifaddrs");
    exit(EXIT_FAILURE);
  }

  /* Walk through linked list, maintaining head pointer so we
   *    *         can free list later */

  for (ifa_item = ifaddr_list; ifa_item != NULL; ifa_item = ifa_item->ifa_next) {
    if (ifa_item->ifa_addr == NULL)
      continue;

    family = ifa_item->ifa_addr->sa_family;
    /* For an AF_INET* interface address, display the address */

    if ((family == AF_INET) && (strcmp(ifa_item->ifa_name,name) ==0)) {
      s = getnameinfo(ifa_item->ifa_addr,
          (family == AF_INET) ? sizeof(struct sockaddr_in) :
          sizeof(struct sockaddr_in6),
          host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
      if (s != 0) {
        printf("getnameinfo() failed: %s\n", gai_strerror(s));
        exit(EXIT_FAILURE);
      }     
      strcpy(addrname, host);
    }
  }
}

static void gethost(const char *arg, const char *name, struct sockaddr_in *sa,
    char *realname, int realname_len)
{
  struct hostent *hp;
  unsigned int if_idx = 0;
  char addrname[MAXHOSTNAMELEN+1];
  int len = strlen(arg);

  (void)memset(sa, 0, sizeof(*sa));
  sa->sin_family = AF_INET;

  /* If it is an IP address, try to convert it to a name to
   * have something nice to display.
   */
  if (inet_aton(name, &sa->sin_addr) != 0) {
    if (realname) {
      (void)strncpy(realname, name, realname_len);
    }
    if (len) {
      pingflags |= F_SOURCE_ADDR;
      TT.iface = NULL;
    }
    return;
  }

  if_idx = if_nametoindex(name);
  if (if_idx != 0) get_ifaddr(addrname, name);
  else {
    strncpy(addrname,name, MAXHOSTNAMELEN);
    addrname[MAXHOSTNAMELEN] = '\0';
    if (len) perror_exit("unknown interface '%s'",name);
  }

  hp = gethostbyname(addrname);
  if (!hp)
    error_exit("Cannot resolve \"%s\" (%s)",name,hstrerror(h_errno));

  if (hp->h_addrtype != AF_INET)
    error_exit("%s only supported with IP", arg);

  (void)memcpy(&sa->sin_addr, hp->h_addr, sizeof(sa->sin_addr));

  if (realname) (void)strncpy(realname, hp->h_name, realname_len);
}

/*
 * Print out the packet, if it came from us.  This logic is necessary
 * because ALL readers of the ICMP socket get a copy of ALL ICMP packets
 * which arrive ('tis only fair).  This permits multiple copies of this
 * program to be run without having intermingled output (or statistics!).
 */
static void pr_pack(u_char *buf, int tot_len, struct sockaddr_in *from)
{
  struct ip *ip;
  struct icmp *icp;
  int i, j, net_len;
  u_char *cp;
  static int old_rrlen;
  static char old_rr[MAX_IPOPTLEN];
  int hlen, dupflag = 0, dumped;
  double triptime = 0.0;
#define PR_PACK_SUB() {if (!dumped) {      \
  dumped = 1;          \
  pr_pack_sub(net_len, inet_ntoa(from->sin_addr),  \
      ntohs((u_int16_t)icp->icmp_seq),  \
      dupflag, ip->ip_ttl, triptime);}}

  /* Check the IP header */
  ip = (struct ip *) buf;
  hlen = ip->ip_hl << 2;
  if (tot_len < datalen + ICMP_MINLEN) return;

  /* Now the ICMP part */
  dumped = 0;
  net_len = tot_len - hlen;
  icp = (struct icmp *)(buf + hlen);
  if (icp->icmp_id != ident) return;
  if (icp->icmp_type == ICMP_ECHOREPLY
      && icp->icmp_id == ident) {
    if (icp->icmp_seq == htons((u_int16_t)(ntransmitted-1)))
      lastrcvd = 1;
    last_rx = now;
    if (first_rx.tv_sec == 0)
      first_rx = last_rx;
    nreceived++;
    if (pingflags & F_TIMING) {
      struct timeval tv;
      struct tv32 tv32;

      (void) memcpy(&tv32, icp->icmp_data, sizeof(tv32));
      tv.tv_sec = ntohl(tv32.tv32_sec);
      tv.tv_usec = ntohl(tv32.tv32_usec);
      triptime = diffsec(&last_rx, &tv);
      tsum += triptime;
      tsumsq += triptime * triptime;
      if (triptime < tmin) tmin = triptime;
      if (triptime > tmax) tmax = triptime;
    }

    if (TST(ntohs((u_int16_t)icp->icmp_seq))) {
      nrepeats++, nreceived--;
      dupflag=1;
    } else {
      SET(ntohs((u_int16_t)icp->icmp_seq));
    }

    if (!dupflag) {
      static u_int16_t last_seqno = 0xffff;
      u_int16_t seqno = ntohs((u_int16_t)icp->icmp_seq);
      u_int16_t gap = seqno - (last_seqno + 1);

      if (gap < 0x8000) last_seqno = seqno;
    }

    if (pingflags & F_QUIET) return;

    PR_PACK_SUB();
  } else if (icp->icmp_type != ICMP_ECHO){
    if (!pr_icmph(icp, from, net_len))
      return;
    dumped = 2;
  }

  /* Display any IP options */
  cp = buf + sizeof(struct ip);
  while (hlen > (int)sizeof(struct ip)) {
    switch (*cp) {
      case IPOPT_EOL:
        hlen = 0;
        break;
      case IPOPT_LSRR:
        hlen -= 2;
        j = *++cp;
        ++cp;
        j -= IPOPT_MINOFF;
        if (j <= 0) continue;
        if (dumped <= 1) {
          j = ((j+3)/4)*4;
          hlen -= j;
          cp += j;
          break;
        }
        PR_PACK_SUB();
        (void)printf("\nLSRR: ");
        for (;;) {
          pr_saddr(cp);
          cp += 4;
          hlen -= 4;
          j -= 4;
          if (j <= 0) break;
          xputc('\n');
        }
        break;
      case IPOPT_RR:
        j = *++cp;  /* get length */
        i = *++cp;  /* and pointer */
        hlen -= 2;
        if (i > j) i = j;
        i -= IPOPT_MINOFF;
        if (i <= 0) continue;
        if (dumped <= 1) {
          if (i == old_rrlen
              && !memcmp(cp, old_rr, i)) {
            if (dumped)
              (void)printf("\t(same route)");
            j = ((i+3)/4)*4;
            hlen -= j;
            cp += j;
            break;
          }
          old_rrlen = i;
          (void) memcpy(old_rr, cp, i);
        }
        if (!dumped) {
          (void)printf("RR: ");
          dumped = 1;
        } else (void)printf("\nRR: ");
        for (;;) {
          pr_saddr(cp);
          cp += 4;
          hlen -= 4;
          i -= 4;
          if (i <= 0) break;
          xputc('\n');
        }
        break;
      case IPOPT_NOP:
        if (dumped <= 1)
          break;
        PR_PACK_SUB();
        (void)printf("\nNOP");
        break;
      default:
        PR_PACK_SUB();
        (void)printf("\nunknown option 0x%x", *cp);
        break;
    }
    hlen--;
    cp++;
  }

  if (dumped) {
    xputc('\n');
    (void)fflush(stdout);
  } 
}

/*
 * Compose and transmit an ICMP ECHO REQUEST packet.  The IP packet
 * will be added on by the kernel.  The ID field is our UNIX process ID,
 * and the sequence number is an ascending integer.  The first PHDR_LEN bytes
 * of the data portion are used to hold a UNIX "timeval" struct in VAX
 * byte-order, to compute the round-trip time.
 */
static void pinger(void)
{
  struct tv32 tv32;
  int i, cc, sw;

  opack_icmp.icmp_code = 0;
  opack_icmp.icmp_seq = htons((u_int16_t)(ntransmitted));

  /* clear the cached route in the kernel after an ICMP
   * response such as a Redirect is seen to stop causing
   * more such packets.  Also clear the cached route
   * periodically in case of routing changes that make
   * black holes come and go.
   */
  if (clear_cache.tv_sec != now.tv_sec) {
    opack_icmp.icmp_type = ICMP_ECHOREPLY;
    opack_icmp.icmp_id = ~ident;
    opack_icmp.icmp_cksum = 0;
    opack_icmp.icmp_cksum = in_cksum((u_int16_t *)&opack_icmp,
        PHDR_LEN);
    sw = 0;
    if (setsockopt(sloop,IPPROTO_IP,IP_HDRINCL,
          (char *)&sw,sizeof(sw)) < 0)
      perror_exit("Can't turn off special IP header");
    if (sendto(sloop, (char *) &opack_icmp, PHDR_LEN, MSG_DONTROUTE,
          (struct sockaddr *)&loc_addr,
          sizeof(struct sockaddr_in)) < 0) {
    }
    sw = 1;
    if (setsockopt(sloop,IPPROTO_IP,IP_HDRINCL,
          (char *)&sw, sizeof(sw)) < 0)
      perror_exit("Can't set special IP header");

    (void)gettimeofday(&clear_cache,0);
  }

  opack_icmp.icmp_type = ICMP_ECHO;
  opack_icmp.icmp_id = ident;
  tv32.tv32_sec = htonl(now.tv_sec);
  tv32.tv32_usec = htonl(now.tv_usec);
  if (pingflags & F_TIMING)
    (void) memcpy(&opack_icmp.icmp_data[0], &tv32, sizeof(tv32));
  cc = datalen + PHDR_LEN;
  opack_icmp.icmp_cksum = 0;
  opack_icmp.icmp_cksum = in_cksum((u_int16_t *)&opack_icmp, cc);

  i = sendto(s, (char *) &opack_icmp, cc, 0,
      (struct sockaddr *)&send_addr, sizeof(struct sockaddr_in));
  if (i != cc) {
    if (i < 0) perror_exit("sendto");
    else error_msg("wrote %s %d chars, ret=%d", hostname, cc, i);
    (void)fflush(stderr);
  }
  lastrcvd = 0;

  CLR(ntransmitted);
  ntransmitted++;

  last_tx = now;
  if (next_tx.tv_sec == 0) {
    first_tx = now;
    next_tx = now;
  }

  /* Transmit regularly, at always the same microsecond in the
   * second when going at one packet per second.
   * If we are at most 100 ms behind, send extras to get caught up.
   * Otherwise, skip packets we were too slow to send.
   */
  if (diffsec(&next_tx, &now) <= interval) {
    do {
      timevaladd(&next_tx, &interval_tv);
    } while (diffsec(&next_tx, &now) < -0.1);
  }
}

static void doit(void)
{
  int cc, once = 1;
  struct sockaddr_in from;
  socklen_t fromlen;
  double sec, last, d_last, resp_last, d_resp;
  struct pollfd fdmaskp[1];

  (void)gettimeofday(&clear_cache,0);
  if (maxwait != 0) {
    last = timeval_to_sec(&clear_cache) + maxwait;
    d_last = 0;
  } else {
    last = 0;
    d_last = 365*24*60*60;
  }

  resp_last = 0;
  d_resp = 0;

  do {
    (void)gettimeofday(&now,0);

    if (last != 0)
      d_last = last - timeval_to_sec(&now);

    if (ntransmitted < npackets && d_last > 0) {
      /* send if within 100 usec or late for next packet */
      sec = diffsec(&next_tx,&now);
      if (sec <= 0.0001 ) {
        pinger();
        sec = diffsec(&next_tx,&now);
      }
      if (sec < 0.0) sec = 0.0;
      if (d_last < sec) sec = d_last;

    } else {
      /* For the last response, wait twice as long as the
       * worst case seen, or 10 times as long as the
       * maximum interpacket interval, whichever is longer.
       */
      if ((nreceived == 0) && wait_resp > 0 && once) {
        (void)gettimeofday(&clear_cache,0);
        resp_last = timeval_to_sec(&clear_cache);

        if (toys.optflags & FLAG_w) resp_last += d_last;
        else resp_last += wait_resp;
        d_resp = 0;
        once = 0;
      }
      if (resp_last != 0) d_resp = resp_last - timeval_to_sec(&now);

      if ((nreceived == 0) && d_resp > 0) sec = d_resp;
      else {
        sec = 2 * tmax;
        if (d_last < sec) sec = d_last;
        if (!last) last = timeval_to_sec(&now) + sec;
      }
      if (sec <= 0) break;
    }

    fdmaskp[0].fd = s;
    fdmaskp[0].events = POLLIN;
    cc = poll(fdmaskp, 1, (int)(sec * 1000));
    if (cc <= 0) {
      if (cc < 0) {
        if (errno == EINTR) continue;
        perror_exit("poll");
      }
      continue;
    }
    fromlen  = sizeof(from);
    cc = recvfrom(s, (char *) packet, packlen,
        0, (struct sockaddr *)&from,
        &fromlen);
    if (cc < 0) {
      if (errno != EINTR) {
        perror_msg("recvfrom");
        (void)fflush(stderr);
      }
      continue;
    }
    (void)gettimeofday(&now, 0);
    pr_pack(packet, cc, &from);

  } while (nreceived < npackets);

  finish(0);
}

void ping_main(void)
{
  const int const_int_1 = 1;
  int  i;//, on = 1;
  unsigned ttl = 0;
  struct in6_addr in6;

  if(!(toys.optflags & FLAG_4) && (inet_pton(AF_INET6, toys.optargs[0], (void*)&in6)))
    toys.optflags |= FLAG_6;
  if (toys.optflags & FLAG_6) {
    //ping6 has 4 options, so 4+3 option placeholders
    //1 for cmdname 1 for NULL and toys.optc
    int cnt = 0, opt = 0;
    char **argv6 = xzalloc((9 + toys.optc) * sizeof(char*)); 
    argv6[cnt++] = "ping6";
    if (toys.optflags & FLAG_c) {
      argv6[cnt++] = "-c";
      argv6[cnt++] = xmsprintf("%d",TT.count);
    }
    if (toys.optflags & FLAG_s) {
      argv6[cnt++] = "-s";
      argv6[cnt++] = xmsprintf("%d",TT.size);
    }
    if (toys.optflags & FLAG_I) {
      argv6[cnt++] = "-I";
      argv6[cnt++] = xmsprintf("%s",TT.iface);
    }
    if (toys.optflags & FLAG_q) argv6[cnt++] = "-q";

    while(opt < toys.optc) argv6[cnt++] = toys.optargs[opt++];
    argv6[cnt] = NULL;
    xexec(argv6);
    /*Not Reached */
    return;
  }

  s = xsocket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  sloop = xsocket(AF_INET, SOCK_RAW, IPPROTO_ICMP);

  if (toys.optflags & FLAG_c) npackets = TT.count;
  if (toys.optflags & FLAG_s) datalen = TT.size;
  if (toys.optflags & FLAG_t) ttl = TT.ttl;
  if (toys.optflags & FLAG_I) {
    gethost("-I", TT.iface, &src_addr, 0, 0);
  }
  if (toys.optflags & FLAG_W) wait_resp = (double)TT.wait_resp;
  if (toys.optflags & FLAG_w) maxwait = (double)TT.wait_exit;
  if (toys.optflags & FLAG_q) pingflags |= F_QUIET;

  sec_to_timeval(interval, &interval_tv);

  if (!npackets) npackets = INT_MAX;

  gethost("", toys.optargs[0], &whereto, hostname, sizeof(hostname));
  if (IN_MULTICAST(ntohl(whereto.sin_addr.s_addr)))
    pingflags |= F_MCAST;
  (void) memcpy(&send_addr, &whereto, sizeof(send_addr));

  loc_addr.sin_family = AF_INET;
  loc_addr.sin_addr.s_addr = htonl((127<<24)+1);

  if (datalen >= PHDR_LEN) pingflags |= F_TIMING;

  packlen = datalen + 60 + 76;  /* MAXIP + MAXICMP */
  packet = xmalloc(packlen);

  for (i = PHDR_LEN; i < datalen; i++)
    opack_icmp.icmp_data[i] = i;

  ident = rand() & 0xFFFF;

  if (ttl) {
    if (setsockopt(s, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl)) < 0)
      perror_exit("Can't set time-to-live");
    if (setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL,  &ttl, sizeof(ttl)) < 0)
      perror_exit("Can't set multicast time-to-live");
  }
  if (pingflags & F_MCAST) {
    if ((pingflags & F_SOURCE_ADDR)
        && setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF,
          (char *) &src_addr.sin_addr,
          sizeof(src_addr.sin_addr)) < 0)
      perror_exit("Can't set multicast source interface");
  } else if (pingflags & F_SOURCE_ADDR) {
    if (setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF,
          (char *) &src_addr.sin_addr,
          sizeof(src_addr.sin_addr)) < 0)
      perror_exit("Can't set source interface/address");
    if (bind(s,  (struct sockaddr*)&src_addr, sizeof(src_addr)))
      perror_exit("bind");
  }
  if (TT.iface && memcmp(&src_addr, &send_addr, sizeof(send_addr))) {
    struct ifreq ifr;
    strncpy(ifr.ifr_name, TT.iface, IFNAMSIZ);
    if (setsockopt(s, SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof(ifr)))
      perror_exit("can't bind to interface %s", TT.iface);
  }

  setsockopt(s, SOL_SOCKET, SO_BROADCAST, &const_int_1, sizeof(const_int_1));
  (void)printf("PING %s (%s):", hostname, inet_ntoa(whereto.sin_addr));
  if (toys.optflags & FLAG_I)
    printf(" from %s:", inet_ntoa(src_addr.sin_addr));
  printf(" %d data bytes.\n", datalen);

  /* When pinging the broadcast address, you can get a lot
   * of answers.  Doing something so evil is useful if you
   * are trying to stress the ethernet, or just want to
   * fill the arp cache to get some stuff for /etc/ethers.
   */
  while (0 > setsockopt(s, SOL_SOCKET, SO_RCVBUF,
        (char*)&bufspace, sizeof(bufspace))) {
    if ((bufspace -= 4096) <= 0)
      perror_exit("Cannot set the receive buffer size");
  }

  /* make it possible to send giant probes, but do not worry now
   * if it fails, since we probably won't send giant probes.
   */
  (void)setsockopt(s, SOL_SOCKET, SO_SNDBUF,
      (char*)&bufspace, sizeof(bufspace));

  (void)signal(SIGINT, prefinish);
  (void)signal(SIGQUIT, prtsig);
  (void)signal(SIGCONT, prtsig);

  doit();
}
