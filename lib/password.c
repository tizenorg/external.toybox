/* pwdutils.c - password read/update helper functions.
 *
 * Copyright 2012 Ashwini Kumar <ak.ashwini@gmail.com>
 */

#include "toys.h"
#include <time.h>

#define URANDOM_PATH    "/dev/urandom"
static unsigned int random_number_generator(int fd)
{      
  unsigned int randnum;
  xreadall(fd, &randnum, sizeof(randnum));
  return randnum;
}      
       
static char inttoc(int i)
{      
  // salt value uses 64 chracters in "./0-9a-zA-Z"
  const char character_set[]="./0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  i &= 0x3f; // masking for using 10 bits only
  return character_set[i];
}      
       
int get_salt(char *salt, char *algo)
{      
  int i, salt_length = 0, offset;
  int randfd;
  if(!strcmp(algo,"des")){
    // 2 bytes salt value is used in des
    salt_length = 2;
    offset = 0;
  } else {
    *salt++ = '$';
    if(!strcmp(algo,"md5")){
      *salt++ = '1';
      // 8 bytes salt value is used in md5
      salt_length = 8;
    } else if(!strcmp(algo,"sha256")){
      *salt++ = '5';
      // 16 bytes salt value is used in sha256
      salt_length = 16;
    } else if(!strcmp(algo,"sha512")){
      *salt++ = '6';
      // 16 bytes salt value is used in sha512
      salt_length = 16;
    } else return -1;
       
    *salt++ = '$';
    offset = 3;
  }    
       
  randfd = xopen(URANDOM_PATH, O_RDONLY);
  for(i=0; i<salt_length; i++)
    salt[i] = inttoc(random_number_generator(randfd));
  salt[salt_length+1] = '\0';
  xclose(randfd);
       
  return offset;
}
void handle(int signo)
{
}

int read_password(char * buff, int buflen, char* mesg)
{
  int i = 0;
  struct termios termio, oldtermio;
  struct sigaction sa, oldsa;
  tcgetattr(0, &oldtermio);
  tcflush(0, TCIFLUSH);
  termio = oldtermio;

  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = handle;
  sigaction(SIGINT, &sa, &oldsa);

  termio.c_iflag &= ~(IUCLC|IXON|IXOFF|IXANY);
  termio.c_lflag &= ~(ECHO|ECHOE|ECHOK|ECHONL|TOSTOP);
  tcsetattr(0, TCSANOW, &termio);

  fputs(mesg, stdout);
  fflush(stdout);

  while (1) {
    int ret = read(0, &buff[i], 1);
    if ( ret < 0 ) {
      buff[0] = 0;
      sigaction(SIGINT, &oldsa, NULL);
      tcsetattr(0, TCSANOW, &oldtermio);
      xputc('\n');
      fflush(stdout);
      return 1;
    } else if (ret == 0 || buff[i] == '\n' || buff[i] == '\r' || buflen == i+1)
    {
      buff[i] = '\0';
      break;
    }
    i++;
  }
  sigaction(SIGINT, &oldsa, NULL);
  tcsetattr(0, TCSANOW, &oldtermio);
  puts("");
  fflush(stdout);
  return 0;
}

static char *get_nextcolon(const char *line, char delim)
{
  char *current_ptr = NULL;
  if((current_ptr = strchr(line, ':')) == NULL) error_exit("Invalid Entry\n");
  return current_ptr;
}

int update_password(char *filename, char* username, char* encrypted)
{
  char *filenamesfx = NULL, *namesfx = NULL;
  char *shadow = NULL, *sfx = NULL;
  FILE *exfp, *newfp;
  int ret = -1; //fail
  struct flock lock;
  char *line = NULL;

  shadow = strstr(filename, "shadow");
  filenamesfx = xmsprintf("%s+", filename);
  sfx = strchr(filenamesfx, '+');

  exfp = fopen(filename, "r+");
  if(!exfp) {
    perror_msg("Couldn't open file %s",filename);
    goto free_storage;
  }

  *sfx = '-';
  ret = unlink(filenamesfx);
  ret = link(filename, filenamesfx);
  if(ret < 0) error_msg("can't create backup file");

  *sfx = '+';
  lock.l_type = F_WRLCK;
  lock.l_whence = SEEK_SET;
  lock.l_start = 0;
  lock.l_len = 0;

  ret = fcntl(fileno(exfp), F_SETLK, &lock);
  if(ret < 0) perror_msg("Couldn't lock file %s",filename);

  lock.l_type = F_UNLCK; //unlocking at a later stage

  newfp = fopen(filenamesfx, "w+");
  if(!newfp) {
    error_msg("couldn't open file for writing");
    ret = -1;
    fclose(exfp);
    goto free_storage;
  }

  ret = 0;
  namesfx = xmsprintf("%s:",username);
  while((line = get_line(fileno(exfp))) != NULL)
  {
    if(strncmp(line, namesfx, strlen(namesfx)) != 0)
      fprintf(newfp, "%s\n", line);
    else {
      char *current_ptr = NULL;
      fprintf(newfp, "%s%s:",namesfx,encrypted);
      current_ptr = get_nextcolon(line, ':'); //past username
      current_ptr++; //past colon ':' after username
      current_ptr = get_nextcolon(current_ptr, ':'); //past passwd
      current_ptr++; //past colon ':' after passwd
      if(shadow) {
        fprintf(newfp, "%u:",(unsigned)(time(NULL))/(24*60*60));
        current_ptr = get_nextcolon(current_ptr, ':');
        current_ptr++; //past time stamp colon.
        fprintf(newfp, "%s\n",current_ptr);
      }
      else fprintf(newfp, "%s\n",current_ptr);
    }

    free(line);
  }
  free(namesfx);
  fcntl(fileno(exfp), F_SETLK, &lock);
  fclose(exfp);

  errno = 0;
  fflush(newfp);
  fsync(fileno(newfp));
  fclose(newfp);
  rename(filenamesfx, filename);
  if(errno) {
    perror_msg("File Writing/Saving failed: ");
    unlink(filenamesfx);
    ret = -1;
  }

free_storage:
  free(filenamesfx);
  return ret;
}

int add_user( char *filename, char *entry)
{
  char *filenamesfx = NULL;
  char *sfx = NULL;
  FILE *exfp, *newfp;
  int ret = -1; //fail
  struct flock lock;
  char *line = NULL;

  filenamesfx = xmsprintf("%s+", filename);
  sfx = strchr(filenamesfx, '+');

  exfp = fopen(filename, "r+");
  if(!exfp) {
    perror_msg("Couldn't open file %s",filename);
    goto free_storage;
  }

  *sfx = '-';
  ret = unlink(filenamesfx);
  ret = link(filename, filenamesfx);
  if(ret < 0) error_msg("can't create backup file");

  *sfx = '+';
  lock.l_type = F_WRLCK;
  lock.l_whence = SEEK_SET;
  lock.l_start = 0;
  lock.l_len = 0;

  ret = fcntl(fileno(exfp), F_SETLK, &lock);
  if(ret < 0) perror_msg("Couldn't lock file %s",filename);

  lock.l_type = F_UNLCK; //unlocking at a later stage

  newfp = fopen(filenamesfx, "w+");
  if(!newfp) {
    error_msg("couldn't open file for writing");
    ret = -1;
    fclose(exfp);
    goto free_storage;
  }
  while((line = get_line(fileno(exfp))) != NULL) {
    fprintf(newfp, "%s\n", line);
    free(line);
  }

  ret = 0;
  fprintf(newfp, "%s\n", entry); //adding the entry

  fcntl(fileno(exfp), F_SETLK, &lock);
  fclose(exfp);

  errno = 0;
  fflush(newfp);
  fsync(fileno(newfp));
  fclose(newfp);
  rename(filenamesfx, filename);
  if(errno) {
    perror_msg("File Writing/Saving failed: ");
    unlink(filenamesfx);
    ret = -1;
  }

free_storage:
  free(filenamesfx);
  return ret;
}

int update_group(struct group *gr, char *filename)
{
  char *filenamesfx = NULL, *namesfx = NULL;
  char *shadow = NULL, *sfx = NULL;
  FILE *exfp, *newfp;
  int ret = -1; //fail
  int found = 0;
  struct flock lock;
  char *line = NULL;

  shadow = strstr(filename, "gshadow");
  filenamesfx = xmsprintf("%s+", filename);
  sfx = strchr(filenamesfx, '+');

  exfp = fopen(filename, "r+");
  if(!exfp) {
    perror_msg("Couldn't open file %s",filename);
    goto free_storage;
  }

  *sfx = '-';
  ret = unlink(filenamesfx);
  ret = link(filename, filenamesfx);
  if(ret < 0) error_msg("can't create backup file");

  *sfx = '+';
  lock.l_type = F_WRLCK;
  lock.l_whence = SEEK_SET;
  lock.l_start = 0;
  lock.l_len = 0;

  ret = fcntl(fileno(exfp), F_SETLK, &lock);
  if(ret < 0) perror_msg("Couldn't lock file %s",filename);

  lock.l_type = F_UNLCK; //unlocking at a later stage

  newfp = fopen(filenamesfx, "w+");
  if(!newfp) {
    error_msg("couldn't open file for writing");
    ret = -1;
    fclose(exfp);
    goto free_storage;
  }

  ret = found = 0;
  namesfx = xmsprintf("%s:",gr->gr_name);
  while((line = get_line(fileno(exfp))) != NULL)
  {
    if(strncmp(line, namesfx, strlen(namesfx)) != 0)
      fprintf(newfp, "%s\n", line);
    else {
      char *current_ptr = NULL;
      int i = 0;
      found = 1;
      current_ptr = get_nextcolon(line, ':'); //past groupname
      current_ptr++; //past colon ':' after groupname
      current_ptr = get_nextcolon(current_ptr, ':'); //past passwd
      current_ptr++; //past colon ':' after passwd
      current_ptr = get_nextcolon(current_ptr, ':'); //past gid or admin list
      current_ptr++; //past colon ':' after gid
      *current_ptr = '\0';
      fprintf(newfp, "%s", line);
      while(gr->gr_mem && gr->gr_mem[i]) {
        if(i > 0) fprintf(newfp, "%c", ',');
        fprintf(newfp, "%s", gr->gr_mem[i++]);
      }
      fprintf(newfp,"\n");
    }
    free(line);
  }

  if(!found) {
    int i = 0;
    fprintf(newfp, "%s:%s:", gr->gr_name, gr->gr_passwd);
    if(shadow) fprintf(newfp, ":");
    else fprintf(newfp, "%d:", gr->gr_gid);
    while(gr->gr_mem && gr->gr_mem[i]) {
      if(i > 0) fprintf(newfp, "%c", ',');
      fprintf(newfp, "%s", gr->gr_mem[i++]);
    }
    fprintf(newfp, "\n");
  }
  free(namesfx);
  fcntl(fileno(exfp), F_SETLK, &lock);
  fclose(exfp);

  errno = 0;
  fflush(newfp);
  fsync(fileno(newfp));
  fclose(newfp);
  rename(filenamesfx, filename);
  if(errno) {
    perror_msg("File Writing/Saving failed: ");
    unlink(filenamesfx);
    ret = -1;
  }

free_storage:
  free(filenamesfx);
  return ret;
}
