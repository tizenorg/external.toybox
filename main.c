/* Toybox infrastructure.
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 */

#include "toys.h"

// Populate toy_list[].

#undef NEWTOY
#undef OLDTOY
#define NEWTOY(name, opts, flags) {#name, name##_main, opts, flags},
#define OLDTOY(name, oldname, opts, flags) {#name, oldname##_main, opts, flags},

struct toy_list toy_list[] = {
#include "generated/newtoys.h"
};

// global context for this command.

struct toy_context toys;
union global_union this;
char toybuf[4096];

struct toy_list *toy_find(char *name)
{
  int top, bottom, middle;

  // If the name starts with "toybox" accept that as a match.  Otherwise
  // skip the first entry, which is out of order.

  if (!strncmp(name,"toybox",6)) return toy_list;
  bottom = 1;

  // Binary search to find this command.

  top = ARRAY_LEN(toy_list)-1;
  for (;;) {
    int result;

    middle = (top+bottom)/2;
    if (middle<bottom || middle>top) return NULL;
    result = strcmp(name,toy_list[middle].name);
    if (!result) return toy_list+middle;
    if (result<0) top=--middle;
    else bottom = ++middle;
  }
}

// Figure out whether or not anything is using the option parsing logic,
// because the compiler can't figure out whether or not to optimize it away
// on its' own.  NEED_OPTIONS becomes a constant allowing if() to optimize
// stuff out via dead code elimination.

#undef NEWTOY
#undef OLDTOY
#define NEWTOY(name, opts, flags) opts ||
#define OLDTOY(name, oldname, opts, flags) opts ||
static const int NEED_OPTIONS =
#include "generated/newtoys.h"
0;  // Ends the opts || opts || opts...

// Setup toybox global state for this command.

void toy_init(struct toy_list *which, char *argv[])
{
  // Drop permissions for non-suid commands.

  if (CFG_TOYBOX_SUID) {
    uid_t uid = getuid(), euid = geteuid();

    if (!(which->flags & TOYFLAG_STAYROOT)) {
      if (uid != euid) xsetuid(euid=uid);
    } else if (CFG_TOYBOX_DEBUG && uid && which != toy_list)
      error_msg("Not installed suid root");

    if ((which->flags & TOYFLAG_NEEDROOT) && euid) error_exit("Not root");
  }

  // Free old toys contents (to be reentrant), but leave rebound if any

  if (toys.optargs != toys.argv+1) free(toys.optargs);
  memset(&toys, 0, offsetof(struct toy_context, rebound));

  toys.which = which;
  toys.argv = argv;
  if (NEED_OPTIONS && which->options) get_optflags();
  else {
    toys.optargs = argv+1;
    for (toys.optc=0; toys.optargs[toys.optc]; toys.optc++);
  }
  toys.old_umask = umask(0);
  if (!(which->flags & TOYFLAG_UMASK)) umask(toys.old_umask);
}

// Like exec() but runs an internal toybox command instead of another file.
// Only returns if it can't find the command, otherwise exit() when done.
void toy_exec(char *argv[])
{
  struct toy_list *which;

  if (!(which = toy_find(argv[0]))) return;
  toy_init(which, argv);
  toys.which->toy_main();
  if (fflush(NULL) || ferror(stdout)) perror_exit("write");
  exit(toys.exitval);
}

// Multiplexer command, first argument is command to run, rest are args to that.
// If first argument starts with - output list of command install paths.

void toybox_main(void)
{
  static char *toy_paths[]={"usr/","bin/","sbin/",0};
  int i, len = 0;

  toys.which = toy_list;
  if (toys.argv[1]) {
    if (CFG_TOYBOX_HELP && !strcmp(toys.argv[1], "--help")) {
      if (toys.argv[2]) toys.which = toy_find(toys.argv[2]);
      if (toys.which) {
        show_help();
        return;
      }
    } else {
      toy_exec(toys.argv+1);
      if (toys.argv[1][0] == '-') goto list;
    }
    
    error_exit("Unknown command %s",toys.argv[1]);
  }

list:
  // Output list of command.
  for (i=1; i<ARRAY_LEN(toy_list); i++) {
    int fl = toy_list[i].flags;
    if (fl & TOYMASK_LOCATION) {
      if (toys.argv[1]) {
        int j;
        for (j=0; toy_paths[j]; j++)
          if (fl & (1<<j)) len += printf("%s", toy_paths[j]);
      }
      len += printf("%s ",toy_list[i].name);
      if (len>65) {
        xputc('\n');
        len=0;
      }
    }
  }
  xputc('\n');
}

int main(int argc, char *argv[])
{
  if (CFG_TOYBOX_I18N) setlocale(LC_ALL, "");

  // Trim path off of command name
  *argv = basename(*argv);

  // Call the multiplexer, adjusting this argv[] to be its' argv[1].
  // (It will adjust it back before calling toy_exec().)
  toys.argv = argv-1;
  toybox_main();
  return 0;
}
