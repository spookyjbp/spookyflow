/* log.c - line logger.
 *
 * 1.2: on the PS3 a line used to be written and flushed to the HDD as it
 * was logged, on whatever thread logged it. With a big log file and the
 * cover worker reading the same drive, one flush could hold the main
 * thread for ~100 ms (a dropped frame after every panel open and after
 * every 10 s perf line). Now, with log_set_async(1), a line only goes into
 * a memory buffer; log_pump(), called from a low-priority thread a few
 * times a second (and by log_close), writes it out. A crash can lose the
 * last fraction of a second. Without async (host tools, tests) every line
 * is still written and flushed at once. */
#include "log.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define LOG_BUF        (64 * 1024)
#define LOG_MAX_BYTES  (2L * 1024 * 1024)   /* a bigger log starts over (1.2) */

static FILE *g_log;
static double g_time;
static int g_echo;
static volatile int g_async;

/* two buffers: loggers fill g_buf[g_cur]; log_pump swaps and writes the other */
static char g_buf[2][LOG_BUF];
static volatile int g_cur, g_len, g_dropped;
static volatile int g_lock;
static volatile int g_pumping;

static void lock(void)   { while (__sync_lock_test_and_set(&g_lock, 1)) { } }
static void unlock(void) { __sync_lock_release(&g_lock); }

int log_open(const char *path)
{
    long size = 0;
    if (g_log) fclose(g_log);
    g_log = fopen(path, "a");
    if (!g_log) return -1;
    if (fseek(g_log, 0, SEEK_END) == 0) size = ftell(g_log);
    if (size > LOG_MAX_BYTES) {               /* keep it small enough to open and send */
        fclose(g_log);
        g_log = fopen(path, "w");
        if (!g_log) return -1;
        fprintf(g_log, "(the log was over %ld MB, so it started over)\n", LOG_MAX_BYTES / (1024 * 1024));
        fflush(g_log);
    }
    return 0;
}

void log_set_async(int on) { if (!on) log_pump(); g_async = on; }

void log_pump(void)
{
    int b, n, d;
    if (__sync_lock_test_and_set(&g_pumping, 1)) return;   /* one writer at a time */
    lock();
    b = g_cur; n = g_len; d = g_dropped;
    g_cur = b ^ 1; g_len = 0; g_dropped = 0;
    unlock();
    if (g_log && (n > 0 || d > 0)) {
        if (n > 0) fwrite(g_buf[b], 1, (size_t)n, g_log);
        if (d > 0) fprintf(g_log, "[%8.3f] log: %d lines dropped (logged faster than written)\n", g_time, d);
        fflush(g_log);
    }
    __sync_lock_release(&g_pumping);
}

void log_close(void)
{
    g_async = 0;
    log_pump();
    if (g_log) { fflush(g_log); fclose(g_log); g_log = 0; }
}

void log_set_time(double seconds) { g_time = seconds; }
void log_set_echo(int on) { g_echo = on; }

void log_printf(const char *fmt, ...)
{
    char line[600];
    va_list ap;
    int n = snprintf(line, sizeof(line), "[%8.3f] ", g_time);
    if (n < 0) n = 0;
    if ((size_t)n >= sizeof(line) - 1) n = (int)sizeof(line) - 2;
    va_start(ap, fmt);
    vsnprintf(line + n, sizeof(line) - 1 - (size_t)n, fmt, ap);
    va_end(ap);
    n = (int)strlen(line);
    line[n++] = '\n';
    lock();
    if (g_async) {
        if (g_len + n <= LOG_BUF) { memcpy(g_buf[g_cur] + g_len, line, (size_t)n); g_len += n; }
        else g_dropped++;
    } else if (g_log) {
        fwrite(line, 1, (size_t)n, g_log); fflush(g_log);
    }
    if (g_echo) fwrite(line, 1, (size_t)n, stdout);
    unlock();
}
