/* iostat.c - Output CPU and I/O Statistics.
 *
 * Copyright 2013 Sandeep Sharma <sandeep.jack2756@gmail.com>
 *

USE_IOSTAT(NEWTOY(iostat, "cdtzkm[!km]", TOYFLAG_USR|TOYFLAG_BIN))

config IOSTAT
  bool "iostat"
  default y
  help
  Usage: iostat [-c] [-d] [-t] [-z] [-k|-m] [ALL|BLOCKDEV...] [INTERVAL [COUNT]]
    
  Report CPU and I/O statistics
    -c  Show CPU utilization
    -d  Show device utilization
    -t  Print current time
    -z  Omit devices with no activity
    -k  Use kb/s
    -m  Use Mb/s
*/

#define FOR_iostat
#include "toys.h"


GLOBALS(
    int all_flag;
    struct double_list *dev_list;
    struct double_list *disk_data_list;
    int interval;
    int count;

    int nr_cpu;
    char *str;
    unsigned div;
    struct tm *time;
    int curr;
)
typedef struct disk_dev_data {
  unsigned long  read_ops;  //1 -# of reads completed 
  unsigned long  read_merged; //2 -# of reads merged
  unsigned long long read_sec; // 3 -# of sectors read
                              //#4 - of milliseconds spent reading (we will surpass it)
  unsigned long  write_ops;  //5 - # of writes completed
  unsigned long write_merged; //6- # of writes merged
  unsigned long long write_sec; //7- # of sectors written
} dev_data_t;

typedef struct disk_dev {
  char *dev_name;    //Enough space for device name, naive
  dev_data_t curr_data;
  dev_data_t prev_data;
}dev_tt;

typedef unsigned long cputime_t; 
struct stat_list {
  cputime_t cputime[10];       //user,nice,system,idle,iowait,irq,softirq,steal,guest.
  cputime_t total_time;       //Total UP time for CPU.
  cputime_t uptime;       //Total UP time for CPU.
};
struct stat_list *stat_array[2]; //current statistics, previous staistics

/*
 * omit non-white spaces.
 */
static char *omitnon_whitespace(const char *s)
{                                                                                                                                                                                                    
  while (*s != '\0' && *s != ' ' && (unsigned char)(*s - 9) > (13 - 9)) s++;
  return (char *) s;
}
static int strtol_range(char *str, int min, int max)
{                     
  char *endptr = NULL;
  errno = 0;          
  long ret_value = strtol(str, &endptr, 10);

  if(errno) perror_exit("Invalid num %s", str);
  else if(endptr && (*endptr != '\0' || endptr == str))
    perror_exit("Not a valid num %s", str);
  if(ret_value >= min && ret_value <= max) return ret_value;
  else perror_exit("Number %s is not in valid [%d-%d] Range\n", str, min, max);
} 
/*
 * Omit white spaces.
 */
static char *omit_whitespace(const char *s)
{   
  while(*s == ' ' || (unsigned char)(*s - 9) <= (13 - 9)) s++;
  return (char *) s;
}
/*
 * get the path with no strip
 */
static char *get_last_path_component_withnostrip(const char *path) {                                                                 
  char *slash = strrchr(path, '/');

  if (!slash || (slash == path && !slash[1]))
    return (char*)path;

  return slash + 1;
}
/*
 * calculate the CPU SMP uptime.
 */
static cputime_t uptime(void)
{
  int fd;
  cputime_t uptime1,uptime2;
  char *buf = NULL;
  fd = xopen("/proc/uptime", O_RDONLY);
  buf = get_line(fd);
  if(!buf) perror_exit("/proc/uptime has no data"); // is it ?
  if (sscanf(buf, "%lu.%lu", &uptime1, &uptime2) != 2) perror_exit("can't read /proc/uptime");
  xclose(fd);
  if (buf) free(buf);
  return uptime1 * sysconf(_SC_CLK_TCK) + uptime2 * sysconf(_SC_CLK_TCK) / 100;
}
/*
 * Embrace cpu stats.
 */
static void get_cpu_stat(struct stat_list **curr_stat) 
{
  int fd, i;
  char *buf = NULL, *p;
  fd = xopen("/proc/stat", O_RDONLY);
  
  while((buf = get_line(fd)) != NULL) {
    if (!(buf[0] == 'c' && buf[1] == 'p' && buf[2] == 'u' && buf[3] == ' ')) {
      free(buf);
      continue;
    }
    p = buf + 4;
    for(i = 0; i <= 9; i++) {
      p = omit_whitespace(p);
      sscanf(p, "%lu", &(*curr_stat)->cputime[i]);
      p = omitnon_whitespace(p);
    }
    free(buf);
    break;
  }
  (*curr_stat)->total_time = 0;
  for(i = 0; i < 8; i++) (*curr_stat)->total_time += (*curr_stat)->cputime[i];
  if (TT.nr_cpu > 0) (*curr_stat)->uptime = uptime();
  xclose(fd);
  return;
}
/*
 * Return true if exists else false
 * from device list.
 */
static int if_not_exists(char *dev_name)
{
  struct double_list *ptr, *temp;
  temp = ptr = TT.dev_list;
  if (temp) temp->prev->next = NULL;
  while(ptr) {
    if (!strcmp(ptr->data, dev_name)) {
      temp->prev->next = temp;
      return 1;
    }
    ptr = ptr->next;
  }
  if (temp) temp->prev->next = temp;
  return 0;
}

#define IS_OVERFLOW(pval, dval) \
  ( (pval <= 0xffffffff) && ((long long) dval < 0) ? 1 : 0)
/*
 *   Compute percent value from current and previous cpu stat records.
 *   values can overflow as its 32bit entry.
 */
static double calulate_precent(cputime_t pval, cputime_t cval , cputime_t del)
{
  uint64_t dval = cval - pval;
  if(IS_OVERFLOW(pval, dval)) dval += ((uint64_t)1 << 16) << 16;
  return ((double)dval / del * 100);
}
/*
 * print disk stats.
 */
static void print_disk_stat(char *device_name, cputime_t del)
{
  #define CUR_PTR  ((dev_tt*)(temp_list->data))->curr_data  //Avoid pain to type these
  #define PREV_PTR ((dev_tt*)(temp_list->data))->prev_data
  struct double_list *temp_list;
  temp_list = TT.disk_data_list;
  
  while(strcmp(device_name, ((dev_tt*)(temp_list->data))->dev_name) != 0 ) temp_list = temp_list->next;

    if (toys.optflags & FLAG_z) //Dont't print if no activity.
      if (PREV_PTR.read_ops== CUR_PTR.read_ops && PREV_PTR.write_ops == CUR_PTR.write_ops) return;

  printf("%-13s %8.2f %12.2f %12.2f %10llu %10llu\n",
      ((dev_tt*)(temp_list->data))->dev_name,
      calulate_precent(PREV_PTR.read_ops + PREV_PTR.write_ops, CUR_PTR.read_ops + CUR_PTR.write_ops, del),
      calulate_precent(PREV_PTR.read_sec, CUR_PTR.read_sec, del) / TT.div,
      calulate_precent(PREV_PTR.write_sec, CUR_PTR.write_sec, del) / TT.div,
      (CUR_PTR.read_sec -  PREV_PTR.read_sec) / TT.div,
      (CUR_PTR.write_sec - PREV_PTR.write_sec) / TT.div);   
}
/*
 * Return NODE address if already exists in list.
 */
static dev_tt* get_ifexists(char *device_name)
{
  struct double_list *list_ptr, *head;
  head = list_ptr = TT.disk_data_list;
  if (head) head->prev->next = NULL;
  while(list_ptr) {
    if ((strcmp(((dev_tt*)(list_ptr->data))->dev_name, device_name)) == 0) {
         head->prev->next = head;
         return (dev_tt*)(list_ptr->data);
    }
    list_ptr = list_ptr->next;
  }
  if (head) head->prev->next = head;
  return NULL;
}
/*
 * Embrace disk stats, depending upon the
 * list (if present) else defaults of 
 * disks.
 */
static void get_disk_stat(cputime_t delta)
{
  int fd;
  char *buf = NULL;
  dev_tt *cur_dev;
  dev_data_t *cur_dev_data;
  fd = xopen("/proc/diskstats", O_RDONLY);
  char device_name[15] = {0,}; //enough as we sscanf 12(MAX_DEV_NAME) only

  while((buf = get_line(fd)) != NULL) {
    sscanf(buf, "%*s %*s %12s", device_name);
    if (TT.dev_list) {
      if (!if_not_exists(device_name)) {
        free(buf);
        continue;
      }
    }

    cur_dev = get_ifexists(device_name);
    if (!cur_dev) {
      cur_dev = xzalloc(sizeof(dev_tt));
      cur_dev->dev_name = xmsprintf("%s", device_name);
      dlist_add(&TT.disk_data_list, (char*)cur_dev); //Double list with data part as dev_tt structure pointers(why we bother about implementing new list).
    }
    cur_dev_data = &cur_dev->curr_data;

    sscanf(buf, "%*s %*s %*s %lu %lu %llu %*s %lu %lu %llu", &cur_dev_data->read_ops, &cur_dev_data->read_merged,
        &cur_dev_data->read_sec,&cur_dev_data->write_ops,
        &cur_dev_data->write_merged,&cur_dev_data->write_sec);
    if (!TT.dev_list && !TT.all_flag && cur_dev_data->read_ops == 0 && cur_dev_data->write_ops == 0) {
      free(buf);
      continue;
    }
    print_disk_stat(cur_dev->dev_name, delta);
    cur_dev->prev_data = *cur_dev_data;
    free(buf);
  }
  xclose(fd);
}
/*
 * Print the Stats for CPU
 */
static void print_cpu_stat(void)
{
  struct stat_list *p, *c;
  c = stat_array[TT.curr];
  p = stat_array[TT.curr ^ 1];
  cputime_t diff = c->total_time - p->total_time;
  if (diff == 0) diff = 1;
  xputs("avg-cpu:  %user   %nice %system %iowait  %steal   %idle");
    xprintf("        %7.2f %7.2f %7.2f %7.2f %7.2f %7.2f\n",
        calulate_precent(p->cputime[0]  , c->cputime[0], diff),
        calulate_precent(p->cputime[1]  , c->cputime[1]  , diff),
        calulate_precent(p->cputime[2] + p->cputime[5] + p->cputime[6],
                  c->cputime[2] + c->cputime[5] + c->cputime[6], diff),
        calulate_precent(p->cputime[4], c->cputime[4], diff),
        calulate_precent(p->cputime[7] , c->cputime[7] , diff),
        calulate_precent(p->cputime[3]  , c->cputime[3]  , diff) );       

}
/*
 * Get the number of cpu counts.
 */
static int get_cpu_count(void)
{
  int fd, count = 0;
  char *buf = NULL;
  fd = xopen("/proc/stat", O_RDONLY);
  while((buf = get_line(fd)))
  {
    if (!strncmp("cpu", buf, 3)) {
      if (*(buf + 3) != ' ') {
        sscanf(buf + 3, "%d", &count);
      } else {
        if (buf) free(buf); 
        continue;
      }
    }
  if (buf) free(buf); 
  }
  xclose(fd);
  return (count + 1);
}
/*
 * printf current time stamp.
 */
static void curr_time_satamp(void)
{
  char tm_date[100] = {0,};
  time_t t;
  t = time(NULL);
  TT.time = localtime(&t);
  strftime(tm_date, sizeof(tm_date), "%x %X", TT.time);
  xprintf("%s\n", tm_date);
}
/*
 * Print the header line
 */
static void print_header(void)
{
  struct utsname u;
  time_t t;
  char date[100] = {0,}; //Fair enough to Hold in "%x" of strftime.
  uname(&u); //can it fails?
  t = time(NULL);
  TT.time = localtime(&t);
  strftime(date, sizeof(date), "%x", TT.time);
  printf("%s %s (%s) \t%s \t_%s_\t(%u CPU)\n\n", u.sysname, u.release, u.nodename,
      date, u.machine, TT.nr_cpu);

}
/*
 * iostat main routine.
 */
void iostat_main(void)
{
  struct stat_list stat1, stat2;
  TT.curr = 0;
  cputime_t delta = 0;
  TT.all_flag = 0;

  if (!(toys.optflags & FLAG_c) && !(toys.optflags & FLAG_d))
    toys.optflags |= (FLAG_c + FLAG_d);

  while(*toys.optargs && !isdigit(**toys.optargs)) {   //Either "ALL" or list of devices.
    if (!strcmp(*toys.optargs, "ALL")) TT.all_flag = 1;
    else {
      char *strip_path;
      strip_path = get_last_path_component_withnostrip(*toys.optargs);
      if (!if_not_exists(strip_path)) dlist_add(&TT.dev_list, strip_path);
    }
    toys.optargs++;
  }
  if (*toys.optargs) TT.interval = strtol_range(*toys.optargs, 0, INT_MAX);
  else TT.interval = 0;

  toys.optargs++;

  if (TT.interval && !*toys.optargs) TT.count  = -1; //infinite
  else if (*toys.optargs) TT.count = strtol_range(*toys.optargs, 0, INT_MAX);
  else TT.count = 1;

  TT.nr_cpu = get_cpu_count();
  if (toys.optflags & FLAG_m) {
    TT.str = "MB";
    TT.div = 2048;
  } else if (toys.optflags & FLAG_k) {
    TT.str = "kB";
    TT.div = 2;
  } else {
    TT.str = "Blk";
    TT.div = 1;
  }
  memset(&stat2, 0, sizeof(struct stat_list)); //Intially prev stat is zero.
  memset(&stat1, 0, sizeof(struct stat_list)); //Intially prev stat is zero.
  stat_array[TT.curr] = &stat1;
  stat_array[TT.curr ^ 1] = &stat2;

  print_header();
  while(1) {

    get_cpu_stat(&stat_array[TT.curr]);  //curr is updated.

    if (toys.optflags & FLAG_t) curr_time_satamp();

    if (toys.optflags & FLAG_c) {
      print_cpu_stat();
      if (toys.optflags & FLAG_d) xputc('\n');
    }
    if (toys.optflags & FLAG_d) {
      if (TT.nr_cpu > 1) delta = (stat_array[TT.curr]->uptime - stat_array[TT.curr^1]->uptime);
      else delta = (stat_array[TT.curr]->total_time - stat_array[TT.curr^1]->total_time);
      xprintf("Device:%15s%6s%s/s%6s%s/s%6s%s%6s%s\n","tps",TT.str,"_read", TT.str,"_wrtn",TT. str,"_read", TT.str,"_wrtn" );
      get_disk_stat(delta == 0 ? 1 : delta);
    }
    xputc('\n');
    if ((TT.count > 0 ) && (--TT.count == 0)) break;
    sleep(TT.interval);
    TT.curr ^= 1; 
  }
  if (CFG_TOYBOX_FREE) {
    struct double_list *temp1;
    temp1 = TT.disk_data_list;
    if(temp1) temp1->prev->next = NULL;
      while(temp1) {
        struct double_list *temp2 = temp1->next;
        free(((dev_tt*)(temp1->data))->dev_name);
        free(temp1->data);
        free(temp1);
        temp1 = temp2;
      }
  }
}
