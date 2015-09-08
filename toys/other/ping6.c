/* ping6.c - ping program for IPv6.
 *
 * Copyright 2012 Ashwini Kumar <ak.ashwini@gmail.com>
 *
 * Not in SUSv4.

USE_PING6(NEWTOY(ping6, "<1>1c#<0s#<0>65535I:q", TOYFLAG_STAYROOT|TOYFLAG_USR|TOYFLAG_BIN))

config PING6
  bool "ping6"
  default y
  help
    usage: ping6 [OPTIONS] HOST

    Send ICMP ECHO_REQUEST packets to network hosts

    Options:
    -c CNT      Send only CNT pings
    -s SIZE     Send SIZE data bytes in packets (default:56)
    -I IFACE/IP Use interface or IP address as source
    -q          Quiet, only displays output at start
                and when finished
*/

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
 *  notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *  notice, this list of conditions and the following disclaimer in the
 *  documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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

#define FOR_ping6
#include "toys.h"

#include <sys/param.h>
#include <sys/uio.h>
#include <net/route.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#include <arpa/nameser.h>
#include <signal.h>
#include <ifaddrs.h>

#define MAXHOSTNAMELEN  64

GLOBALS(
  char *iface;
  long size;
  long count;
  int sock;
  long nreceived;     /* # of packets we got back */ 
  long nrepeats;      /* number of duplicates */
  long ntransmitted;    /* sequence # for outbound packets = #sent */
  int timing;     /* flag to do timing */
  double tmin;  /* minimum round trip time */
  double tmax;    /* maximum round trip time */
  double tsum;    /* sum of all times, for doing average */
  struct sockaddr_in6 src;
  struct sockaddr_in6 dst;
  struct timeval interval; //default 1 second delay between pings
  char *outpack;
  u_char *packet;
  int packlen;
  uint8_t myid; //id for the packets.
  char hostname[64/*MAXHOSTNAMELEN*/];
)

struct tv32 {
  u_int32_t tv32_sec;
  u_int32_t tv32_usec;
};

#define ICMP6ECHOLEN  8   /* icmp echo header len excluding time */
#define A(bit)    rcvd_tbl[(bit)>>3]  /* identify byte in array */
#define B(bit)    (1 << ((bit) & 0x07))   /* identify bit in byte */
#define SET(bit)  (A(bit) |= B(bit))
#define CLR(bit)  (A(bit) &= (~B(bit)))
#define TST(bit)  (A(bit) & B(bit))

/*
 * MAX_DUP_CHK is the number of bits in received table, i.e. the maximum
 * number of received sequence numbers we can keep track of.  Change 128
 * to 8192 for complete accuracy...
 */
#define MAX_DUP_CHK (8 * 8192)
int mx_dup_ck = MAX_DUP_CHK;
char rcvd_tbl[MAX_DUP_CHK / 8];
 

/*
 * Print the ping summary and exit 
 */
static void summary(void)
{
  xprintf("\n--- %s ping6 statistics ---\n", TT.hostname);
  xprintf("%ld packets transmitted, ", TT.ntransmitted);
  xprintf("%ld packets received, ", TT.nreceived);
  if (TT.nrepeats) xprintf("+%ld duplicates, ", TT.nrepeats);
  if (TT.ntransmitted) {
    if (TT.nreceived > TT.ntransmitted)
      xprintf("-- somebody's duplicating packets!");
    else xprintf("%.1f%% packet loss",
        ((((double)TT.ntransmitted - TT.nreceived) * 100.0) /
         TT.ntransmitted));
  }     
  xputc('\n');   
  if (TT.nreceived && TT.timing) {
    /* Only display average to microseconds */
    double num = TT.nreceived + TT.nrepeats;
    double avg = TT.tsum / num;
    xprintf("round-trip min/avg/max = %.3f/%.3f/%.3f ms\n",
        TT.tmin, avg, TT.tmax);
    (void)fflush(stdout);
  }     
  (void)fflush(stdout);  
}

/* Got an INTERRUPT like CTL^C, Print summary and exit
 */
static void onint(int notused)
{   
  summary();

  (void)signal(SIGINT, SIG_DFL);
  (void)kill(getpid(), SIGINT);
}

/* Compose an ICMP6_ECHO_REQUEST packet and send it to destination.
 */
static int pinger()
{
  struct icmp6_hdr *icp;
  int i, cc;
  int seq;

  if (TT.count && TT.ntransmitted >= TT.count)
    return(-1); /* no more transmission */

  icp = (struct icmp6_hdr *)TT.outpack;
  memset(icp, 0, sizeof(*icp));
  icp->icmp6_cksum = 0;
  seq = TT.ntransmitted++; //seq is incremental, at every request
  CLR(seq % mx_dup_ck);

  icp->icmp6_type = ICMP6_ECHO_REQUEST;
  icp->icmp6_code = 0;
  icp->icmp6_id = htons(TT.myid);
  icp->icmp6_seq = ntohs(seq);
  if (TT.timing) { //add timing information to packet only if packet size is long enough to hold.
    struct timeval tv;
    struct tv32 *tv32;
    (void)gettimeofday(&tv, NULL);
    tv32 = (struct tv32 *)&TT.outpack[ICMP6ECHOLEN];
    tv32->tv32_sec = htonl(tv.tv_sec); //convert the value to network order
    tv32->tv32_usec = htonl(tv.tv_usec);
  }
  cc = ICMP6ECHOLEN + TT.size; //total request size.


  i = sendto(TT.sock, (char *) TT.outpack, cc, 0,
      (struct sockaddr *)&TT.dst, sizeof(struct sockaddr_in6)); //send it to destination

  if (i < 0 || i != cc)  {
    if (i < 0) perror_exit("sendto");
    xprintf("ping6: wrote %s %d chars, ret=%d\n",
        TT.hostname, cc, i);
  }

  return(0);
}

/*
 * Transmit an ICMP ECHO packet to destination host, if no more ping is required
 * wait for the response and exit after timeout.
 */
static void retransmit(void)
{
  struct itimerval itimer;

  if (pinger() == 0) return;

  /*
   * If we're not transmitting any more packets, change the timer
   * to wait two round-trip times if we've received any packets or
   * ten seconds if we haven't.
   */
  if (TT.nreceived) {
    itimer.it_value.tv_sec =  2 * TT.tmax / 1000;
    if (itimer.it_value.tv_sec == 0)
      itimer.it_value.tv_sec = 1;
  } else itimer.it_value.tv_sec = 10; //default MAX wait for 1st response
  itimer.it_interval.tv_sec = 0;
  itimer.it_interval.tv_usec = 0;
  itimer.it_value.tv_usec = 0;

  (void)signal(SIGALRM, onint);
  (void)setitimer(ITIMER_REAL, &itimer, NULL);
} 

/*
 * SIGALRM handler, for ping interval timeout.
 */
static void onsignal(int sig)
{
  retransmit();
}

/* 
 * tvsub --
 *  Subtract 2 timeval structs:  out = out - in.  Out is assumed to
 * be >= in.
 */ 
static void tvsub(struct timeval *out, struct timeval *in)
{  
  if ((out->tv_usec -= in->tv_usec) < 0) {
    --out->tv_sec;
    out->tv_usec += 1000000;
  }
  out->tv_sec -= in->tv_sec;
}

/*
 * Get HOPLIMIT value from the message
 */
static int get_hoplim(struct msghdr *mhdr)
{
  struct cmsghdr *cm;
  int *val;

  for (cm = (struct cmsghdr *)CMSG_FIRSTHDR(mhdr); cm;
      cm = (struct cmsghdr *)CMSG_NXTHDR(mhdr, cm)) {
    if (!cm->cmsg_len) return(-1);

    if (cm->cmsg_level == IPPROTO_IPV6 &&
        cm->cmsg_type == IPV6_HOPLIMIT &&
        cm->cmsg_len == CMSG_LEN(sizeof(int))) {
      val = (int *)CMSG_DATA(cm);
      return *val;
    }
  }
  return(-1);
}

/*
 * Unpack the message recieved from destination host
 */
static void unpack(u_char *packet, int cc, struct msghdr *mhdr)
{
  struct icmp6_hdr *icp;
  int hoplim;
  struct sockaddr_in6 *from;
  struct timeval tv, tp;
  struct tv32 *tpp;
  double triptime = 0;
  int dupflag;
  u_int16_t seq;
  char buf[INET6_ADDRSTRLEN];

  (void)gettimeofday(&tv, NULL);

  if (!mhdr || !mhdr->msg_name ||
      mhdr->msg_namelen != sizeof(struct sockaddr_in6) ||
      ((struct sockaddr *)mhdr->msg_name)->sa_family != AF_INET6) {
    error_msg("invalid peername");
    return;
  }
  from = (struct sockaddr_in6 *)mhdr->msg_name;
  if (cc < sizeof(struct icmp6_hdr)) {
    error_msg("packet too short (%d bytes) from %s", cc,
        inet_ntop(AF_INET6, (void*)&from->sin6_addr, buf, sizeof(buf))); //short packet received
    return;
  }
  icp = (struct icmp6_hdr *)packet;

  if ((hoplim = get_hoplim(mhdr)) == -1) {
    error_msg("failed to get receiving hop limit");
    return;
  }

  if (ntohs(icp->icmp6_id) != TT.myid) return;
  if (icp->icmp6_type == ICMP6_ECHO_REPLY ) { 
    /* this is the REPLY packet destined to us. */
    seq = ntohs(icp->icmp6_seq);
    ++TT.nreceived;
    if (TT.timing) {
      tpp = (struct tv32 *)(icp + 1);
      tp.tv_sec = ntohl(tpp->tv32_sec);
      tp.tv_usec = ntohl(tpp->tv32_usec);
      tvsub(&tv, &tp);
      triptime = ((double)tv.tv_sec) * 1000.0 +
        ((double)tv.tv_usec) / 1000.0;
      TT.tsum += triptime;
      if (triptime < TT.tmin) TT.tmin = triptime;
      if (triptime > TT.tmax) TT.tmax = triptime;
    }

    if (TST(seq % mx_dup_ck)) { //duplicate packet received
      ++TT.nrepeats;
      --TT.nreceived;
      dupflag = 1;
    } else {
      SET(seq % mx_dup_ck);
      dupflag = 0;
    }

    if (toys.optflags & FLAG_q) return;
    /* print the REPLY message */
    xprintf("%d bytes from %s: seq=%u", cc,
        inet_ntop(AF_INET6, (void*)&from->sin6_addr, buf, sizeof(buf)), seq);
    xprintf(" ttl=%d", hoplim);

    if (TT.timing) xprintf(" time=%.3f ms", triptime);
    if (dupflag) xprintf("(DUP!)");
    xputs("");
  }
  else { 
    /* An ERROR packet received from destination, decode it for proper reason */
    switch(icp->icmp6_type) {
      case ICMP6_DST_UNREACH:
        xprintf("Destination unreachable\n");
        break;
      case ICMP6_PACKET_TOO_BIG:
        xprintf("Packet too big\n");
        break;
      case ICMP6_TIME_EXCEEDED:
        xprintf("Time to live exceeded\n");
        break;
      case ICMP6_PARAM_PROB:
        xprintf("Parameter problem\n");
        break;
      case ICMP6_ECHO_REQUEST:
        xprintf("Echo Request\n");
        break;
      case ICMP6_ECHO_REPLY:
        xprintf("Echo Reply\n");
        break;
      case MLD_LISTENER_QUERY:
        xprintf("Listener Query\n");
        break;
      case MLD_LISTENER_REPORT:
        xprintf("Listener Report\n");
        break;
      case MLD_LISTENER_REDUCTION:
        xprintf("Listener Done\n");
        break;
      default:
        xprintf("Bad ICMP type: %d", icp->icmp6_type);
        break;
    }
  }
}

/*
 * do_ping() recieve messages from destination host,
 * unpack them to decode for success of ping or failure reason.
 */
static void do_ping()
{
  for (;;) {  //infinite loop
    struct msghdr m;
    u_char buf[1024];
    struct iovec iov[2];
    int cc;
    struct sockaddr_in6 from;
    m.msg_name = (caddr_t)&from; //this will be populated by recvmsg
    m.msg_namelen = sizeof(from);
    memset(&iov, 0, sizeof(iov));
    iov[0].iov_base = (caddr_t)TT.packet;
    iov[0].iov_len = TT.packlen;
    m.msg_iov = iov;
    m.msg_iovlen = 1;
    m.msg_control = (caddr_t)buf; 
    m.msg_controllen = sizeof(buf);

    cc = recvmsg(TT.sock, &m, 0);
    if (cc < 0) {
      if (errno != EINTR) {
        perror_msg("recvmsg");
        sleep(1);
      }  
      continue;
    } else if (!cc) continue;
    else {
      /*
       * an ICMPv6 message (probably an echoreply) arrived.
       */
      unpack(TT.packet, cc, &m);
    }  
    if (TT.count && TT.nreceived >= TT.count) break; //break if number of packets (cmdline input) is received
  }
}

/*
 * Resolve the hostname to address info, based on IPv6 resolution.
 */
static void resolve_name(int flags, char *name, char *hostname, struct sockaddr_in6 *sock)
{
  struct addrinfo hints, *res;
  int ret_ga;

  memset(&hints, 0, sizeof(struct addrinfo)); 
  hints.ai_flags = flags;
  hints.ai_family = AF_INET6;
  hints.ai_socktype = SOCK_RAW;
  hints.ai_protocol = IPPROTO_ICMPV6;

  ret_ga = getaddrinfo(toys.optargs[0], NULL, &hints, &res);
  if (ret_ga) error_exit("Hostname: %s", gai_strerror(ret_ga));
  if (hostname) {
    if (res->ai_canonname) strncpy(hostname, res->ai_canonname, MAXHOSTNAMELEN);
    else strncpy(hostname, toys.optargs[0], MAXHOSTNAMELEN);
  }
  if (!res->ai_addr) error_exit("getaddrinfo failed"); 

  memcpy(sock, res->ai_addr, res->ai_addrlen);
  freeaddrinfo(res);
}

/*
 * get the interface address for the given interface device.
 * Resolution is done for IPv6 address family only
 */
static void get_src(void *src, char *name)
{  
  struct ifaddrs *ifaddr_list, *ifa_item;
  int family;  

  if (getifaddrs(&ifaddr_list) == -1) perror_exit("getifaddrs");

  /* Walk through linked list, maintaining head pointer so we
     can free list later */

  for (ifa_item = ifaddr_list; ifa_item; ifa_item = ifa_item->ifa_next) {
    if (!ifa_item->ifa_addr) continue;

    family = ifa_item->ifa_addr->sa_family;

    if ((family == AF_INET6) && !(strcmp(ifa_item->ifa_name,name))) {
      memcpy(src, ifa_item->ifa_addr, sizeof(struct sockaddr_in6)); //copy the address details to src
      break;
    }
  }
  freeifaddrs(ifaddr_list); // free the memory allocated.
}

void ping6_main(void)
{
  int optval, if_idx;
  const int const_int_1 = 1;
  struct itimerval itimer;
  char buf[INET6_ADDRSTRLEN];
  TT.tmin = 999999999.0; //min time to start with
  TT.tmax = 0.0;
  TT.tsum = 0.0;
  TT.interval.tv_sec = 1;
  TT.interval.tv_usec = 0;
  TT.ntransmitted = 0;
  TT.nreceived = 0;
  if (!(toys.optflags & FLAG_s)) TT.size = 56;//defualt size;
  TT.sock = xsocket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);

  if (toys.optflags & FLAG_I) { //-I interface/address 
    TT.src.sin6_family = AF_INET6; 
    if (!inet_pton(AF_INET6, TT.iface, (void*)&TT.src.sin6_addr)) { //check if the input iface address is numeric and proper
      if_idx = if_nametoindex(TT.iface); //get the interface index of given interface
      if (if_idx) get_src((void*)&TT.src, TT.iface); //if the interface is proper, get the socket addr for the same
      else error_exit("Unknown source interface");
    }
  }

  resolve_name(AI_CANONNAME, toys.optargs[0], TT.hostname, &TT.dst);

  if (toys.optflags & FLAG_I) {
    if (bind(TT.sock, (struct sockaddr *)&TT.src, sizeof(TT.src)))
      perror_exit("bind failed: ");
  }

  if (TT.size >= sizeof(struct tv32)) TT.timing = 1;
  else TT.timing = 0;

  TT.packlen = TT.size + 40 + ICMP6ECHOLEN + 256; //IPLEN = 40, EXTRA=256
  TT.packet = xmalloc(TT.packlen);
  TT.outpack = xmalloc(TT.size + sizeof(struct icmp6_hdr));
  TT.myid = getpid();

  optval = 64;// default IPV6_DEFHLIM;
  if (IN6_IS_ADDR_MULTICAST(&TT.dst.sin6_addr))
    if (setsockopt(TT.sock, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
          &optval, sizeof(optval)) == -1)
      error_exit("IPV6_MULTICAST_HOPS ");

#ifdef ICMP6_FILTER
  {/* ICMP6 filter settings */ 
    struct icmp6_filter filt;
    if (!(toys.optflags & (FLAG_c << 1))) {
      ICMP6_FILTER_SETBLOCKALL(&filt);
      ICMP6_FILTER_SETPASS(ICMP6_ECHO_REPLY, &filt);
    } else ICMP6_FILTER_SETPASSALL(&filt);

    if (setsockopt(TT.sock, IPPROTO_ICMPV6, ICMP6_FILTER, &filt,
          sizeof(filt)) < 0) perror_exit("setsockopt(ICMP6_FILTER): ");
  }
#endif /*ICMP6_FILTER*/

  optval = 1;
#ifdef IPV6_RECVHOPLIMIT                
  if (setsockopt(TT.sock, IPPROTO_IPV6, IPV6_RECVHOPLIMIT, &optval,
        sizeof(optval)) < 0)              
    error_msg("setsockopt(IPV6_RECVHOPLIMIT)"); 
#else  /* old adv. API */                 
  if (setsockopt(TT.sock, IPPROTO_IPV6, IPV6_HOPLIMIT, &optval,
        sizeof(optval)) < 0)              
    error_msg("setsockopt(IPV6_HOPLIMIT)"); 
#endif
  setsockopt(TT.sock, SOL_SOCKET, SO_BROADCAST, &const_int_1, sizeof(const_int_1));
  xprintf("PING %s (%s):", TT.hostname,                                                                   
      inet_ntop(AF_INET6, (void*)&TT.dst.sin6_addr, buf, sizeof(buf)));
  if (toys.optflags & FLAG_I)
    xprintf(" from %s:", inet_ntop(AF_INET6, (void*)&TT.src.sin6_addr, buf, sizeof(buf)));
  xprintf(" %ld data bytes.\n", TT.size);
  signal(SIGINT, onint);
  signal(SIGALRM, onsignal);
  itimer.it_interval = TT.interval;
  itimer.it_value = TT.interval;
  (void)setitimer(ITIMER_REAL, &itimer, NULL); //interval timer between pings
  retransmit();
  do_ping();
  summary();
}
