#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <assert.h>
#include <semaphore.h>

#ifdef DEBUG
#include <pthread.h>
#endif

#include "logging.h"
#include "util.h"

#ifdef BACKTRACE
#include <execinfo.h>
#endif

struct {
	FILE* file;
	loglevel_t loglevel;
	bool color;
	sem_t write_sem;
} logger[MAX_LOGGER];
int loggerCount = 0;

void (*_criticalHandler)() = NULL;

void setLogging(FILE* file, loglevel_t loglevel, bool color) {
	if (loggerCount == MAX_LOGGER - 1) {
		return;
	}

	bool found = false;
	for (int i = 0; i < loggerCount; i++) {
		if ((logger[i].file == file)) {
			found = true;
			logger[i].loglevel = loglevel;
			logger[i].color = color;
			break;
		}
	}

	if (!found) {
		logger[loggerCount].file = file;
		logger[loggerCount].loglevel = loglevel;
		logger[loggerCount].color = color;		
		sem_init(&(logger[loggerCount].write_sem), 0, 1);
		loggerCount++;
	}
}

void setCriticalHandler(void (*handler)()) {
	_criticalHandler = handler;
}

void callCritical() {
	if (_criticalHandler != NULL)
		_criticalHandler();
}

#define BACKTRACE_BUFFER_SIZE 16
void printBacktrace() {
	#ifdef BACKTRACE

	void* buffer[BACKTRACE_BUFFER_SIZE];
	char** strings;
	int entries = backtrace(buffer, BACKTRACE_BUFFER_SIZE);
	strings = backtrace_symbols(buffer, entries);
	
	if (strings == NULL) {
		fprintf(stderr, "Error while backtracing: %s\n", strerror(errno));
		return;
	}

	int tmp = (entries < BACKTRACE_BUFFER_SIZE) ? entries : entries - 1;

	// from 1 to ignore printBacktrace
	for (int i = 1; i < tmp; i++) {
		fprintf(stderr, "  at %s\n", strings[i]);
	}
	if (tmp < entries) {
		fprintf(stderr, "  ...\n");
	}

	#else

	fprintf(stderr, "Error: Not compiled with backtrace support.\n");

	#endif
}

char* getLoglevelString(loglevel_t loglevel, bool color) {
	#define DEBUG_STRING    "[DEBUG]"
	#define VERBOSE_STRING  "[VERBOSE]"
	#define INFO_STRING     "[INFO]"
	#define WARN_STRING     "[WARNING]"
	#define ERROR_STRING    "[ERROR]"
	#define CRITICAL_STRING "[CRITICAL]"
	#define UNKNOWN_STRING  "[UNKNOWN]"
	#define CUSTOM_STRING   ""

	#define DEBUG_COLOR     "\033[37m"
	#define VERBOSE_COLOR   "\033[35m"
	#define INFO_COLOR      "\033[36m"
	#define WARN_COLOR      "\033[33m"
	#define ERROR_COLOR     "\033[31m"
	#define CRITICAL_COLOR  "\033[41m\033[30m"
	#define UNKNOWN_COLOR   "\033[41m\033[30m"
	#define CUSTOM_COLOR    "\033[37m"

	#define COLOR_END       "\033[0m"

	switch(loglevel) {
		case DEBUG:
			if (color)
				return DEBUG_COLOR DEBUG_STRING COLOR_END;
			else
				return DEBUG_STRING;
			break;
		case VERBOSE:
			if (color)
				return VERBOSE_COLOR VERBOSE_STRING COLOR_END;
			else
				return VERBOSE_STRING;
			break;
		case INFO:
			if (color)
				return INFO_COLOR INFO_STRING COLOR_END;
			else
				return INFO_STRING;
			break;
		case WARN:
			if (color)
				return WARN_COLOR WARN_STRING COLOR_END;
			else
				return WARN_STRING;
			break;
		case ERROR:
			if (color)
				return ERROR_COLOR ERROR_STRING COLOR_END;
			else
				return ERROR_STRING;
			break;
		case CRITICAL:
			if (color)
				return CRITICAL_COLOR CRITICAL_STRING COLOR_END;
			else
				return CRITICAL_STRING;
			break;
		default:
			
			if (loglevel >= CUSTOM_LOGLEVEL_OFFSET) {
				if (color)
					return CUSTOM_COLOR CUSTOM_STRING COLOR_END;
				else
					return CUSTOM_STRING;
			} else {
				if (color)
					return UNKNOWN_COLOR UNKNOWN_STRING COLOR_END;
				else
					return UNKNOWN_STRING;
			}
			break;
	}

	assert(false);
	return NULL;
}

void vlogging(loglevel_t loglevel, const char* format, va_list argptr) {
	char* timestamp = getTimestamp();

	for(int i = 0; i < loggerCount; i++) {
		if (loglevel < logger[i].loglevel)
			continue;
		if (logger[i].loglevel >= CUSTOM_LOGLEVEL_OFFSET) {
			if (loglevel != logger[i].loglevel)
				continue;
		} else if (loglevel >= CUSTOM_LOGLEVEL_OFFSET)
			continue;
		
		char* loglevelString = getLoglevelString(loglevel, logger[i].color);

		va_list local;
		va_copy(local, argptr);

		sem_wait(&(logger[i].write_sem));

		fprintf(logger[i].file, "%s %s ", timestamp, loglevelString);
		#ifdef DEBUG
		fprintf(logger[i].file, "[%ld] ", pthread_self());
		#endif
		vfprintf(logger[i].file, format, local);
		fprintf(logger[i].file, "\n");

		sem_post(&(logger[i].write_sem));

		va_end(local);
	}
	
	free(timestamp);

	if (loglevel == CRITICAL)
		callCritical();
}

void logging(loglevel_t loglevel, const char* format, ...) {
	va_list argptr;
	va_start(argptr, format);
	vlogging(loglevel, format, argptr);
	va_end(argptr);
}

void debug(const char* format, ...) {
	va_list argptr;
	va_start(argptr, format);
	vlogging(DEBUG, format, argptr);
	va_end(argptr);
}

void verbose(const char* format, ...) {
	va_list argptr;
	va_start(argptr, format);
	vlogging(VERBOSE, format, argptr);
	va_end(argptr);
}

void info(const char* format, ...) {
	va_list argptr;
	va_start(argptr, format);
	vlogging(INFO, format, argptr);
	va_end(argptr);
}

void warn(const char* format, ...) {
	va_list argptr;
	va_start(argptr, format);
	vlogging(WARN, format, argptr);
	va_end(argptr);
}

void error(const char* format, ...) {
	va_list argptr;
	va_start(argptr, format);
	vlogging(ERROR, format, argptr);
	va_end(argptr);
}

void critical(const char* format, ...) {
	va_list argptr;
	va_start(argptr, format);
	vlogging(CRITICAL, format, argptr);
	va_end(argptr);
}


loglevel_t strtologlevel(const char* string) {
	if (strcasecmp(string, "debug") == 0) {
		return DEBUG;
	} else if (strcasecmp(string, "info") == 0) {
		return INFO;
	} else if (strcasecmp(string, "warn") == 0) {
		return WARN;
	} else if (strcasecmp(string, "error") == 0) {
		return ERROR;
	} else if (strcasecmp(string, "critical") == 0) {
		return CRITICAL;
	} else {
		return UNKNOWN;
	}
}
