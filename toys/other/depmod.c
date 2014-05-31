/* depmod.c - Outputs a dependancy list file suitable for the modprobe utility.
 * 
 * Copyright 2012 Sandeep Sharma <sandeep_2756@yahoo.com>
 *
 * Not in SUSv4.

USE_DEPMOD(NEWTOY(depmod, "b:na",TOYFLAG_NEEDROOT|TOYFLAG_SBIN))

config DEPMOD
       bool "depmod"
       default y
       help
          usage: depmod [-a][-n] [-b basedir] version MODULE...
          -a          Probe all modules, Default
          -n          Write the dependency file on stdout only 
          -b Basedir  Basedir needed  when modules are in staging state
*/
#define FOR_depmod
#include "toys.h"
#include <sys/utsname.h> /* For uname()--version */
/*
 * Single module strcuture.
 */
struct module {
  struct module *next;
  char *name, *modname;
  struct double_list *deps;
  struct double_list *aliases;
  struct double_list *sym;
  struct module *next_dep; //updated while ordering deps.
};

/* Macros */
#define MODULE_NAME_LEN 256
#define DFT_BASE_DIR "/lib/modules/"

GLOBALS(
    char *b_options;
    struct module *modules;
)

/*
 * Get base name from the input name.
 */
static const char *get_basename(char *name)
{
  const char *c = strrchr(name, '/');
  if (c) return c + 1;
  return name;
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

/*
 * free all the list nodes on error OR on exit
 */
static void free_list(struct double_list* list)
{      
  struct double_list *tmp = NULL;
  if(!list) return;
  list->prev->next = NULL;
  tmp = list;
  while(tmp) {
    list=list->next;
    if(tmp->data) free(tmp->data);
    free(tmp);
    tmp = list;
  }    
  return;  
}   


/*
 * Find any module from the module list.
 */
static struct module *find_module(struct module *modules, const char *modname)
{
  struct module *m;
  for (m = modules; m != NULL; m = m->next) {
    if (strcmp(m->modname, modname) == 0)
      return m;
  }
  return NULL;
}
/*
 * Update the dep list by updating next_dep pointer.
 * List deps one after other and if next dep is already present in existing
 * dep list unlink it from list and add it to tail.
 */
static struct module* reorder_deps(struct module *modules, struct module *start, struct module *deps_mod)
{   
  struct module *m, *temp;
  struct double_list *n;
  n = deps_mod->deps;
  n->prev->next = NULL; //Break double list so that we traverse like singly linked list
  for(; n != NULL; n = n->next) {
    m = find_module(modules, n->data);
    if(m == NULL) return deps_mod;
    if(m->next_dep != NULL) {  //already in the list
      temp = start; //start from start. 
      while(temp->next_dep) { //Find already present, unlink it.
        if(temp->next_dep == m) {
          temp->next_dep = m->next_dep;
          m->next_dep = NULL;
          break;
        }
        temp = temp->next_dep;
      }
    }
    deps_mod->next_dep = m; //Update dep pointer to link it in dep list.
    m->next_dep = NULL;
    deps_mod = m;
    deps_mod = reorder_deps(modules, start, deps_mod); //recurse over deps.
  }
  return deps_mod;
}
/*
 * Seprate string with "," as token and create double list of tokens.
 */
static int string_tokenise(char *str, struct double_list **llist, char *token)
{
  char *ptr, *start;
  char *val = NULL;
  int token_lenth = 0;

  if(!str) {
    perror_msg("error: ");
    return 0;
  }  
  start = str;
  while((ptr = strsep(&start, token)) != NULL) {  
    val = xstrdup(ptr);
    dlist_add(llist, val);
    token_lenth += strlen(ptr);
  }
  return token_lenth;
}
/*
 * Read each module and create dep,sym and alias list for each module.
 */
static void  parse_module(struct dirtree *new)
{
  char modname[MODULE_NAME_LEN];
  char *image, *temptr;
  int fd;
  char *image_index;
  struct module *info;
  off_t len;
  char *fname, *full_path;

  struct module  **first = &TT.modules; //List each module.
  full_path = dirtree_path(new, NULL);
  fname = full_path;

  if(*fname == '.') fname += 2; // skip .// else keep path as /path/to/modules/
  fd = open(full_path, O_RDONLY); // Don't want to die if we are not able to open few modules.
  if(fd < 0) {
    perror_msg("error:");
    return ;
  }
  len = fdlength(fd); //calculate length of module
  xclose(fd);
  image = xreadfile(full_path); // readfile or die 

  info = xzalloc(sizeof(struct module));
  info->next = *first;
  *first = info;
  info->next_dep = NULL; //keep dep pointer NULL, will modify at ordering time.
  info->name = xstrdup(fname);
  info->modname = xstrdup(path2mod(fname, modname));
  image_index = image + len;
  for (temptr = image; temptr < image_index; temptr++) {
    if (strncmp(temptr, "depends=", 8) == 0) { // search for keyword "depends="  
      char *p;
      temptr += 8;
      for (p = temptr; *p; p++)
        if (*p == '-')
          *p = '_';
      temptr += string_tokenise(temptr, &info->deps, ","); //list the deps as string tokens
    }
    if(strncmp(temptr, "alias=", 6) == 0) { //list aliases
      dlist_add(&info->aliases, xstrdup(temptr + 6));
      temptr += strlen(temptr);
    } 
    if(strncmp(temptr, "__ksymtab_", 10) == 0) { //list symbols
      temptr += 10;
      if (strncmp(temptr, "gpl", 3) == 0 || strcmp(temptr, "strings") == 0) continue;
      dlist_add(&info->sym, xstrdup(temptr));
      temptr += strlen(temptr);
    }
  }
  free(image);
  image = NULL;
  return ;
}
/*
 * Callback function. Return if not module else parse.
 */
static int check_module(struct dirtree *new)
{
  if(strstr(new->name, ".ko") != NULL) parse_module(new);
  if(dirtree_notdotdot(new)) return DIRTREE_RECURSE; //Don't want DIRTREE_SAVE
  return 0;
}
/*
 * concatenate string and path
 */
static char *concat_file_path(char *path, char *default_path)
{
  char *str;
  if('/' == path[strlen(path) - 1]) {
    while(*default_path == '/') ++default_path;
    str = xmsprintf("%s%s", path, default_path);
  }
  else if(*default_path != '/') str = xmsprintf("%s/%s", path, default_path); 
  else str = xmsprintf("%s%s", path, default_path); 
  if(str) return str;
  else perror_msg("error: "); 
  return NULL;
}
/*
 * Depmod main function.
 */
void depmod_main(void)
{
  struct module *m, *dep, *temp_ptr;
  struct double_list *llist, *ptr; 
  char *version;
  char *base_dir, *mod_dir;
  int temp, i;
  struct utsname uts_name;

  base_dir = DFT_BASE_DIR; //set the basedirectory to default base dir
  if(toys.optargs[0] != NULL && sscanf(toys.optargs[0], "%u.%u.%u", &temp, &temp, &temp) == 3) version = toys.optargs[0];
  else { 
    uname(&uts_name);
    version = uts_name.release;
  }
  if(toys.optflags & FLAG_b) {
    if(!(base_dir = concat_file_path(TT.b_options, DFT_BASE_DIR)))  // -b, Add staging state dir to DFT_BASE_DIR.
      perror_exit("error:");
  }
  if(!(mod_dir = concat_file_path(base_dir, version))) perror_exit("error:");
  else xchdir(mod_dir);

  if(toys.optargs[0] != NULL && sscanf(toys.optargs[0], "%u.%u.%u", &temp, &temp, &temp) != 3) {
    for(i = 0; toys.optargs[i] != NULL; i++) {
      if(toys.optargs[0][0] != '/') perror_exit("%s is a relative path\n", toys.optargs[0]);
      else dirtree_read(toys.optargs[i], check_module);
    }
  }
  else if(toys.optargs[1] != NULL) { 
    for(i = 1;toys.optargs[i] != NULL; i++) {
      if(toys.optargs[1][0] != '/') perror_exit("%s is a relative path\n", toys.optargs[1]);
      else dirtree_read(toys.optargs[i], check_module);
    }
  }
  else dirtree_read("./", check_module); //Recurse from present dir

  /*generate deps*/
  if(!(toys.optflags & FLAG_n)) {
    if(freopen("modules.dep", "w", stdout) == NULL) {
      perror_msg("Could not open modules.dep: ");
      goto CLEAN_EXIT;
    }
  }
  for(m = TT.modules; m != NULL; m = m->next) {
    printf("%s:", m->name);
    temp_ptr = m;
    reorder_deps(TT.modules, m, m);
    while (temp_ptr->next_dep != NULL) {
      dep = temp_ptr->next_dep;
      printf(" %s", dep->name);
      temp_ptr->next_dep = NULL; //Unlink just printed dep 
      temp_ptr = dep;
    }
    xputc('\n');
  }
  /*genrate aliases*/
  if(!(toys.optflags & FLAG_n)) {
    if(freopen("modules.alias", "w", stdout) == NULL) {
      perror_msg("Could not open modules.alias: ");
      goto CLEAN_EXIT;
    }
  }
  for(m = TT.modules; m != NULL; m = m->next) {
    llist = m->aliases;
    ptr = llist;
    while(llist) {
      const char *f = get_basename(m->name);
      int l = strchr(f, '.') - f;
      printf("alias %s %.*s\n", ptr->data, l, f);
      ptr = ptr->next;
      if(ptr == llist) break;
    }
  }      
  /*generate symbols*/
  if(!(toys.optflags & FLAG_n)) {
    if(freopen("modules.sym", "w", stdout) == NULL) {
      perror_msg("Could not open modules.sym: ");
      goto CLEAN_EXIT;
    }
  }
  for(m = TT.modules; m != NULL; m = m->next) {
    llist = m->sym;
    ptr = llist;
    while(llist) {
      const char *f = get_basename(m->name);
      int l = strchr(f, '.') - f;
      printf("alias symbol %s %.*s\n", ptr->data, l, f);
      ptr = ptr->next;
      if(ptr == llist) break;
    }
  }
CLEAN_EXIT:
  while(TT.modules) {
    struct module *old = TT.modules;
    TT.modules = TT.modules->next;
    free_list(old->deps);
    free_list(old->aliases);
    free_list(old->sym);
    free(old->name);
    free(old->modname);
    free(old);
  }
}
