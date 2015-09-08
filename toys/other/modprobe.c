/* modprobe.c - modprobe utility.
 *
 * Copyright 2012 Madhur Verma <mad.flexi@gmail.com>
 *
 * Not in SUSv4.

USE_MODPROBE(NEWTOY(modprobe, "alrqvsDb", TOYFLAG_SBIN))

config MODPROBE
  bool "modprobe"
  default y
  help
    usage: modprobe [-alrqvsDb] MODULE [symbol=value][...]

    modprobe utility - inserts modules and dependencies.

       -a    Load multiple MODULEs
       -l    List (MODULE is a pattern)
       -r    Remove MODULE (stacks) or do autoclean
       -q    Quiet
       -v    Verbose
       -s    Log to syslog
       -D    Show dependencies
       -b    Apply blacklist to module names too
*/
#define FOR_modprobe
#include "toys.h"
#include <sys/utsname.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <fnmatch.h>

#define DBASE_SIZE    256

GLOBALS(
  struct arg_list *probes;
  struct arg_list *dbase[256]; //#define DBASE_SIZE 256 if modified please update
  char *cmdopts;
  int   nudeps;
  uint8_t  symreq;
)

#define flagGet(f,v,d)  (toys.optflags & f) ? v : d
#define flagChk(f)    (toys.optflags & f) ? 1 : 0

/*
 * Standard configuration defination
 * can be moved to kconfig in future releases
 */

#define CONFIG_MODUTILS_SYMBOLS 1
#define CONFIG_MODUTILS_ALIAS  1

#define MODULES_DIR    "/lib/modules"
#define DEPMODE_FILE  "modules.dep"

static void (*dbg)(char *format, ...);
static void dummy(char *format, ...){
	return;
}

/*
 * Modules flag definations
 */
#define MOD_ALOADED    0x0001
#define MOD_BLACKLIST  0x0002
#define MOD_FNDDEPMOD  0x0004
#define MOD_NDDEPS    0x0008

/*
 *  Current probing modules info
 */
typedef struct module_s
{
  char *cmdname;        // name from argv
  char *name;          // stripped of name.
  struct arg_list *rnames;  // real names if name is aliased
  char *depent;        // entry line from modules.dep containing dependency
  char *opts;          // options to pass for init module
  uint32_t flags;        // flags for this module
  struct arg_list *dep;    // dependency list for this module
} module_t;

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
 * maximum module name length
 */
#define MODNAME_LEN        256
/*
 * Converts path name FILE to module name also allocates memory
 * for holding the string if MOD is NULL,
 * Returns the pointer to the string.
 *
 */
static char *path2mod(char *file, char *mod)
{
	int i;
	char *from;

	if (!file) return NULL;
	if (!mod) mod = xmalloc(MODNAME_LEN);
	from = get_last_path_component_withnostrip(file);
	for (i = 0; i < (MODNAME_LEN-1) && from[i] != '\0' && from[i] != '.'; i++)
		mod[i] = (from[i] == '-') ? '_' : from[i];
	mod[i] = '\0';
	return mod;
}

#ifndef _GNU_SOURCE
/*
 * locate character in string.
 */
static char *strchrnul(char *s, int c)
{
  while(*s != '\0' && *s != c) s++;
  return (char*)s;
}
#endif

/*
 * Adds options in opts from toadd by reallocating opts to
 * store all data;
 */
static char *add_opts(char *opts, const char *toadd)
{
  if (toadd){
    int optlen = 0;
    if (opts != NULL) optlen = strlen(opts);
    opts = xrealloc(opts, optlen + strlen(toadd) + 2);
    sprintf(opts + optlen, " %s", toadd);
  }
  return opts;
}

/* Remove first element from the list and return it */
static void *llist_popme(struct arg_list **head)
{
  char *data = NULL;
  struct arg_list *temp = *head;

  if (temp) {
    data = temp->arg;
    *head = temp->next;
    free(temp);
  }
  return data;
}

/*
 * Creates new node with given DATA and Adds before the node OLD of link list.
 * TODO: its higly recommended that this function is added to the llist.c
 *     and also structures should be typedefed for easy use. i.e
 *     typedef struct arg_list arglist_t;
 */
static void llist_add(struct arg_list **old, void *data)
{
  struct arg_list *new = xmalloc(sizeof(struct arg_list));
  new->arg = (char*)data;
  new->next = *old;
  *old = new;
}

/*
 * Creates new node with given DATA and Adds at tail of link list.
 * TODO: its higly recommended that this function is added to the llist.c
 */
static void llist_add_tail(struct arg_list **head, void *data)
{
  while (*head) head = &(*head)->next;
  *head = xzalloc(sizeof(struct arg_list));
  (*head)->arg = (char*)data;
}

/* Reverse list order. */
static struct arg_list *llist_rev(struct arg_list *list)
{
  struct arg_list *rev = NULL;
  while (list) {
    struct arg_list *next = list->next;
    list->next = rev;
    rev = list;
    list = next;
  }
  return rev;
}

/*
 *  Returns module_t from the data base if found NULL otherwise
 *
 *  if ps == 1 then creates module entry and adds it do data base
 *   and also returns it.
 */
static module_t *get_mod(char *mod, uint8_t ps)
{
  char name[MODNAME_LEN];
  module_t *modentry;
  struct arg_list *temp;
  unsigned i, hash = 0;

  path2mod(mod, name);
  for (i = 0; name[i]; i++) hash = ((hash*31) + hash) + name[i];
  hash %= DBASE_SIZE;
  for (temp = TT.dbase[hash]; temp; temp = temp->next) {
    modentry = (module_t*) temp->arg;
    if (strcmp(modentry->name, name) == 0) return modentry;
  }
  if (!ps) return NULL;
  modentry = xzalloc(sizeof(*modentry));
  modentry->name = xstrdup(name);
  llist_add(&TT.dbase[hash], modentry);
  return modentry;
}

/*
 * Reads one line from fl with \ continuation
 * returns allocated string pointer in *li
 * Don't forget to free it :)
 */
static int read_line(FILE *fl, char **li)
{
  char *nxtline = NULL, *line;
  int len, nxtlen, linelen, nxtlinelen;

DROP_LINE:
  line = NULL;
  linelen = nxtlinelen = 0;
  len = getline(&line, (size_t*)&linelen, fl);
  if (len <= 0) return len;
  /* checking for commented lines */
  if(line[0] == '#'){
    free(line);
    goto DROP_LINE;
  }
  for (;;){
    if (line[len - 1] == '\n') len--;
    /* checking line continuation */
    if (len == 0 || line[len - 1] != '\\') break;
    len--;
    nxtlen = getline(&nxtline, (size_t*)&nxtlinelen, fl);
    if (nxtlen <= 0) break;
    if (linelen < len + nxtlen + 1){
      linelen = len + nxtlen + 1;
      line = xrealloc(line, linelen);
    }
    memcpy(&line[len], nxtline, nxtlen);
    len += nxtlen;
  }
  line[len] = '\0';
  *li = xstrdup(line);
  if (line) free(line);
  if (nxtline) free(nxtline);
  return len;
}

/*
 * Action to be taken on all config files in default directories
 * checks for aliases, options, install, remove and blacklist
 */
static int config_action(struct dirtree *node)
{
  FILE *fc;
  char *filename, *tokens[3], *line, *linecp = NULL;
  module_t *modent;
  int tcount = 0;

  if (!dirtree_notdotdot(node)) return 0;
  if (S_ISDIR(node->st.st_mode)) return DIRTREE_RECURSE;

  //process only regular file
  if (S_ISREG(node->st.st_mode)) {
    filename = dirtree_path(node, NULL);
    fc = fopen(filename, "r");
    if (fc == NULL) return 0;
    while (read_line(fc, &line) > 0) {
      char *tk = NULL;
      if (strlen(line) == 0) goto DO_AGAIN;
      linecp = xstrdup(line);
      for (tk = strtok(linecp, "# \t"), tcount = 0; tk; tk = strtok(NULL, "# \t"), tcount++) {
        tokens[tcount] = tk;
        if (tcount == 2) {
          tokens[2] = line + strlen(tokens[0]) + strlen(tokens[1]) + 2;
          break;
        }
      }
      if (tk == NULL) goto DO_AGAIN;
      /* process the tokens[0] contains first word of config line */
      if (strcmp(tokens[0], "alias") == 0) {
        struct arg_list *temp;
        char aliase[MODNAME_LEN], *realname;
        if (tokens[2] == NULL) goto DO_AGAIN;
        path2mod(tokens[1], aliase);
        for (temp = TT.probes; temp; temp = temp->next) {
          modent = (module_t*) temp->arg;
          if (fnmatch(aliase, modent->name, 0) != 0) goto DO_AGAIN;
          realname = path2mod(tokens[2], NULL);
          llist_add(&modent->rnames, realname);
          if (modent->flags & MOD_NDDEPS) {
            modent->flags &= ~MOD_NDDEPS;
            TT.nudeps--;
          }
          modent = get_mod(realname, 1);
          if (!(modent->flags & MOD_NDDEPS)) {
            modent->flags |= MOD_NDDEPS;
            TT.nudeps++;
          }
        }
      } else if (strcmp(tokens[0], "options") == 0) {
        if (tokens[2] == NULL) goto DO_AGAIN;
        modent = get_mod(tokens[1], 1);
        modent->opts = add_opts(modent->opts, tokens[2]);
      } else if (strcmp(tokens[0], "include") == 0) dirtree_read(tokens[1], config_action);
      else if (strcmp(tokens[0], "blacklist") == 0) get_mod(tokens[1], 1)->flags |= MOD_BLACKLIST;
      else if (strcmp(tokens[0], "install") == 0) goto DO_AGAIN;
      else if (strcmp(tokens[0], "remove") == 0) goto DO_AGAIN;
      else error_msg("Invalid option %s found in file %s", tokens[0], filename);

DO_AGAIN:  free(line);
      line = NULL;
      free(linecp);
      linecp = NULL;
    }
    fclose(fc);
    fc = NULL;
  }
  return 0;
}

/*
 *  Reads lines from modules.dep files.
 *  and displays all matching modules and returns 0
 *  only used for -l option.
 */
static int depmode_read_entry(char *cmdname)
{
  char *line, *modname, *name;
  FILE *fe;
  int ret = -1;

  fe = xfopen(DEPMODE_FILE, "r");
  while (read_line(fe, &line) > 0){
    modname = xstrdup(line);
    char *tmp = strchr(modname, ':');
    if (tmp != NULL){
      *tmp = '\0';
      tmp = NULL;
      name = basename(modname);
      tmp = strchr(name, '.');
      if (tmp != NULL) *tmp = '\0';
      if(cmdname == NULL){
    	  if (tmp != NULL) *tmp = '.';
    	  printf("%s\n", modname);
    	  ret = 0;
      }else if (fnmatch(cmdname, name, 0) == 0){
          if (tmp != NULL) *tmp = '.';
          printf("%s\n", modname);
          ret = 0;
      }
    }
    free(modname);
    free(line);
  }
  return ret;
}

/*
 * Finds dependencies for modules ffrom the modules.dep file.
 */
static void find_dep(void)
{
  char *line, *modname;
  FILE *fe;
  module_t *mod;

  fe = xfopen(DEPMODE_FILE, "r");
  while (read_line(fe, &line) > 0){
    modname = xstrdup(line);
    char *tmp = strchr(modname, ':');
    if (tmp != NULL){
      *tmp = '\0';
      mod = get_mod(modname, 0);
      if (mod == NULL){
        free(modname);
        continue;
      }
      if ((mod->flags & MOD_ALOADED) && !(flagChk((FLAG_r | FLAG_D)))){
        free(modname);
        free(line);
        continue;
      }
      mod->flags |= MOD_FNDDEPMOD;
      if ((mod->flags & MOD_NDDEPS) && (mod->dep == NULL)){
        TT.nudeps--;
        llist_add(&mod->dep, xstrdup(modname));
        tmp++;
        if (*tmp){
          char *tok;
          while ((tok = strsep(&tmp, " \t")) != NULL){
            if (tok[0] == '\0') continue;
            llist_add_tail(&mod->dep, xstrdup(tok));
          }
        }
      }
    }
    free(modname);
    free(line);
  }
  fclose(fe);
  fe = NULL;
}

/* Unloads given modules from system
 * if modules == NULL does auto remove.
 */
static int rm_mod(char *modules, uint32_t flags)
{
  errno = 0;
  if(modules){
    int len = strlen(modules);
    if (len > 3 && !strcmp(&modules[len-3], ".ko" )) modules[len-3] = 0;
  }
  if(!flags) flags = O_NONBLOCK|O_EXCL;
  syscall(__NR_delete_module, modules, flags);
  return errno;
}

/*
 * Insert module same as insmod implementation.
 */
static int ins_mod(char *modules, char *flags)
{
  char *buf = NULL;
  int len, res;
  int fd = xopen(modules, O_RDONLY);

  len = fdlength(fd);
  buf = xmalloc(len);
  xreadall(fd, buf, len);
  close(fd);

  while (flags && strlen(toybuf) + strlen(flags) + 2 < sizeof(toybuf)){
    strcat(toybuf, flags);
    strcat(toybuf, " ");
  }
  res = syscall(__NR_init_module, buf, len, toybuf);
  if (CFG_TOYBOX_FREE && buf != toybuf) free(buf);
  if (res) perror_exit("failed to load %s ", toys.optargs[0]);
  return res;
}

/*
 * Adds module_t entry by name in probes list
 * and sets other variables as needed.
 */
static void add_mod(char *name)
{
  module_t *mod;
  mod = get_mod(name, 1);
  if (!(flagChk((FLAG_r | FLAG_D))) && (mod->flags & MOD_ALOADED)){
    dbg("skipping %s, it is already loaded\n", name);
    return;
  }
  dbg("queuing %s\n", name);
  mod->cmdname = name;
  mod->flags |= MOD_NDDEPS;
  llist_add_tail(&TT.probes, mod);
  TT.nudeps++;
  if (CONFIG_MODUTILS_SYMBOLS&& strncmp(mod->name, "symbol:", 7) == 0) TT.symreq = 1;
}

/*
 * Parse cmdline options suplied for module
 */
static char *add_cmdopt(char **argv)
{
  char *opt = xzalloc(1);;
  int lopt = 0;

  while (*++argv){
    const char *fmt, *var, *val;
    var = *argv;
    opt = xrealloc(opt, lopt + 2 + strlen(var) + 2);
    /* chking for key=val or key = val */
    fmt = "%.*s%s ";
    val = strchrnul((char *)var, '=');
    if (*val){
      val++;
      if (strchr(val, ' ')) fmt = "%.*s\"%s\" ";
    }
    lopt += sprintf(opt + lopt, fmt, (int) (val - var), var, val);
  }
  return opt;
}

/*
 * Probes a single module and loads all its dependencies
 */
static int go_probe(module_t *m)
{
  int rc = 0, first = 1;

  if (!(m->flags & MOD_FNDDEPMOD)) {
    if (!flagChk(FLAG_s)) error_msg("module %s not found in modules.dep", m->name);
    return -ENOENT;
  }
  dbg("go_prob'ing %s\n", m->name);
  if (!flagChk(FLAG_r)) m->dep = llist_rev(m->dep);
  while (m->dep) {
    module_t *m2;
    char *fn, *options;
    rc = 0;
    fn = llist_popme(&m->dep);
    m2 = get_mod(fn, 1);
    /* are we removing ? */
    if (flagChk(FLAG_r)) {
      if (m2->flags & MOD_ALOADED) {
        rc = rm_mod(m2->name, O_EXCL);
        if (rc) {
          if (first) {
            perror_msg("can't unload module %s", m2->name);
            break;
          }
        } else m2->flags &= ~MOD_ALOADED;
      }
      first = 0;
      continue;
    }
    options = m2->opts;
    m2->opts = NULL;
    /* are we only checking dependencies ? */
    if (flagChk(FLAG_D)) {
      dbg(options ? "insmod %s %s\n" : "insmod %s\n", fn, options);
      if(options) free(options);
      continue;
    }
    if (m2->flags & MOD_ALOADED) {
      dbg("%s is already loaded, skipping\n", fn);
      if(options) free(options);
      continue;
    }
    /* none of above is true insert the module. */
    rc = ins_mod(fn, options);
    dbg("loaded %s '%s', rc:%d\n", fn, options, rc);
    if (rc == EEXIST) rc = 0;
    if(options) free(options);
    if (rc) {
      perror_msg("can't load module %s (%s)", m2->name, fn);
      break;
    }
    m2->flags |= MOD_ALOADED;
  }
  return rc;
}

/*
 * Main function for modeprobe.
 */
void modprobe_main(void)
{
  struct utsname uts;
  char **argv = toys.optargs;
  FILE *fs;
  module_t *module;

  dbg = dummy;
  if(flagChk(FLAG_v)) dbg = xprintf;

  if ((toys.optc < 1) && (((flagChk(FLAG_r))&&(flagChk(FLAG_l)))||(!((flagChk(FLAG_r))||(flagChk(FLAG_l)))))) {
	  toys.exithelp++;
	  error_exit(" Syntex Error.");
  }
  /* Check for -r flag without arg if yes then do auto remove */
  if ((flagChk(FLAG_r)) && (toys.optc == 0)){
    if (rm_mod(NULL, O_NONBLOCK | O_EXCL) != 0)	perror_exit("rmmod");
    return;
  }

  /* change directory to /lib/modules/<release>/ */
  xchdir(MODULES_DIR);
  uname(&uts);
  xchdir(uts.release);

  /* modules.dep processing for dependency check.*/
  if (flagChk(FLAG_l)){
    if (depmode_read_entry(toys.optargs[0]) == -1) error_exit("no module found.");
    return;
  }
  /* Read /proc/modules to get loadded modules. */
  fs = xfopen("/proc/modules", "r");
  char *procline = NULL;
  while (read_line(fs, &procline) > 0){
    *(strchr(procline, ' ')) = '\0';
    get_mod(procline, 1)->flags = MOD_ALOADED;
    free(procline);
    procline = NULL;
  }
  fclose(fs);
  fs = NULL;
  if (flagChk(FLAG_a) || flagChk(FLAG_r)) {
    do{
      add_mod(*argv++);
    } while (*argv);
  } else {
    add_mod(argv[0]);
    TT.cmdopts = add_cmdopt(argv);
  }
  if(TT.probes == NULL){
    error_msg("All modules loaded successfully. ");
    return;
  }
  dirtree_read("/etc/modprobe.conf", config_action);
  dirtree_read("/etc/modprobe.d", config_action);
  if (CONFIG_MODUTILS_SYMBOLS && TT.symreq) dirtree_read("modules.symbols", config_action);
  if (CONFIG_MODUTILS_ALIAS && TT.nudeps) dirtree_read("modules.alias", config_action);
  find_dep();
  while ((module = llist_popme(&TT.probes)) != NULL) {
    if (module->rnames == NULL) {
      dbg("probing by module name\n");
      /* This is not an alias. Literal names are blacklisted
       * only if '-b' is given.
       */
      if (!(flagChk(FLAG_b)) || !(module->flags & MOD_BLACKLIST)) go_probe(module);
      continue;
    }
    do { /* Probe all real names for the alias */
      char *real = llist_pop(&module->rnames);
      module_t *m2;
      dbg("probing alias %s by realname %s\n", module->name, real);
      m2 = get_mod(real, 0);
      if(m2 == NULL) continue;
      if (!(m2->flags & MOD_BLACKLIST) && (!(m2->flags & MOD_ALOADED) || (flagChk((FLAG_r | FLAG_D)))))
        go_probe(m2);
      free(real);
    } while (module->rnames != NULL);
  }
}
