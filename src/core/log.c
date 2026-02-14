#include "core/log.h"

#include <stdio.h>
#include <stdarg.h>
#include <time.h>

static LogLevel s_min_level = LOG_LEVEL_TRACE;

static const char *level_strings[] = {
    "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

static const char *level_colors[] = {
    "\033[90m",   /* TRACE  - gray   */
    "\033[36m",   /* DEBUG  - cyan   */
    "\033[32m",   /* INFO   - green  */
    "\033[33m",   /* WARN   - yellow */
    "\033[31m",   /* ERROR  - red    */
    "\033[35m",   /* FATAL  - magenta */
};

void log_init(LogLevel min_level) {
    s_min_level = min_level;
}

void log_output(LogLevel level, const char *file, int line, const char *fmt, ...) {
    if (level < s_min_level) return;

    /* timestamp */
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    char time_buf[16];
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", lt);

    /* strip path to just filename */
    const char *fname = file;
    for (const char *p = file; *p; p++) {
        if (*p == '/' || *p == '\\') fname = p + 1;
    }

    fprintf(stderr, "%s%s %-5s %s:%d: ",
            level_colors[level], time_buf, level_strings[level], fname, line);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fprintf(stderr, "\033[0m\n");
    fflush(stderr);
}
