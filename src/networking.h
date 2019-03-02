#ifndef NETWORKING_H
#define NETWORKING_H

#include <stdbool.h>
#include <sys/time.h>

#include "headers.h"
#include "misc.h"

#define MAX_CONNECTIONS (512)

#define NR_CONNECTION_STATE (6)
enum connectionState {
	ESTABLISHED = 0, 
	HEADERS_COMPLETE = 1, 
	DATA_COMPLETE = 2, 
	PROCESSING = 3, 
	ABORTED = 4, 
	CLOSED = 5, 
	FREED = 6
};

struct timing {
	struct timeval states[NR_CONNECTION_STATE];
	struct timeval lastUpdate;
};

struct connection {
	bool used;
	enum connectionState connectionState;
	struct metaData metaData;
	struct headers headers;
	size_t currentHeaderLength;
	char* currentHeader;
	int fd;
	struct timing timing;
};

typedef handler_t (*handlerGetter_t)(struct metaData metaData, const char* host);

struct bind {
	const char* address;
	const char* port;
};

struct binds {
	int number;
	struct bind* binds;
};

struct networkingConfig {
	struct binds binds;
	long connectionTimeout;
	struct headers defaultResponse;
	handlerGetter_t getHandler;
};

void initNetworking(struct networkingConfig networkingConfig);

#endif
