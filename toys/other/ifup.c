/* ifup.c - Bring a network interface up/down.
 *
 * Copyright 2012 Ranjan Kumar <ranjankumar.bth@gmail.com>
 *
 * Not in SUSv4.
 *
USE_IFUP(NEWTOY(ifup, "mfvnai:", TOYFLAG_SBIN))
USE_IFUP(OLDTOY(ifdown,ifup, "mfvnai:", TOYFLAG_SBIN))

config IFUP
  bool "ifup/ifdown"
  default y
  help
    usage: ifup [[anmvf] [i FILE]] [IFACE...]
    usage: ifdown [[anmvf] [i FILE]] [IFACE...]

    ifup - Bring a network interface up.
    ifdown - Take a network interface down.

    -a 		De/configure all interfaces marked "auto".
    -i FILE	Use file for interface definitions.
    -n 		Print out what would happen, but don't do it. (Note: It doesn't disable mapping).
    -m 		Don't run any mapping.
    -v 		Print out what would happen before doing it.
    -f		Force De/configuration.
*/

#define FOR_ifup
#include "toys.h"
#include <net/if.h>
#include <fnmatch.h>
#include <unistd.h>

GLOBALS(
  char *interface_filename;
  int isup;
  char **g_env_var;
  const char *g_path_var;
  const char *g_shell;
)

#define AUTO 0
#define IFACE 1
#define MAPPING 2
#define NETWORK_INTERFACE_STATE_FILE "/var/run/ifstate"

//forward declaration.
struct _iface_fields;

typedef struct _mapping_fields {
  char **map_ifaces_list;
  int num_map_ifaces;
  char *script_name;
  char **mapping_list;
  int num_mapping_items;
}MAPPING_FIELDS;

typedef struct _method_list {
  char *method_name;
  int (*funptr)(struct _iface_fields *);
}METHOD_LIST;

typedef struct _variable {
  char *var_name;
  char *var_val;
}VARIABLE;

typedef struct _iface_fields {
  char *iface_name;
  char *iface_af;
  METHOD_LIST *iface_method;
  char *iface_method_name;
  VARIABLE *var;
  int num_vars;
}IFACE_FIELDS;

typedef struct _config_type {
  struct double_list *auto_interfaces;
  struct double_list *iface;
  struct double_list *mapping;
}CONFIG_TYPE;

static const char *multiple_specified_keywords[] = {
  "up",
  "down",
  "pre-up",
  "post-down",
  NULL
};

static char *get_word(char **buff);

/*
 * copy string from src to dest -> only number of bytes.
 */
static char *safe_strncpy(char *dst, const char *src, size_t size)
{
  if(!size) return dst;
  dst[--size] = '\0';
  return strncpy(dst, src, size);
}
/*
 * Remove white spaces from the given string.
 */
static char *omit_whitespace(char *s)
{
  while(*s == ' ' || (unsigned char)(*s - 9) <= (13 - 9)) s++;
  return (char *) s;
}
/*
 * Remove non white spaces from the given string.
 */
static char *omitnon_whitespace(char *s)
{
  while (*s != '\0' && *s != ' ' && (unsigned char)(*s - 9) > (13 - 9)) s++;
  return (char *) s;
}
// Find out if the last character of a string matches with the given one.
// Don't underrun the buffer if the string length is 0.
static char *find_last_char(char *str, int c)
{
  if (str && *str) {
    size_t sz = strlen(str) - 1;
    str += sz;
    if ( (unsigned char)*str == c) return (char*)str;
  }
  return NULL;
}

static char *xxstrdup(char *s)
{
  if(s == NULL) return NULL;
  return xstrdup(s);
}

/*
 * Format the name string.
 * a. small letters in capital letters
 * b. '-' in '_'
 * c. numeric values remain same.
 */
static char *format_name(const char *name)
{
  char *str = (char *)name;
  int index = 0;
  while(str[index] != '\0') {
    if(str[index] == '-') str[index] = '_';
    if(str[index] >= 'a' && str[index] <= 'z')
      str[index] -= ('a' - 'A');
    if(isalnum(str[index]) || str[index] == '_'); //do nothing.
    index++;
  }
  return str;
}

/*
 * Prepare environment variables for a command.
 */
static void set_env_var(IFACE_FIELDS *ifd, const char *mode)
{
  int index = 0, i = 0;

  //free environment variables(if any) and reallocate the variables.
  if(TT.g_env_var != NULL) {
    while(TT.g_env_var[index]) {
      free(TT.g_env_var[index]);
      TT.g_env_var[index] = NULL;
      index++;
    }
    free(TT.g_env_var);
    TT.g_env_var = NULL;
  }
  //alloc memory to hold the environment variables.
  TT.g_env_var = xzalloc((ifd->num_vars + 6) * sizeof(char *)); // 6 -> 5 vars(IFACE + ADDRFAM + METHOD + MODE + PATH) and a NULL pointer.
  index = 0;
  while(i < ifd->num_vars) {
    char *str;
    if( (!strcmp(ifd->var[i].var_name, "up"))
        ||(!strcmp(ifd->var[i].var_name, "down"))
        ||(!strcmp(ifd->var[i].var_name, "pre-up"))
        ||(!strcmp(ifd->var[i].var_name, "post-down")) ) {
      i++;
      continue;
    }
    str = xstrdup(ifd->var[i].var_name);
    TT.g_env_var[index++] = xmsprintf("IF_%s=%s", format_name(str), ifd->var[i].var_val);
    free(str);
    str = NULL;
    i++;
  }
  TT.g_env_var[index++] = xmsprintf("%s=%s", "IFACE", ifd->iface_name);
  TT.g_env_var[index++] = xmsprintf("%s=%s", "ADDRFAM", ifd->iface_af);
  TT.g_env_var[index++] = xmsprintf("%s=%s", "METHOD", ifd->iface_method_name);
  TT.g_env_var[index++] = xmsprintf("%s=%s", "MODE", mode);

  if(TT.g_path_var)
    TT.g_env_var[index++] = xmsprintf("%s=%s", "PATH", TT.g_path_var);

  return;
}

/*
 * Execute the given command and return the status.
 */
static int execute_action(const char *cmd)
{ 
  pid_t pid, w_pid;
  int status;
  if((toys.optflags & FLAG_v) || (toys.optflags & FLAG_n))
    xprintf("%s\n", cmd);
  if(toys.optflags & FLAG_n) return 1;

  fflush(NULL);
  switch(pid = vfork()) {
    case -1:// error
      return 0;
    case 0: { //child
              int exit_val;
              execle(TT.g_shell, TT.g_shell, "-c", cmd, (char *) NULL, TT.g_env_var);
              exit_val = (errno == ENOENT) ? 127 : 126;
              _exit(exit_val);
            }
            break;
    default:
            break;
  }
  //wait for child process termination and get the status of the child process.
  do {
    w_pid = waitpid(pid, &status, 0);
  }while((w_pid == -1) && (errno == EINTR));

  if((!WIFEXITED(status)) || (WEXITSTATUS(status) != 0))
    return 0;
  return 1;
}

/*
 * if "up/down/pre-up/post-down" option is in the iface list then execute it.
 * otherwise run the script "/etc/network/if-up.d" (an e.g.).
 */
static int execute_option(IFACE_FIELDS *ifd, const char *option)
{
  int i = 0;
  char *cmd;
  int cmd_status = 0;

  while(i < ifd->num_vars) {
    if(!strcmp(ifd->var[i].var_name, option)) {
      cmd_status = execute_action(ifd->var[i].var_val);
      if(!cmd_status) return 0;
    }
    i++;
  }
  cmd = xmsprintf("run-parts /etc/network/if-%s.d", option);
  cmd_status = execute_action(cmd);
  free(cmd);
  return cmd_status;
}

/*
 * Add the upcoming string in the command string.
 */
static void add_str_to_cmd(char **cmd, const char *str, size_t size)
{
  char *ptr = *cmd;
  int len = (ptr ? strlen(ptr) : 0);
  size++;
  ptr = xrealloc(ptr, len + size);
  //copy the string to cmd.
  safe_strncpy(ptr + len, str, size);
  *cmd = ptr;
}

/*
 * Get the value of the variable from the list (if there).
 * otherwise return NULL.
 */
static char *get_varval_from_list(IFACE_FIELDS *ifd, const char *var_name, int var_len)
{
  int i = 0;
  if( (!strncmp(var_name, "iface", var_len))
      || (!strncmp(var_name, "label", var_len)) )
    return ifd->iface_name;

  while(i < ifd->num_vars) {
    if(!strncmp(var_name, ifd->var[i].var_name, var_len))
      return ifd->var[i].var_val;
    i++;
  }
  return NULL;
}

/*
 * Count the number of set bits in the netmask.
 */
static int count_bits(const char *bnmask)
{
  unsigned int num_of_set_bits = 0;
  struct in_addr addr;

  if(!inet_aton(bnmask, &addr)) return -1; //IP format is not correct.
  while(addr.s_addr) {
    if((addr.s_addr & 1) == 1) num_of_set_bits++;
    addr.s_addr >>= 1;
  }
  return num_of_set_bits;
}

/*
 * Prepare command with the help of row_cmd.
 */
static char *prepare_command(const char *row_cmd, IFACE_FIELDS *ifd)
{
#define ERROR_PERCENT 10001
#define ERROR_BRACES  10002
#define ERROR_VARVAL  10003
  char *cmd = NULL;
  char *wordptr = NULL, *word = NULL, *line;
  int num_of_open_braces = 0;
  int cur_cmd_len = 0;

  line = wordptr = xstrdup((char *)row_cmd);
  word = get_word(&wordptr);

  while(word) {
    if(word[0] == '%') {
      char *var_val, *next_percent;
      int word_len = 0;

      word++;
      word_len = strlen(word);

      //e.g. "%broadcast%]"
      if(word[word_len - 1] == ']') {
        num_of_open_braces--;
        next_percent = strchr(word, '%');
        if(!next_percent) {
          errno = ERROR_PERCENT;
          goto ERROR_CONDITION;
        }
        var_val = get_varval_from_list(ifd, word, (next_percent - word));
        if(var_val) {
          //add it to the cmd
          if(strncmp(word, "hwaddress", 9) == 0)
            var_val = omit_whitespace(omitnon_whitespace(var_val));
          add_str_to_cmd(&cmd, var_val, strlen(var_val)+1);
          cmd[strlen(cmd)] = ' ';
          cur_cmd_len = 0;
          goto NEXT_WORD;
        }
        else {
          cmd[cur_cmd_len] = '\0';
          cur_cmd_len = 0;
          goto NEXT_WORD;
        }
      }
      //e.g. %address%/%bnmask%
      else {
        next_percent = strchr(word, '%');
        if(!next_percent) {
          errno = ERROR_PERCENT;
          goto ERROR_CONDITION;
        }
        var_val = get_varval_from_list(ifd, word, (next_percent - word));
        if(var_val) {
          add_str_to_cmd(&cmd, var_val, strlen(var_val)+1);
          cmd[strlen(cmd)] = ' ';
          word = next_percent+1;
          //if NULL go to the next word or check for other conditions.
          if(word[0] == '\0') goto NEXT_WORD;
          else {
            //check for next option.
            cmd[strlen(cmd)-1] = *word;
            word++;
            continue;
          }
        }
        else if(!strncmp(word, "bnmask", 6)) {
          var_val = get_varval_from_list(ifd, "netmask", 7);
          if(var_val) {
            //number of set bits in netmask.
            unsigned int num_of_set_bits = 0;
            num_of_set_bits = count_bits(var_val);
            if(num_of_set_bits > 0) {
              const char *bnmask = utoa(num_of_set_bits);
              add_str_to_cmd(&cmd, bnmask, strlen(bnmask)+1);
              cmd[strlen(cmd)] = ' ';
              goto NEXT_WORD;
            }
          }
          else {
            errno = ERROR_VARVAL;
            goto ERROR_CONDITION;
          }
        }
        else {
          errno = ERROR_VARVAL;
          goto ERROR_CONDITION;
        }
      }//end of "e.g. %address%/%bnmask%"
    }//end of "if(word[0] == '%')"
    else if(word[0] == '[') {
      word++;
      num_of_open_braces++;
      cur_cmd_len = cmd ? strlen(cmd) : 0;
    }
    //special cases "e.g. /var/run/wvdial.%iface%"
    else {
      char *ptr = strchr(word, '%');
      if(ptr) {
        *ptr = '\0';
        char *var_val;
        char *next_percent = strchr(++ptr, '%');
        if(!next_percent) {
          errno = ERROR_PERCENT;
          goto ERROR_CONDITION;
        }
        var_val = get_varval_from_list(ifd, ptr, (next_percent - ptr));
        if(var_val) {
          char *new_word = xmsprintf("%s%s%s", word, var_val, ++next_percent);
          add_str_to_cmd(&cmd, new_word, strlen(new_word)+1);
          cmd[strlen(cmd)] = ' ';
          free(new_word);
          goto NEXT_WORD;
        }
        else {
          errno = ERROR_VARVAL;
          goto ERROR_CONDITION;
        }
      }
    }
    if(word[0] != '\0') {
      add_str_to_cmd(&cmd, word, strlen(word)+1);
      cmd[strlen(cmd)] = ' ';
    }
NEXT_WORD:
    word = get_word(&wordptr);
  }
  free(line);
  if(num_of_open_braces != 0) {
    free(cmd);
    errno = ERROR_BRACES;
    return NULL;
  }
  cmd[strlen(cmd) ? (strlen(cmd)-1) : 0] = '\0';
  return cmd;
ERROR_CONDITION:
  free(line);
  line = NULL;
  free(cmd);
  return NULL;
#undef ERROR_PERCENT
#undef ERROR_BRACES
#undef ERROR_VARVAL
}

/*
 * De/configure interface manually.
 */
static int if_manual(IFACE_FIELDS *ifd)
{
  //for ifup
  if(TT.isup) {
    set_env_var(ifd, "start");
    if(!execute_option(ifd, "pre-up")) return 0;
    if(!execute_option(ifd, "up")) return 0;
  }
  //for ifdown
  else {
    set_env_var(ifd, "stop");
    if(!execute_option(ifd, "down")) return 0;
    if(!execute_option(ifd, "post-down")) return 0;
  }
  return 1;
}

/*
 * Bring up the interface with wvdial method.
 */
static int if_wvdial_up(IFACE_FIELDS *ifd)
{
  int cmd_status = 0;
  char *cmd = prepare_command("start-stop-daemon --start -x wvdial -p /var/run/wvdial.%iface% -b -m -- [ %provider%]", ifd);
  if(!cmd) return -1;

  set_env_var(ifd, "start");
  if(!execute_option(ifd, "pre-up")) {
    free(cmd);
    return 0;
  }
  cmd_status = execute_action(cmd);
  free(cmd);
  if(cmd_status != 1) return 0;
  if(!execute_option(ifd, "up")) return 0;

  return 1;
}

/*
 * Down the interface with wvdial method.
 */
static int if_wvdial_down(IFACE_FIELDS *ifd)
{
  int cmd_status = 0;
  char *cmd = prepare_command("start-stop-daemon --stop -x wvdial -p /var/run/wvdial.%iface% -s 2", ifd);
  if(!cmd) return -1;

  set_env_var(ifd, "stop");
  cmd_status = execute_action(cmd);
  free(cmd);
  if(cmd_status != 1) return 0;
  if(!execute_option(ifd, "post-down")) return 0;

  return 1;
}

/*
 * De/configure interface with wvdial method.
 */
static int if_wvdial(IFACE_FIELDS *ifd)
{
  if(TT.isup) return(if_wvdial_up(ifd));
  else return(if_wvdial_down(ifd));
}

/*
 * Bring up the interface with ppp method.
 */
static int if_ppp_up(IFACE_FIELDS *ifd)
{
  int cmd_status = 0;
  char *cmd = prepare_command("pon [ %provider%]", ifd);
  if(!cmd) return -1;

  set_env_var(ifd, "start");
  if(!execute_option(ifd, "pre-up")) {
    free(cmd);
    return 0;
  }
  cmd_status = execute_action(cmd);
  free(cmd);
  if(cmd_status != 1) return 0;
  if(!execute_option(ifd, "up")) return 0;

  return 1;
}

/*
 * Down the interface with ppp method.
 */
static int if_ppp_down(IFACE_FIELDS *ifd)
{
  int cmd_status = 0;
  char *cmd = prepare_command("poff [ %provider%]", ifd);
  if(!cmd) return -1;

  set_env_var(ifd, "stop");
  cmd_status = execute_action(cmd);
  free(cmd);
  if(cmd_status != 1) return 0;
  if(!execute_option(ifd, "post-down")) return 0;

  return 1;
}

/*
 * De/configure interface with ppp method.
 */
static int if_ppp(IFACE_FIELDS *ifd)
{
  if(TT.isup) return(if_ppp_up(ifd));
  else return(if_ppp_down(ifd));
}

/*
 * Bring up the interface with static method.
 */
static int if_static_up(IFACE_FIELDS *ifd)
{
  int cmd_status = 0;
  char *cmd[3] = {NULL, };
  int index = 0;

  cmd[index++] = prepare_command("ip addr add %address%/%bnmask% [broadcast %broadcast%] "
      "dev %iface% [peer %pointopoint%] [label %label%]", ifd);
  cmd[index++] = prepare_command("ip link set [mtu %mtu%] [addr %hwaddress%] %iface% up", ifd);
  cmd[index++] = prepare_command("[ip route add default via %gateway% dev %iface%]", ifd);

  for(index = 0; index < 3; index++)
    if(!cmd[index]) return -1;

  set_env_var(ifd, "start");
  if(!execute_option(ifd, "pre-up")) {
    for(index = 0; index < 3; index++)
      free(cmd[index]);
    return 0;
  }

  for(index = 0; index < 3; index++) {
    int status = 0;
    status = execute_action(cmd[index]);
    if(status != 1) status = 0;
    cmd_status += status;
    free(cmd[index]);
  }

  if(cmd_status != 3) return 0;
  if(!execute_option(ifd, "up")) return 0;

  return 1;
}

/*
 * Down the interface with static method.
 */
static int if_static_down(IFACE_FIELDS *ifd)
{
  int cmd_status = 0;
  char *cmd[2] = {NULL, };
  int index = 0;

  cmd[index++] = prepare_command("ip addr flush dev %iface%", ifd);
  cmd[index++] = prepare_command("ip link set %iface% down", ifd);

  for(index = 0; index < 2; index++)
    if(!cmd[index]) return -1;

  set_env_var(ifd, "stop");
  if(!execute_option(ifd, "down")) {
    for(index = 0; index < 2; index++)
      free(cmd[index]);
    return 0;
  }

  for(index = 0; index < 2; index++) {
    int status = 0;
    status = execute_action(cmd[index]);
    if(status != 1) status = 0;
    cmd_status += status;
    free(cmd[index]);
  }

  if(cmd_status != 2) return 0;
  if(!execute_option(ifd, "post-down")) return 0;

  return 1;
}

/*
 * De/configure interface with static method.
 */
static int if_static(IFACE_FIELDS *ifd)
{
  if(TT.isup) return(if_static_up(ifd));
  else return(if_static_down(ifd));
}

/*
 * Bring up the interface with bootp method.
 */
static int if_bootp_up(IFACE_FIELDS *ifd)
{
  int cmd_status = 0;
  char *cmd = prepare_command("bootpc [--bootfile %bootfile%] --dev %iface%"
      " [--server %server%] [--hwaddr %hwaddr%]"
      " --returniffail --serverbcast", ifd);
  if(!cmd) return -1;

  set_env_var(ifd, "start");
  if(!execute_option(ifd, "pre-up")) {
    free(cmd);
    return 0;
  }

  cmd_status = execute_action(cmd);
  free(cmd);

  if(cmd_status != 1) return 0;
  if(!execute_option(ifd, "up")) return 0;

  return 1;
}

/*
 * De/configure interface with bootp method.
 */
static int if_bootp(IFACE_FIELDS *ifd)
{
  if(TT.isup) return(if_bootp_up(ifd));
  else return(if_static_down(ifd));
}

/*
 * Bring up the interface with dhcp method.
 */
static int if_dhcp_up(IFACE_FIELDS *ifd)
{
  int cmd_status = 0;
  char *cmd[2] = {NULL, };
  int index = 0;

  cmd[index++] = prepare_command("ip link set [addr %hwaddress%] %iface% up", ifd);
  cmd[index++] = prepare_command("udhcpc -R -n -p /var/run/udhcpc.%iface%.pid "
      "-i %iface% [-H %hostname%] [-c %client%] [-s %script%] [ %udhcpc_opts%]", ifd);

  for(index = 0; index < 2; index++)
    if(!cmd[index]) return -1;

  set_env_var(ifd, "start");
  if(!execute_option(ifd, "pre-up")) {
    for(index = 0; index < 2; index++)
      free(cmd[index]);
    return 0;
  }

  for(index = 0; index < 2; index++) {
    cmd_status = execute_action(cmd[index]);
    if(cmd_status != 1) {
      free(cmd[index]);
      return 0;
    }
    free(cmd[index]);
  }

  if(!execute_option(ifd, "up")) return 0;
  return 1;
}

/*
 * Down the interface with dhcp method.
 */
static int if_dhcp_down(IFACE_FIELDS *ifd)
{
  int cmd_status = 0;
  char *cmd[3] = {NULL, };
  int index = 0;

  cmd[index++] = prepare_command("test -f /var/run/udhcpc.%iface%.pid && "
      "kill `cat /var/run/udhcpc.%iface%.pid` 2>/dev/null", ifd);
  cmd[index++] = prepare_command("ip addr flush dev %iface%", ifd);
  cmd[index++] = prepare_command("ip link set %iface% down", ifd);

  for(index = 0; index < 3; index++)
    if(!cmd[index]) return -1;

  set_env_var(ifd, "stop");
  if(!execute_option(ifd, "down")) {
    for(index = 0; index < 3; index++)
      free(cmd[index]);
    return 0;
  }

  for(index = 0; index < 3; index++) {
    int status = 0;
    status = execute_action(cmd[index]);
    usleep(100000);
    if(status != 1) status = 0;
    cmd_status += status;
    free(cmd[index]);
  }

  if(cmd_status != 3) return 0;
  if(!execute_option(ifd, "post-down")) return 0;

  return 1;
}

/*
 * De/configure interface with dhcp method.
 */
static int if_dhcp(IFACE_FIELDS *ifd)
{
  if(TT.isup) return(if_dhcp_up(ifd));
  else return(if_dhcp_down(ifd));
}

/*
 * Bring up the interface with loopback method.
 */
static int if_loopback_up(IFACE_FIELDS *ifd)
{
  int cmd_status = 0;
  char *cmd[2] = {NULL, };
  int index = 0;

  cmd[index++] = prepare_command("ip addr add 127.0.0.1/8 dev %iface%", ifd);
  cmd[index++] = prepare_command("ip link set %iface% up", ifd);

  for(index = 0; index < 2; index++)
    if(!cmd[index]) return -1;

  set_env_var(ifd, "start");
  if(!execute_option(ifd, "pre-up")) {
    for(index = 0; index < 2; index++)
      free(cmd[index]);
    return 0;
  }

  for(index = 0; index < 2; index++) {
    int status = 0;
    status = execute_action(cmd[index]);
    if(status != 1) status = 0;
    cmd_status += status;
    free(cmd[index]);
  }

  if(cmd_status != 2) return 0;
  if(!execute_option(ifd, "up")) return 0;

  return 1;
}

/*
 * Down the interface with loopback method.
 */
static int if_loopback_down(IFACE_FIELDS *ifd)
{
  int cmd_status = 0;
  char *cmd[2] = {NULL, };
  int index = 0;

  cmd[index++] = prepare_command("ip addr flush dev %iface%", ifd);
  cmd[index++] = prepare_command("ip link set %iface% down", ifd);

  for(index = 0; index < 2; index++)
    if(!cmd[index]) return -1;

  set_env_var(ifd, "stop");
  if(!execute_option(ifd, "down")) {
    for(index = 0; index < 2; index++)
      free(cmd[index]);
    return 0;
  }

  for(index = 0; index < 2; index++) {
    int status = 0;
    status = execute_action(cmd[index]);
    if(status != 1) status = 0;
    cmd_status += status;
    free(cmd[index]);
  }

  if(cmd_status != 2) return 0;
  if(!execute_option(ifd, "post-down")) return 0;

  return 1;
}

/*
 * De/configure interface with loopback method.
 */
static int if_loopback(IFACE_FIELDS *ifd)
{
  if(TT.isup) return(if_loopback_up(ifd));
  else return(if_loopback_down(ifd));
}

static const METHOD_LIST methods[] = {
  { "manual",   if_manual, },
  { "wvdial",   if_wvdial, },
  { "ppp",      if_ppp, },
  { "static",   if_static, },
  { "bootp",    if_bootp, },
  { "dhcp",     if_dhcp, },
  { "loopback", if_loopback, },
  { NULL, NULL, },
};

/*
 * Bring up the interface with v4tunnel method.
 */
static int if6_v4tunnel_up(IFACE_FIELDS *ifd)
{
  int cmd_status = 0;
  char *cmd[4] = {NULL, };
  int index = 0;

  cmd[index++] = prepare_command("ip tunnel add %iface% mode sit remote "
      "%endpoint% [local %local%] [ttl %ttl%]", ifd);
  cmd[index++] = prepare_command("ip link set %iface% up", ifd);
  cmd[index++] = prepare_command("ip addr add %address%/%netmask% dev %iface%", ifd);
  cmd[index++] = prepare_command("[ip route add ::/0 via %gateway%]", ifd);

  for(index = 0; index < 4; index++)
    if(!cmd[index]) return -1;

  set_env_var(ifd, "start");
  if(!execute_option(ifd, "pre-up")) {
    for(index = 0; index < 4; index++)
      free(cmd[index]);
    return 0;
  }
  for(index = 0; index < 4; index++) {
    int status = 1;
    if(cmd[index][0])
      status = execute_action(cmd[index]);
    if(status != 1) status = 0;
    cmd_status += status;
    free(cmd[index]);
  }

  if(cmd_status != 4) return 0;
  if(!execute_option(ifd, "up")) return 0;

  return 1;
}

/*
 * Down the interface with v4tunnel method.
 */
static int if6_v4tunnel_down(IFACE_FIELDS *ifd)
{
  int cmd_status = 0;
  char *cmd = prepare_command("ip tunnel del %iface%", ifd);
  if(!cmd) return -1;

  set_env_var(ifd, "stop");
  cmd_status = execute_action(cmd);
  free(cmd);
  if(cmd_status != 1) return 0;
  if(!execute_option(ifd, "post-down")) return 0;

  return 1;
}

/*
 * De/configure interface with v4tunnel method.
 */
static int if6_v4tunnel(IFACE_FIELDS *ifd)
{
  if(TT.isup) return(if6_v4tunnel_up(ifd));
  else return(if6_v4tunnel_down(ifd));
}

/*
 * Bring up the interface with static method.
 */
static int if6_static_up(IFACE_FIELDS *ifd)
{
  int cmd_status = 0;
  char *cmd[3] = {NULL, };
  int index = 0;

  cmd[index++] = prepare_command("ip addr add %address%/%netmask% dev %iface% [label %label%]", ifd);
  cmd[index++] = prepare_command("ip link set [mtu %mtu%] [addr %hwaddress%] %iface% up", ifd);
  cmd[index++] = prepare_command("[ip route add ::/0 via %gateway%]", ifd);

  for(index = 0; index < 3; index++)
    if(!cmd[index]) return -1;

  set_env_var(ifd, "start");
  if(!execute_option(ifd, "pre-up")) {
    for(index = 0; index < 3; index++)
      free(cmd[index]);
    return 0;
  }

  for(index = 0; index < 3; index++) {
    int status = 1;
    if(cmd[index][0])
      status = execute_action(cmd[index]);
    if(status != 1) status = 0;
    cmd_status += status;
    free(cmd[index]);
  }

  if(cmd_status != 3) return 0;
  if(!execute_option(ifd, "up")) return 0;

  return 1;
}

/*
 * Down the interface with static method.
 */
static int if6_static_down(IFACE_FIELDS *ifd)
{
  int cmd_status = 0;
  char *cmd = prepare_command("ip link set %iface% down", ifd);
  if(!cmd) return -1;

  set_env_var(ifd, "stop");
  cmd_status = execute_action(cmd);
  free(cmd);

  if(cmd_status != 1) return 0;
  if(!execute_option(ifd, "post-down")) return 0;

  return 1;
}

/*
 * De/configure interface with static method.
 */
static int if6_static(IFACE_FIELDS *ifd)
{
  if(TT.isup) return(if6_static_up(ifd));
  else return(if6_static_down(ifd));
}

/*
 * De/configure interface manually.
 */
static int if6_manual(IFACE_FIELDS *ifd)
{
  //for ifup
  if(TT.isup) {
    set_env_var(ifd, "start");
    if(!execute_option(ifd, "pre-up")) return 0;
    if(!execute_option(ifd, "up")) return 0;
  }
  //for ifdown
  else {
    set_env_var(ifd, "stop");
    if(!execute_option(ifd, "down")) return 0;
    if(!execute_option(ifd, "post-down")) return 0;
  }
  return 1;
}

/*
 * Bring up the interface with loopback method.
 */
static int if6_loopback_up(IFACE_FIELDS *ifd)
{
  int cmd_status = 0;
  char *cmd[2] = {NULL, };
  int index = 0;

  cmd[index++] = prepare_command("ip addr add ::1 dev %iface%", ifd);
  cmd[index++] = prepare_command("ip link set %iface% up", ifd);

  for(index = 0; index < 2; index++)
    if(!cmd[index]) return -1;

  set_env_var(ifd, "start");
  if(!execute_option(ifd, "pre-up")) {
    for(index = 0; index < 2; index++)
      free(cmd[index]);
    return 0;
  }

  for(index = 0; index < 2; index++) {
    int status = 0;
    status = execute_action(cmd[index]);
    if(status != 1) status = 0;
    cmd_status += status;
    free(cmd[index]);
  }

  if(cmd_status != 2) return 0;
  if(!execute_option(ifd, "up")) return 0;

  return 1;
}

/*
 * Down the interface with loopback method.
 */
static int if6_loopback_down(IFACE_FIELDS *ifd)
{
  int cmd_status = 0;
  char *cmd = prepare_command("ip link set %iface% down", ifd);
  if(!cmd) return -1;

  set_env_var(ifd, "stop");
  cmd_status = execute_action(cmd);
  free(cmd);

  if(cmd_status != 1) return 0;
  if(!execute_option(ifd, "post-down")) return 0;

  return 1;
}

/*
 * De/configure interface with loopback method.
 */
static int if6_loopback(IFACE_FIELDS *ifd)
{
  if(TT.isup) return(if6_loopback_up(ifd));
  else return(if6_loopback_down(ifd));
}

static const METHOD_LIST methods6[] = {
  { "v4tunnel", if6_v4tunnel, },
  { "static",   if6_static, },
  { "manual",   if6_manual, },
  { "loopback", if6_loopback, },
  { NULL, NULL, },
};

/*
 * display help info and exit from application.
 */
static void show_ifup_help(void)
{
  toys.exithelp++;
  error_exit("Invalid Argument");
}

/*
 * Find duplicate string from the list.
 * if found, return the node address
 * else return NULL.
 */
struct double_list *llist_is_duplicate_str(struct double_list *list, const char *str)
{
  struct double_list *old = list;
  while(list) {
    if(strcmp(list->data, str) == 0) return list;

    list = list->next;
    //End of list.
    if(old == list) break;
  }
  return NULL;
}

/*
 * Validate the list to find out any duplication of iface name and address family.
 * if found, Prompt error message and exit the application.
 * if not, continue adding items in the list.
 */
void validate_list(CONFIG_TYPE *conf, IFACE_FIELDS *cur_iface)
{
  struct double_list *list = conf->iface;
  struct double_list *old = list;
  while(list) {
    IFACE_FIELDS *list_iface = (IFACE_FIELDS *)list->data;
    if( (!strcmp(list_iface->iface_name, cur_iface->iface_name))
        && (!strcmp(list_iface->iface_af, cur_iface->iface_af)) )
      perror_exit("Duplicate interface '%s'", list_iface->iface_name);

    list = list->next;
    //End of list.
    if(old == list) break;
  }
  return;
}

/*
 * Validate the list to find out any duplication of keyword like address/netmask/gateway etc.
 * (except up/down/pre-up/post-down keywords).
 */
static void validate_dup_var(IFACE_FIELDS *cur_iface, const char *cur_word)
{
  int index = 0;
  int is_mul_spec_str = 0;
  if(!cur_iface->var) return;
  while(multiple_specified_keywords[index]) {
    if(!strcmp(cur_word, multiple_specified_keywords[index])) {
      is_mul_spec_str = 1;
      break;
    }
    index++;
  }

  if(!is_mul_spec_str) {
    index = 0;
    while(index < cur_iface->num_vars) {
      if(!strcmp(cur_iface->var[index].var_name, cur_word))
        error_exit("Duplicate option is: '%s'", cur_word);
      index++;
    }
  }
  return;
}

/*
 * If the last char of any line is '\',
 * concatinate next line (if present) with the current line.
 */
static char *concat_next_line(int fd, char *line)
{
  char *ptr = NULL;
  char *next_line = NULL;
  while((next_line = get_line(fd)) != NULL) {
    if((ptr = find_last_char(next_line, '\\')) != NULL) {
      *ptr = '\0';
      ptr = xmsprintf("%s%s", line, next_line);
      free(next_line);
      free(line);
      line = ptr;
    }
    else {
      ptr = xmsprintf("%s%s", line, next_line);
      free(next_line);
      free(line);
      break;
    }
  }
  return ptr;
}

/*
 * Get next word from the given line and move buffer pointer to the next word.
 */
static char *get_word(char **buff)
{
  int wordlen = 0;
  char *bufptr = omit_whitespace(*buff);
  //if the 1st char of the line is NULL then return NULL.
  //as the line can be an empty line.
  if(*bufptr == '\0') return NULL;
  wordlen = strcspn(bufptr, " \t\n"); //the reject pinter is either space/tab/newline.
  if(bufptr[wordlen] != '\0')
    bufptr[wordlen++] = '\0'; //NULL terminated word.

  //remove any space is there before next word.
  *buff = bufptr + wordlen;
  *buff = omit_whitespace(*buff);
  return bufptr;
}

/*
 * Find out the function name and its pointer from METHOD_LIST.
 * if found, assign it to IFACE_FIELDS.
 * if not, report the error message and exit the application.
 */
static void get_method_info(IFACE_FIELDS *iface_fields, const METHOD_LIST *mlist)
{
  int match_found = 0;

  if(!iface_fields->iface_method_name)
    perror_exit("Unknown method = '%s'", iface_fields->iface_method_name);

  while(mlist->method_name) {
    if(strcmp(mlist->method_name, iface_fields->iface_method_name) == 0) {
      iface_fields->iface_method = (METHOD_LIST *)mlist;
      match_found = 1;
      break;
    }
    mlist++;
  }
  if(!match_found) perror_exit("Unknown method = '%s'", iface_fields->iface_method_name);
  return;
}

/*
 * Parse the given config file and prepare list of config types (auto/iface/mapping).
 */
static CONFIG_TYPE *read_interface_file(const char *interface_filename)
{
  CONFIG_TYPE *conf = NULL;
  IFACE_FIELDS *iface_fields = NULL;
  MAPPING_FIELDS *mapping_fields = NULL;
  int fd;
  int flag_current_interface = AUTO;
  char *line = NULL;
  char *wordptr = NULL;
  char *word = NULL;

  conf = xzalloc(sizeof(*conf));
  fd = xopen((char *)interface_filename, O_RDONLY);

  while((line = get_line(fd)) != NULL) {
    //if last char is '\' and if it is '\' then concatinate the next line.
    {
      char *ptr = find_last_char(line, '\\');
      if(ptr) {
        *ptr = '\0';
        ptr = concat_next_line(fd, line);
        if(ptr) line = ptr;
      }
    }//end of concatinate the next line.
    wordptr = line;
    word = get_word(&wordptr);
    if(word == NULL || word[0] == '#') {
      free(line);
      line = NULL;
      continue;
    }
    if(!strcmp(word, "auto")) {
      //add the list of auto interfaces in a list and through error on duplicate interfaces.
      while((word = get_word(&wordptr)) != NULL) {
        //check here for duplicate interface.
        if(llist_is_duplicate_str(conf->auto_interfaces, word))
          perror_exit("multiple declaration if interface: '%s'", line);
        dlist_add(&(conf->auto_interfaces), xstrdup(word));
      }
      flag_current_interface = AUTO;
    }//End of Auto.
    else if(!strcmp(word, "iface")) {
      char *iface_name;
      char *iface_af;
      char *iface_method_name;

      iface_fields = xzalloc(sizeof(*iface_fields));
      iface_name = get_word(&wordptr);
      iface_af = get_word(&wordptr);
      iface_method_name = get_word(&wordptr);

      //check for method name presence.
      if(iface_method_name == NULL) perror_exit("Method name is missing: '%s'", line);

      //if some more word is there in the line, it is an error.
      word = get_word(&wordptr);
      if(word != NULL) perror_exit("Too many parameters: '%s'", line);

      iface_fields->iface_name = xstrdup(iface_name);
      iface_fields->iface_af = xstrdup(iface_af);
      iface_fields->iface_method_name = xstrdup(iface_method_name);

      if(!strcmp(iface_fields->iface_af, "inet"))
        get_method_info(iface_fields, methods);
      else if(!strcmp(iface_fields->iface_af, "inet6"))
        get_method_info(iface_fields, methods6);
      else perror_exit("Unknown address family: '%s'", iface_af);

      //validate for duplicate interface name and address family.
      validate_list(conf, iface_fields);
      dlist_add(&(conf->iface), (char *)iface_fields);
      flag_current_interface = IFACE;
    }//End of Iface.
    else if(!strcmp(word, "mapping")) {
      mapping_fields = xzalloc(sizeof(*mapping_fields));

      //prepare map_ifaces_list
      mapping_fields->map_ifaces_list = (char **)xzalloc(sizeof(*mapping_fields->map_ifaces_list));
      while((word = get_word(&wordptr)) != NULL) {
        mapping_fields->map_ifaces_list[mapping_fields->num_map_ifaces++] = xstrdup(word);
      }
      dlist_add(&(conf->mapping), (char *)mapping_fields);
      flag_current_interface = MAPPING;
    }//End of Mapping.

    else {
      //use it for variables.
      if(flag_current_interface == IFACE) {
        if(!word || wordptr[0] == '\0')
          error_exit("No more option is available: '%s'", line);
        //check for multiple options except up/down/pre-up/post-down
        validate_dup_var(iface_fields, word);
        //allocate the memory for the current variable.
        iface_fields->var = xrealloc(iface_fields->var, sizeof(*iface_fields->var) * (iface_fields->num_vars + 1));
        iface_fields->var[iface_fields->num_vars].var_name = xstrdup(word);
        iface_fields->var[iface_fields->num_vars].var_val = xstrdup(wordptr);
        iface_fields->num_vars++;
      }//End of Iface fields.
      else if(flag_current_interface == MAPPING) {
        if(!strcmp(word, "script")) {
          //There should be only one script in a mapping.
          if(mapping_fields->script_name != NULL)
            error_exit("multiple script in mapping '%s'", line);

          mapping_fields->script_name = xxstrdup(get_word(&wordptr));
        }
        else if(!strcmp(word, "map")) {
          //allocate double pointer memory only one time for a node.
          if(mapping_fields->mapping_list == NULL)
            mapping_fields->mapping_list = (char **)xzalloc(sizeof(*mapping_fields->mapping_list));
          word = get_word(&wordptr);
          mapping_fields->mapping_list[mapping_fields->num_mapping_items++] = xxstrdup(word);
        }
        //if neither script nor map then it will be an error.
        else error_exit("misplaced option '%s'", line);
      }//End of mapping fields.
      else error_exit("Options are misplaced '%s'", line);
    }//End of else
    free(line);
    line = NULL;
  }
  xclose(fd);
  return conf;
}

/*
 * Get interface's current state (using the file "/var/run/ifstate")
 * and prepare the state list based on it.
 * From the state list, find out the state of the given iface.
 */
static void get_iface_state(struct double_list **state_list, struct double_list **cur_iface_state, const char *iface_name)
{
  struct double_list *start = NULL, *end = NULL;
  char *line = NULL, *word = NULL, *wordptr = NULL;
  int iface_name_len = strlen(iface_name);
  int fd = open(NETWORK_INTERFACE_STATE_FILE, O_RDONLY);
  if(fd < 0) return;

  while((line = get_line(fd)) != NULL) {
    wordptr = line;
    word = get_word(&wordptr);
    if(word) dlist_add(state_list, xstrdup(word));
    free(line);
    line = NULL;
  }
  xclose(fd);

  start = end = *state_list;
  while(start) {
    if(!strncmp(start->data, iface_name, iface_name_len)) {
      char *ptr = &(start->data[iface_name_len]);
      if(*ptr == '=') {
        *cur_iface_state = start; //the pointer to the current node.
        break;
      }
    }
    start = start->next;
    if(start == end) break;
  }
  return;
}

/*
 * Free the state list.
 */
static void free_state_list(struct double_list *state_list)
{
  struct double_list *cur_node = state_list;
  if(!cur_node) return;
  cur_node->prev->next = NULL; //break the circular list.
  while(cur_node) {
    struct double_list *tmp_node = cur_node;
    cur_node = cur_node->next;
    free(tmp_node->data);
    free(tmp_node);
  }
  return;
}

/*
 * Deallocate all memories, used for CONFIG_TYPE list.
 */
static void clean_config(CONFIG_TYPE **r_conf)
{
  CONFIG_TYPE *conf = *r_conf;
  if(!conf) return;

  //for auto
  struct double_list *cur_node = conf->auto_interfaces;
  if(cur_node) {
    cur_node->prev->next = NULL; //break the circular list.
    while(cur_node) {
      struct double_list *tmp = cur_node;
      cur_node = cur_node->next;
      free(tmp->data);
      tmp->data = NULL;
      free(tmp);
      tmp = NULL;
    }
  }

  //for iface
  cur_node = conf->iface;
  if(cur_node) {
    cur_node->prev->next = NULL; //break the circular list.
    while(cur_node) {
      struct double_list *tmp = cur_node;
      int index = 0;
      IFACE_FIELDS *iface = (IFACE_FIELDS *) tmp->data;

      free(iface->iface_af);
      free(iface->iface_method_name);
      free(iface->iface_name);
      iface->iface_af = iface->iface_method_name = iface->iface_name = NULL;

      while(index < iface->num_vars) {
        free(iface->var[index].var_name);
        free(iface->var[index].var_val);
        iface->var[index].var_name = iface->var[index].var_val = NULL;
        index++;
      }
      free(iface->var);
      iface->var = NULL;

      cur_node = cur_node->next;
      free(tmp);
    }
  }
  //for mapping.
  cur_node = conf->mapping;
  if(cur_node) {
    cur_node->prev->next = NULL; //break the circular list.
    while(cur_node) {
      struct double_list *tmp = cur_node;
      int index = 0;
      MAPPING_FIELDS *mapping = (MAPPING_FIELDS *) tmp->data;
      if(mapping->script_name) {
        free(mapping->script_name);
        mapping->script_name = NULL;
      }
      while(index < mapping->num_map_ifaces) {
        free(mapping->map_ifaces_list[index]);
        mapping->map_ifaces_list[index] = NULL;
        index++;
      }
      free(mapping->map_ifaces_list);
      mapping->map_ifaces_list = NULL;
      index = 0;
      while(index < mapping->num_mapping_items) {
        if(mapping->mapping_list[index]) {
    	  free(mapping->mapping_list[index]);
          mapping->mapping_list[index] = NULL;
        }
        index++;
      }
      free(mapping->mapping_list);
      mapping->mapping_list = NULL;

      cur_node = cur_node->next;
      free(tmp);
    }
  }
  free(conf);
  conf = NULL;
  return;
}

/*
 * Duplicate the given fd.
 */
void xdup2(int oldfd, int newfd)
{
  if(oldfd == newfd) return;
  if(dup2(oldfd, newfd) != newfd)
    perror_exit("can't duplicate file descriptor");
  close(oldfd);
  return;
}

/*
 * Execute the current mapping.
 */
static char *execute_mapping(MAPPING_FIELDS *mapping_fields, char *data)
{
  FILE *wr_fp;
  pid_t pid, w_pid;
  char *argv[] = { mapping_fields->script_name, data, NULL };
  int rd_pfields[2] = {-1, -1};
  int wr_pfields[2] = {-1, -1};
  int index = 0, status = 0;

  char *liface = xstrdup(data);

  if(pipe(rd_pfields)) perror_exit("can't create pipe");
  if(pipe(wr_pfields)) perror_exit("can't create pipe");
  fflush(NULL);

  switch(pid = vfork()) {
    case -1:// error
      perror_exit("error in creating child process");
      break;
    case 0: { //child
              xclose(rd_pfields[1]); //close write fd of child.
              xclose(wr_pfields[0]); //close read fd of child.
              xdup2(rd_pfields[0], 0);
              xdup2(wr_pfields[1], 1);
              execvp(argv[0], argv);
              toys.exitval = (errno == ENOENT) ? 127 : 126;
              perror_exit("can't execute '%s'", argv[0]);
            }
            break;
  }
  //parent
  xclose(rd_pfields[0]); //close read fd of parent.
  xclose(wr_pfields[1]); //close write fd of parent.

  //fd -> file pointer.
  wr_fp = fdopen(rd_pfields[1], (((rd_pfields[1] << 1) + 1) & 1) ? "w" : "r");
  if(!wr_fp) error_exit("out of memory");

  //Write mappings to stdin of mapping script.
  while(index < mapping_fields->num_map_ifaces)
    fprintf(wr_fp, "%s\n", mapping_fields->mapping_list[index++]);

  fclose(wr_fp);

  //wait for child process termination and get the status of the child process.
  do
    w_pid = waitpid(pid, &status, 0);
  while((w_pid == -1) && (errno == EINTR));

  if (WIFEXITED(status) && !WEXITSTATUS(status)) {
    //script executed successfully. Get the new interface name.
    char *new_iface = get_line(wr_pfields[0]);
    if(new_iface) {
      char *ptr = &new_iface[strlen(new_iface) - 1];
      while(ptr >= new_iface && isspace(*ptr))
        *(ptr--) = '\0';
      free(liface);
      liface = new_iface;
    }
  }
  xclose(wr_pfields[0]);

  return liface;
}

/*
 * Update the current state of the interface.
 * write the current state of interface in the file "/var/run/ifstate" -> for ifup.
 * Remove the interface from the file "/var/run/ifstate" -> for ifdown.
 */
static void update_cur_state(const char *data, const char *iface_name)
{
  FILE *fp;
  struct double_list *state_list = NULL;
  struct double_list *cur_iface_state = NULL;
  struct double_list *cur_node = NULL;
  get_iface_state(&state_list, &cur_iface_state, data);

  //for ifup.
  if(TT.isup) {
    char *iface = xmsprintf("%s=%s", data, iface_name);
    if(!cur_iface_state) dlist_add(&state_list, iface);
    else {
      free(cur_iface_state->data);
      cur_iface_state->data = NULL;
      cur_iface_state->data = iface;
    }
  }
  //for ifdown.
  else {
    struct double_list *start_node = state_list;
    while(state_list) {
      //if only one node is in the list.
      if((state_list == cur_iface_state) && (state_list->next == state_list)) {
        free(state_list->data);
        state_list->data = NULL;
        free(state_list);
        state_list = NULL;
        break;
      }
      //if more than one node is in the list.
      if(state_list == cur_iface_state) {
        state_list->prev->next = state_list->next;
        state_list->next->prev = state_list->prev;
        state_list = state_list->next;
        cur_iface_state->next = cur_iface_state->prev = NULL;
        free(cur_iface_state->data);
        free(cur_iface_state);
        break;
      }
      state_list = state_list->next;
      if(state_list == start_node) break;
    }
  }
  fp = xfopen(NETWORK_INTERFACE_STATE_FILE, "w");
  cur_node = state_list;
  while(cur_node) {
    if(cur_node->data) fprintf(fp, "%s\n", cur_node->data);
    cur_node = cur_node->next;
    if(cur_node == state_list) break;
  }
  fclose(fp);
  free_state_list(state_list);
  return;
}

/*
 * configure the interface.
 */
static int config_interface(CONFIG_TYPE *conf, const char **interface)
{
  struct double_list *curlist = NULL;
  struct double_list *start = NULL;
  int is_cmd_failed = 0;

  if(interface && *interface) {
    while(*interface) {
      dlist_add(&curlist, (char *)*interface);
      interface++;
    }
  }
  else curlist = conf->auto_interfaces;

  start = curlist;
  //go through one by one interfaces.
  while(curlist) {
    char *data, *iface_name, *ptr;
    int cmd_status = 0;
    int visited_iface = 0;

    data = iface_name = ptr = NULL;
    data = xstrdup(curlist->data);
    ptr = strchr(data, '=');
    if(ptr) {
      *ptr = '\0';
      ptr++;
    }
    else ptr = data;
    iface_name = xstrdup(ptr);

    //with "-f" option; get the current state of the interface.
    //otherwise ignore the current state of the interface.
    if(!(toys.optflags & FLAG_f)) {
      struct double_list *state_list = NULL;
      struct double_list *cur_iface_state = NULL;
      get_iface_state(&state_list, &cur_iface_state, data);
      //"ifup" and current interface is already configured - error message.
      if(TT.isup && cur_iface_state) {
        error_msg("interface '%s' already configured", data);
        free_state_list(state_list);
        goto NEXT_INTERFACE;
      }
      //"ifdown" and current interface is not configured - error message.
      else if(!TT.isup && !cur_iface_state) {
        error_msg("interface '%s' not configured", data);
        free_state_list(state_list);
        goto NEXT_INTERFACE;
      }
      free_state_list(state_list);
    }
    //For mapping.
    if(TT.isup && !(toys.optflags & FLAG_m)) {
      struct double_list *m_list, *list_ptr;
      list_ptr = m_list = conf->mapping;
      while(m_list) {
        int index = 0;
        MAPPING_FIELDS *mapping_fields = (MAPPING_FIELDS *) m_list->data;
        while(index < mapping_fields->num_map_ifaces) {

          if(fnmatch(mapping_fields->map_ifaces_list[index++], iface_name, 0))
            continue;
          if(toys.optflags & FLAG_v)
            xprintf("Running mapping script '%s' on '%s'\n", mapping_fields->script_name, iface_name);
          if(iface_name) {
        	  free(iface_name);
        	  iface_name = NULL;
          }
          iface_name = execute_mapping(mapping_fields, data);
          break;
        }
        m_list = m_list->next;
        if(m_list == list_ptr) break;
      }
    }//End of mapping.

    //For iface.
    {
      struct double_list *iface_list, *list_ptr;
      iface_list = list_ptr = conf->iface;

      while(iface_list) {
        IFACE_FIELDS *cur_iface = (IFACE_FIELDS *) iface_list->data;
        if(!strcmp(iface_name, cur_iface->iface_name)) {
          char *old_iface = NULL;
          int is_new_iface = 0;

          visited_iface = 1;

          //validate the new iface name, which is coming from "abc=def" format, and the list's iface name.
          if(strcmp(cur_iface->iface_name, data)) {
            old_iface = cur_iface->iface_name;
            cur_iface->iface_name = data;
            is_new_iface = 1;
          }
          cmd_status = cur_iface->iface_method->funptr(cur_iface);

          //-1 and 0 are the failure status.
          if(cmd_status == -1) {
            error_msg("Missing variable for %s/%s", iface_name, cur_iface->iface_af);
            is_cmd_failed = 1;
          }
          else if(cmd_status == 0) is_cmd_failed = 1;
          if(is_new_iface) cur_iface->iface_name = old_iface;
        }
        iface_list = iface_list->next;
        if(iface_list == list_ptr) break;
      }
    }

    if(!visited_iface && !(toys.optflags & FLAG_f)) {
      error_msg("ignoring unknown interface %s", iface_name);
      is_cmd_failed = 1;
    }
    //with "-n" option; donot update the current state of the interface.
    else if(!(toys.optflags & FLAG_n))
      update_cur_state(data, iface_name);

NEXT_INTERFACE:
    free(data);
    free(iface_name);
    curlist = curlist->next;
    //already visited all the nodes.
    if(start == curlist) break;
  }
  return is_cmd_failed;
}

/*
 * ifup/ifdown main function.
 */
void ifup_main(void)
{
#define DEFAULT_INTERFACE_FILE_PATH "/etc/network/interfaces"
  CONFIG_TYPE *conf;
  char **argv = toys.optargs;
  TT.isup = (toys.which->name[2] == 'u');

  //"-a" and interface name are mutual-exclusive.
  if((toys.optflags & FLAG_a) && *argv)
    show_ifup_help();
  else if(!(toys.optflags & FLAG_a) && !*argv)
    show_ifup_help();

  //parse the file and prepare the list of interfaces.
  if(toys.optflags & FLAG_i) conf = read_interface_file(TT.interface_filename);
  else conf = read_interface_file(DEFAULT_INTERFACE_FILE_PATH);

  TT.g_path_var = xxstrdup(getenv("PATH"));
  TT.g_shell = get_shell();

  //With "-a" option there should not be any interface.
  if(toys.optflags & FLAG_a) toys.exitval = config_interface(conf, NULL);
  //Without "-a" option there should be an interface.
  else toys.exitval = config_interface(conf, (const char **)argv);

  if(TT.g_path_var) free((char *)TT.g_path_var);
  free((char *)TT.g_shell);
  clean_config(&conf);
  return;
#undef DEFAULT_INTERFACE_FILE_PATH
}//End of main function.
