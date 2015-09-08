/* init.c - init program.
 *
 * Copyright 2012 Harvind Singh <harvindsingh1981@gmail.com>
 *
 * Not in SUSv4.

USE_INIT(NEWTOY(init, "", TOYFLAG_SBIN))

config INIT
  bool "init"
  default y
  help
    usage: init

    init the system.
*/

#include "toys.h"
#include<linux/vt.h>
#include<sys/types.h>
#include<sys/reboot.h>
struct action_list_seed {
  struct action_list_seed *next;
  pid_t pid;
  uint8_t action;
  char terminal_name[40];
  char command[256];
};
struct action_list_seed *action_list_pointer = NULL;
int caught_signal;

//INITTAB action defination
#define SYSINIT 0x01
#define WAIT  0x02
#define ONCE  0x04
#define RESPAWN  0x08
#define ASKFIRST  0x10
#define CTRLALTDEL  0x20
#define  SHUTDOWN  0x40
#define  RESTART  0x80

static void initialize_console(void)
{
  int file_dis;
  char *p;
  p = getenv("CONSOLE");
  if (p) xprintf("CONSOLE found----%s\n",p);
  else {
    p = getenv("console");
    if (p) xprintf("console found----%s\n",p);
  }
  if (!p) {
    file_dis = open("/dev/null", O_RDWR);
    if (file_dis >= 0) {
      while(file_dis < 2) file_dis = dup(file_dis);
      while(file_dis > 2) close(file_dis--);
    }
  } else {
    file_dis = open(p, O_RDWR | O_NONBLOCK | O_NOCTTY);
    if (file_dis < 0) xprintf("Unable to open console %s\n",p);
    else {
      dup2(file_dis,0);
      dup2(file_dis,1);
      dup2(file_dis,2);
    }
  }
  p = getenv("TERM");
#ifdef VT_OPENQRY
  int terminal_no;
  if (ioctl(0,VT_OPENQRY,&terminal_no)) {
    if (!p || strcmp(p,"linux") == 0) putenv((char*)"TERM=vt102");
  } else
#endif  
  { 
    if (!p) putenv((char*)"TERM=linux");
  }
}
static void set_sane_term(void)
{
  struct termios terminal;
  tcgetattr(0, &terminal);
  terminal.c_cc[VINTR] = 3;//ctrl-c
  terminal.c_cc[VQUIT] = 28;/*ctrl-\*/
  terminal.c_cc[VERASE] = 127;//ctrl-?
  terminal.c_cc[VKILL] = 21;//ctrl-u
  terminal.c_cc[VEOF] = 4;//ctrl-d
  terminal.c_cc[VSTART] = 17;//ctrl-q
  terminal.c_cc[VSTOP] = 19;//ctrl-s
  terminal.c_cc[VSUSP] = 26;//ctrl-z

  terminal.c_line = 0;
  terminal.c_cflag = terminal.c_cflag&(CRTSCTS|PARODD|PARENB|CSTOPB|CSIZE|CBAUDEX|CBAUD);
  terminal.c_cflag = terminal.c_cflag|(CLOCAL|HUPCL|CREAD);
  terminal.c_iflag = IXON|IXOFF|ICRNL;//enable start/stop input and output control + map CR to NL on input
  terminal.c_oflag = ONLCR|OPOST;//Map NL to CR-NL on output
  terminal.c_lflag = IEXTEN|ECHOKE|ECHOCTL|ECHOK|ECHOE|ECHO|ICANON|ISIG;//extended input character processing+ + +echo kill+echo erased character+
  tcsetattr(0, TCSANOW, &terminal);
}

static void set_enviornment(void)
{
  putenv((char*)"HOME=/");
  putenv((char*)"PATH=/sbin:/usr/sbin:/bin:/usr/bin");
  putenv((char*)"SHELL=/bin/sh");
  putenv((char*)"USER=root");
}
static void add_new_action(uint8_t action,char *command,char *term)
{
  struct action_list_seed *x,**y;
  y = &action_list_pointer;
  x = *y;
  while(x != NULL) {
    if ((strcmp(x->command,command) == 0) && (strcmp(x->terminal_name,term) == 0)) {
      *y = x->next;//remove from the list
      while(*y != NULL) y = &(*y)->next;//traverse through list till end
      x->next = NULL;
      break;
    }
    y = &(x)->next;
    x = *y;
  }
  //create a new node
  if (x == 0) x = xzalloc(sizeof(*x));
  *y = x;
  x->action = action;
  strncpy(x->command, command, sizeof(x->command));
  x->command[sizeof(x->command) - 1] = '\0';
  strncpy(x->terminal_name, term, sizeof(x->terminal_name));
  x->terminal_name[sizeof(x->terminal_name) - 1] = '\0';
}
static void console_message(const char *format, ...)
{
  char message[128];
  va_list arguments;
  unsigned int length;
  ssize_t x;

  va_start(arguments, format);
  length = vsnprintf(message,sizeof(message)-2/*reserve space for \n and \0*/,format,arguments)+1;
  if (length > (sizeof(message) - 1)) length = sizeof(message) - 1;
  va_end(arguments);
  message[length++] = '\n';
  message[length] = '\0';
  do {
    x = write(2, message, length);//ensure write is not effected by any signal.
  } while(x < 0 && errno == EINTR);
}
static void inittab_parsing(void)
{
#define ONE_TOKEN 1
#define TWO_TOKENS 2
#define THREE_TOKENS 3
#define FOUR_TOKENS 4
  FILE *fp;
  char file_line_buffer[256];
  char terminal_name[40];
  char command[256];
  uint8_t action = 0;
  char *p;
  char *extracted_token;
  int line_number = 0;
  int token_count = 0;

  fp = fopen("/etc/inittab","r");
  if (fp == NULL) {
    console_message("Unable to open /etc/inittab. Using Default inittab");
    add_new_action(SYSINIT, "/etc/init.d/rcS", "");
    add_new_action(RESPAWN, "/sbin/getty -n -l /bin/sh -L 115200 tty1 vt100", "");
  } else {
    while((p = fgets(file_line_buffer,sizeof(file_line_buffer),fp)) != NULL) { //read single line from /etc/inittab
      line_number++;
      token_count = 0;
      action = 0;
      while((extracted_token = strsep(&p,":")) != NULL) {
        token_count++;
        switch (token_count) {
          case ONE_TOKEN:
            strcpy(terminal_name,"/dev/");
            if (*extracted_token != '\0') {
              strncat(terminal_name, extracted_token, 40);//prepend /dev/ to terminal name
              terminal_name[39] = '\0';
            }
            else strcpy(terminal_name, "");
            break;
          case TWO_TOKENS:
            break;
          case THREE_TOKENS:
            if (!strcmp(extracted_token, "sysinit")) action = 0x01;
            else if (!strcmp(extracted_token, "askfirst")) action = 0x10;  
            else if (!strcmp(extracted_token, "ctrlaltdel")) action = 0x20;  
            else if (!strcmp(extracted_token,"respawn"))action = 0x08;  
            else if (!strcmp(extracted_token, "shutdown"))action = 0x40;  
            else if (!strcmp(extracted_token, "restart")) action = 0x80;  
            else if (!strcmp(extracted_token, "wait")) action = 0x02;  
            else if (!strcmp(extracted_token, "once")) action = 0x04;
            else console_message("Invalid action at line number %d ---- ignoring",line_number);
            break;
          case FOUR_TOKENS:
            strncpy(command, strsep(&extracted_token, "\n"), sizeof(command));//eliminate trailing \n character using strsep;
            command[sizeof(command) - 1] = '\0';
            break;
          default:
            break;
        }
      }  //while token
      if (action != 0) add_new_action(action, command, terminal_name);  
    }//while line
    fclose(fp);
    fp = NULL;
  }
#undef ONE_TOKEN
#undef TWO_TOKENS
#undef THREE_TOKENS
#undef FOUR_TOKENS
}
static void run_command(char *command)
{
  char *final_command[128];
  char temp_buffer[256+6];
  int hyphen;
  hyphen = (command[0]=='-');
  command = command + hyphen;
  if (strpbrk(command, "?<>'\";[]{}\\|=()*&^$!`~") == NULL) {
    char *next_command;
    char *extracted_command;
    int x = 0;
    next_command = strcpy(temp_buffer, command - hyphen);
    command = next_command + hyphen;
    while((extracted_command = strsep(&next_command," \t")) != NULL) {
      if (*extracted_command != '\0') {
        final_command[x] = extracted_command;
        x++;
      }
    }
    final_command[x] = NULL;
  } else {
    sprintf(temp_buffer, "exec %s", command);
    command = "-/bin/sh"+1;
    final_command[0] = (char*)("-/bin/sh"+!hyphen);
    final_command[1] = (char*)"-c";
    final_command[2] = temp_buffer;
    final_command[3] = NULL;
  }
  if (hyphen) ioctl(0,TIOCSCTTY,0);
  execvp(command, final_command);
  console_message("unable to run %s",command);
}
//runs all same type of actions
static pid_t final_run(struct action_list_seed *x)
{
  pid_t pid;
  int fd;
  sigset_t signal_set;
  sigfillset(&signal_set);
  sigprocmask(SIG_BLOCK, &signal_set, NULL);
  if (x->action & ASKFIRST) pid = fork();
  else pid = vfork();
  if (pid != 0) {
    //parent process or error
    //unblock the signals
    sigfillset(&signal_set);
    sigprocmask(SIG_UNBLOCK, &signal_set, NULL);
    return pid;      
  }
  //new born child process
  //reset signals to default behavior and remove signal mask
  signal(SIGUSR1,SIG_DFL);
  signal(SIGUSR2,SIG_DFL);
  signal(SIGTERM,SIG_DFL);
  signal(SIGQUIT,SIG_DFL);
  signal(SIGINT,SIG_DFL);
  signal(SIGHUP,SIG_DFL);
  signal(SIGTSTP,SIG_DFL);
  signal(SIGSTOP,SIG_DFL);
  sigset_t signal_set_c;
  sigfillset(&signal_set_c);
  sigprocmask(SIG_UNBLOCK, &signal_set_c, NULL);
  setsid();//new session
  if (x->terminal_name[0]) {
    close(0);
    fd = open(x->terminal_name,(O_RDWR|O_NONBLOCK),0600);
    if (fd != 0) {
      console_message("Unable to open %s,%s\n",x->terminal_name,strerror(errno));
      _exit(EXIT_FAILURE);
    } else {
      dup2(0,1);
      dup2(0,2);
    }
  }
  set_sane_term();
  /*******code for ASKFIRST handling to be written******************/
  //to be clarified because this feature is specific to busy box; 
  /*****************************************************************/
  run_command(x->command);
  _exit(-1);
}
static struct action_list_seed* mark_as_terminated_process(pid_t pid)
{
  struct action_list_seed *x;
  if (pid > 0) {
    for (x = action_list_pointer; x; x = x->next) {
      if (x->pid == pid) {
        x->pid = 0;
        return x;
      }
    }
  }
  return NULL;
}
static void waitforpid(pid_t pid)
{
  if (pid <= 0) return;
  for(;;) {
    pid_t y = wait(NULL);
    mark_as_terminated_process(y);
    if (kill(y,0)) break;
  }
}
static void run_action_from_list(int action)
{
  struct action_list_seed *x;
  pid_t pid;
  x = action_list_pointer;
  for (x = action_list_pointer; x; x = x->next) {
    if ((x->action & action) == 0) continue;
    if (x->action & (SHUTDOWN|ONCE|SYSINIT|CTRLALTDEL|WAIT)) {
      pid = final_run(x);
      if (x->action&(SHUTDOWN|SYSINIT|CTRLALTDEL|WAIT)) waitforpid(pid);
    }
    if (x->action & (ASKFIRST|RESPAWN))
      if (!(x->pid)) x->pid = final_run(x);
  }
}
static void halt_poweroff_reboot_handler(int sig_no)
{
  unsigned int reboot_magic_no = 0;
  pid_t pid;
  signal(SIGUSR1,SIG_DFL);
  signal(SIGUSR2,SIG_DFL);
  signal(SIGTERM,SIG_DFL);
  signal(SIGQUIT,SIG_DFL);
  signal(SIGINT,SIG_DFL);
  signal(SIGHUP,SIG_DFL);
  signal(SIGTSTP,SIG_DFL);
  signal(SIGSTOP,SIG_DFL);
  sigset_t signal_set_c;
  sigfillset(&signal_set_c);
  sigprocmask(SIG_UNBLOCK,&signal_set_c, NULL);
  run_action_from_list(SHUTDOWN);
  console_message("The system is going down NOW!");
  kill(-1, SIGTERM);
  console_message("Sent SIGTERM to all processes");
  sync();
  sleep(1);
  kill(-1,SIGKILL);
  sync();
  switch (sig_no) {
    case SIGUSR1:
      console_message("Requesting system halt");
      reboot_magic_no=RB_HALT_SYSTEM;
      break;
    case SIGUSR2:
      console_message("Requesting system poweroff");
      reboot_magic_no=RB_POWER_OFF;
      break;
    case SIGTERM:  
      console_message("Requesting system reboot");
      reboot_magic_no=RB_AUTOBOOT;
      break;
    default:
      break;
  }
  sleep(1);
  pid = vfork();
  if (pid == 0) {
    reboot(reboot_magic_no);
    _exit(EXIT_SUCCESS);
  }
  while(1) sleep(1);
}
static void restart_init_handler(int sig_no)
{
  struct action_list_seed *x;
  pid_t pid;
  int fd;
  for (x = action_list_pointer; x; x = x->next) {
    if (!(x->action&RESTART)) continue;
    signal(SIGUSR1,SIG_DFL);
    signal(SIGUSR2,SIG_DFL);
    signal(SIGTERM,SIG_DFL);
    signal(SIGQUIT,SIG_DFL);
    signal(SIGINT,SIG_DFL);
    signal(SIGHUP,SIG_DFL);
    signal(SIGTSTP,SIG_DFL);
    signal(SIGSTOP,SIG_DFL);
    sigset_t signal_set_c;
    sigfillset(&signal_set_c);
    sigprocmask(SIG_UNBLOCK,&signal_set_c,NULL);
    run_action_from_list(SHUTDOWN);
    console_message("The system is going down NOW!");
    kill(-1,SIGTERM);
    console_message("Sent SIGTERM to all processes");
    sync();
    sleep(1);
    kill(-1,SIGKILL);
    sync();

    if (x->terminal_name[0]) {
      close(0);
      fd = open(x->terminal_name,(O_RDWR|O_NONBLOCK),0600);
      if (fd != 0) {
        console_message("Unable to open %s,%s\n",x->terminal_name,strerror(errno));
        sleep(1);
        pid = vfork();
        if (pid == 0) {
          reboot(RB_HALT_SYSTEM);
          _exit(EXIT_SUCCESS);
        }
        while(1) sleep(1);
      } else {
        dup2(0,1);
        dup2(0,2);
        set_sane_term();
        run_command(x->command);
      }
    }
  }
}
static void catch_signal(int sig_no)
{
  caught_signal = sig_no;
  console_message("signal seen");
}
static void pause_handler(int sig_no)
{
  int signal_backup,errno_backup;
  pid_t pid;
  errno_backup = errno;
  signal_backup = caught_signal;
  signal(SIGCONT, catch_signal);
  while(1) {
    if (caught_signal == SIGCONT) break;
    do
      pid = waitpid(-1,NULL,WNOHANG);
    while((pid==-1) && (errno=EINTR));
    mark_as_terminated_process(pid);
    sleep(1);
  }
  signal(SIGCONT,SIG_DFL);
  errno = errno_backup;
  caught_signal = signal_backup;
}
static void assign_signal_handler(void)
{
  struct sigaction sig_act;
  signal(SIGUSR1, halt_poweroff_reboot_handler);//halt
  signal(SIGUSR2, halt_poweroff_reboot_handler);//poweroff
  signal(SIGTERM, halt_poweroff_reboot_handler);//reboot
  signal(SIGQUIT, restart_init_handler);//restart init
  memset(&sig_act, 0, sizeof(sig_act));
  sigfillset(&sig_act.sa_mask);
  sigdelset(&sig_act.sa_mask, SIGCONT);
  sig_act.sa_handler = pause_handler;
  sigaction(SIGTSTP, &sig_act, NULL);
  sigaction(SIGSTOP, &sig_act, NULL);
  memset(&sig_act, 0, sizeof(sig_act));
  sig_act.sa_handler = catch_signal;
  sigaction(SIGINT, &sig_act, NULL);
  memset(&sig_act, 0, sizeof(sig_act));
  sig_act.sa_handler = catch_signal;
  sigaction(SIGHUP, &sig_act, NULL);  
}

static int check_if_pending_signals(void)
{
  int signal_caught = 0;
  while(1) {
    int sig = caught_signal;
    if (!sig) return signal_caught;
    caught_signal = 0;
    signal_caught = 1;
    if (sig == SIGINT) run_action_from_list(CTRLALTDEL);
  }
}
void init_main(void)
{
  if (getpid() != 1) {
    xprintf("Already running\n");
    return;
  }
  xprintf("Starting init......\n");
  initialize_console();
  set_sane_term();

  if (chdir("/") != 0) xprintf("cannot change directory to '/'\n");
  if (!setsid()) xprintf("Initial setsid() failed\n");

  set_enviornment();
  inittab_parsing();  
  assign_signal_handler();
  run_action_from_list(SYSINIT);
  check_if_pending_signals();
  run_action_from_list(WAIT);
  check_if_pending_signals();
  run_action_from_list(ONCE);
  while(1) {
    int suspected_WNOHANG;
    suspected_WNOHANG = check_if_pending_signals();
    run_action_from_list(RESPAWN | ASKFIRST);
    suspected_WNOHANG = suspected_WNOHANG|check_if_pending_signals();
    sleep(1);//let cpu breath
    suspected_WNOHANG = suspected_WNOHANG|check_if_pending_signals();
    if (suspected_WNOHANG) suspected_WNOHANG=WNOHANG;
    while(1) {
      pid_t pid;
      pid = waitpid(-1, NULL, suspected_WNOHANG);
      if (pid <= 0) break;
      mark_as_terminated_process(pid);
      suspected_WNOHANG = WNOHANG;
    }
  }
}
