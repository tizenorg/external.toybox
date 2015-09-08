/* mv.c - Move file / Directory.
 *
 * Copyright 2012 Ranjan Kumar <ranjankumar.bth@gmail.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/mv.html
 *
USE_MV(NEWTOY(mv, "<2fin[-fin]", TOYFLAG_BIN))

config MV
  bool "mv"
  default y
  help
    usage: mv [-fin] SOURCE DEST
       mv [-fin] SOURCE... DIRECTORY

    Rename SOURCE to DEST or Move SOURCES(s) to DIRECTORY.

    -f  Don't Prompt before overwrite
    -i  interactive, prompt before overwrite
    -n  Don't overwrite an existing file
*/

#define FOR_mv
#include "toys.h"

GLOBALS(
  char *destname;
  char *srcname;
  int destisdir;
  int keep_symlinks;
  struct arg_list *link_nodes;
)

typedef struct link_node_ent {
  ino_t ino;
  dev_t dev;
  char *name;
} link_node_ent_t;

/*
 * free the inodes which are stored for hard link reference
 */
static void free_link_nodes(void *data)
{ 
  void *arg = ((struct arg_list*)data)->arg;
  if (arg) free(arg);
  free(data);
} 

/*
 * allocate and add a node to the list
 */
static void llist_add_link_node(struct arg_list **old, void *data)
{ 
  struct arg_list *new = xmalloc(sizeof(struct arg_list));
  new->arg = (char*)data;
  new->next = *old;
  *old = new;
}

/* 
 * check if the given stat entry is already there in list or not
 */
static char *is_link_node_present(struct stat *st)
{  
  struct arg_list *temparg = NULL;
  link_node_ent_t *ent = NULL;
  if (!TT.link_nodes) return NULL;
  for (temparg = TT.link_nodes; temparg; temparg = (struct arg_list *)temparg->next) {
    ent = (link_node_ent_t *)temparg->arg;
    if (ent && ent->ino == st->st_ino && ent->dev == st->st_dev) return ent->name;
  }
  return NULL;
}

/*
 * Copy an individual file or directory to target.
 */
static void mv_file(char *src, char *dst, struct stat *srcst)
{
  int fdout = -1;
  struct stat std;

  if (!stat(TT.destname, &std) &&
      (srcst->st_dev == std.st_dev && srcst->st_ino == std.st_ino))
    error_exit("recursion bad source '%s'", src);

  if (S_ISDIR(srcst->st_mode)) {//Copy directory or file to destination.
    struct stat st2;
    /* Always make directory writeable to us, so we can create files in it.
     * Yes, there's a race window between mkdir() and open(). So make sure
     * that what we open _is_ a directory rather than something else.
     * Can't do fchmod() etc. here?
     */
    if ((mkdir(dst, srcst->st_mode | 0200) && errno != EEXIST)
        || 0>(fdout = open(dst, 0)) || fstat(fdout, &st2) || !S_ISDIR(st2.st_mode))
      perror_exit("mkdir '%s'", dst);

  } else if (TT.keep_symlinks && S_ISLNK(srcst->st_mode)) {
    char *link = xreadlink(src);
    //How do we get a filehandle to them?  O_NOFOLLOW causes the open to fail.
    if (!link || symlink(link, dst)) perror_msg("link '%s'", dst);
    free(link);
    return;
  } else {
    int fdin, i;
    link_node_ent_t *ino_details = NULL;
    //if present then link and return
    char *link_name = is_link_node_present(srcst);
    if (link_name) {
      if ((i = link(link_name, dst))) {
        if (errno == EEXIST && (toys.optflags & FLAG_f)) unlink(dst);
        if (i) perror_msg("link '%s'", dst);
      }
      return;
    }

    ino_details = xzalloc(sizeof(link_node_ent_t));
    ino_details->ino = srcst->st_ino;
    ino_details->dev = srcst->st_dev;
    ino_details->name = xstrdup(dst);
    llist_add_link_node(&TT.link_nodes, (void*)ino_details);

    fdin = xopen(src, O_RDONLY);
    for (i = 2 ; i; i--) {
      fdout = open(dst, O_RDWR|O_CREAT|O_TRUNC, srcst->st_mode);
      if (fdout >= 0 || !(toys.optflags & FLAG_f)) break;
      unlink(dst);
    }
    if (fdout < 0) perror_exit("%s", dst);
    xsendfile(fdin, fdout);
    xclose(fdin);
    unlink(src);
  }
  xclose(fdout);
}

/*
 * Callback from dirtree_read() for each file/directory under a source dir.
 */
static int mv_node(struct dirtree *node)
{
  int len = 0;
  char *s = NULL, *path = dirtree_path(node, 0);

  if (!dirtree_notdotdot(node)) return 0;

  s = path + strlen(TT.srcname); //get the actual file/dir to be copied
  len = strlen(TT.destname);
  if (len && TT.destname[len - 1] != '/')
    s = xmsprintf("%s/%s", TT.destname, ((*s == '/') ? s+1 : s));
  else s = xmsprintf("%s%s", TT.destname, ((*s == '/') ? s+1 : s));
  if (node->data == -1) rmdir(path);
  else mv_file(path, s, &(node->st));
  free(s);
  free(path); 
  return (DIRTREE_RECURSE | ((node->data == -1) ? 0 : DIRTREE_COMEAGAIN));
}

/*
 * Move source file/dir from source to destination.
 */
static int do_move(char *src, char *dst) 
{
  struct stat sb;
  /* Conditions (1):
   * When dest path exists and '-f' option is not set:
   *   (a) The permissions of the dest path do not permit writing & the stdin is a terminal.
   *   (b) The '-i' option is set.
   * The mv utility shall write a prompt to stderr & read a line from stdin.
   * If the response is not affirmative, mv shall do nothing more with the current src file.
   */
  if ( !(toys.optflags & FLAG_f)  && !access(dst, F_OK) ) {
    int ask = 1;
    if (toys.optflags & FLAG_n) return 0;
    if (toys.optflags & FLAG_i) {
      if (access(src, F_OK)) {
        perror_msg("rename '%s'", src);
        return 1;
      }
      (void)fprintf(stderr, "overwrite %s? ", dst);
    } else if (access(dst, W_OK) && (!stat(dst, &sb)) ) {
      if (access(src, F_OK)) {
        perror_msg("rename '%s'", src);
        return 1;
      }
      (void)fprintf(stderr, "override %ud/%ud for %s? ", sb.st_uid, sb.st_gid, dst);
    } else ask = 0;
    if (ask) {
      if (!yesno("", 0)) return 0;
    }
  }
  /* Conditions (2 & 3):
   * When rename() succeeds, mv shall do nothing more with the current src file.
   *   If it fails for any other reason than EXDEV, mv shall write a diagnostic
   *   message to the stderr and do nothing more with the current src file.
   * When dest path exists and it is a directory; where as src file is not a directory,
   *    or src file is a file of type directory, mv shall write a diagnostic message
   *    to stderr and do nothing more with the current src file.
   */
  if (!rename(src, dst)) return 0;
  else if (errno != EXDEV) {
    perror_msg("'%s' to '%s failed'",src, dst);
    return 1;
  }

  /* Conditions (4):
   * When dest path exists, mv shall attempt to remove it. If it fails for any
   *   reason, mv shall write a diagnostic message to the stderr and do nothing
   *   more with the current src file.
   */
  if (!lstat(dst, &sb) && unlink(dst)) {
    perror_msg("cannot remove '%s'", dst);
    return 1;
  }

  /* Conditions (5):
   * The file hierarchy rooted in src file shall be duplicated as a file
   *   hierarchy rooted in the dest path.
   */
  if (lstat(src, &sb)) {
    perror_msg("'%s'", src);
    return 1;
  }

  if (S_ISDIR(sb.st_mode)) {
    mv_file(src, dst, &sb);
    struct dirtree *root = dirtree_add_node(0, src, 0);
    if (root) dirtree_handle_callback(root, mv_node);
  } else mv_file(src, dst, &sb);
  return 0;
}

/*
 * Move main function.
 */
void mv_main(void)
{
  struct stat stc, std;
  int i, len;

  TT.destname = toys.optargs[--toys.optc];

  if (!stat(TT.destname, &std) && S_ISDIR(std.st_mode)) TT.destisdir++;
  else if (toys.optc > 1) error_exit("'%s' not directory", TT.destname);

  //Loop through each src file.
  for (i = 0; i < toys.optc; i++) {
    char *dst = NULL, *src = toys.optargs[i];
    //Skip nonexistent sources.
    TT.keep_symlinks = 1;

    //Skip when src=dest
    if (!strcmp(src, TT.destname)) {
      error_msg("'%s' and '%s' are same", src, TT.destname);
      continue;
    }

    if (TT.keep_symlinks ? lstat(src, &stc) : stat(src, &stc)
        || (stc.st_dev == std.st_dev && stc.st_ino == std.st_ino)) {
      perror_msg("bad source '%s'", src);
      continue;
    }

    len = strlen(src);
    while (len && src[--len] == '/') src[len] = '\0';

    dst = strrchr(src, '/');
    if (dst && TT.destisdir) TT.srcname = xstrndup(src, (dst - src + 1));
    else if (TT.destisdir)  TT.srcname = "";
    else TT.srcname = xstrdup(src);

    //Copy directory or file.
    if (TT.destisdir) {
      dst = src + strlen(TT.srcname);
      len = strlen(TT.destname);
      if (len && TT.destname[len - 1] != '/')
        dst = xmsprintf("%s/%s", TT.destname, dst);
      else dst = xmsprintf("%s%s", TT.destname, dst);
    } else dst = TT.destname;

    toys.exitval = do_move(src, dst);
    if (TT.destisdir) free(dst);
  }

  if (CFG_TOYBOX_FREE) {
    if (TT.link_nodes) llist_traverse(TT.link_nodes, free_link_nodes);
  }
}
