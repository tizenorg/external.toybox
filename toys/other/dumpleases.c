/* dumpleases.c - Dump the leases granted by udhcpd.
 *
 * Copyright 2013 Sandeep Sharma <sandeep.jack2756@gmail.com>
 *

USE_DUMPLEASES(NEWTOY(dumpleases, ">0arf:[!ar]", TOYFLAG_USR|TOYFLAG_BIN))

config DUMPLEASES
  bool "dumpleases"
  default y
  help
  Usage: dumpleases [-r|-a] [-f LEASEFILE]

    Display DHCP leases granted by udhcpd
    -f FILE,  Lease file
    -r        Show remaining time
    -a        Show expiration time
*/

#define FOR_dumpleases
#include "toys.h"

#define DEF_LEASE_FILE "/var/lib/misc/udhcpd.leases"

GLOBALS(
    char *file;
)
/*
 * lease structure
 */
struct lease {                                                                                                                                                                                   
  uint32_t expires;
  uint32_t lease_nip;
  uint8_t lease_mac[6];
  char hostname[20];
  uint8_t pad[2]; //Padding
};
/*
 * Dumpleases main function.
 */
void dumpleases_main(void)
{
  int fd, i;
  struct in_addr addr;
  struct lease lease_struct;
  int64_t written_time , current_time, exp;
  if(!(toys.optflags & FLAG_f)) TT.file = DEF_LEASE_FILE;

  fd = xopen(TT.file, O_RDONLY);
  xprintf("Mac Address       IP Address      Host Name           Expires %s\n", (toys.optflags & FLAG_a) ? "at" : "in");
  xread(fd, &written_time, sizeof(written_time));
  current_time = time(NULL);
  written_time = SWAP_BE64(written_time);
  if(current_time < written_time) written_time = current_time;

  while(sizeof(lease_struct) == (readall(fd, &lease_struct, sizeof(lease_struct)))) {
    const char *fmt = ":%02x" + 1;
    for (i = 0; i < 6; i++) {
      printf(fmt, lease_struct.lease_mac[i]);
      fmt = ":%02x";
    }
    addr.s_addr = lease_struct.lease_nip;
    lease_struct.hostname[19] = '\0';
    xprintf(" %-16s%-20s", inet_ntoa(addr),lease_struct.hostname );
    exp = ntohl(lease_struct.expires) + written_time;
    if (exp <= current_time) {
      xputs("expired");
      continue;
    }
    if (!(toys.optflags & FLAG_a)) { 
      unsigned dt, hr, m;
      unsigned expires = exp - current_time;
      dt = expires / (24*60*60); expires %= (24*60*60);
      hr = expires / (60*60); expires %= (60*60);
      m = expires / 60; expires %= 60;
      if (dt) xprintf("%u days ", dt);
      xprintf("%02u:%02u:%02u\n", hr, m, (unsigned)expires);
    } else {
      fputs(ctime((const time_t*)&exp), stdout);
    }
  }
  xclose(fd);
}
