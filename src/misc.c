#include <unistd.h>
#include <stdlib.h>

#include <pthread.h>

#include "misc.h"

int startCopyThread(int from, int to, pthread_t* thread) {
	struct fileCopy* files = malloc(sizeof(struct fileCopy));
	if (files < 0)
		return -1;

	files->readFd = from;
	files->writeFd = to;

	return pthread_create(thread, NULL, &fileCopyThread, files);
}

void* fileCopyThread(void* data) {
	struct fileCopy* files = (struct fileCopy*) data;
	char* c;

	while(read(files->readFd, &c, 1) > 0)
		write(files->writeFd, &c, 1);

	close(files->readFd);
	close(files->writeFd);

	free(files);

	return NULL;
}
