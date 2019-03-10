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

#ifdef SSL_SUPPORT
#include "ssl.h"
#endif

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

typedef struct handler (*handlerGetter_t)(struct metaData metaData, const char* host, struct bind* bind);

struct threads {
	pthread_t request;
	pthread_t response;
	pthread_t helper[2];
	struct handler handler;
	int requestFd;
	int responseFd;
	int _requestFd;
	int _responseFd;
};

struct connection {
	enum connectionState state;
	struct peer peer;
	struct bind* bind;
	volatile sig_atomic_t inUse;
	int readfd;
	int writefd;
	struct metaData metaData;
	struct headers headers;
	size_t currentHeaderLength;
	char* currentHeader;
	struct timing timing;
	struct threads threads;
	#ifdef SSL_SUPPORT
	struct ssl_connection* sslConnection;
	#endif
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

void networking_init(struct networkingConfig networkingConfig);

#endif
