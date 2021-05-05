#ifndef UTIL_H
#define UTIL_H

#include <stdbool.h>
#include <pthread.h>

void strremove(char* string, int index, int number);

char* symbolicRealpath(const char* file);

int isInDir(const char* filename, const char* dirname);

struct fileCopy {
	int readFd;
	int writeFd;
	bool closeWriteFd;
};
int startCopyThread(int from, int to, bool closeWriteFd, pthread_t* thread);
void* fileCopyThread(void* data);

int strlenOfNumber(long long number);

char* getTimestamp();

#endif
