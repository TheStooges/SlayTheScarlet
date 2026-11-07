/* sts.log, next to the DLL. */
#ifndef STS_LOG_H
#define STS_LOG_H

/* Adds a line. Safe from any thread. */
void slog(const char *fmt, ...);

#endif
