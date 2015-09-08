/* lsattr.c - List file attributes on a Linux second extended file system.
 *
 * Copyright 2013 Ranjan Kumar <ranjankumar.bth@gmail.com>
 *
 * Not in SUSv4.
 * 
USE_LSATTR(NEWTOY(lsattr, "vldaR", TOYFLAG_BIN))

config LSATTR
  bool "lsattr"
  default y
  help
    usage: lsattr [-Radlv] [Files...]

    List file attributes on a Linux second extended file system.

    -R Recursively list attributes of directories and their contents.
    -a List all files in directories, including files that start with `.'.
    -d List directories like other files, rather than listing their contents.
    -l List long flag names.
    -v List the file's version/generation number.
*/
#define FOR_lsattr

#include "toys.h"

static const EXT2_ATTRS toys_ext2_attrs[] = {
	{"Secure_Deletion",               EXT2_SECRM_FL,        's'},
	{"Undelete",                      EXT2_UNRM_FL,         'u'},
	{"Compression_Requested",         EXT2_COMPR_FL,        'c'},
	{"Synchronous_Updates",           EXT2_SYNC_FL,         'S'},
	{"Immutable",                     EXT2_IMMUTABLE_FL,    'i'},
	{"Append_Only",                   EXT2_APPEND_FL,       'a'},
	{"No_Dump",                       EXT2_NODUMP_FL,       'd'},
	{"No_Atime",                      EXT2_NOATIME_FL,      'A'},
	{"Indexed_directory",             EXT2_INDEX_FL,        'I'},
	{"Journaled_Data",                EXT3_JOURNAL_DATA_FL, 'j'},
	{"No_Tailmerging",                EXT2_NOTAIL_FL,       't'},
	{"Synchronous_Directory_Updates", EXT2_DIRSYNC_FL,      'D'},
	{"Top_of_Directory_Hierarchies",  EXT2_TOPDIR_FL,       'T'},
	{NULL,                            -1,                    0},
};

/*
 * Print the flag's name based on the flag values.
 */
static void print_flag_name(unsigned long fileflag)
{
  int name_found = 0;
  const EXT2_ATTRS *ptr_to_ext2_attr = toys_ext2_attrs;

  while(ptr_to_ext2_attr->name) {
    if(fileflag & ptr_to_ext2_attr->inode_flag) {
      if(name_found) xprintf(", ");
      xprintf("%s", ptr_to_ext2_attr->name);
      name_found = 1;
    }
    ptr_to_ext2_attr++;
  }
  if(!name_found) xprintf("---");
  xprintf("\n");
  return;
}

/*
 * Accumulate flag characters and return it's as string.
 */
static char *get_flag_str(unsigned long fileflag)
{
  char *flag_str = NULL;
  int index = 0;
  const EXT2_ATTRS *ptr_to_ext2_attr = toys_ext2_attrs;
  unsigned int flag_str_len = sizeof(toys_ext2_attrs)/sizeof(toys_ext2_attrs[0]);

  flag_str = xzalloc(flag_str_len + 1);
  while(ptr_to_ext2_attr->name) {
    if(fileflag & ptr_to_ext2_attr->inode_flag) flag_str[index++] = ptr_to_ext2_attr->optchar;
    else flag_str[index++] = '-';
    ptr_to_ext2_attr++;
  }
  return flag_str;
}

/*
 * Print file attributes.
 */
static void print_file_attr(char *path)
{
  unsigned long fileflag = 0;
  unsigned long version = 0;

  int fd = -1;
  struct stat sb;

  if(!stat(path, &sb) && !S_ISREG(sb.st_mode) && !S_ISDIR(sb.st_mode)) {
    errno = EOPNOTSUPP;
    perror_msg("reading '%s'", path);
    return;
  }

  if(-1 == (fd=open(path, O_RDONLY | O_NONBLOCK))) {
    perror_msg("reading '%s'", path);
    return;
  }

  if(toys.optflags & FLAG_v) {
    if(get_e2fs_version(fd, &version) == -1) {
      perror_msg("reading %s", path);
      goto ERROR;
    }
    xprintf("%5lu ", version);
  }

  if(get_e2fs_flag(fd, &sb, &fileflag) == -1) {
    perror_msg("while reading flags on '%s'", path);
    goto ERROR;
  }
  else {
    if(toys.optflags & FLAG_l) {
      xprintf("%-50s ", path);
      print_flag_name(fileflag);
    }
    else {
      char *flag_str = get_flag_str(fileflag);
      xprintf("%s %s\n", flag_str, path);
      free(flag_str);
    }
  }
ERROR:
  xclose(fd);
  return;
}

/*
 * Get directory information.
 */
static int retell_dir(struct dirtree *root)
{
  char *fpath = NULL;

  if(toys.optflags & FLAG_d) {
    fpath = dirtree_path(root, NULL);
    print_file_attr(fpath);
    free(fpath);
    return 0;
  }

  if(root->data == -1) {
    xputc('\n');
    return 0;
  }

  if(S_ISDIR(root->st.st_mode) && (root->parent == NULL)) return (DIRTREE_RECURSE | DIRTREE_COMEAGAIN);

  fpath = dirtree_path(root, NULL);
  //Special case: with '-a' option and '.'/'..' also included in the printing list.
  if( (root->name[0] != '.') || (toys.optflags & FLAG_a) ) {
    print_file_attr(fpath);

    if(S_ISDIR(root->st.st_mode) && (toys.optflags & FLAG_R) && dirtree_notdotdot(root) ) {
      xprintf("\n%s:\n", fpath);
      free(fpath);
      return (DIRTREE_RECURSE | DIRTREE_COMEAGAIN);
    }
  }
  free(fpath);
  return 0;
}

/*
 * lsattr main function.
 */
void lsattr_main(void)
{
  char **argv = toys.optargs;
  char *tmp = NULL;
  int allocflag = 0;
  if(!*argv) {
    tmp = *argv = xstrdup(".");
    allocflag = 1;
  }
  while (*argv) {
    dirtree_read(*argv, retell_dir);
    argv++;
  }
  if(allocflag) free(tmp);
  return;
}
