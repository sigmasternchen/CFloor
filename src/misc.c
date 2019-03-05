#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#include <pthread.h>

#include "misc.h"

const char* getHTTPVersionString(enum httpVersion version) {
	switch(version) {
		case HTTP10:
			return "HTTP/1.0";
		case HTTP11:
			return "HTTP/1.1";
		default:
			return "UNKNOWN";
	}
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
