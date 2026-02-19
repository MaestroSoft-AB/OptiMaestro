#include <stdio.h> 
#include <stdlib.h> 
#include <stdbool.h> 
#include <sys/stat.h> 
#include <errno.h> 
#include <string.h> 


#include <maestroutils/error.h>
#include <maestroutils/file_utils.h>
#include <maestroutils/time_utils.h>

typedef enum {
    LOG_LEVEL_INFO = 0,
    LOG_LEVEL_WARN = 1,
    LOG_LEVEL_ERROR = 2
} LogLevel;

int log_init(const char* filepath);
void log_close(void);

void log_write(LogLevel level, const char* file, int line, const char* func, const char* fmt, ...);

#define LOG_INFO(fmt, ...) log_write(LOG_LEVEL_INFO, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) log_write(LOG_LEVEL_WARN, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) log_write(LOG_LEVEL_ERROR, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)