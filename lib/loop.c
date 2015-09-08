/* loop.c - loop device helper functions.
 *
 * Copyright 2012 Ashwini Kumar <ak.ashwini@gmail.com>
 */
 
#include "toys.h"
#include <linux/loop.h>

#define NUM_OF_LOOP_DEVS  1048576 // 2^20 = 20 is the number of bits for minor number 

/*get a free loopdev */
int get_loopdevice(char *loopdev)
{       
  char *tmploop;
  int n = 0, fd;
  struct loop_info info;

  for(n = 0; n < NUM_OF_LOOP_DEVS; n++) { //loop thru all loop devices, untill free device found
    tmploop = xmsprintf("/dev/loop%d",n);
    fd = open(tmploop, O_RDONLY);
    if(fd < 0) {
      free(tmploop);
      if(errno == ENOENT) return 0;
      else perror_exit("/dev/loop<N>");                                                                   
    }
    else {   
      if(ioctl(fd, LOOP_GET_STATUS, &info) && (errno == ENXIO)) {
        /* ENXIO is returned if the loop device is not bind */
        strncpy(loopdev, tmploop, LO_NAME_SIZE);
        loopdev[LO_NAME_SIZE - 1] = '\0';
        xclose(fd);
        free(tmploop);
        return 1;
      }
    }   
    xclose(fd);
    free(tmploop);
  }

  return 0;
}

/*
 * Set the loop device on a given file/device at the supplied offset
 */
int set_loopdevice(char *loopdev, char *file, off_t offset)
{
  int file_fd, device_fd;
  struct loop_info info;

  file_fd = open(file, O_RDWR);
  if (file_fd < 0) {
    file_fd = open(file, O_RDONLY);
    if(file_fd < 0 ) {
      perror_msg("%s: open backing file failed",file);
      return 1;
    }
  }
  device_fd = open(loopdev, O_RDWR);
  if (device_fd < 0) {
    device_fd = open(loopdev, O_RDONLY);
    if(device_fd < 0) {
      perror_msg("open loop device failed");
      xclose(file_fd);
      return 1;
    }
  }
  if(ioctl(device_fd, LOOP_GET_STATUS, &info) && errno == ENXIO) { //device is free;
    if (ioctl(device_fd, LOOP_SET_FD, file_fd) ==0) {
      memset(&info, 0, sizeof(info));
      strncpy(info.lo_name, file, LO_NAME_SIZE);
      info.lo_name[LO_NAME_SIZE - 1] = '\0';
      info.lo_offset = offset;
      if(ioctl(device_fd, LOOP_SET_STATUS, &info)) goto LOOP_ERROR;
    }
    else {
LOOP_ERROR:
      perror_msg("ioctl LOOP_SET_FD failed");
      xclose(file_fd);
      xclose(device_fd);
      return 1;
    }
  } else if (strcmp(file, (char *)info.lo_name) != 0
        || offset != info.lo_offset
      ) {
    xclose(file_fd);
    xclose(device_fd);
    perror_msg("%s: device is busy", loopdev);
    return 1;
  }

  xclose(file_fd);
  xclose(device_fd);
  return 0;
}

/*
 * delete the setup loop device
 */
int delete_loopdevice(char *loopdev)
{
  int loop_fd = open(loopdev, O_RDONLY);
  if (loop_fd < 0) {
    perror_msg("%s: open loop device failed", loopdev);
    return 1;
  }  
  if (ioctl(loop_fd, LOOP_CLR_FD, 0) < 0) {
    perror_msg("%s: ioctl LOOP_CLR_FD failed", loopdev);
    xclose(loop_fd);
    return 1;
  }

  xclose(loop_fd);
  return 0;
}
