#ifndef MISC_H
#define MISC_H

#include <stdbool.h>

#include <pthread.h>

enum method {
	GET, POST, PUT
};

enum httpVersion {
	HTTP10, HTTP11
};

struct metaData {
	enum method method;
	enum httpVersion httpVersion;
	char* path;
	char* queryString;
};

/*
 * recursive headers. 
 * I don't know how to fix this a better way.
 */
#include "headers.h"

const char* getHTTPVersionString(enum httpVersion version);

union userData {
	int integer;
	void* ptr;
};

struct request {
	struct metaData metaData;
	struct headers* headers;
	int fd;
	void* _private;
	union userData user;
};

struct response {
	int (*sendHeader)(int statusCode, struct headers* headers, struct request* request);
};

typedef void (*handler_t)(struct request request, struct response response);


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
