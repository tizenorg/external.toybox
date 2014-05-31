/* printf.c - Format and Print the data.
 *
 * Copyright 2012 Sandeep Sharma <sandeep.jack2756@gmail.com>
 *
 * See http://pubs.opengroup.org/onlinepubs/9699919799/utilities/printf.html

USE_PRINTF(NEWTOY(printf, "<1", TOYFLAG_USR|TOYFLAG_BIN))

config PRINTF 
    bool "printf"
    default y
    help
    usage: printf Format [Arguments..]
    
    Format and print ARGUMENT(s) according to FORMAT.
    Format is 'C' control output as 'C' printf.
*/

#define FOR_printf
#include "toys.h"

GLOBALS(
  char *hv_w;
  char *hv_p;
  int encountered;
)

/*
 * Calculate width and precision from format string,
 */
static int find_w_p()                      //Handle width and prec.
{
  char *ptr, *str;
  errno = 0;
  str = *toys.optargs;
  if(*str == '-') str++;
  long value = strtol(str, &ptr, 10);
  if(errno) perror_msg("Not a valid num %s, %d", str, errno);
  if(ptr && (*ptr != '\0' || ptr == (str))) perror_msg("Not a valid num %s", *toys.optargs);
  if(*--str == '-') return (int)(-1 * value);
  else return value;
}
/*
 * Set have_width and have_prec global variables.
 */
static void set_w_p(char *start)              
{
  TT.hv_p = NULL;
  TT.hv_w = NULL;
  TT.hv_p = strstr(start, ".*");        
  TT.hv_w = strchr(start, '*');        
  if((TT.hv_w-1) == TT.hv_p) TT.hv_w = NULL;       //pitfall: handle diff b/w * and .*
}
/*
 * Add ll to Interger formats and L to floating point
 * as we have to handle max. possible given value.
 */
static char* get_format(char *f)                
{
  int len = strlen(f);
  char last = f[len - 1];
  f[len - 1] = '\0';
  char *post = "";
  if(strchr("diouxX", last)) post = "ll";  // add ll to integer modifier.
  else if(strchr("feEgG", last)) post = "L"; // add L to float modifier.
  return xmsprintf("%s%s%c", f, post, last);
}
/*
 * Print the long values with width and prec taken 
 * care of.
 */
static void print_long(char *format, long long llong_value, int p, int w)
{
  if(!TT.hv_w) {
    if(!TT.hv_p) printf(format,llong_value);
    else printf(format,p,llong_value);
  }
  else {
    if(!TT.hv_p) printf(format,w,llong_value);
    else printf(format,w,p,llong_value);
  }
}
/*
 * Print the arguments with corresponding conversion and 
 * width and precision.
 */
static void print(char *fmt, int w, int p, int l)
{
  char *endptr = NULL;
  char *ptr = (fmt+l-1);
  long long llong_value;
  long double double_value;
  char *format = NULL;
  errno = 0;
  switch(*ptr) {
    case'd': /*FALL_THROUGH*/
    case'i':
      if(*toys.optargs != NULL) {  
        if(**toys.optargs == '\'' || **toys.optargs == '"') 
          llong_value = *((*toys.optargs) + 1);
        else {
          llong_value = strtoll(*toys.optargs, &endptr, 0);
          if(errno) { 
            perror_msg("Invalid num %s", *toys.optargs);
            llong_value = 0;
          }
          else {
            if(endptr && (*endptr != '\0' || endptr == (*toys.optargs))) {
              perror_msg("Not a valid num %s",*toys.optargs);
              llong_value = 0;
            }
          }
        }
      }
      else llong_value = 0;

      format = get_format(fmt);
      print_long(format, llong_value, p, w);
      break;
    case'o': /*FALL_THROUGH*/
    case'u': /*FALL_THROUGH*/
    case'x': /*FALL_THROUGH*/
    case'X':
      if(*toys.optargs != NULL) {
        if(**toys.optargs == '\'' || **toys.optargs == '"')
          llong_value = *((*toys.optargs) + 1);
        else {
          llong_value = strtoll(*toys.optargs, &endptr, 0);
          if(errno) {
            perror_msg("Invalid num %s", *toys.optargs);
            llong_value = 0;
          }
          else {
            if(endptr && (*endptr != '\0' || endptr == (*toys.optargs))) {
              perror_msg("Not a valid num %s", *toys.optargs);
              llong_value = 0;
            }
          }
        }
      }
      else llong_value = 0;

      format = get_format(fmt);
      print_long(format, llong_value, p, w);
      break;
    case'g': /*FALL_THROUGH*/
    case'e': /*FALL_THROUGH*/
    case'E': /*FALL_THROUGH*/
    case'G': /*FALL_THROUGH*/
    case'f':
      if(*toys.optargs != NULL) {
        double_value = strtold(*toys.optargs, &endptr);
        if(*endptr != '\0' || endptr == *toys.optargs) {
          perror_msg("Not a valid num %s", *toys.optargs);
          double_value = 0;
        }
      }
      else double_value = 0;

      format = get_format(fmt);
      if(!TT.hv_w) {
        if(!TT.hv_p) printf(format, double_value);
        else printf(format, p, double_value);
      }
      else {
        if(!TT.hv_p) printf(format, w, double_value);
        else printf(format, w, p, double_value);
      }
      break;
    case's':
      if(!TT.hv_w) {
        if(!TT.hv_p) printf(fmt, (*toys.optargs ? *toys.optargs : ""));
        else printf(fmt, p, (*toys.optargs ? *toys.optargs : ""));
      }
      else {
        if(!TT.hv_p) printf(fmt, w, (*toys.optargs ? *toys.optargs : ""));
        else printf(fmt,w,p,(*toys.optargs ? *toys.optargs : ""));
      }
      break;
    case'c':
      printf(fmt, (*toys.optargs ? **toys.optargs : '\0'));
      break;
  }
  free(format);
  format = NULL;
}
/*
 * Handle the escape sequences. 
 */
static int handle_slash(char **esc_val)
{
  char *ptr = *esc_val;
  unsigned  base = 0;
  unsigned num = 0;
  int esc_length = 0;
  unsigned result = 0, count = 0;
  if(*ptr == 'x') {
    ptr++;
    esc_length++; // Hexadecimal escape sequence have only 1 or 2 digits, xHH.
    base = 16;
  }
  else if(isdigit(*ptr)) base = 8;  // Octal escape sequence have 1,2 or 3 digits, xHHH. Leading "0" (\0HHH) we are ignoring.

  while(esc_length < 3 && base != 0) {
    num = tolower(*ptr) - '0';
    if(num > 10) num += ('0' - 'a' + 10);
    if(num >= base) { 
      if(base == 16) {
        esc_length--;
        if(esc_length == 0) { // Invalid hex values eg. /xvd, print it as it is /xvd
          result = '\\';
          ptr--;
        }
      }
      break;
    }
    esc_length++;
    result = count * base + num;
    count = result;
    ptr++;
  }
  if(base != 0) {
    ptr--;
    *esc_val = ptr;
    return (char)result;
  }
  else {
    switch(*ptr) {
      case 'n': 
        result = '\n';
        break;
      case 't': 
        result = '\t';
        break;
      case 'e': 
        result = (char)27;
        break;
      case 'b': 
        result = '\b';
        break;
      case 'a': 
        result = '\a';
        break;
      case 'f': 
        result = '\f';
        break;
      case 'v': 
        result = '\v';
        break;
      case 'r': 
        result = '\r';
        break;
      case '\\': 
        result = '\\';
        break;
      default :
        result = '\\';
        ptr--; // Let pointer pointing to / we will increment after returning.
        break;
    }
  }
  *esc_val = ptr;
  return (char)result;
}
/*
 * Handle "%b" option with '\' interpreted.
 */
static void print_esc_str(char *str)              
{
  while(*str != '\0') {
    if(*str == '\\') {
      str++; // Go to escape char     
      xputc(handle_slash(&str)); //print corresponding char
      str++;          
    }
    else xputc(*str++);
  }
}
/*
 * Prase the format string and print.
 */
static void parse_print(char *f)
{
  char *start, *p;
  char format_specifiers[] = "diouxXfeEgGcs";
  int len = 0;
  int width = 0;
  int prec = 0;

  while(*f) {
    switch(*f) {
      case '%':
        start = f++;
        len++;
        if(*f == '%') {
          xputc('%');
          break;
        }
        if(*f == 'b') {
          if(*toys.optargs) {
            print_esc_str(*toys.optargs);
            TT.encountered = 1;
          }
          else print_esc_str("");
          if(*toys.optargs) toys.optargs++;
          break;
        }
        if(strchr("-+# ", *f)) {            //Just consume -+# printf will take care.
          f++;
          len++;
        }
        if(*f == '*') {  
          f++;
          len++;
          if(*toys.optargs != NULL) {
            width = find_w_p();
            toys.optargs++;
          }
        }
        else { 
          while(isdigit(*f)) {
            f++;
            len++;
          }
        }
        if(*f == '.') {
          f++;
          len++;
          if(*f == '*') {
            f++;
            len++;
            if(*toys.optargs != NULL) {
              prec = find_w_p();
              toys.optargs++;
            }
          }
          else {
            while(isdigit(*f)) {
              f++;
              len++;
            }
          }
        }
        p = strchr(format_specifiers, *f);
        if(p == NULL) {
          perror_exit("Missing OR Invalid format specifier");
          return;
        }
        else {
          len++;
          set_w_p(start);
          p = xmalloc(len+1);
          memcpy(p, start, len);
          p[len] = '\0';
          print(p, width, prec, len);
          if(*toys.optargs) toys.optargs++;
          free(p);
          p = NULL;
        } 
        TT.encountered = 1;
        break;
      case'\\':
        if(f[1]) {
          if(*++f == 'c') exit(0); //Got '\c', so no further output  
          xputc(handle_slash(&f));
        }
        else xputc(*f);
        break;
      default:
        xputc(*f);
        break;
    }
    f++;
    len = 0;
  }
  return;
}
/*
 * Printf main function.
 */
void printf_main(void)
{
  char *format = NULL;
  TT.encountered = 0;
  format = *toys.optargs;
  toys.optargs++;
  parse_print(format); //printf acc. to format..
  while((*toys.optargs != NULL) && TT.encountered) parse_print(format); //Re-use FORMAT arg as necessary to convert all given ARGS.
  xflush();
}
