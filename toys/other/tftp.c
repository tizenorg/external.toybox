/* tftp.c - TFTP client.
 *
 * Copyright 2012 Madhur Verma <mad.flexi@gmail.com>
 *
 * Not in SUSv4.

USE_TFTP(NEWTOY(tftp, "<1b#<8>65464r:l:gp", TOYFLAG_USR|TOYFLAG_BIN))

config TFTP
  bool "tftp"
  default y
  help
    usage: tftp [OPTIONS] HOST [PORT]

    Transfer file from/to tftp server.

    -l FILE Local FILE
    -r FILE Remote FILE
    -g    Get file
    -p    Put file
    -b SIZE Transfer blocks of SIZE octets(8 <= SIZE <= 65464)
*/

#define FOR_tftp
#include "toys.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

GLOBALS(
  char *local_file;
  char *remote_file;
  long block_size;
)

#define flagGet(f,v,d)    ((toys.optflags & f) ? v : d)
#define flagChk(f)      ((toys.optflags & f) ? 1 : 0)

#define TFTP_PORT      69
#define TFTP_TIMEOUT    10
#define TFTP_BLKSIZE    512
#define TFTP_RETRIES    3

#define TFTP_ACKHEADERSIZE  4
#define TFTP_ERRHEADERSIZE  4
#define TFTP_DATAHEADERSIZE 4

#define TFTP_MAXPACKETSIZE  (TFTP_DATAHEADERSIZE + TFTP_BLKSIZE)
#define TFTP_PACKETSIZE    TFTP_MAXPACKETSIZE

#define TFTP_DATASIZE    (TFTP_PACKETSIZE-TFTP_DATAHEADERSIZE)
#define TFTP_IOBUFSIZE    (TFTP_PACKETSIZE+8)

#define TFTP_OP_RRQ      1  /* Read Request      RFC 1350, RFC 2090 */
#define TFTP_OP_WRQ      2  /* Write Request     RFC 1350 */
#define TFTP_OP_DATA    3  /* Data chunk      RFC 1350 */
#define TFTP_OP_ACK      4  /* Acknowledgement     RFC 1350 */
#define TFTP_OP_ERR      5  /* Error Message     RFC 1350 */
#define TFTP_OP_OACK    6  /* Option acknowledgment RFC 2347 */

#define TFTP_ER_NONE    0  /* No error */
#define TFTP_ER_NOSUCHFILE  1  /* File not found */
#define TFTP_ER_ACCESS    2  /* Access violation */
#define TFTP_ER_FULL    3  /* Disk full or allocation exceeded */
#define TFTP_ER_ILLEGALOP  4  /* Illegal TFTP operation */
#define TFTP_ER_UNKID    5  /* Unknown transfer ID */
#define TFTP_ER_EXISTS    6  /* File already exists */
#define TFTP_ER_UNKUSER    7  /* No such user */
#define TFTP_ER_NEGOTIATE  8  /* Terminate transfer due to option negotiation */

#define TFTP_ES_NOSUCHFILE  "File not found"
#define TFTP_ES_ACCESS    "Access violation"
#define TFTP_ES_FULL    "Disk full or allocation exceeded"
#define TFTP_ES_ILLEGALOP  "Illegal TFTP operation"
#define TFTP_ES_UNKID    "Unknown transfer ID"
#define TFTP_ES_EXISTS    "File already exists"
#define TFTP_ES_UNKUSER    "No such user"
#define TFTP_ES_NEGOTIATE  "Terminate transfer due to option negotiation"

/*
 * convert str to long within given range
 */
static int strtol_range(char *str, int min, int max)
{
  char *endptr = NULL;
  errno = 0;
  long ret_value = strtol(str, &endptr, 10);
  if(errno) perror_exit("Invalid num %s", str);
  else {
    if(endptr && (*endptr != '\0' || endptr == str))
      perror_exit("Not a valid num %s", str);
  }
  if(ret_value >= min && ret_value <= max) return ret_value;
  else  perror_exit("Number %s is not in valid [%d-%d] Range\n", str, min, max);
}

/*
 * Initializes SERVER with ADDR and returns socket.
 */
static int init_tftp(struct sockaddr_in *server, in_addr_t addr)
{
  struct timeval to;
  int sd, ret;
  const int set = 1;

  int port = TFTP_PORT;
  sd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sd < 0) perror_exit("socket create failed");
  to.tv_sec = TFTP_TIMEOUT;
  to.tv_usec = 0;
  ret = setsockopt(sd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(struct timeval));
  ret = setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set));
  if (ret < 0) perror_exit("setsockopt failed");
  if(toys.optc == 2) port = strtol_range(toys.optargs[1], 1, 65535);
  memset(server, 0, sizeof(struct sockaddr_in));
  server->sin_family = AF_INET;
  server->sin_addr.s_addr = addr;
  server->sin_port = htons(port);
  return sd;
}

/*
 * Makes a request packet in BUFFER with OPCODE and file PATH of MODE
 * and returns length of packet.
 */
static int mkpkt_request(uint8_t *buffer, int opcode, const char *path, int mode)
{
  buffer[0] = opcode >> 8;
  buffer[1] = opcode & 0xff;
  if(strlen(path) > TFTP_BLKSIZE) error_exit("path too long");
  return sprintf((char*) &buffer[2], "%s%c%s", path, 0, (mode ? "octet" : "netascii")) + 3;
}

/*
 * Makes an acknowledgement packet in BUFFER of BLOCNO
 * and returns packet length.
 */
static int mkpkt_ack(uint8_t *buffer, uint16_t blockno)
{
  buffer[0] = TFTP_OP_ACK >> 8;
  buffer[1] = TFTP_OP_ACK & 0xff;
  buffer[2] = blockno >> 8;
  buffer[3] = blockno & 0xff;
  return 4;
}

/*
 * Makes an error packet in BUFFER with ERRORCODE and ERRORMSG.
 * and returns packet length.
 */
static int mkpkt_err(uint8_t *buffer, uint16_t errorcode, const char *errormsg)
{
  buffer[0] = TFTP_OP_ERR >> 8;
  buffer[1] = TFTP_OP_ERR & 0xff;
  buffer[2] = errorcode >> 8;
  buffer[3] = errorcode & 0xff;
  strcpy((char*) &buffer[4], errormsg);
  return strlen(errormsg) + 5;
}

/*
 * Recieves data from server in BUFF with socket SD and updates FROM
 * and returns read length.
 */
static ssize_t read_server(int sd, void *buf, size_t len, struct sockaddr_in *from)
{
  int alen;
  ssize_t nb;

  for (;;) {
    memset(buf, 0, len);
    alen = sizeof(struct sockaddr_in);
    nb = recvfrom(sd, buf, len, 0, (struct sockaddr*) from, (socklen_t*) &alen);
    if (nb < 0) {
      if (errno == EAGAIN) {
        perror_msg("server read timed out");
        return nb;
      }else if (errno != EINTR) {
        perror_msg("server read failed");
        return nb;
      }
    }else return nb;
  }
  return nb;
}

/*
 * sends data to server TO from BUFF of length LEN through socket SD
 * and returns successfully send bytes number.
 */
static ssize_t write_server(int sd, const void *buf, size_t len, struct sockaddr_in *to)
{
  ssize_t nb;
  for (;;) {
    nb = sendto(sd, buf, len, 0, (struct sockaddr*) to, sizeof(struct sockaddr_in));
    if (nb < 0) {
      if (errno != EINTR) {
        perror_msg("server write failed");
        return nb;
      }
    } else return nb;
  }
  return nb;
}

/*
 * checks packet for data and updates block no
 */
static inline int check_data(const uint8_t *packet, uint16_t *opcode, uint16_t *blockno)
{
  *opcode = (uint16_t) packet[0] << 8 | (uint16_t) packet[1];
  if (*opcode == TFTP_OP_DATA) {
    *blockno = (uint16_t) packet[2] << 8 | (uint16_t) packet[3];
    return 0;
  }
  return -1;
}

/*
 * Makes data packet through FD from file OFFSET in buffer PACKET of BLOCKNO
 */
static int mkpkt_data(int fd, off_t offset, uint8_t *packet, uint16_t blockno)
{
  off_t tmp;
  int nbytesread;

  packet[0] = TFTP_OP_DATA >> 8;
  packet[1] = TFTP_OP_DATA & 0xff;
  packet[2] = blockno >> 8;
  packet[3] = blockno & 0xff;
  tmp = lseek(fd, offset, SEEK_SET);
  if (tmp == (off_t) -1) {
    perror_msg("lseek failed");
    return -1;
  }
  nbytesread = readall(fd, &packet[TFTP_DATAHEADERSIZE], TFTP_DATASIZE);
  if (nbytesread < 0) return -1;
  return nbytesread + TFTP_DATAHEADERSIZE;
}

/*
 * Receives ACK responses from server and updates blockno
 */
static int read_ack(int sd, uint8_t *packet, struct sockaddr_in *server, uint16_t *port, uint16_t *blockno)
{
  struct sockaddr_in from;
  ssize_t nbytes;
  uint16_t opcode, rblockno;
  int packetlen, retry;

  for (retry = 0; retry < TFTP_RETRIES; retry++) {
    for (;;) {
      nbytes = read_server(sd, packet, TFTP_IOBUFSIZE, &from);
      if (nbytes < TFTP_ACKHEADERSIZE) {
        if (nbytes == 0) error_msg("Connection lost.");
        else if (nbytes > 0) error_msg("Short packet: %d bytes", nbytes);
        else error_msg("Server read ACK failure.");
        break;
      } else {
        if (!*port) {
          *port = from.sin_port;
          server->sin_port = from.sin_port;
        }
        if (server->sin_addr.s_addr != from.sin_addr.s_addr) {
          error_msg("Invalid address in DATA.");
          continue;
        }
        if (*port != server->sin_port) {
          error_msg("Invalid port in DATA.");
          packetlen = mkpkt_err(packet, TFTP_ER_UNKID, TFTP_ES_UNKID);
          (void) write_server(sd, packet, packetlen, server);
          continue;
        }
        opcode = (uint16_t) packet[0] << 8 | (uint16_t) packet[1];
        rblockno = (uint16_t) packet[2] << 8 | (uint16_t) packet[3];

        if (opcode != TFTP_OP_ACK) {
          error_msg("Bad opcode.");
          if (opcode > 5) {
            packetlen = mkpkt_err(packet, TFTP_ER_ILLEGALOP, TFTP_ES_ILLEGALOP);
            (void) write_server(sd, packet, packetlen, server);
          }
          break;
        }
        if(blockno != NULL) *blockno = rblockno;
        return 0;
      }
    }
  }
  error_msg("Timeout, Waiting for ACK.");
  return -1;
}

/*
 * receives file from server.
 */
static int file_get(void)
{
  struct in_addr inaddr;
  struct sockaddr_in server, from;
  uint8_t *packet;
  uint16_t blockno = 0, opcode, rblockno;
  int len, sd, fd, retry, nbytesrecvd = 0, ndatabytes, ret, result = -1;

  packet = (uint8_t*) xzalloc(TFTP_IOBUFSIZE);
  fd = xcreate(TT.local_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  inet_aton(toys.optargs[0], &inaddr);
  sd = init_tftp(&server, inaddr.s_addr);
  if (sd < 0) {
    unlink(TT.local_file);
    goto errout_with_fd;
  }

  do {
    blockno++;
    for (retry = 0; retry < TFTP_RETRIES; retry++) {
      if (blockno == 1) {
        len = mkpkt_request(packet, TFTP_OP_RRQ, TT.remote_file, 1);
        ret = write_server(sd, packet, len, &server);
        if (ret != len){
          unlink(TT.local_file);
          goto errout_with_sd;
        }
        server.sin_port = 0;
      }
      nbytesrecvd = read_server(sd, packet, TFTP_IOBUFSIZE, &from);
      if (nbytesrecvd > 0) {
        if (server.sin_addr.s_addr != from.sin_addr.s_addr) {
          error_msg("Invalid address in DATA.");
          retry--;
          continue;
        }
        if (server.sin_port && server.sin_port != from.sin_port) {
          error_msg("Invalid port in DATA.");
          len = mkpkt_err(packet, TFTP_ER_UNKID, TFTP_ES_UNKID);
          ret = write_server(sd, packet, len, &from);
          retry--;
          continue;
        }
        if (nbytesrecvd < TFTP_DATAHEADERSIZE) {
          error_msg("Tiny data packet ignored.");
          continue;
        }
        if (check_data(packet, &opcode, &rblockno) != 0
            || blockno != rblockno) {

        if(opcode == TFTP_OP_ERR){
        	switch(opcode){
        	  case TFTP_ER_NOSUCHFILE: error_msg(TFTP_ES_NOSUCHFILE); break;
        	  case TFTP_ER_ACCESS: error_msg(TFTP_ES_ACCESS); break;
        	  case TFTP_ER_FULL: error_msg(TFTP_ES_FULL); break;
        	  case TFTP_ER_ILLEGALOP: error_msg(TFTP_ES_ILLEGALOP); break;
        	  case TFTP_ER_UNKID: error_msg(TFTP_ES_UNKID); break;
        	  case TFTP_ER_EXISTS: error_msg(TFTP_ES_EXISTS); break;
        	  case TFTP_ER_UNKUSER: error_msg(TFTP_ES_UNKUSER); break;
        	  case TFTP_ER_NEGOTIATE: error_msg(TFTP_ES_NEGOTIATE); break;
        	  default: error_msg("DATA Check failure."); break;
        	}
        }
        if (opcode > 5) {
          len = mkpkt_err(packet, TFTP_ER_ILLEGALOP, TFTP_ES_ILLEGALOP);
          ret = write_server(sd, packet, len, &from);
        }
        continue;
        }
        if (!server.sin_port) server.sin_port = from.sin_port;
        break;
      }
    }
    if (retry == TFTP_RETRIES) {
      error_msg("Retry limit exceeded.");
      unlink(TT.local_file);
      goto errout_with_sd;
    }
    ndatabytes = nbytesrecvd - TFTP_DATAHEADERSIZE;
    if (writeall(fd, packet + TFTP_DATAHEADERSIZE, ndatabytes) < 0){
      unlink(TT.local_file);
      goto errout_with_sd;
    }
    len = mkpkt_ack(packet, blockno);
    ret = write_server(sd, packet, len, &server);
    if (ret != len){
      unlink(TT.local_file);
      goto errout_with_sd;
    }
  } while (ndatabytes >= TFTP_DATASIZE);

  result = 0;

errout_with_sd: close(sd);
errout_with_fd: close(fd);
  free(packet);
  return result;
}

/*
 * Sends file to server.
 */
int file_put(void)
{
  struct in_addr inaddr;
  struct sockaddr_in server;
  uint8_t *packet;
  off_t offset = 0;
  uint16_t blockno = 1, rblockno, port = 0;
  int packetlen, sd, fd, retry = 0, ret, result = -1;

  packet = (uint8_t*)xzalloc(TFTP_IOBUFSIZE);
  fd = xopen(TT.local_file, O_RDONLY);
  inet_aton(toys.optargs[0], &inaddr);
  sd = init_tftp(&server, inaddr.s_addr);
  if (sd < 0) goto errout_with_fd;

  for (;;) {  //first loop for request send and confirmation from server.
    packetlen = mkpkt_request(packet, TFTP_OP_WRQ, TT.remote_file, 1);
    ret = write_server(sd, packet, packetlen, &server);
    if (ret != packetlen) goto errout_with_sd;
    if (read_ack(sd, packet, &server, &port, NULL) == 0) break;
    if (++retry > TFTP_RETRIES) {
      error_msg("Retry count exceeded.");
      goto errout_with_sd;
    }
  }
  for (;;) {  // loop for data sending and receving ack from server.
    packetlen = mkpkt_data(fd, offset, packet, blockno);
    if (packetlen < 0) goto errout_with_sd;

    ret = write_server(sd, packet, packetlen, &server);
    if (ret != packetlen) goto errout_with_sd;

    if (read_ack(sd, packet, &server, &port, &rblockno) == 0) {
      if (rblockno == blockno) {
        if (packetlen < TFTP_PACKETSIZE) break;
        blockno++;
        offset += TFTP_DATASIZE;
        retry = 0;
        continue;
      }
    }
    if (++retry > TFTP_RETRIES) {
      error_msg("Retry count exceeded.");
      goto errout_with_sd;
    }
  }
  result = 0;

errout_with_sd: close(sd);
errout_with_fd: close(fd);
  free(packet);
  return result;
}

void tftp_main(void)
{
  // TODO: remove these two chks when base arg parsing is fixed.
  if((flagChk(FLAG_p)) && (flagChk(FLAG_g))) error_exit("Can't do GET and PUT at the same time.");
  if((!flagChk(FLAG_p)) && (!flagChk(FLAG_g))) error_exit("provide an action GET or PUT.");

  if(flagChk(FLAG_r)){
	  if(!(flagChk(FLAG_l))){
		  char *slash = strrchr(TT.remote_file, '/');
		  TT.local_file = (slash) ? slash + 1 : TT.remote_file;
	  }
  }else if (flagChk(FLAG_l)) TT.remote_file = TT.local_file;
  else error_exit("Please provide some files.");

  if(flagChk(FLAG_g)) file_get();
  if(flagChk(FLAG_p)) file_put();
}
