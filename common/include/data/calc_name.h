#ifndef __CALCS_NAME_H__
#define __CALCS_NAME_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CALCS_DEFAULT_DIRECTORY "/var/lib/maestro/calcs"
#define CALCS_DAILY_NAME_FORMAT "%s/%s-Daily_%s_SP%i_SE%i.%s"

/* Unified helper for optimizer and server to find the same calc files */
static inline char* calc_name_get_daily(
    const char* _dir,  // Directory for saved files
    const char* _name, // Name of facility
    const char* _ext,  // File extension without leading "."
    int _epd,          // entries per day, ie 96/24
    int _price_class,  // 1-4
    time_t _date) 
{
  int name_len = 0;
  char filename_buf[512];
  struct tm tm = *localtime(&_date);
  char date_str[11]; // YYYY-MM-DD
  strftime(date_str, sizeof(date_str), "%Y-%m-%d", &tm);

  name_len = snprintf(filename_buf, sizeof(filename_buf), 
    CALCS_DAILY_NAME_FORMAT,
    _dir, _name, date_str, _epd, _price_class, _ext);

  if (name_len < 0 || (size_t)name_len >= sizeof(filename_buf)) {
    fprintf(stderr, "calc_name - Failed to build filename");
    return NULL;
  }

  char* filename = (char*)malloc(name_len + 1);
  if (!filename) {
    fprintf(stderr, "calc_name - Failed to allocate memory");
    return NULL;
  }
  memcpy(filename, filename_buf, name_len);
  filename[name_len] = '\0';

  return filename;
}

#endif
