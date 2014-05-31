/* traceroute - trace the route to "host".
 *
 * Copyright 2012 Madhur Verma <mad.flexi@gmail.com>
 *
 * Not in SUSv4.

USE_TRACEROUTE(NEWTOY(traceroute, "<1>2f#<0>255z#<0>86400g*w#<0>86400t#<0>255s:q#<1>255p#<1>65535m#<1>255rvndlIUF64", TOYFLAG_USR|TOYFLAG_BIN))

config TRACEROUTE
  bool "traceroute"
  default y
  help
    usage: traceroute [-46FUIldnvr] [-f 1ST_TTL] [-m MAXTTL] [-p PORT] [-q PROBES]
    [-s SRC_IP] [-t TOS] [-w WAIT_SEC] [-g GATEWAY] [-i IFACE] [-z PAUSE_MSEC] HOST [BYTES]

    Trace the route to HOST

    -4,-6   Force IP or IPv6 name resolution
    -F    Set the don't fragment bit
    -U    Use UDP datagrams instead of ICMP ECHO
    -I    Use ICMP ECHO instead of UDP datagrams
    -l    Display the TTL value of the returned packet
    -f    Start from the 1ST_TTL hop (instead from 1)(RANGE 0 to 255)
    -d    Set SO_DEBUG options to socket
    -n    Print numeric addresses
    -v    verbose
    -r    Bypass routing tables, send directly to HOST
    -m    Max time-to-live (max number of hops)(RANGE 1 to 255)
    -p    Base UDP port number used in probes(default 33434)(RANGE 1 to 65535)
    -q    Number of probes per TTL (default 3)(RANGE 1 to 255)
    -s    IP address to use as the source address
    -t    Type-of-service in probe packets (default 0)(RANGE 0 to 255)
    -w    Time in seconds to wait for a response (default 3)(RANGE 0 to 86400)
    -g    Loose source route gateway (8 max)
    -z    Pause Time in sec (default 0)(RANGE 0 to 86400)
*/
#define FOR_traceroute
#include "toys.h"
#include <netinet/in.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>
#include <linux/if_ether.h>
#include <arpa/inet.h>
#include  <sys/poll.h>
#include <time.h>

GLOBALS(
  long max_ttl;
  long port;
  long ttl_probes;
  char *src_ip;
  long tos;
  long wait_time;
  struct arg_list *loose_source;
  long pause_time;
  long first_ttl;

  char* hostname;
  int recv_sock;
  int snd_sock;
  unsigned msg_len;
  struct ip *packet;
  struct sockaddr_in dest;
  struct sockaddr_in from;
  uint32_t gw_list[9];
)


#define flag_get(f,v,d)    ((toys.optflags & f) ? v : d)
#define flag_chk(f)      ((toys.optflags & f) ? 1 : 0)

#define RANGE_PACKET_SIZE  32768
#define DEF_UDP_PORT    33434
#define DEF_MAX_TTL      30
#define HOST_NAME_SIZE    1025
#define DEF_PROBE_VAL    3
#define DEF_WAIT_TIME    3
#define NGATEWAYS      8
#define ICMP_HD_SIZE    8
#define send_icmp      ((struct icmp *)(TT.packet +   1))
#define send_udp      ((struct udphdr *)(TT.packet + 1))

typedef struct payload_s {
  unsigned char seq;
  unsigned char ttl;
  struct timeval tv __attribute__((__packed__));
} payload_t;

payload_t *send_data;

/*
 * Computes and returns checksum SUM of buffer P of length LEN
 */
static u_int16_t in_cksum(u_int16_t *p, u_int len)
{
  u_int32_t sum = 0;
  int nwords = len >> 1;

  while (nwords-- != 0) sum += *p++;
  if (len & 1) {
    union {
      u_int16_t w;
      u_int8_t c[2];
    } u;
    u.c[0] = *(u_char *) p;
    u.c[1] = 0;
    sum += u.w;
  }
  /* end-around-carry */
  sum = (sum >> 16) + (sum & 0xffff);
  sum += (sum >> 16);
  return (~sum);
}

/*
 * sends a single probe packet with sequence SEQ and
 * time-to-live TTL
 */
void send_probe(int seq, int ttl)
{
  int res, len;
  void *out;

  if (flag_chk(FLAG_U)) {
    send_data->seq = seq;
    send_data->ttl = ttl;
  } else {
    send_icmp->icmp_seq = htons(seq);
    send_icmp->icmp_cksum = 0;
    send_icmp->icmp_cksum = in_cksum((uint16_t *) send_icmp, TT.msg_len - sizeof(struct ip));
    if (send_icmp->icmp_cksum == 0) send_icmp->icmp_cksum = 0xffff;
  }
  TT.dest.sin_port = TT.port + seq;

#ifdef IP_TTL
  res = setsockopt(TT.snd_sock, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl));
  if (res < 0) perror_exit("setsockopt ttl %d", ttl);
#endif

  if (flag_chk(FLAG_U)) {
    out = send_data;
    len = sizeof(payload_t);
  } else {
    out = send_icmp;
    len = TT.msg_len - sizeof(struct ip);
  }
  res = sendto(TT.snd_sock, out, len, 0, (struct sockaddr *) &TT.dest, sizeof(TT.dest));
  if (res != len) perror_exit(" sendto");
}

/*
 * converst str to long and checks the range of STR as MIN < STR < MAX
 */
static int strtol_range(char *str, int min, int max)
{
  char *endptr = NULL;
  errno=0;
  long ret_value = strtol(str,&endptr,10);
  if(errno) perror_exit("Invalid num %s",str);
  else {
    if(endptr && (*endptr!='\0'||endptr == str))
      perror_exit("Not a valid num %s",str);
  }
  if(ret_value >= min && ret_value <= max) return ret_value;
  else  perror_exit("Number %s is not in valid [%d-%d] Range",str,min,max);
}

/*
 * Traceroute main routine.
 */
void traceroute_main(void)
{
  const int set = 1;
  uint32_t ident;
  unsigned opt_len = 0;
  unsigned pack_size, tyser = 0;
  int lsrr = 0, ret;
  struct addrinfo *info, *rp;

  if (flag_chk(FLAG_g)) {
    struct arg_list *node = TT.loose_source;

    while (node) {
      struct sockaddr_in *sin = xzalloc(sizeof(struct sockaddr_in));
      if (lsrr >= NGATEWAYS) error_exit("no more than %d gateways", NGATEWAYS);

      rp = xzalloc(sizeof(struct addrinfo));
      rp->ai_family = AF_INET;
      rp->ai_socktype = SOCK_STREAM;

      ret = getaddrinfo(node->arg, NULL, rp, &info);
      if (ret || !info) perror_exit("LSRR BAD ADDRESS: can't find : %s ", TT.hostname);
      free(rp);

      for (rp = info; rp != NULL; rp = rp->ai_next) {
        if (rp->ai_addrlen == sizeof(struct sockaddr_in)) {
          memcpy(sin, rp->ai_addr, rp->ai_addrlen);
          break;
        }
      }
      if (!rp) perror_exit("Resolve failed ");
      freeaddrinfo(info);

      TT.gw_list[lsrr] = sin->sin_addr.s_addr;
      free(sin);
      ++lsrr;
      node = node->next;
    }
    opt_len = (lsrr + 1) * sizeof(TT.gw_list[0]);
  }

  pack_size = sizeof(struct ip) + opt_len;
  pack_size += (flag_chk(FLAG_U)) ? sizeof(struct udphdr) + sizeof(payload_t) : ICMP_HD_SIZE;

  if (toys.optc == 2) TT.msg_len = strtol_range(toys.optargs[1], pack_size, RANGE_PACKET_SIZE);

  TT.msg_len = (TT.msg_len < pack_size) ? pack_size : TT.msg_len;
  TT.recv_sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if (TT.recv_sock <= 0) perror_exit("Failed to create recv sock.");
  if (flag_chk(FLAG_d)
      && (setsockopt(TT.recv_sock, SOL_SOCKET, SO_DEBUG, &set, sizeof(set)) < 0))
    perror_exit("SO_DEBUG failed ");
  if (flag_chk(FLAG_r)
      && (setsockopt(TT.recv_sock, SOL_SOCKET, SO_DONTROUTE, &set, sizeof(set)) < 0))
    perror_exit("SO_DONTROUTE failed ");

  TT.hostname = toys.optargs[0];
  TT.port = flag_get(FLAG_p, TT.port, DEF_UDP_PORT);
  TT.snd_sock =(flag_chk(FLAG_U)) ? socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP):socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if (TT.snd_sock <= 0) error_exit("Failed to create send sock.");

#ifdef IP_OPTIONS
  if (lsrr > 0) {
    unsigned char optlist[MAX_IPOPTLEN];
    unsigned size;

    TT.gw_list[lsrr] = TT.dest.sin_addr.s_addr;
    ++lsrr;

    optlist[0] = IPOPT_NOP;
    /* loose source route option */
    optlist[1] = IPOPT_LSRR;
    size = lsrr * sizeof(TT.gw_list[0]);
    optlist[2] = size + 3;
    optlist[3] = IPOPT_MINOFF;
    memcpy(optlist + 4, TT.gw_list, size);

    if (setsockopt(TT.snd_sock, IPPROTO_IP, IP_OPTIONS,
        (char *)optlist, size + sizeof(TT.gw_list[0])) < 0)
      perror_exit("LSRR IP_OPTIONS");
  }
#endif
#ifdef SO_SNDBUF
  if (setsockopt(TT.snd_sock, SOL_SOCKET, SO_SNDBUF, &TT.msg_len, sizeof(TT.msg_len)) < 0)
    perror_exit("SO_SNDBUF failed ");
#endif
#ifdef IP_TOS
  if (flag_chk(FLAG_t) && setsockopt(TT.snd_sock, IPPROTO_IP, IP_TOS, &tyser, sizeof(tyser)) < 0)
    perror_exit("IP_TOS %d failed ", TT.tos);
#endif
#ifdef IP_DONTFRAG
  if (flag_chk(FLAG_F) && (setsockopt(TT.snd_sock, IPPROTO_IP, IP_DONTFRAG, &set, sizeof(set)) < 0))
    perror_exit("IP_DONTFRAG failed ");
#endif

  if (flag_chk(FLAG_d)
      && (setsockopt(TT.snd_sock, SOL_SOCKET, SO_DEBUG, &set, sizeof(set)) < 0))
    perror_exit("SO_DEBUG failed ");
  if (flag_chk(FLAG_r)
      && (setsockopt(TT.snd_sock, SOL_SOCKET, SO_DONTROUTE, &set, sizeof(set)) < 0))
    perror_exit("SO_DONTROUTE failed ");

  rp = xzalloc(sizeof(struct addrinfo));
  rp->ai_family = AF_INET;
  rp->ai_socktype = flag_chk(FLAG_U) ? SOCK_DGRAM : SOCK_RAW;
  rp->ai_protocol = flag_chk(FLAG_U) ? IPPROTO_UDP : IPPROTO_ICMP;

  ret = getaddrinfo(TT.hostname, NULL, rp, &info);
  if (ret || !info) perror_exit("BAD ADDRESS: can't find : %s ", TT.hostname);
  free(rp);

  for (rp = info; rp != NULL; rp = rp->ai_next) {
    if (rp->ai_addrlen == sizeof(struct sockaddr_in)) {
      memcpy(&TT.dest, rp->ai_addr, rp->ai_addrlen);
      break;
    }
  }
  if (!rp) perror_exit("Resolve failed ");
  freeaddrinfo(info);

  TT.packet = xmalloc(TT.msg_len);
  ident = getpid();

  if (flag_chk(FLAG_U)) {
    send_data = (payload_t *) (send_udp + 1);
  } else {
    ident |= 0x8000;
    send_icmp->icmp_type = ICMP_ECHO;
    send_icmp->icmp_id = htons(ident);
  }
  if (flag_chk(FLAG_s)) {
      struct sockaddr_in *source =  xzalloc(sizeof(struct sockaddr_in));
      inet_aton(TT.src_ip, &source->sin_addr);
        if (setsockopt(TT.snd_sock, IPPROTO_IP, IP_MULTICAST_IF, (struct sockaddr*)&source, sizeof(*source)))
          perror_exit("can't set multicast source interface");
      bind(TT.snd_sock,(struct sockaddr*)&source, sizeof(*source));
      free(source);
  }
  struct pollfd pfd[1];
  pfd[0].fd = TT.recv_sock;
  pfd[0].events = POLLIN;
  int tv = flag_get(FLAG_w ,TT.wait_time, DEF_WAIT_TIME) * 1000;

  int seq, fexit = 0, ttl = flag_get(FLAG_f, TT.first_ttl, 1);
  TT.max_ttl = flag_get(FLAG_m, TT.max_ttl, DEF_MAX_TTL);
  TT.ttl_probes = flag_get(FLAG_q, TT.ttl_probes, DEF_PROBE_VAL);

  printf("traceroute to %s(%s)", TT.hostname, inet_ntoa(TT.dest.sin_addr));
  if (flag_chk(FLAG_s)) printf(" from s");
  printf(", %ld hops max, %u byte packets\n", TT.max_ttl, TT.msg_len);

  if(ttl > TT.max_ttl) perror_exit("ERROR :Range for -f is 1 to %d (max ttl)", TT.max_ttl);
  for (; ttl <= TT.max_ttl; ++ttl) {
    int probe;
    struct timeval t1, t2;
    struct sockaddr_in last;
    memset(&last, 0, sizeof(last));

    printf("%2d", ttl);
    fflush(NULL);

    for (probe = 0, seq = 0; probe < TT.ttl_probes; ++probe) {
      int res = 0, tleft = tv;
      if (probe != 0 && flag_chk(FLAG_z)) sleep(TT.pause_time);
      fexit = 0;

      send_probe(++seq, ttl);
      gettimeofday(&t1, NULL);

POLL_IN:   res = poll(pfd, 1, tleft);
      gettimeofday(&t2, NULL);

      if (res == 0) {
        printf("  *");
        continue;
      }
      if (res < 0) {
        tleft = tv - (t2.tv_sec * 1000000ULL + t2.tv_usec)
            + (t1.tv_sec * 1000000ULL + t1.tv_usec);
        goto POLL_IN;
      }
      if (pfd[0].revents) {
        unsigned addrlen = sizeof(TT.from);
        struct ip *rcv_pkt = (struct ip*) toybuf;
        int pmtu = 0;
        int rcv_len = recvfrom(TT.recv_sock, rcv_pkt, sizeof(toybuf),
            MSG_DONTWAIT, (struct sockaddr *) &TT.from, &addrlen);
        if (rcv_len > 0) {
          struct icmp *ricmp;
          int icmp_res = -1;
          ricmp = (struct icmp *) ((void*)rcv_pkt + (rcv_pkt->ip_hl << 2));

          if (ricmp->icmp_code == ICMP_UNREACH_NEEDFRAG)
            pmtu = ntohs(ricmp->icmp_nextmtu);

          if ((ricmp->icmp_type == ICMP_TIMXCEED
              && ricmp->icmp_code == ICMP_TIMXCEED_INTRANS)
              || ricmp->icmp_type == ICMP_UNREACH
              || ricmp->icmp_type == ICMP_ECHOREPLY) {

            const struct ip *hip;
            const struct udphdr *hudp;
            struct icmp *hicmp;

            hip = &ricmp->icmp_ip;

            if (flag_chk(FLAG_U)) {
              hudp = (struct udphdr*) (hip + (hip->ip_hl << 2));
              if ((hip->ip_hl << 2) + 12 <= rcv_len && hip->ip_p == IPPROTO_UDP
                  && hudp->dest == htons(TT.port + seq))
                icmp_res = (ricmp->icmp_type == ICMP_TIMXCEED ?-1 : ricmp->icmp_code);
            } else {
              hicmp = (struct icmp *) ((void*)hip + (hip->ip_hl << 2));
              if (ricmp->icmp_type == ICMP_ECHOREPLY && ricmp->icmp_id == htons(ident)
                  && ricmp->icmp_seq == htons(seq))
                icmp_res = ICMP_UNREACH_PORT;

              if ((hip->ip_hl << 2) + ICMP_HD_SIZE <= rcv_len
                  && hip->ip_p == IPPROTO_ICMP
                  && hicmp->icmp_id == htons(ident)
                  && hicmp->icmp_seq == htons(seq))
                icmp_res = (ricmp->icmp_type == ICMP_TIMXCEED ? -1 : ricmp->icmp_code);
            }
          }

          if (addrlen > 0) {
            unsigned delta = (t2.tv_sec * 1000000ULL + t2.tv_usec)
                - (t1.tv_sec * 1000000ULL + t1.tv_usec);

            if (memcmp(&last.sin_addr, &TT.from.sin_addr,
                sizeof(struct in_addr)) != 0) {

              if (!flag_chk(FLAG_n)) {
                char host[HOST_NAME_SIZE];
                if (getnameinfo((struct sockaddr *) &TT.from,
                    sizeof(TT.from), host, HOST_NAME_SIZE, NULL, 0, 0)
                    == 0)
                  printf("  %s (", host);
              }
              printf(" %s", inet_ntoa(TT.from.sin_addr));
              if (!flag_chk(FLAG_n) )
                printf(")");
              last = TT.from;
            }
            printf("  %u.%03u ms", delta / 1000, delta % 1000);
            if (flag_chk(FLAG_l)) printf(" (%d)", rcv_pkt->ip_ttl);
			if (flag_chk(FLAG_v)) {
				printf(" %d bytes from %s : icmp type %d code %d\t",
						rcv_len, inet_ntoa(TT.from.sin_addr),
						ricmp->icmp_type, ricmp->icmp_code);
			}
          } else if (addrlen <= 0)
            printf("\t!H");

			if (ricmp->icmp_type == ICMP_DEST_UNREACH) {
				switch (icmp_res) {
				case ICMP_UNREACH_PORT:
				  if (rcv_pkt->ip_ttl <= 1)
					printf(" !");
				  break;
				case ICMP_UNREACH_NET:
				  printf(" !N");
				  ++fexit;
				  break;
				case ICMP_UNREACH_HOST:
				  printf(" !H");
				  ++fexit;
				  break;
				case ICMP_UNREACH_PROTOCOL:
				  printf(" !P");
				  ++fexit;
				  break;
				case ICMP_UNREACH_NEEDFRAG:
				  printf(" !F-%d", pmtu);
				  ++fexit;
				  break;
				case ICMP_UNREACH_SRCFAIL:
				  printf(" !S");
				  ++fexit;
				  break;
				case ICMP_UNREACH_FILTER_PROHIB:  /* FALLTHROUGH */
				case ICMP_UNREACH_NET_PROHIB: /* misuse */
				  printf(" !A");
				  ++fexit;
				  break;
				case ICMP_UNREACH_HOST_PROHIB:
				  printf(" !C");
				  ++fexit;
				  break;
				case ICMP_UNREACH_HOST_PRECEDENCE:
				  printf(" !V");
				  ++fexit;
				  break;
				case ICMP_UNREACH_PRECEDENCE_CUTOFF:
				  printf(" !C");
				  ++fexit;
				  break;
				case ICMP_UNREACH_NET_UNKNOWN:  /* FALLTHROUGH */
				case ICMP_UNREACH_HOST_UNKNOWN:
				  printf(" !U");
				  ++fexit;
				  break;
				case ICMP_UNREACH_ISOLATED:
				  printf(" !I");
				  ++fexit;
				  break;
				case ICMP_UNREACH_TOSNET:  /* FALLTHROUGH */
				case ICMP_UNREACH_TOSHOST:
				  printf(" !T");
				  ++fexit;
				  break;
				}
			}
        } else if (rcv_len <= 0) {
          tleft = tv - (t2.tv_sec * 1000000ULL + t2.tv_usec)
              + (t1.tv_sec * 1000000ULL + t1.tv_usec);
          goto POLL_IN;
        }
      }
    }
    printf("\n");
    if (( memcmp(&TT.from.sin_addr, &TT.dest.sin_addr, sizeof(struct in_addr)) == 0) || (fexit >= TT.ttl_probes -1))
      break;
  }
}
