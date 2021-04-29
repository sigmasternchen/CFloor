#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>

#include <pthread.h>

#include "util.h"
#include "logging.h"

void strremove(char* string, int index, int number) {
	int length = strlen(string);

	if (number + index > length)
		number = length - index;
		
	memmove(string + index, string + index + number, length - number - index + 1);
}

char* symbolicRealpath(const char* file) {
	int length = strlen(file);

	char* tmp = malloc(length + 1 + 1);
	if (tmp == NULL) {
		error("util: Couldn't allocate memory for realpath: %s", strerror(errno));
		return NULL;
	}

	if (file[0] != '/') {
		strcpy(tmp, "/");
		strcat(tmp, file);
	} else {	
		strcpy(tmp, file);
	}

	char* result = tmp;

	/*
	 * remove /./ and /../
	 * might introduce new //
	 */

	char last[3] = {0, 0, 0};	
	for(int i = 0; i < length; i++) {
		char c = tmp[i];

		if (c == '/') {
			if (last[0] == '.' && last[1] == '/') {
				strremove(tmp, i - 1, 2);
				length -= 2;
				i -= 2;
			} else if (last[0] == '.' && last[1] == '.' && last[2] == '/') {
				tmp[i - 3] = '\0';
				char* lastDir = rindex(tmp, '/');
				if (lastDir == NULL) {
					// tmp beginns with "/../"
					// shift forward and reset last[]
					// results in "..//"; the double / gets removed afterwards
					tmp[0] = '.';
					tmp[1] = '.';
					tmp[2] = '/';
					tmp += 3;
					length -= 3 + 1;
					i = 0;
					last[0] = 0;
					last[1] = 0;
				} else {				
					int lastDirLength = strlen(lastDir);
					tmp[i - 3] = '/';
					strremove(tmp, lastDir - tmp, lastDirLength + 3);
					length -= lastDirLength + 3;
					i -= lastDirLength + 3;					
				}
			}
		}

		last[2] = last[1];
		last[1] = last[0];
		last[0] = c;
	}

	/*
	 * remove //
	 */

	tmp = result;
	last[0] = 0;
	length = strlen(tmp);

	for(int i = 0; i < length; i++) {
		char c = tmp[i];

		if (c == '/') {
			if (last[0] == '/') {
				strremove(tmp, i, 1);
				length -= 1;
				i -= 1;
			}
		}

		last[0] = c;
	}

	return result;
}

int isInDir(const char* file, const char* dir) {
	int length = strlen(dir);

	if (dir[length - 1] == '/')
		length--;

	if (strncmp(file, dir, length) == 0) {
		if (file[length] == '\0')
			return 1;
		if (file[length] == '/')
			return 1;
	}

	return 0;
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

#define FILE_COPY_BUFFER_SIZE (1024)

size_t fileCopyThreadFallback(struct fileCopy* files) {

	char c[FILE_COPY_BUFFER_SIZE];

	size_t total = 0;
	int tmp;
	while((tmp = read(files->readFd, c, FILE_COPY_BUFFER_SIZE)) > 0) {
		write(files->writeFd, c, tmp);
		total += tmp;
	}
	
	return total;
}

void* fileCopyThread(void* data) {
	struct fileCopy* files = (struct fileCopy*) data;

	errno = EACCES;
	
	size_t total = 0;
	int tmp;
	while((tmp = splice(files->readFd, NULL, files->writeFd, NULL, FILE_COPY_BUFFER_SIZE, 0)) > 0) {
		total += tmp;
	}

	if (errno != EACCES) {
		debug("util: splice: %s", strerror(errno));
		debug("util: falling back to userland copy");
		total = fileCopyThreadFallback(files);
	}
	
	debug("util: filecopy: %d bytes copied", total);

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
