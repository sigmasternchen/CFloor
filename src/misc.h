#ifndef MISC_H
#define MISC_H

#include "headers.h"

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

struct request {
	struct metaData metaData;
	struct headers* headers;
	int fd;
	void* _private;
};

struct response {
	int (*sendHeader)(int statusCode, struct headers headers);
};

typedef void (*handler_t)(struct request request, struct response response);

#endif
