/* find.c - search for files in a directory hierarchy and take corresponding address. 
 *
 * Copyright 2013 Sandeep Sharma <sandeep.jack2756@gmail.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/find.html 

USE_FIND(NEWTOY(find, NULL, TOYFLAG_USR|TOYFLAG_BIN))

config FIND
  bool "find"
  default y
  help
    Usage: find [PATH]... [OPTIONS] [ACTIONS]

	Search for files and perform actions on them.
	First failed action stops processing of current file.
	Defaults: PATH is current directory, action is '-print'

	-follow             Follow symlinks

	-xdev               Don't descend directories on other filesystems

	-maxdepth N         Descend at most N levels. -maxdepth 0 applies
                    	actions to command line arguments only

	-mindepth N         Don't act on first N levels

	-depth               Act on directory after traversing it

	Actions:
	( ACTIONS )         Group actions for -o / -a

	! ACT               Invert ACT's success/failure

	ACT1 [-a] ACT2      If ACT1 fails, stop, else do ACT2

	ACT1 -o ACT2        If ACT1 succeeds, stop, else do ACT2

	-name PATTERN       Match file name (w/o directory name) to PATTERN

	-iname PATTERN      Case insensitive -name

	-path PATTERN       Match path to PATTERN

	-ipath PATTERN      Case insensitive -path

	-regex PATTERN      Match path to regex PATTERN

	-type X             File type is X (one of: f,d,l,b,c,...)

	-perm MASK          At least one mask bit (+MASK), all bits (-MASK),
            			    or exactly MASK bits are set in file's mode

	-mtime DAYS         mtime is greater than (+N), less than (-N),
            			    or exactly N days in the past

	-mmin MINS          mtime is greater than (+N), less than (-N),
      		            or exactly N minutes in the past

	-newer FILE         mtime is more recent than FILE's

	-inum N             File has inode number N

	-user NAME/ID       File is owned by given user

	-group NAME/ID      File is owned by given group

	-size N[bck]        File size is N (c:bytes,k:kbytes,b:512 bytes(def.))
            			    +/-N: file size is bigger/smaller than N

	-links N            Number of links is greater than (+N), less than (-N),
            			    or exactly N

	-prune              If current file is directory, don't descend into it
            			    If none of the following actions is specified, -print is assumed

	-print              Print file name

	-print0             Print file name, NUL terminated

	-exec CMD ARG ;     Run CMD with all instances of {} replaced by
			                file name. Fails if CMD exits with nonzero

	-delete             Delete current file/directory. Turns on -depth option
*/

#define FOR_find
#include "toys.h"
#include <fnmatch.h>
#include <regex.h>

#define LONE_CHAR(s,c)     ((s)[0] == (c) && !(s)[1])
#ifndef FNM_CASEFOLD    //To use fnmatch() as case insensitive.
#define FNM_CASEFOLD (1<<4)
#endif
GLOBALS(
  char *cur_path;
  int is_print;
  int min_depth;
  int max_depth;
  int depth;
  int mm_depth_flag;
  int depth_flag;     //fist files and then directory [action modifier]
  int follo_symlinks; //Default donot follow symlinks.
  int prune_flag; 
  int xdev_flag;
  int dev_count;
  int invert_flag; // ! [Invert the actions result]
  char *userpath;
)

dev_t *xdev_t;      //on which we have to recurse only.

/*Any action has corresponding action_node */
typedef struct _action_node {
  int (*action_func)(struct _action_node *, struct dirtree *);
  struct _action_node *next;
  int not_flag;
  char plminus;
  int iname_flag;
  int ipath_flag;
  struct _action_node **sub_expr;
  union {
    char *pattern;
    char **e_argv;
    int e_argc;
    regex_t reg_pattern;
    int mask_type;
    mode_t mode;
    time_t time;
    ino_t inode_num;
    unsigned int ugid;
    unsigned size;
    unsigned long link_nr;
  }un;
}ACTION_NODE;

enum node_type { INAME = 0, AND, OR, EXEC, PAREN, PATH, MINDEPTH, MAXDEPTH, DEPTH
                   ,FOLLOW, PRUNE, PRINT, DELETE, REGEX, XDEV, TYPE, PERM, MTIME, MMIN,
                    NAME, IPATH, PRINT0, NEWER, INUM, USER, GROUP, SIZE, LINKS, INVERT
                };


struct option_list {
  char *option_name;
  enum node_type type;
};

static struct option_list list[] = {
  { "-iname", INAME},
  { "-a", AND},
  { "-o", OR},
  {"-exec", EXEC},
  {"(", PAREN},
  {"-path", PATH},
  {"-mindepth", MINDEPTH},
  {"-maxdepth", MAXDEPTH},
  {"-depth", DEPTH},
  {"-follow", FOLLOW},
  {"-prune", PRUNE},
  {"-print", PRINT},
  {"-delete", DELETE},
  {"-regex", REGEX},
  {"-xdev",XDEV},
  {"-type", TYPE},
  {"-perm", PERM},
  {"-mtime", MTIME},
  {"-mmin", MMIN},
  {"-name", NAME},
  {"-ipath", IPATH},
  {"-print0",PRINT0},
  {"-newer", NEWER},
  {"-inum", INUM},
  {"-user", USER},
  {"-group", GROUP},
  {"-size", SIZE},
  {"-links", LINKS},
  {"!", INVERT},
  { NULL, 0 },
};

ACTION_NODE **action_array_ptr; //GLOBAL LIST OF GROUP OF ACTIONS

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
/* Get last path component with no strip.
 * e.g.
 * "/"    -> "/"
 * "abc"    -> "abc"
 * "abc/def"  -> "def"
 * "abc/def/" -> ""
 */
static char *get_last_path_component_withnostrip(char *path)
{
  char *slash = strrchr(path, '/');
  if (!slash || (slash == path && !slash[1])) return (char*)path;
  return slash + 1;
}
/*
 * Get last path component with strip.
 * e.g.
 * "/"    -> "/"
 * "abc"    -> "abc"
 * "abc/def"  -> "def"
 * "abc/def/" -> "def"
 */
static char *get_last_path_component_withstrip(char *path)
{
  char *slash = find_last_char(path, '/');
  if (slash)
    while (*slash == '/' && slash != path) *slash-- = '\0';
  return get_last_path_component_withnostrip(path);
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
static unsigned long strtoul_range(char *str, unsigned long min, unsigned long max)
{
  char *endptr = NULL;
  errno = 0;
  unsigned long ret_value = strtoul(str, &endptr, 10);

  if(errno) perror_exit("Invalid num %s", str);
  else if(endptr && (*endptr != '\0' || endptr == str))
    perror_exit("Not a valid num %s", str);
  if(ret_value >= min && ret_value <= max) return ret_value;
  else perror_exit("Number %s is not in valid [%d-%d] Range\n", str, min, max);
}
/*
 * replace '{}' in arg to current file name for exec command.
 */
static void substitute_name(ACTION_NODE * node, struct dirtree *new)
{
  char *src = NULL;
  char *tail = src;
  char *dest;
  int count = 0 ,i = 0;
  char *needle; //For count computation only.
  int len = strlen(TT.cur_path); 
  while(node->un.e_argv[i]) {
    needle = src = tail = node->un.e_argv[i];
    count = 0;
    while((needle = strstr(needle, "{}")) != NULL) {
      needle += 2;
      count++;
    }
    if(!count) {
      i++;
      continue;
    }
    char *temp = node->un.e_argv[i];
    node->un.e_argv[i] = dest = xzalloc((len * count) + (strlen(src) - (2 * count)) + 1); //sizeof of final string
    while((tail = strstr(src, "{}"))) {
      memcpy(dest, src, (tail - src));
      dest += tail - src;
      strcpy(dest, TT.cur_path);
      src = tail + 2;
      dest += len;
    }
    strcpy(dest, src);
    free(temp);
    i++;
    count = 0;
  }
}
/*
 * execute action. Invert retun status with not_flag (!)
 */
static int exec_actions(ACTION_NODE **array, struct dirtree *new) 
{
  ACTION_NODE *ptr;
  int i = 0;
  int ret = 0, not_flg = 0;
  if(!array || !*array) return 1;

  while(array[i]) {
      ptr = array[i];
    while(1) {
      not_flg = ptr->not_flag;
      ret = ptr->action_func(ptr, new);
      if(not_flg) {   //Invert the result for ! .
        if(ret) ret = 0;
        else ret = 1;
      }
      if(!ret) break;  //if we fail, try next action in next action group
      else ptr = ptr->next; //Success, move to next action group [act1 -a act2]
      if(!ptr) return ret; //no more actions, return status.
    }
    i++;  //next action group [act1 -o act2]
  }
  return ret; 
}
/***********************************************************/
         /***Action Function Pionters****/

static int f_iname(ACTION_NODE * node, struct dirtree *new)
{
  char *path_component = get_last_path_component_withstrip(new->name);
  return(!fnmatch(node->un.pattern, path_component, node->iname_flag ? FNM_CASEFOLD : 0 ));
}
/*
 * exec for each argument in find search path.
 */
static int f_exec(ACTION_NODE * node, struct dirtree *new)
{
  int status;
  pid_t pid = fork();
  if(pid == 0) {
    if(S_ISDIR(new->st.st_mode)) {
      if(TT.depth_flag) {
        if(new->data == -1) {
          substitute_name(node, new);
          xexec(node->un.e_argv);
          return -1; //NOT REACHABLE
        }
        else exit(0);
      }
      else if(new->data == 0){
        substitute_name(node, new);
        xexec(node->un.e_argv);
        return -1; //NOT REACHABLE
      }
      else exit(0);
    }
    else {
      substitute_name(node, new);
      xexec(node->un.e_argv);
      return -1; //NOT REACHABLE
    }
  }
  else if(pid > 0){ 
    waitpid(pid, &status, 0);
    return (WEXITSTATUS(status) == 0);
  }
  else perror_exit("Fork failed");
}
/*
 * called for each parenthesis [ \( \) ], occured on command line.
 */
static int f_paren(ACTION_NODE *node, struct dirtree *new)
{
  return exec_actions(node->sub_expr, new);
}

static int dotdot(char *name)                                                                                                                                                                     
{
    if (name[0]=='.' && (!name[1] || (name[1]=='.' && !name[2]))) return 1;
     
      return 0;
}

static int f_path(ACTION_NODE *node, struct dirtree *new)
{
  char *path;
  int ret;
  path = TT.cur_path;
  if((strlen(path) - strlen(TT.userpath)) == 1) {
    if(dotdot(TT.userpath))
      path = TT.userpath;
  }
  ret = !fnmatch(node->un.pattern, path, node->ipath_flag ? FNM_CASEFOLD : 0);
  return ret; 
}
static int f_prune(ACTION_NODE *node, struct dirtree *new)
{
  TT.prune_flag = 1; 
  return 1; 
}
/*
 * print action Taking care of depth flag
 */
static int f_print(ACTION_NODE *node, struct dirtree *new)
{
  if(S_ISDIR(new->st.st_mode)) {
    if(TT.depth_flag) {
      if(new->data == -1) 
        puts(TT.cur_path);
      return 1;
    }
    else {
      if(new->data == 0) 
        puts(TT.cur_path);
      return 1;
    }
  }
  else puts(TT.cur_path);
  return 1;
}
static int f_print0(ACTION_NODE *node, struct dirtree *new)
{
  if(S_ISDIR(new->st.st_mode)) {
    if(TT.depth_flag) {
      if(new->data == -1) 
        xprintf("%s%c", TT.cur_path, '\0');
      return 1;
    }
    else {
      if(new->data == 0) 
        xprintf("%s%c", TT.cur_path, '\0');
      return 1;
    }
  }
  else xprintf("%s%c", TT.cur_path, '\0');
  return 1;
}
/*
 * Delete action, taking care of depth flag as delete action
 * turn on the depth flag. (-delete action - BE CAREFUL)
 */
static int f_delete(ACTION_NODE *node, struct dirtree *new)
{
 if(!new->data && S_ISDIR(new->st.st_mode)) return 1; //we will delete it in DIRTREE_COMEAGAIN
 int ret;
 if(S_ISDIR(new->st.st_mode))
   ret = rmdir(TT.cur_path);
 else
   ret = unlink(TT.cur_path);
 
 if(ret < 0) perror_msg("%s", new->name);
 return 1; //Always successful.
}
/*
 * execute the compiled regex pattern
 */
static int f_regex(ACTION_NODE *node, struct dirtree *new)
{
  regmatch_t match;
  char *path = TT.cur_path;
  if (regexec(&node->un.reg_pattern, path, 1, &match, 0)) return 0; 
  if (match.rm_so) return 0; 
  if (path[match.rm_eo]) return 0;
  return 1;
}
static int f_type(ACTION_NODE *node, struct dirtree *new)
{
 return((new->st.st_mode & S_IFMT) == node->un.mask_type);
}
static int f_perm(ACTION_NODE *node, struct dirtree *new)
{
  if(node->plminus == '+') return (new->st.st_mode & node->un.mode) != 0;
  if(node->plminus == '-') return (new->st.st_mode & node->un.mode) == node->un.mode;
  return (new->st.st_mode & 07777) == node->un.mode;
}
static int f_mtime(ACTION_NODE *node, struct dirtree *new)
{
  time_t file_age = time(NULL) - new->st.st_mtime;
  time_t time_sec = node->un.time*24*60*60;
  if(node->plminus == '+') return (file_age >= (time_sec + 24*60*60));
  if(node->plminus == '-') return file_age < time_sec;
  return file_age >= time_sec && file_age < (time_sec + 24*60*60);
}
static int f_mmin(ACTION_NODE *node, struct dirtree *new)
{
  time_t file_age = time(NULL) - new->st.st_mtime;
  time_t time_sec = node->un.time*60;
  if(node->plminus == '+') return (file_age >= (time_sec + 60));
  if(node->plminus == '-') return file_age < time_sec;
  return file_age >= time_sec && file_age < (time_sec + 60);
}
static int f_newer(ACTION_NODE *node, struct dirtree *new)
{
  return (node->un.time < new->st.st_mtime);
}
static int f_inum(ACTION_NODE *node, struct dirtree *new)
{
  return (new->st.st_ino == node->un.inode_num);
}
static int f_user(ACTION_NODE *node, struct dirtree *new)
{
  return (new->st.st_uid == node->un.ugid);
}
static int f_group(ACTION_NODE *node, struct dirtree *new)
{
  return (new->st.st_gid == node->un.ugid);
}
static int f_size(ACTION_NODE *node, struct dirtree *new)
{
  if(node->plminus == '+') return new->st.st_size > node->un.size;
  if(node->plminus == '-') return new->st.st_size < node->un.size;
  return new->st.st_size == node->un.size;
}
static int f_links(ACTION_NODE *node, struct dirtree *new)
{   
    if(node->plminus == '-') return (new->st.st_nlink <  node->un.link_nr);
    if(node->plminus == '+')return (new->st.st_nlink >  node->un.link_nr);
    return (new->st.st_nlink == node->un.link_nr);
}  

/******** Helper Functions *******************************/
/*
 * Return the corresponding action node option
 * else error.
 */
enum node_type find_option(char *option) {
  struct option_list *ptr;
  ptr = list;
  while(ptr->option_name) {
    if(strcmp(ptr->option_name, option) == 0) return ptr->type;
    else ptr++;
  }
  perror_exit("Unrecognised option %s", option);
}
/*
 * alloacte a action node. Ecah action action has 
 * one action node, correpondingly.
 */
ACTION_NODE *allocate_node(ACTION_NODE *node) {
  if(!node) {                                  
    node = xzalloc(sizeof(ACTION_NODE));
    node->next = NULL;
  }
  else {                                            
    node->next = xzalloc(sizeof(ACTION_NODE));
    node = node->next;
    node->next = NULL;
  }
  node->not_flag = TT.invert_flag;  //Handle '!' for each action.
  TT.invert_flag = 0;
  return node;
}
unsigned find_multiplier(char *size_str)
{
  char *p ;
  unsigned mul;
  p = size_str;
  p += (strlen(size_str) - 1);
  switch(*p) {
    case 'c':
      mul = 1;
      *p = '\0';
      break;
    case 'w':
      mul = 2;
      *p = '\0';
      break;
    case 'b':
      mul = 512;
      *p = '\0';
      break;
    case 'k':
      mul = 1024;
      *p = '\0';
      break;
    default:
      mul = 512;
      break;
  }
  mul *= strtoul_range(size_str, 0, ULONG_MAX);
  return mul;
}
/*
 * Make dirtree to understand . as ./
 * and .. as ../ .
 */
static char *make_pathproper(char *str)
{
  char *path = str;
  switch(strlen(str)) {
    case 1:
      if(str[0] == '.') path = xstrdup("./");
      break;
    case 2:
      if(str[0] == '.' && str[1] == '.') path = xstrdup("../");
      break;
    default:
      break;
  }
  return path;
}
/*
 * Return mask for given argument for
 * type action.
 */
static int get_mask(char *type)
{
  switch(*type) {
  case 'b':
     return S_IFBLK;
  case 'c':
     return S_IFCHR;
  case 'd':
     return S_IFDIR;
  case 'p':
     return S_IFIFO;
  case 'f':
     return S_IFREG;
  case 'l':
     return S_IFLNK;
  case 's':
     return S_IFSOCK;
  default:
     perror_exit("Invalid -type argument");
   }
}
/*
 * Parse args and form list of action node accordingly.
 * This function is Heart of the Implemntaion.
 */
static ACTION_NODE** parse_args(char ** argv)
{
  ACTION_NODE *action_ptr = NULL;
  ACTION_NODE **action_array;
  int current_group = 1;
  char *endptr = NULL; //For xstrtoul only
  TT.invert_flag = 0;
  action_array = xzalloc(2 * sizeof(ACTION_NODE *));
  while(*argv) {
    enum node_type option_no = find_option(*argv);
    switch(option_no) {                          //we are here only if we have valid option.
      case INAME:
        argv++;
        if(!*argv) perror_exit("need argument[s]");
        if(!action_ptr) action_ptr = action_array[current_group - 1] = allocate_node(action_ptr);
        else action_ptr = allocate_node(action_ptr);
        action_ptr->iname_flag = 1;
        action_ptr->un.pattern = (*argv);
        action_ptr->action_func = f_iname;
        break;
      case AND:
        break;
      case OR: // "-o" start new group of actions.
        TT.invert_flag = 0;
        action_array = xrealloc(action_array, (current_group + 2) * sizeof(ACTION_NODE *));
        action_array[current_group + 1] = NULL;
        current_group++;
        action_ptr = NULL; // new group with new actions
        break;
      case EXEC:
        TT.is_print = 0;
        argv++; //after -exec --args till--; or +
        int i = 0, j = 0;
        char **tmp = argv;
        if(!*argv) perror_exit(" -exec need argument[s]"); //exit before any allocation.
        if(!action_ptr) action_ptr = action_array[current_group - 1] = allocate_node(action_ptr);
        else action_ptr = allocate_node(action_ptr);
        action_ptr->action_func = f_exec;
        while(1) {
          if(!*argv) perror_exit("No terminating ; or +");
          if(((argv[0][0] == ';' || argv[0][0] == '+') && argv[0][1] == '\0')) break;
          argv++;
          i++;
        }
        j = i;
        if(i == 0) perror_exit("-exec need argument[s]");
        action_ptr->un.e_argv = xzalloc((i + 1) * sizeof(char*)); 
        int p = 0;
        while(i) {
          action_ptr->un.e_argv[p++] = xstrdup(*tmp);
          i--;
          tmp++;
        }
        action_ptr->un.e_argv[j] = NULL; //Overwrite ';' or '+' by NULL
        break;
      case PAREN:
        {
          char **argvp = argv;
          int nest = 1;
          while(1) {
            if(!*++argvp) perror_exit("Umatched '(' ");
            if(LONE_CHAR(*argvp, '(')) {
              nest++;
            }
            else if(LONE_CHAR(*argvp, ')') && !--nest) {
              *argvp = NULL;
              break;
            }
          }
          if(!action_ptr) action_ptr = action_array[current_group - 1] = allocate_node(action_ptr);
          else action_ptr = allocate_node(action_ptr);
          current_group = 1; //we are in sub_expr so we are at first action group
          action_ptr->sub_expr = parse_args(argv + 1);
          *argvp = (char*) ")"; /* restore NULLed parameter */                                                                                                                                    
          argv = argvp; 
          action_ptr->action_func = f_paren;
          break;
        }
      case PATH:
        argv++;
        if(!*argv) perror_exit("need argument[s]");
        if(!action_ptr) action_ptr = action_array[current_group - 1] = allocate_node(action_ptr);
        else action_ptr = allocate_node(action_ptr);
        action_ptr->ipath_flag = 0;
        action_ptr->un.pattern = (*argv);
        action_ptr->action_func = f_path;
        break;
      case MINDEPTH:
        TT.mm_depth_flag = 1;
        argv++;
        if(!*argv) perror_exit("need argument[s]");
        TT.min_depth = strtol_range(*argv, 0, LONG_MAX); 
        break;
      case MAXDEPTH:
        TT.mm_depth_flag = 1;
        argv++;
        if(!*argv) perror_exit("need argument[s]");
        TT.max_depth = strtol_range(*argv, 0, LONG_MAX);
        break;
      case DEPTH:
        TT.depth_flag = 1;
        TT.prune_flag = 0;
        break;
      case FOLLOW:
        TT.follo_symlinks = 1;
        break;
      case PRUNE:
        if(!action_ptr) action_ptr = action_array[current_group - 1] = allocate_node(action_ptr);
        else action_ptr = allocate_node(action_ptr);
        action_ptr->action_func = f_prune;
        break;
      case PRINT:
        TT.is_print = 0;
        if(!action_ptr) action_ptr = action_array[current_group - 1] = allocate_node(action_ptr);
        else action_ptr = allocate_node(action_ptr);
        action_ptr->action_func = f_print;
        break;
      case DELETE: 
        TT.is_print = 0; //don't try to print which you have deleted, Obviously.
        if(!action_ptr) action_ptr = action_array[current_group - 1] = allocate_node(action_ptr);
        else action_ptr = allocate_node(action_ptr);
        TT.depth_flag = 1; // -delete set depth.
        action_ptr->action_func = f_delete;
        break;
      case REGEX:
        argv++;
        if(!*argv) perror_exit("need argument[s]");
        if(!action_ptr) action_ptr = action_array[current_group - 1] = allocate_node(action_ptr);
        else action_ptr = allocate_node(action_ptr);
        if(regcomp(&action_ptr->un.reg_pattern, *argv, 0) != 0)
          perror_msg("Bad Regex: %s", *argv);
        action_ptr->action_func = f_regex;
        break;
      case XDEV:
        TT.xdev_flag = 1;
        break;
      case TYPE:
        argv++;
        if(!*argv) perror_exit("need argument[s]");
        if(strlen(*argv) > 1) perror_exit("Invalid arg '%s' to -type", *argv);
        if(!action_ptr) action_ptr = action_array[current_group - 1] = allocate_node(action_ptr);
        else action_ptr = allocate_node(action_ptr);
        action_ptr->un.mask_type = get_mask(*argv);
        action_ptr->action_func = f_type;
        break;
      case PERM:
        argv++;
        char *pp = NULL;
        if(!*argv) perror_exit("need argument[s]");
        if(!action_ptr) action_ptr = action_array[current_group - 1] = allocate_node(action_ptr);
        else action_ptr = allocate_node(action_ptr);
        pp = *argv;
        if(*pp == '+' || *pp == '-') {
          action_ptr->plminus = *pp;
          pp++;
        }
        if(!m_parse(pp, &action_ptr->un.mode))
          error_exit("Bad mode '%s'", *argv);
        action_ptr->action_func = f_perm;
        break;
      case MTIME:
        argv++;
        endptr = NULL;
        if(!*argv) perror_exit("need argument[s]");
        if(!action_ptr) action_ptr = action_array[current_group - 1] = allocate_node(action_ptr);
        else action_ptr = allocate_node(action_ptr);
        pp = *argv;
        if(*pp == '+' || *pp == '-') {
          action_ptr->plminus = *pp;
          pp++;
        }
        action_ptr->un.time = (time_t)strtol_range(pp, 0, LONG_MAX);
        action_ptr->action_func = f_mtime;
        break;
        case MMIN:
        argv++;
        endptr = NULL;
        if(!*argv) perror_exit("need argument[s]");
        if(!action_ptr) action_ptr = action_array[current_group - 1] = allocate_node(action_ptr);
        else action_ptr = allocate_node(action_ptr);
        pp = *argv;
        if(*pp == '+' || *pp == '-') {
          action_ptr->plminus = *pp;
          pp++;
        }
        action_ptr->un.time = (time_t)strtol_range(pp, 0, LONG_MAX);
        action_ptr->action_func = f_mmin;
        break;
        case NAME:
        argv++;
        if(!*argv) perror_exit("need argument[s]");
        if(!action_ptr) action_ptr = action_array[current_group - 1] = allocate_node(action_ptr);
        else action_ptr = allocate_node(action_ptr);
        action_ptr->iname_flag = 0;
        action_ptr->un.pattern = (*argv);
        action_ptr->action_func = f_iname;
        break;
        case IPATH:
        argv++;
        if(!*argv) perror_exit("need argument[s]");
        if(!action_ptr) action_ptr = action_array[current_group - 1] = allocate_node(action_ptr);
        else action_ptr = allocate_node(action_ptr);
        action_ptr->ipath_flag = 1;
        action_ptr->un.pattern = (*argv);
        action_ptr->action_func = f_path;
        break;
        case PRINT0:
        TT.is_print = 0;
        if(!action_ptr) action_ptr = action_array[current_group - 1] = allocate_node(action_ptr);
        else action_ptr = allocate_node(action_ptr);
        action_ptr->action_func = f_print0;
        break;
        case NEWER:
        argv++; 
        struct stat st;
        if(!*argv) perror_exit("need argument[s]");
        if(!action_ptr) action_ptr = action_array[current_group - 1] = allocate_node(action_ptr);
        else action_ptr = allocate_node(action_ptr);
        action_ptr->action_func = f_newer;
        if(stat(*argv, &st)) perror_exit("%s", *argv);
        action_ptr->un.time = st.st_mtime;
        break;
        case INUM:
        argv++;
        errno = 0;
        endptr = NULL;
        if(!*argv) perror_exit("need argument[s]");
        if(!action_ptr) action_ptr = action_array[current_group - 1] = allocate_node(action_ptr);
        else action_ptr = allocate_node(action_ptr);
        action_ptr->action_func = f_inum;
        if( *argv[0] == '-' || *argv[0] == '+') error_exit("Invalid num %s",*argv);
        action_ptr->un.inode_num = strtoul_range(*argv, 0, ULONG_MAX); 
        break;
        case USER:
        argv++; 
        errno = 0;
        if(!*argv) perror_exit("need argument[s]");
        if(!action_ptr) action_ptr = action_array[current_group - 1] = allocate_node(action_ptr);
        else action_ptr = allocate_node(action_ptr);
        action_ptr->action_func = f_user;
        action_ptr->un.ugid = xstrtoul(*argv, &endptr, 10);
        if(errno || *endptr != '\0' || *argv[0] == '-' || *argv[0] == '+') {
        struct passwd *user;
        user = getpwnam(*argv);
        if(!user) error_exit("Unknown user '%s'", *argv);
        action_ptr->un.ugid = user->pw_uid;
        }
        break;
        case GROUP:
        argv++; 
        if(!*argv) perror_exit("need argument[s]");
        if(!action_ptr) action_ptr = action_array[current_group - 1] = allocate_node(action_ptr);
        else action_ptr = allocate_node(action_ptr);
        action_ptr->action_func = f_group;
        action_ptr->un.ugid = xstrtoul(*argv, &endptr, 10);
        if(errno || *endptr != '\0' || *argv[0] == '-' || *argv[0] == '+') {
        struct group *group;
        group = getgrnam(*argv);
        if(!group) error_exit("Unknown group '%s'", *argv);
        action_ptr->un.ugid = group->gr_gid;
        }
        break;
        case SIZE:
        argv++;
        endptr = NULL;
        if(!*argv) perror_exit("need argument[s]");
        if(!action_ptr) action_ptr = action_array[current_group - 1] = allocate_node(action_ptr);
        else action_ptr = allocate_node(action_ptr);
        pp = *argv;
        if(*pp == '+' || *pp == '-') {
          action_ptr->plminus = *pp;
          pp++;
        }
        if(*pp == '+' || *pp == '-') perror_exit("Invalid arg '%s'", *argv);
        action_ptr->un.size = find_multiplier(pp);
        action_ptr->action_func = f_size;
        break;
        case LINKS:
        argv++;
        endptr = NULL;
        if(!*argv) perror_exit("need argument[s]");
        if(!action_ptr) action_ptr = action_array[current_group - 1] = allocate_node(action_ptr);
        else action_ptr = allocate_node(action_ptr);
        pp = *argv;
        if(*pp == '+' || *pp == '-') {
          action_ptr->plminus = *pp;
          pp++;
        }
        action_ptr->action_func = f_links;
        action_ptr->un.link_nr = strtoul_range(pp, 0, ULONG_MAX); 
        break;
        case INVERT:
        TT.invert_flag ^= 1;
        break;
        default:
        // NOT REACHABLE as we have already exited for invalid option(case match)
        break;
    }
    argv++; //Next action in command line args.
  }
  return action_array;
}


/*
 * Dirtree callback function. Take actions and print files and dirs
 * corresponding to maxdepth, mindepth and depth on commandline.
 */
static int do_find(struct dirtree *new)
{
  int ret = 0;
  if(dirtree_notdotdot(new) == 0) return 0;

  if(TT.cur_path) {
    free(TT.cur_path);
    TT.cur_path = NULL;
  }
  TT.cur_path = dirtree_path(new, NULL);

  if(TT.xdev_flag && S_ISDIR(new->st.st_mode)) {
    int i = 0, j, found = 0;
    j = TT.dev_count;
    while(j--) {
      if(xdev_t[i++] == new->st.st_dev) {
        found = 1;
        break;                                                                                                                                                                                       
      } else found = 0;
    }
    if(!found) return 0;
  }

  if(TT.mm_depth_flag) {
    if(TT.depth < TT.min_depth || (TT.depth == TT.min_depth && new->data == -1)) goto ret_t1;
  }

  ret = exec_actions(action_array_ptr, new); //Take Actions.
  

  if(ret && TT.is_print) {
    if(TT.depth_flag && S_ISDIR(new->st.st_mode)) { //Process directories "after" traversing them.
      if(new->data == -1) 
       puts(TT.cur_path); 
       goto ret_t1;
    }
    if(S_ISDIR(new->st.st_mode)) {
      if(new->data == 0) puts(TT.cur_path); //Process directories and then stuff in it.
      else goto ret_t1;
    }
    else puts(TT.cur_path); 
  }

  if(TT.prune_flag) { //we have ancountered -prune for this action group.
    TT.prune_flag = 0;
    return 0; //Dont desend into this DIR
  }

ret_t1:

  if(S_ISDIR(new->st.st_mode)) {
    if(new->data == -1) TT.depth--;
    else TT.depth++;
  }
  if(TT.mm_depth_flag) {
    if(TT.depth  > TT.max_depth) {
      if(TT.depth_flag && S_ISDIR(new->st.st_mode) && new->data == 0) {
        puts(TT.cur_path);
      }
      TT.depth--;
      return 0;
    }
  } 
  return (DIRTREE_COMEAGAIN |(TT.follo_symlinks ? DIRTREE_SYMFOLLOW : 0));
}
/*
 * Find main function.
 */
static void free_action_nodes(void)
{
  ACTION_NODE *ptr, *temp;
  int i = 0;
  while(action_array_ptr[i]) {
    temp = ptr = action_array_ptr[i];
    while(ptr) {
      temp = ptr->next;
      free(ptr);
      ptr = temp;
    }
    i++;
  }
}
void find_main(void)
{
  char **paths, **p;
  char **argv;
  int i = 0;
  argv = toys.argv;
  argv++;
  TT.is_print = 1;
  TT.max_depth = INT_MAX;
  TT.cur_path = NULL;
  p = paths = xmalloc((toys.optc + 1) * sizeof(char *)); //Store paths here .
  while(*argv != NULL) {
    if(argv[0][0] == '-') break;
    if((argv[0][0] == '!' || argv[0][0] == '(') && argv[0][1] == '\0') break;
    *p++ = *argv++;
    TT.dev_count++;
  }
  if(p == paths) {
    p[0] = (char*)"./";
    p++;
    TT.dev_count++;
  }
  *p = NULL;
  action_array_ptr = parse_args(argv);
  if(TT.xdev_flag) {
    struct stat st_buf;
    xdev_t = xzalloc(TT.dev_count * sizeof(xdev_t[0]));
    for(i = 0; paths[i]; i++) {
      if(stat(paths[i], &st_buf) == 0) {
        xdev_t[i] = st_buf.st_dev;
      }
    }
  }
  i = 0;
  while(paths[i]) {
    TT.userpath = paths[i];
    char *path = make_pathproper(paths[i]);
    dirtree_read(path, do_find);
    i++;
  }
  if(CFG_TOYBOX_FREE){
    if(TT.cur_path) free(TT.cur_path);
    free(paths);
    free_action_nodes();
    free(action_array_ptr);
  }
}
