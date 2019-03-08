#ifndef UTIL_H
#define UTIL_H

#include <stdbool.h>
#include <pthread.h>

bool isInDir(const char* filename, const char* dirname);

struct fileCopy {
	int readFd;
	int writeFd;
	bool closeWriteFd;
};
int startCopyThread(int from, int to, bool closeWriteFd, pthread_t* thread);
void* fileCopyThread(void* data);

char* strclone(const char* string);

int strlenOfNumber(long long number);

#endif
