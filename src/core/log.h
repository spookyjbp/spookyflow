/* log.h - line logger, flushed after every line so it survives crashes. */
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

#endif
