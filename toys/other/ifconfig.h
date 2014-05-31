/* ifconfig.h - Header file for ifconfig command.
 *
 * Copyright 2012 Ranjan Kumar <ranjankumar.bth@gmail.com>
 *
 * Not in SUSv4.
 *
*/

#include <net/if.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <alloca.h>

//Structure Declaration
typedef struct _proc_net_dev_info {
  char        ifrname[IFNAMSIZ]; //interface name.
  unsigned long long   receive_bytes; //total bytes received
  unsigned long long   receive_packets; //total packets received
  unsigned long     receive_errors; //bad packets received
  unsigned long     receive_drop; //no space in linux buffers
  unsigned long     receive_fifo; //receiver fifo overrun
  unsigned long     receive_frame; //received frame alignment error
  unsigned long     receive_compressed;
  unsigned long     receive_multicast; //multicast packets received

  unsigned long long   transmit_bytes; //total bytes transmitted
  unsigned long long   transmit_packets; //total packets transmitted
  unsigned long     transmit_errors; //packet transmit problems
  unsigned long     transmit_drop; //no space available in linux
  unsigned long     transmit_fifo;
  unsigned long     transmit_colls;
  unsigned long     transmit_carrier;
  unsigned long     transmit_compressed; //num_tr_compressed;
}PROC_NET_DEV_INFO;

//man netdevice
typedef struct _iface_list {
  int    hw_type;
  short   ifrflags; //used for addr, broadcast, and mask.
  short   ifaddr; //if set print ifraddr, irrdstaddr, ifrbroadaddr and ifrnetmask.
  struct sockaddr ifraddr;
  struct sockaddr ifrdstaddr;
  struct sockaddr ifrbroadaddr;
  struct sockaddr ifrnetmask;
  struct sockaddr ifrhwaddr;
  int    ifrmtu;
  int   ifrmetric;
  PROC_NET_DEV_INFO dev_info;
  int   txqueuelen;
  struct ifmap ifrmap;
  int non_virtual_iface;
  struct  _iface_list *next; //, *prev;
}IFACE_LIST;


#define HW_NAME_LEN 20
#define HW_TITLE_LEN 30

typedef struct _hw_info {
  char hw_name[HW_NAME_LEN];
  char hw_title[HW_TITLE_LEN];
  int     hw_addrlen;
}HW_INFO;

static const char *const field_format[] = {
  "%n%llu%u%u%u%u%n%n%n%llu%u%u%u%u%u",
  "%llu%llu%u%u%u%u%n%n%llu%llu%u%u%u%u%u",
  "%llu%llu%u%u%u%u%u%u%llu%llu%u%u%u%u%u%u"
};

#define PROC_NET_DEV "/proc/net/dev"
#define PROC_NET_IFINET6 "/proc/net/if_inet6"
#define NO_RANGE -1
#define IO_MAP_INDEX 0x100

static int show_iface(char *iface_name);
static void print_ip6_addr(IFACE_LIST *l_ptr);
static void clear_list(void);

//from /net/if.h
static char *iface_flags_str[] = {
      "UP",
      "BROADCAST",
      "DEBUG",
      "LOOPBACK",
      "POINTOPOINT",
      "NOTRAILERS",
      "RUNNING",
      "NOARP",
      "PROMISC",
      "ALLMULTI",
      "MASTER",
      "SLAVE",
      "MULTICAST",
      "PORTSEL",
      "AUTOMEDIA",
      "DYNAMIC",
      NULL
};
//from /usr/include/linux/netdevice.h
#ifdef IFF_PORTSEL
//Media selection options.
# ifndef IF_PORT_UNKNOWN
enum {
    IF_PORT_UNKNOWN = 0,
    IF_PORT_10BASE2,
    IF_PORT_10BASET,
    IF_PORT_AUI,
    IF_PORT_100BASET,
    IF_PORT_100BASETX,
    IF_PORT_100BASEFX
};
# endif
#endif

//from kernel header ipv6.h
#define IPV6_ADDR_ANY 0x0000U
#define IPV6_ADDR_LOOPBACK  0x0010U
#define IPV6_ADDR_LINKLOCAL  0x0020U
#define IPV6_ADDR_SITELOCAL  0x0040U
#define IPV6_ADDR_COMPATv4  0x0080U

//==================================================================================
//for the param settings.

//for ipv6 add/del
struct ifreq_inet6 {
  struct in6_addr ifrinte6_addr;
  uint32_t ifrinet6_prefixlen;
  int ifrinet6_ifindex;
};

#ifndef SIOCSKEEPALIVE
# define SIOCSKEEPALIVE  (SIOCDEVPRIVATE)        /* Set keepalive timeout in sec */
# define SIOCGKEEPALIVE  (SIOCDEVPRIVATE+1)        /* Get keepalive timeout */
#endif

#ifndef SIOCSOUTFILL
# define SIOCSOUTFILL  (SIOCDEVPRIVATE+2)        /* Set outfill timeout */
# define SIOCGOUTFILL  (SIOCDEVPRIVATE+3)        /* Get outfill timeout */
#endif

#ifndef INFINIBAND_ALEN
# define INFINIBAND_ALEN 20
#endif

static void set_data(int sockfd, struct ifreq *ifre, char *kval, int request, const char *req_name);
static void set_flags(int sockfd, struct ifreq *ifre, int arg_flag, int flag); //verify
static void set_mtu(int sockfd, struct ifreq *ifre, const char *mtu); //verify
static void set_metric(int sockfd, struct ifreq *ifre, const char *metric); //verify
static void set_qlen(int sockfd, struct ifreq *ifre, const char *qlen); //verify
static void set_address(int sockfd, const char *host_name, struct ifreq *ifre, int request, const char *req_name);
static void set_hw_address(int sockfd, char ***argv, struct ifreq *ifre, int request, const char *req_name);
static void set_ipv6_addr(int sockfd, struct ifreq *ifre, const char *ipv6_addr, int request, const char *req_name);
static void set_memstart(int sockfd, struct ifreq *ifre, const char *start_addr, int request, const char *req_name);
static void set_ioaddr(int sockfd, struct ifreq *ifre, const char *baddr, int request, const char *req_name);
static void set_irq(int sockfd, struct ifreq *ifre, const char *irq_val, int request, const char *req_name);


//==================================================================================
