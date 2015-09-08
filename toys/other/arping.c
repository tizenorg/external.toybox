/* arping - send ARP REQUEST to a neighbour host.
 *
 * Copyright 2013 Sandeep Sharma <sandeep.jack2756@gmail.com>
 *
 * Not in SUSv4.

USE_ARPING(NEWTOY(arping, "<1>1s:I:w#<0c#<0AUDbqf", TOYFLAG_USR|TOYFLAG_SBIN))

config ARPING
  bool "arping"
  default y
  help
    Usage: arping [-fqbDUA] [-c CNT] [-w TIMEOUT] [-I IFACE] [-s SRC_IP] DST_IP

    Send ARP requests/replies

    -f         Quit on first ARP reply
    -q         Quiet
    -b         Keep broadcasting, don't go unicast
    -D         Duplicated address detection mode
    -U         Unsolicited ARP mode, update your neighbors
    -A         ARP answer mode, update your neighbors
    -c N       Stop after sending N ARP requests
    -w TIMEOUT Time to wait for ARP reply, seconds
    -I IFACE   Interface to use (default eth0)
    -s SRC_IP  Sender IP address
      DST_IP    Target IP address
*/
#define FOR_arping
#include "toys.h"
#include <arpa/inet.h>
#include <netinet/ether.h>

extern void *mempcpy(void *dest, const void *src, size_t n);

GLOBALS(
    long count;
    unsigned long time_out;
    char *iface;
    char *src_ip;

    char *dest_ip;
    int sockfd;
    struct in_addr source_addr;
    struct in_addr dest_addr;
    unsigned start;
    unsigned end;
    unsigned sent_at;
    unsigned sent_nr;
    unsigned rcvd_nr;
    unsigned brd_sent;
    unsigned rcvd_req;
    unsigned brd_rcv;
    unsigned unicast_flag;
)
struct sockaddr_ll src_pk; 
struct sockaddr_ll dst_pk; 
struct timeval t1, t2;

/*
 * gets information of INTERFACE and updates IFINDEX, MAC and IP
 */
static int get_interface(const char *interface, int *ifindex, uint32_t *oip, uint8_t *mac)
{
  struct ifreq req;
  int fd;
  struct sockaddr_in *ip;

  fd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
  if (fd < 0) {
    error_msg("IFACE : fail to get interface. ERROR : %d\n", fd);
    return fd;
  }
  req.ifr_addr.sa_family = AF_INET;
  strncpy(req.ifr_name, interface, IFNAMSIZ);
  req.ifr_name[IFNAMSIZ-1] = '\0';

  if (ioctl(fd, SIOCGIFFLAGS, &req) != 0) {
    error_msg("Is interface %s configured ?\n", interface);
    close(fd);
    return -1;
  }
  if (!(req.ifr_flags & IFF_UP)) return -1;

  if (oip) {
    if (ioctl(fd, SIOCGIFADDR, &req) != 0){
      error_msg("Is interface %s configured ?\n", interface);
      close(fd);
      return -1;
    }
    ip = (struct sockaddr_in*) &req.ifr_addr;
    *oip = ntohl(ip->sin_addr.s_addr);
  }
  if (ifindex) {
    if (ioctl(fd, SIOCGIFINDEX, &req) != 0) {
      error_msg("IFACE : NO INDEX interface %s\n", interface);
      close(fd);
      return -1;
    }
    *ifindex = req.ifr_ifindex;
  }
  if (mac) {
    if (ioctl(fd, SIOCGIFHWADDR, &req) != 0) {
      error_msg("IFACE : NO MAC interface %s\n", interface);
      close(fd);
      return -1;
    }
    memcpy(mac, req.ifr_hwaddr.sa_data, 6);
  }
  close(fd);
  return 0;
}

/*
 * SIGINT handler, Print Number
 * of Packets send or receive details.
 */
static void done(int sig)
{
  if(!(toys.optflags & FLAG_q)) {
    xprintf("Sent %u probe(s) (%u broadcast(s))\n", TT.sent_nr, TT.brd_sent);
    xprintf("Received %u repl%s (%u request(s), %u broadcast(s))\n", TT.rcvd_nr,
        TT.rcvd_nr == 1 ? "y":"ies", TT.rcvd_req, TT.brd_rcv);
  }
  if(toys.optflags & FLAG_D) exit(!!TT.rcvd_nr);
  if(toys.optflags & FLAG_U) exit(EXIT_SUCCESS); //In-U mode, No replies is expected so always true
  exit(!TT.rcvd_nr);
}
/*
 * Update exit value on exit
 * with corresponding option set/Unset
 */
static void exit_t(void) 
{
  if(toys.optflags & FLAG_D) toys.exitval = 0;
  else toys.exitval = 2;
  exit(toys.exitval);
}
/*
 * Create and Send Packet 
 */
static void send_packet()
{
  int ret;
  unsigned char toybuf_tmp[256] = {0,};
  struct arphdr *arp_h = (struct arphdr *) toybuf_tmp;
  unsigned char *ptr = (unsigned char *)(arp_h + 1);

  arp_h->ar_hrd = htons(ARPHRD_ETHER);
  arp_h->ar_pro = htons(ETH_P_IP);
  arp_h->ar_hln = src_pk.sll_halen;
  arp_h->ar_pln = 4;  
  arp_h->ar_op = (toys.optflags & FLAG_A) ? htons(ARPOP_REPLY) : htons(ARPOP_REQUEST);

  ptr = mempcpy(ptr, &src_pk.sll_addr, src_pk.sll_halen);
  ptr = mempcpy(ptr, &TT.source_addr, 4);
  if(toys.optflags & FLAG_A) ptr = mempcpy(ptr, &src_pk.sll_addr, src_pk.sll_halen);
  else ptr = mempcpy(ptr, &dst_pk.sll_addr, src_pk.sll_halen);

  ptr = mempcpy(ptr, &TT.dest_addr, 4);
  ret = sendto(TT.sockfd, toybuf_tmp, ptr - toybuf_tmp, 0, (struct sockaddr *)&dst_pk, sizeof(dst_pk));
  if(ret == ptr - toybuf_tmp) {
    gettimeofday(&t1, NULL);
    TT.sent_at = (t1.tv_sec * 1000000ULL + (t1.tv_usec));
    TT.sent_nr++;
    if(!TT.unicast_flag) TT.brd_sent++;
  }
}
/*
 * Receive Packet and filter
 * with valid checks.
 */
static int recv_from(unsigned char *recv_pk, struct sockaddr_ll *from, int *recv_len)
{
  struct arphdr *arp_hdr;
  unsigned char *p;
  struct in_addr s_ip, d_ip;
  arp_hdr = (struct arphdr *)recv_pk;
  p = (unsigned char *)(arp_hdr + 1);

  if(arp_hdr->ar_op != htons(ARPOP_REQUEST) && arp_hdr->ar_op != htons(ARPOP_REPLY))
    return 1;       

  if(from->sll_pkttype != PACKET_HOST                 
      && from->sll_pkttype != PACKET_BROADCAST            
      && from->sll_pkttype != PACKET_MULTICAST)           
    return 1;       


  if(arp_hdr->ar_pro != htons(ETH_P_IP)                    
      || (arp_hdr->ar_pln != 4)        
      || (arp_hdr->ar_hln != src_pk.sll_halen)                     
      || (*recv_len < (int)(sizeof(*arp_hdr) + 2 * (4 + arp_hdr->ar_hln))))
    return 1;       

  memcpy(&s_ip.s_addr, p + arp_hdr->ar_hln, 4);
  memcpy(&d_ip.s_addr, p + arp_hdr->ar_hln + 4 + arp_hdr->ar_hln, 4); 

  if(TT.dest_addr.s_addr != s_ip.s_addr) return 1;
  if(toys.optflags & FLAG_D) {
    if(TT.source_addr.s_addr && TT.source_addr.s_addr != d_ip.s_addr) return 1;
    if(memcmp(p, &src_pk.sll_addr, src_pk.sll_halen) == 0) return 1;
  }
  else {
    if(TT.source_addr.s_addr != d_ip.s_addr ) return 1;
  }
  if(!(toys.optflags & FLAG_q)) {
    printf("%scast re%s from %s [%s]",
        from->sll_pkttype == PACKET_HOST ? "Uni" : "Broad",
        arp_hdr->ar_op == htons(ARPOP_REPLY) ? "ply" : "quest",
        inet_ntoa(s_ip), ether_ntoa((struct ether_addr *) p));
    if (TT.sent_at) {                                                                                                                                                                     
      unsigned delta;
      gettimeofday(&t2, NULL);
      delta = (t2.tv_sec * 1000000ULL + (t2.tv_usec)) - TT.sent_at;
      xprintf(" %u.%03ums\n", delta / 1000, delta % 1000);
      xflush();
    }
  }

  TT.rcvd_nr++;
  if(from->sll_pkttype != PACKET_HOST) TT.brd_rcv++;
  if(arp_hdr->ar_op == htons(ARPOP_REQUEST)) TT.rcvd_req++;
  if(toys.optflags & FLAG_f) done(0);
  if(!(toys.optflags & FLAG_b)) {
    memcpy(dst_pk.sll_addr, p, src_pk.sll_halen); //if not FLAG_b, now unicast.
    TT.unicast_flag = 1;
  }
  return 0;
}
/* Alarm signal Handle,
 * send packets in one sec.
 * interval.
 */
static void send_signal(int sig)
{
  struct timeval start;
  
  gettimeofday(&start, NULL);
  if(!TT.start) TT.end = TT.start = start.tv_sec * 1000 + start.tv_usec / 1000; //In milisecounds.
  else TT.end = start.tv_sec*1000 + start.tv_usec / 1000; //In milisecounds.
  if(toys.optflags & FLAG_c){
    if(!TT.count) done(0);
    TT.count--; //Decremented before sending, anyhow we are here iff, we have count.(if count = 0, already have exited)
  }
  if((toys.optflags & FLAG_w) && ((TT.end - TT.start) > ((TT.time_out)*1000)))
    done(0);
  send_packet();
  alarm(1);
}
/*
 * arping main function. Parse args 
 * and options.
 */
void arping_main(void)
{
  struct ifreq ifr;
  int if_index;
  int recv_len;
  struct sockaddr_ll from;
  unsigned char *recv_pk;
  socklen_t len;
  
  TT.brd_sent = TT.sent_nr = TT.rcvd_nr = TT.rcvd_req = 0;
  TT.unicast_flag = 0;

  if(toys.optflags & FLAG_D) toys.optflags |= FLAG_f;
  if(toys.optflags & FLAG_A) toys.optflags |= FLAG_U;

  if(!(toys.optflags & FLAG_I)) TT.iface = "eth0";

  TT.sockfd = socket(AF_PACKET, SOCK_DGRAM, 0);
  if (TT.sockfd < 0) perror_exit("Socket");

  TT.dest_ip = toys.optargs[0]; //Destination addr, already exited if this is NULL.
  memset(&ifr, 0, sizeof(ifr));

  strncpy(ifr.ifr_name, TT.iface, sizeof(ifr.ifr_name));
  get_interface(TT.iface, &if_index, NULL, NULL);
  src_pk.sll_ifindex = if_index;

  if((ioctl(TT.sockfd, SIOCGIFFLAGS, (char*)&ifr)) < 0) perror_exit("SIOCGIFFLAGS");
  if (!(ifr.ifr_flags & IFF_UP)) 
    if (!(toys.optflags & FLAG_q)) perror_exit("Interface \"%s\" is down\n", TT.iface);
  if (ifr.ifr_flags & (IFF_NOARP | IFF_LOOPBACK)) {
    if (!(toys.optflags & FLAG_q)) {
      xprintf("Interface \"%s\" is not ARPable\n", TT.iface);
      exit_t();
    }
  }

  if(inet_aton(TT.dest_ip, &TT.dest_addr) == 0) {
    struct hostent *hp;
    hp = gethostbyname2(TT.dest_ip, AF_INET);
    if (!hp) perror_exit("arping: unknown host %s\n", TT.dest_ip);
    memcpy(&TT.dest_addr, hp->h_addr, 4);
  }

  if((toys.optflags & FLAG_s) && !(inet_aton(TT.src_ip, &TT.source_addr))) perror_exit("Bad source address");
  if(!(toys.optflags & FLAG_D) && (toys.optflags & FLAG_U) && TT.source_addr.s_addr == 0) TT.source_addr = TT.dest_addr;

  if(!(toys.optflags & FLAG_D) || TT.source_addr.s_addr) {
    struct sockaddr_in saddr;
    int p_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(p_fd < 0) perror_exit("Socket");
    if (setsockopt(p_fd, SOL_SOCKET, SO_BINDTODEVICE, TT.iface, strlen(TT.iface)) == -1) perror_exit("setsockopt");
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    if(TT.source_addr.s_addr) {
      saddr.sin_addr = TT.source_addr;
      if(bind(p_fd, (struct sockaddr*)&saddr, sizeof(saddr)) == -1) perror_exit("bind");
    }
    else {
      uint32_t oip;
      saddr.sin_port = htons(1025);
      saddr.sin_addr = TT.dest_addr;
      if(connect(p_fd, (struct sockaddr *) &saddr, sizeof(saddr)) < 0)
        perror_exit("cannot connect to remote host");
      get_interface(TT.iface, NULL, &oip, NULL);
      TT.source_addr.s_addr = htonl(oip);
    }
    xclose(p_fd);
  }
  src_pk.sll_family = AF_PACKET;
  src_pk.sll_protocol = htons(ETH_P_ARP);
  if(bind(TT.sockfd, (struct sockaddr *)&src_pk, sizeof(src_pk))) perror_exit("bind");
  socklen_t alen = sizeof(src_pk);
  getsockname(TT.sockfd, (struct sockaddr *)&src_pk, &alen);
  if(src_pk.sll_halen == 0) {
    perror_msg("src is not arpable");
    exit_t();
  }
  if (!(toys.optflags & FLAG_q)) {
    xprintf("ARPING to %s", inet_ntoa(TT.dest_addr));
    xprintf(" from %s via %s\n", inet_ntoa(TT.source_addr), TT.iface);
  }

  dst_pk = src_pk;
  memset(dst_pk.sll_addr, -1, dst_pk.sll_halen); //First packet always broadcast.
  signal(SIGINT, done);
  signal(SIGALRM, send_signal);

  recv_pk = xmalloc(4096);
  send_signal(0); // Send first Brodcast message.
  while(1) {
    len = sizeof(from);
    recv_len = recvfrom(TT.sockfd, recv_pk, 4096, 0, (struct sockaddr *)&from, &len);
    if(recv_len < 0) continue;
    recv_from(recv_pk, &from, &recv_len);
  }
}
