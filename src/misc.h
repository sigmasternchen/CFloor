#ifndef MISC_H
#define MISC_H

#include <stdbool.h>

#include <pthread.h>

#include <arpa/inet.h>

enum method {
	GET, HEAD, POST, PUT, DELETE, CONNECT, OPTIONS, TRACE, PATCH 
};

enum protocol {
	HTTP10, HTTP11
};

struct metaData {
	enum method method;
	enum protocol protocol;
	char* path;
	char* queryString;
	char* uri;
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

struct bind_private {
	pthread_t threadId;
	int socketFd;
};

struct bind {
	const char* address;
	const char* port;
	bool tls;
	union userData settings;
	struct bind_private _private;
};

struct peer {
	// INET6_ADDRSTRLEN should be enough
	char addr[INET6_ADDRSTRLEN + 1];
	char* name;
	int port;
	char portStr[5 + 1];
};

struct request {
	struct metaData metaData;
	struct peer peer;
	struct headers* headers;
	struct bind bind;
	int fd;
	union userData userData;
	void* _private;
};

struct response {
	int (*sendHeader)(int statusCode, struct headers* headers, struct request* request);
};

typedef void (*handler_t)(struct request request, struct response response);

struct handler {
	handler_t handler;
	union userData data;
};

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
