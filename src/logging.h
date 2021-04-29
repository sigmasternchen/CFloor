#ifndef LOGGING_H
#define LOGGING_H

#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>

typedef int loglevel_t;

#define UNKNOWN (-1)
#define DEBUG (0)
#define VERBOSE (1)
#define INFO (2)
#define WARN (3)
#define ERROR (4)
#define CRITICAL (5)

#define CUSTOM_LOGLEVEL_OFFSET (128)

#define EXIT_DEVASTATING (255)

#define DEFAULT_LOGLEVEL (WARN)

#define MAX_LOGGER (10)

void setLogging(FILE* file, loglevel_t loglevel, bool color);
void setCriticalHandler(void (*handler)());
void callCritical();

void printBacktrace();

void logging(loglevel_t loglevel, const char* format, ...);
void debug(const char* format, ...);
void verbose(const char* format, ...);
void info(const char* format, ...);
void warn(const char* format, ...);
void error(const char* format, ...);
void critical(const char* format, ...);

loglevel_t strtologlevel(const char* string);

#endif
