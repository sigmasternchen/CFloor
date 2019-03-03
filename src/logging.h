#ifndef LOGGING_H
#define LOGGING_H

#include <stdarg.h>

enum loglevel {
	DEBUG, VERBOSE, INFO, WARN, ERROR, CRITICAL
};

#define EXIT_DEVASTATING (255)

#define DEFAULT_LOGLEVEL (WARN)

#define MAX_LOGGER (10)

void setLogging(FILE* file, enum loglevel loglevel, bool color);
void setCriticalHandler(void (*handler)());

void printBacktrace();

void logging(enum loglevel loglevel, const char* format, ...);
void debug(const char* format, ...);
void verbose(const char* format, ...);
void info(const char* format, ...);
void warn(const char* format, ...);
void error(const char* format, ...);
void critical(const char* format, ...);

#endif
