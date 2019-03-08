#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>

#include <pthread.h>

#include "util.h"

bool isInDir(const char* file, const char* dir) {
	int length = strlen(dir);

	if (dir[length - 1] == '/')
		length--;

	if (strncmp(file, dir, length) == 0) {
		if (file[length] == '\0')
			return true;
		if (file[length] == '/')
			return true;
	}

	return false;
}

int startCopyThread(int from, int to, bool closeWriteFd, pthread_t* thread) {
	struct fileCopy* files = malloc(sizeof(struct fileCopy));
	if (files < 0)
		return -1;

	files->readFd = from;
	files->writeFd = to;
	files->closeWriteFd = closeWriteFd;

	return pthread_create(thread, NULL, &fileCopyThread, files);
}

void* fileCopyThread(void* data) {
	struct fileCopy* files = (struct fileCopy*) data;
	char c;

	while(read(files->readFd, &c, 1) > 0) {
		write(files->writeFd, &c, 1);
	}

	if (files->closeWriteFd)
		close(files->writeFd);

	free(files);

	return NULL;
}

char* strclone(const char* string) {
	char* result = malloc(strlen(string) + 1);
	if (result == NULL)
		return NULL;
	strcpy(result, string);
	return result;
}

int strlenOfNumber(long long number) {
	int result = 1;

	while(number > 9) {
		number /= 10;
		result++;
	}

	return result;
}
