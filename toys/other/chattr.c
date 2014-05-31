/* chattr.c - Change file attributes on a Linux second extended file system.
 *
 * Copyright 2013 Ranjan Kumar <ranjankumar.bth@gmail.com>
 *
 * Not in SUSv4.
 * 
USE_CHATTR(NEWTOY(chattr, NULL, TOYFLAG_BIN))

config CHATTR
  bool "chattr"
  default y
  help
    usage: chattr [-R] [-+=AacDdijsStTu] [-v version] [Files...]

    Change file attributes on a Linux second extended file system.

    Operators:
      '-' Remove attributes.
      '+' Add attributes.
      '=' Set attributes.

    Attributes:
      A  Don't track atime.
      a  Append mode only.
      c  Enable compress.
      D  Write dir contents synchronously.
      d  Don't backup with dump.
      i  Cannot be modified (immutable).
      j  Write all data to journal first.
      s  Zero disk storage when deleted.
      S  Write file contents synchronously.
      t  Disable tail-merging of partial blocks with other files.
      u  Allow file to be undeleted.
      -R Recurse.
      -v Set the file's version/generation number.
*/
#define FOR_chattr

#include "toys.h"

typedef struct _chattr_params {
  unsigned long add_attr_val;
  unsigned long rem_attr_val;
  unsigned long set_attr_val;
  unsigned long version;
  unsigned char add_operator;
  unsigned char rem_operator;
  unsigned char set_operator;
  unsigned char version_flag;
  unsigned char recursive;
}CHATTR_PARAMS;

CHATTR_PARAMS chattr_param_info;

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
  {NULL,                            -1,                     0},
};

/*
 * Display help info and exit from application.
 */
static void show_chattr_help(void)
{
  toys.exithelp++;
  error_exit("Invalid Argument");
}

/*
 * Return flag value.
 */
static unsigned long get_flag_val(const char ch)
{
  const EXT2_ATTRS *ptr = toys_ext2_attrs;
  while(ptr->name) {
    if(ptr->optchar == ch) return ptr->inode_flag;
    ptr++;
  }
  return 0;
}

/*
 * Parse command line argument and fill the chattr_param_info structure.
 */
static void parse_cmdline_arg(char ***argv)
{
  char *arg = **argv;

  if(!arg) show_chattr_help();
  else if(arg && (!strcmp(arg, "--help"))) show_chattr_help();

  while(arg) {
    unsigned long flag_val;
    char *ptr = NULL;

    switch(arg[0]) {
      case '-': 
        {
          for(ptr = ++arg; *ptr; ptr++) {
            if(*ptr == 'R') {
              chattr_param_info.recursive = 1;
              continue;
            }
            else if(*ptr == 'v') {//after -v, get the version from the next cmdline input.
              char *endptr;
              arg = *(*argv += 1);
              if(!arg) show_chattr_help();
              if(*arg == '-') perror_exit("Invalid Number '%s'", arg);
              chattr_param_info.version = xstrtoul(arg, &endptr, 0);
              if(*endptr) perror_exit("bad version '%s'", arg);
              chattr_param_info.version_flag = 1;
              continue;
            }
            else {
              if((flag_val = get_flag_val(*ptr)) == 0) show_chattr_help();
              chattr_param_info.rem_attr_val |= flag_val;
              chattr_param_info.rem_operator = 1;
            }
          }//End of for loop.
        }
        break;
      case '+': 
        {
          chattr_param_info.add_operator = 1;
          for(ptr = ++arg; *ptr; ptr++) {
            if((flag_val = get_flag_val(*ptr)) == 0) show_chattr_help();
            chattr_param_info.add_attr_val |= flag_val;
          }
        }
        break;
      case '=': 
        {
          chattr_param_info.set_operator = 1;
          for(ptr = ++arg; *ptr; ptr++) {
            if((flag_val = get_flag_val(*ptr)) == 0) show_chattr_help();
            chattr_param_info.set_attr_val |= flag_val;
          }
        }
        break;
      default:
        return;
    }//end of switch case.
		arg = *(*argv += 1);
	}//end of while loop.
	return;
}

/*
 * Update attribute of given file.
 */
static int update_attr(struct dirtree *root)
{
  unsigned long flag_val = 0;
  char *fpath = NULL;
  int fd = -1;

  if(!dirtree_notdotdot(root)) return 0;

  //if file is a link and recursive is set then escape the file.
  //else if file is not regular+link+dir(like fifo or dev file) then escape the file.
  if( (S_ISLNK(root->st.st_mode) && chattr_param_info.recursive)
    || (!S_ISREG(root->st.st_mode) && !S_ISLNK(root->st.st_mode) && !S_ISDIR(root->st.st_mode)) )
    return 0;

  fpath = dirtree_path(root, NULL);

  if(-1 == (fd=open(fpath, O_RDONLY | O_NONBLOCK))) {
    free(fpath);
    return DIRTREE_ABORT;
  }

  //Get current attr of file.
  if(get_e2fs_flag(fd, &root->st, &flag_val) == -1) {
    perror_msg("while reading flags on '%s'", fpath);
    free(fpath);
    xclose(fd);
    return DIRTREE_ABORT;
  }

  //either '=' option will be there in cmdline or '-' / '+'.
  if(chattr_param_info.set_operator) { //for '=' operator.
    if(set_e2fs_flag(fd, &root->st, chattr_param_info.set_attr_val) == -1)
      perror_msg("while setting flags on '%s'", fpath);
  }
  else { //for '-' / '+' operator.
    //remove attributes from existing attribute.
    if(chattr_param_info.rem_operator)
      flag_val &= ~(chattr_param_info.rem_attr_val);
    //add attributes with existing attribute.
    if(chattr_param_info.add_operator)
      flag_val |= chattr_param_info.add_attr_val;
    if(!S_ISDIR(root->st.st_mode))
      flag_val &= ~EXT2_DIRSYNC_FL;
    if(set_e2fs_flag(fd, &root->st, flag_val) == -1)
      perror_msg("while setting flags on '%s'", fpath);
  }
  //set file version (if version flag is set).
  if(chattr_param_info.version_flag) {
    if(set_e2fs_version(fd, chattr_param_info.version) == -1)
      perror_msg("while setting version on '%s'", fpath);
  }
  free(fpath);
  xclose(fd);
  //go for directory (if any).
  if(S_ISDIR(root->st.st_mode) && chattr_param_info.recursive)
    return DIRTREE_RECURSE;
  return 0;
}

/*
 * chattr main function.
 */
void chattr_main(void)
{
  char **argv = toys.optargs;
  memset(&chattr_param_info, 0, sizeof(CHATTR_PARAMS));

  parse_cmdline_arg(&argv);
 
  if(!*argv) show_chattr_help();
  if(chattr_param_info.set_operator && (chattr_param_info.add_operator || chattr_param_info.rem_operator))
    error_exit("'=' is incompatible with '-' and '+'");

  if((chattr_param_info.rem_attr_val & chattr_param_info.add_attr_val) != 0)
    error_exit("Can't both set and unset same flag.");

  if( !(chattr_param_info.add_operator
        || chattr_param_info.rem_operator
        || chattr_param_info.set_operator
        || chattr_param_info.version_flag) )
    error_exit(("Must use '-v', '=', '-' or '+'"));

  for(; *argv; argv++)
    dirtree_read(*argv, update_attr);
  toys.exitval = 0; //always set success at this point.
  return;
}
