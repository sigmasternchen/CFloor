#ifndef NETWORKING_H
#define NETWORKING_H

#include <stdbool.h>
#include <sys/time.h>
#include <pthread.h>
#include <signal.h>

#include <sys/socket.h>
#include <netinet/in.h>

#include "headers.h"
#include "misc.h"

#define NR_CONNECTION_STATE (5)
enum connectionState {
	OPENED = 0,
	PROCESSING = 1, 
	ABORTED = 2, 
	CLOSED = 3, 
	FREED = 4
};

struct timing {
	struct timespec states[NR_CONNECTION_STATE];
	struct timespec lastUpdate;
};

struct bind_private {
	pthread_t threadId;
	int socketFd;
};

struct bind {
	const char* address;
	const char* port;
	bool tls;
	struct bind_private _private;
};

typedef handler_t (*handlerGetter_t)(struct metaData metaData, const char* host, struct bind* bind);

struct threads {
	pthread_t request;
	pthread_t response;
	pthread_t helper[2];
	handler_t handler;
	int requestFd;
	int responseFd;
	int _requestFd;
	int _responseFd;
};

struct connection {
	enum connectionState state;
	struct sockaddr_in client;
	struct bind* bind;
	volatile sig_atomic_t inUse;
	int fd;
	struct metaData metaData;
	struct headers headers;
	size_t currentHeaderLength;
	char* currentHeader;
	struct timing timing;
	struct threads threads;
};

struct binds {
	int number;
	struct bind* binds;
};

struct networkingConfig {
	struct binds binds;
	long connectionTimeout;
	long maxConnections;
	struct headers defaultHeaders;
	handlerGetter_t getHandler;
};

#define CLEANUP_INTERVAl (1000)

#define LISTEN_BACKLOG (1024)

#define TIMING_CLOCK CLOCK_REALTIME

void initNetworking(struct networkingConfig networkingConfig);

#endif
