/* ping.c - ping program.
 *
 * Copyright 2012 Ashwini Kumar <ak.ashwini@gmail.com>
 *
 * Not in SUSv4.
 
USE_PING(NEWTOY(ping, "<1>1Q#<0>255t#<0>255c#<0s#<0>65535I:W#<0w#<0q46", TOYFLAG_STAYROOT|TOYFLAG_USR|TOYFLAG_BIN))
 
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
    -Q ToS      Set Quality of Service
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
  long tos;
  int ntransmitted;
  int nreceived;
  int ident;
  int nrepeats;
  int sock;
)

#define F_QUIET    0x0001    // minimize all output
#define F_TIMING  0x0002    // room for a timestamp
#define F_SOURCE_ADDR  0x0004   // set source IP address/interface

u_char  rcvd_tbl[2048]; //MAX_DUP_CHK
#define A(seq)  rcvd_tbl[(seq/8)%sizeof(rcvd_tbl)]  // byte in array
#define B(seq)  (1 << (seq & 0x07))  // bit in byte
#define SET(seq) (A(seq) |= B(seq))
#define CLR(seq) (A(seq) &= (~B(seq)))
#define TST(seq) (A(seq) & B(seq))

struct tv32 {
  int32_t tv32_sec;
  int32_t tv32_usec;
};

u_char *packet;
int packlen, pingflags = 0, bufspace = IP_MAXPACKET;
double tmax = 0.0, tsum = 0.0, tmin = 999999999.0;
struct timeval now;
struct sockaddr_in src_addr, send_addr;  //from where to Who

#define PHDR_LEN sizeof(struct tv32)  // size of timestamp header
#define MAXHOSTNAMELEN  64
char hostname[MAXHOSTNAMELEN + 1];
static struct {
  union {
    u_char u_buf[(IP_MAXPACKET-60-8)+offsetof(struct icmp, icmp_data)];
    struct icmp u_icmp;
  } o_u;
} out_pack;
#define  opack_icmp  out_pack.o_u.u_icmp

/* Compute the IP checksum
 *  This assumes the packet is less than 32K long.
 */
static u_int16_t in_cksum(u_int16_t *p, u_int len)
{
  u_int32_t sum = 0;
  int nwords = len >> 1;

  while (nwords--) sum += *p++;
  if (len & 1) {
    union {
      u_int16_t w;
      u_int8_t c[2];
    } u;
    u.c[0] = *(u_char *)p;
    u.c[1] = 0;
    sum += u.w;
  }

  // end-around-carry 
  sum = (sum >> 16) + (sum & 0xffff);
  sum += (sum >> 16);
  return (~sum);
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
  if (header) xprintf("\n--- %s PING Statistics ---\n", hostname);
  xprintf("%d packets transmitted, ", TT.ntransmitted);
  xprintf("%d packets received, ", TT.nreceived);
  if (TT.nrepeats) xprintf("+%d duplicates, ", TT.nrepeats);
  if (TT.ntransmitted) {
    if (TT.nreceived > TT.ntransmitted) xprintf("-- somebody's duplicating packets!");
    else xprintf("%.1f%% packet loss", (((TT.ntransmitted-TT.nreceived)*100.0)/TT.ntransmitted));
  }
  xputc('\n');
  if (TT.nreceived && (pingflags & F_TIMING)) {
    double n = TT.nreceived + TT.nrepeats;
    double avg = (tsum / n);

    xprintf("round-trip min/avg/max = %.3f/%.3f/%.3f ms\n",
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
  signal(SIGQUIT, SIG_DFL);
  summary(1);
  exit(TT.nreceived > 0 ? 0 : 2);
}

// Print a descriptive string about an ICMP header other than an echo reply.
static int pr_icmph(struct icmp *icp)
{
  switch (icp->icmp_type ) {
    case ICMP_UNREACH:
      xprintf("Destination Unreachable");
      break;
    case ICMP_SOURCEQUENCH:
      xprintf("Source Quench");
      break;
    case ICMP_REDIRECT:
      xprintf("Redirect (change route)");
      break;
    case ICMP_ECHO:
      xprintf("Echo Request");
      break;
    case ICMP_ECHOREPLY:
      // displaying other's pings is too noisy
      return 0;
    case ICMP_TIME_EXCEEDED:
      xprintf("Time Exceeded");
      break;
    case ICMP_PARAMETERPROB: 
      xprintf("Parameter Problem");
      break;
    case ICMP_TIMESTAMP:
      xprintf("Timestamp Request");
      break;
    case ICMP_TIMESTAMPREPLY: 
      xprintf("Timestamp Reply");
      break;
    case ICMP_INFO_REQUEST:
      xprintf("Information Request");
      break;
    case ICMP_INFO_REPLY:  
      xprintf("Information Reply");
      break;
    case ICMP_ADDRESS:   
      xprintf("Address Mask Request");
      break;
    case ICMP_ADDRESSREPLY: 
      xprintf("Address Mask Reply");
      break;
    default:
      xprintf("Bad ICMP type: %d", icp->icmp_type);
      break;
  }
  return 1;
}

static void get_ifaddr(char *addrname, const char* name)
{
  struct ifaddrs *ifaddr_list, *ifa_item;
  int family, s;  
  char host[NI_MAXHOST];

  if (getifaddrs(&ifaddr_list) == -1) perror_exit("getifaddrs");

  for (ifa_item = ifaddr_list; ifa_item; ifa_item = ifa_item->ifa_next) {
    if (!ifa_item->ifa_addr) continue;

    family = ifa_item->ifa_addr->sa_family;
    if ((family == AF_INET) && !(strcmp(ifa_item->ifa_name,name))) {
      s = getnameinfo(ifa_item->ifa_addr, sizeof(struct sockaddr_in),
          host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
      if (s) error_exit("getnameinfo() failed: %s\n", gai_strerror(s));
      strncpy(addrname, host, MAXHOSTNAMELEN);
      break;
    }
  }
  freeifaddrs(ifaddr_list);
}

static void gethost(const char *arg, const char *name, struct sockaddr_in *sa,
    char *realname, int realname_len)
{
  struct hostent *hp;
  unsigned int if_idx = 0, len = strlen(arg);
  char addrname[MAXHOSTNAMELEN+1];

  memset(sa, 0, sizeof(*sa));
  sa->sin_family = AF_INET;

  if (inet_aton(name, &sa->sin_addr)) {
    if (realname) strncpy(realname, name, realname_len -1);
    if (len) {
      pingflags |= F_SOURCE_ADDR;
      TT.iface = NULL;
    }
    return;
  }

  if (len) {
    if ((if_idx = if_nametoindex(name))) get_ifaddr(addrname, name);
    else perror_exit("unknown interface '%s'",name);
  } else strncpy(addrname,name, MAXHOSTNAMELEN);

  addrname[MAXHOSTNAMELEN] = '\0';
  hp = gethostbyname(addrname);
  if (!hp) error_exit("Cannot resolve \"%s\" (%s)",name,hstrerror(h_errno));
  if (hp->h_addrtype != AF_INET) error_exit("%s only supported with IP", arg);
  memcpy(&sa->sin_addr, hp->h_addr, sizeof(sa->sin_addr));
  if (realname) strncpy(realname, hp->h_name, realname_len -1);
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
  int net_len, hlen, dupflag = 0;
  double triptime = 0.0;

  // Check the IP header
  ip = (struct ip *) buf;
  hlen = ip->ip_hl << 2;
  if (tot_len < TT.size + ICMP_MINLEN) return;

  // Now the ICMP part 
  net_len = tot_len - hlen;
  icp = (struct icmp *)(buf + hlen);
  if (ntohs(icp->icmp_id) != TT.ident) return;
  if (icp->icmp_type == ICMP_ECHOREPLY) {
    TT.nreceived++;
    if (pingflags & F_TIMING) {
      struct timeval tv;
      struct tv32 tv32;

      memcpy(&tv32, icp->icmp_data, sizeof(tv32));
      tv.tv_sec = ntohl(tv32.tv32_sec);
      tv.tv_usec = ntohl(tv32.tv32_usec);
      triptime = ((now.tv_sec - tv.tv_sec)*1.0
          + (now.tv_usec - tv.tv_usec)/1000000.0);
      tsum += triptime;
      if (triptime < tmin) tmin = triptime;
      if (triptime > tmax) tmax = triptime;
    }

    if (TST(ntohs((u_int16_t)icp->icmp_seq))) {
      TT.nrepeats++, TT.nreceived--;
      dupflag = 1;
    } else SET(ntohs((u_int16_t)icp->icmp_seq));

    if (!dupflag) {
      static u_int16_t last_seqno = 0xffff;
      u_int16_t seqno = ntohs((u_int16_t)icp->icmp_seq);
      u_int16_t gap = seqno - (last_seqno + 1);

      if (gap < 0x8000) last_seqno = seqno;
    }

    if (pingflags & F_QUIET) return;

    xprintf("%d bytes from %s: seq=%u ttl=%d", net_len, inet_ntoa(from->sin_addr), 
        ntohs((u_int16_t)icp->icmp_seq), ip->ip_ttl);
    if (pingflags & F_TIMING) xprintf(" time=%.3f ms", triptime*1000.0);
    if (dupflag) xprintf(" (DUP!)");
  } else if (icp->icmp_type != ICMP_ECHO) pr_icmph(icp);
  else return;
  xputc('\n');
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
  int i, cc;

  gettimeofday(&now,0);
  opack_icmp.icmp_code = 0;
  opack_icmp.icmp_seq = htons((u_int16_t)(TT.ntransmitted));
  opack_icmp.icmp_type = ICMP_ECHO;
  opack_icmp.icmp_id = htons(TT.ident);
  tv32.tv32_sec = htonl(now.tv_sec);
  tv32.tv32_usec = htonl(now.tv_usec);
  if (pingflags & F_TIMING) memcpy(&opack_icmp.icmp_data[0], &tv32, sizeof(tv32));
  cc = TT.size + PHDR_LEN;
  opack_icmp.icmp_cksum = 0;//reset before Checksum computation
  opack_icmp.icmp_cksum = in_cksum((u_int16_t *)&opack_icmp, cc);

  i = sendto(TT.sock, (char *) &opack_icmp, cc, 0,
      (struct sockaddr *)&send_addr, sizeof(struct sockaddr_in));
  if (i != cc) {
    if (i < 0) perror_exit("sendto");
    else error_msg("wrote %s %d chars, ret=%d", hostname, cc, i);
  }
  CLR(TT.ntransmitted);
  TT.ntransmitted++;
}

static void send_ping(int dummy)
{
  struct itimerval itimer;
  if (TT.wait_exit && (TT.wait_exit == TT.ntransmitted)) finish(0);
  if (TT.count && (TT.ntransmitted >= TT.count)) {
    //waiting for the last response.
    if (TT.nreceived) {
      itimer.it_value.tv_sec = 2 * tmax/1000;
      if (itimer.it_value.tv_sec == 0) itimer.it_value.tv_sec = 1;
    } else itimer.it_value.tv_sec = (TT.wait_resp) ? TT.wait_resp : 10; //default 10 secs
    itimer.it_value.tv_usec = 0;
    itimer.it_interval.tv_sec = itimer.it_interval.tv_usec = 0;
    signal(SIGALRM, finish);
    setitimer(ITIMER_REAL, &itimer, NULL);
  } else pinger();
}

static void doit(void)
{
  int cc;
  struct sockaddr_in from;
  socklen_t fromlen;

  for(;;) {
    fromlen  = sizeof(from);
    cc = recvfrom(TT.sock, (char *) packet, packlen,
        0, (struct sockaddr *)&from, &fromlen);
    if (cc < 0) {
      if (errno != EINTR) perror_msg("recvfrom");
      continue;
    }
    gettimeofday(&now, 0);
    pr_pack(packet, cc, &from);
    if(TT.count && (TT.nreceived >= TT.count)) break;
  }
  finish(0);
}

void ping_main(void)
{
  int const_int_1 = 1, i;
  struct in6_addr in6;
  struct itimerval itimer;
  struct timeval interval_tv = {1, 0}; //1sec interval

  if(!(toys.optflags & FLAG_4) && (inet_pton(AF_INET6, toys.optargs[0], (void*)&in6)))
    toys.optflags |= FLAG_6;
  if (toys.optflags & FLAG_6) {
    //ping6 support 4 options
    //1 for cmdname 1 for NULL and toys.optc
    int cnt = 0, opt = 0;
    char **argv6 = xzalloc((6 + toys.optc) * sizeof(char*)); 
    argv6[cnt++] = "ping6";
    if (toys.optflags & FLAG_c) argv6[cnt++] = xmprintf("-c%d",TT.count);
    if (toys.optflags & FLAG_s) argv6[cnt++] = xmprintf("-s%d",TT.size);
    if (toys.optflags & FLAG_I) argv6[cnt++] = xmprintf("-I%s",TT.iface);
    if (toys.optflags & FLAG_q) argv6[cnt++] = "-q";

    while(opt < toys.optc) argv6[cnt++] = toys.optargs[opt++];
    xexec(argv6);
  }

  TT.sock = xsocket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if (!(toys.optflags & FLAG_s)) TT.size = 64 - PHDR_LEN;
  if (toys.optflags & FLAG_I) gethost("-I", TT.iface, &src_addr, 0, 0);
  if (toys.optflags & FLAG_q) pingflags |= F_QUIET;

  gethost("", toys.optargs[0], &send_addr, hostname, sizeof(hostname));
  if (TT.size >= PHDR_LEN) pingflags |= F_TIMING;
  packlen = TT.size + 60 + 76;  /* MAXIP + MAXICMP */
  packet = xmalloc(packlen);

  TT.ident = rand() & 0xFFFF;
  for (i = PHDR_LEN; i < (int)TT.size; i++) opack_icmp.icmp_data[i] = i;

  if (TT.ttl) {
    if (setsockopt(TT.sock, IPPROTO_IP, IP_TTL, &TT.ttl, sizeof(TT.ttl)) < 0)
      perror_exit("Can't set time-to-live");
    if (setsockopt(TT.sock, IPPROTO_IP, IP_MULTICAST_TTL,  &TT.ttl, sizeof(TT.ttl)) < 0)
      perror_exit("Can't set multicast time-to-live");
  }
  if ((toys.optflags & FLAG_Q) &&
      setsockopt(TT.sock, IPPROTO_IP, IP_TOS, &TT.tos, sizeof(TT.tos)) < 0)
    perror_exit("IP_TOS %d failed ", TT.tos);

  if (pingflags & F_SOURCE_ADDR) {
    if (setsockopt(TT.sock, IPPROTO_IP, IP_MULTICAST_IF,
          (char *) &src_addr.sin_addr,
          sizeof(src_addr.sin_addr)) < 0)
      perror_exit("Can't set source interface/address");
    if (bind(TT.sock,  (struct sockaddr*)&src_addr, sizeof(src_addr)))
      perror_exit("bind");
  }
  if (TT.iface && memcmp(&src_addr, &send_addr, sizeof(send_addr))) {
    struct ifreq ifr;
    xstrncpy(ifr.ifr_name, TT.iface, IFNAMSIZ);
    if (setsockopt(TT.sock, SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof(ifr)))
      perror_msg("can't bind to interface %s", TT.iface);
  }
  //enable PING to Broadcast address
  setsockopt(TT.sock, SOL_SOCKET, SO_BROADCAST, &const_int_1, sizeof(const_int_1));
  xprintf("PING %s (%s):", hostname, inet_ntoa(send_addr.sin_addr));
  if (toys.optflags & FLAG_I) xprintf(" from %s:", inet_ntoa(src_addr.sin_addr));
  xprintf(" %d data bytes.\n", (int)TT.size);

  /* When pinging the broadcast address, you can get a lot
   * of answers.  Doing something so evil is useful if you
   * are trying to stress the ethernet, or just want to
   * fill the arp cache to get some stuff for /etc/ethers.
   */
  while (0 > setsockopt(TT.sock, SOL_SOCKET, SO_RCVBUF,
        (char*)&bufspace, sizeof(bufspace)))
    if ((bufspace -= 4096) <= 0) 
      perror_exit("Cannot set the receive buffer size");

  signal(SIGINT, finish);
  signal(SIGQUIT, prtsig);
  signal(SIGCONT, prtsig);
  itimer.it_interval = interval_tv;
  itimer.it_value = interval_tv;
  signal(SIGALRM, send_ping);
  setitimer(ITIMER_REAL, &itimer, NULL); //interval timer between ping
  send_ping(0);
  doit();
}

/*
 * Copyright (c) 1989, 1993
 *  The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Mike Muuss.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
