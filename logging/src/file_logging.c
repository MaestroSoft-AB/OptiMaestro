#include "file_logging.h"
#include <stdarg.h>
#include <time.h>
#include <string.h>

static FILE* g_log_file = NULL;

static const char* level_to_string(LogLevel level) {
    switch (level) {
        case LOG_LEVEL_INFO: return "INFO";
        case LOG_LEVEL_WARN: return "WARN";
        case LOG_LEVEL_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

static void get_timestamp(char* out, size_t out_size) {
    time_t now = time(NULL);
    struct tm t;

#if defined(_WIN32)
    localtime_s(&t, &now);

#else 
    localtime_r(&now, &t);
#endif

    strftime(out, out_size, "%Y-%m-%d %H:%M:%S", &t);
}

int log_init(const char* filepath) {
    if (!filepath) return -1;

    g_log_file = fopen(filepath, "a");
    if (!g_log_file) return -2;

    setvbuf(g_log_file, NULL, _IOLBF, 0);
    return 0;
}

void log_close(void) {
    if (g_log_file) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
}

void log_write(LogLevel level, const char* file, int line, const char* func, const char* fmt, ...) {
    char ts[32];
    get_timestamp(ts, sizeof(ts));

    const char* level_str = level_to_string(level);

    FILE* out = (level == LOG_LEVEL_ERROR) ? stderr : stdout;

    fprintf(out, "%s [%s] %s:%d (%s): ", ts, level_str, file, line, func);

    va_list args;
    va_start(args, fmt);
    vfprintf(out, fmt, args);
    va_end(args);

    fputc('\n', out);
    fflush(out);

    if (g_log_file) {
        fprintf(g_log_file, "%s [%s] %s:%d (%s): ", ts, level_str, file, line, func);

        va_start(args, fmt);
        vfprintf(g_log_file, fmt, args);
        va_end(args);

        fputc('\n', g_log_file);
        fflush(g_log_file);
    }
}