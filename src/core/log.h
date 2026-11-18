/* log.h - line logger: flushed after every line, or (1.2.1, PS3) from a background thread. */
#ifndef SF_LOG_H
#define SF_LOG_H

int  log_open(const char *path);   /* appends; returns 0 on success */
void log_close(void);
void log_set_time(double seconds); /* timestamp prefix source */
void log_printf(const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;
/* Mirror lines to stdout as well (host tools). */
void log_set_echo(int on);
/* 1.2.1: async: lines go to a memory buffer and log_pump() writes them out
 * (call it from a background thread; log_close pumps the rest). */
void log_set_async(int on);
void log_pump(void);

#endif
