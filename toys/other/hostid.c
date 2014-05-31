/* hostid.c - Print the numeric identifier for the current host.
 *
 * Copyright 2013 Ranjan Kumar <ranjankumar.bth@gmail.com>
 *
 * Not in SUSv4.

USE_HOSTID(NEWTOY(hostid, ">0", TOYFLAG_USR|TOYFLAG_BIN))

config HOSTID
  bool "hostid"
  default y
  help
    usage: hostid

    Print the numeric identifier for the current host.
*/

#include "toys.h"

/*
 * host id main function.
 */
void hostid_main(void)
{
  xprintf("%08lx\n", gethostid());
}
