/* log.c - line logger, flushed after every line. */
#include "log.h"
#include <stdarg.h>
#include <stdio.h>

static FILE *g_log;
static double g_time;
static int g_echo;

int log_open(const char *path)
{
    if (g_log) fclose(g_log);
    g_log = fopen(path, "a");
    return g_log ? 0 : -1;
}

void log_close(void)
{
    if (g_log) { fflush(g_log); fclose(g_log); g_log = 0; }
}

void log_set_time(double seconds) { g_time = seconds; }
void log_set_echo(int on) { g_echo = on; }

static volatile int g_lock;

void log_printf(const char *fmt, ...)
{
    char line[600];
    va_list ap;
    int n = snprintf(line, sizeof(line), "[%8.3f] ", g_time);
    if (n < 0) n = 0;
    if ((size_t)n >= sizeof(line)) n = (int)sizeof(line) - 1;
    va_start(ap, fmt);
    vsnprintf(line + n, sizeof(line) - (size_t)n, fmt, ap);
    va_end(ap);
    /* the library scan logs from a worker thread: serialize file writes */
    while (__sync_lock_test_and_set(&g_lock, 1)) { }
    if (g_log) { fputs(line, g_log); fputc('\n', g_log); fflush(g_log); }
    if (g_echo) { fputs(line, stdout); fputc('\n', stdout); }
    __sync_lock_release(&g_lock);
}
