#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "networking.h"
#include "linked.h"
#include "logging.h"
#include "signals.h"

struct networkingConfig networkingConfig;

linkedList_t connectionList;

linkedList_t connectionsToFree;
void cleanup() {
	signal_block_all();

	link_t* link = linked_first(&connectionList);

	int length = 0;
	int unlinked = 0;

	while(link != NULL) {
		length++;

		struct timespec time;
		// no need to check result; none of the errors can happen
		clock_gettime(TIMING_CLOCK, &time);

		bool unlink = false;

		struct connection* connection = link->data;
		long diffms = (time.tv_sec - connection->timing.lastUpdate.tv_sec) * 1000 + (time.tv_nsec / 1000000 - connection->timing.lastUpdate.tv_nsec / 1000000);

		if (connection->state != OPENED) {
			unlink = true;
		} else if (diffms > networkingConfig.connectionTimeout) {
			unlink = true;
			linked_push(&connectionsToFree, connection);
		}

		if (unlink) {	
			unlinked++;
			linked_unlink(link);
		}

		link = linked_next(link);
	}

	info("cleanup: %d/%d unlinked", unlinked, length);

	link = linked_first(&connectionsToFree);

	length = 0;
	int freed = 0;
	while(link != NULL) {
		length++;
		struct connection* connection = link->data;
		if (connection->inUse == 0) {
			freed++;
			if (connection->metaData.path != NULL)
				free(connection->metaData.path);
			if (connection->metaData.queryString != NULL)
				free(connection->metaData.queryString);
			if (connection->currentHeader != NULL)
				free(connection->currentHeader);

			for (int i = 0; i < connection->headers.number; i++) {
				if (connection->headers.headers[i].key != NULL)
					free(connection->headers.headers[i].key);
				if (connection->headers.headers[i].value != NULL)
					free(connection->headers.headers[i].value);
			}
			if (connection->headers.headers != NULL)
				free(connection->headers.headers);

			close(connection->fd);

			free(connection);

			linked_unlink(link);
		}

		link = linked_next(link);
	}

	info("cleanup: %d/%d freed", freed, length);
}


pthread_t dataThreadId;
void dataHandler(int signo) {
	info("data handler got called.");
}
void* dataThread(void* ignore) {
	signal_block_all();
	signal_allow(SIGIO);

	signal_setup(SIGIO, &dataHandler);

	while(true) {
		sleep(0xffff);
	}
}

void updateTiming(struct connection* connection, bool stateChange) {
	struct timespec time;

	// no need to check result; none of the errors can happen
	clock_gettime(TIMING_CLOCK, &time);

	connection->timing.lastUpdate = time;
	if (stateChange)	
		connection->timing.states[connection->state] = time;
}

void* listenThread(void* _bind) {
	signal_block_all();

	struct bind* bindObj = (struct bind*) _bind;

	info("networking: Starting to listen on %s:%s", bindObj->address, bindObj->port);

	struct addrinfo hints;
	struct addrinfo* result;
	struct addrinfo* rp;

	memset(&hints, 0, sizeof(struct addrinfo));

	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	hints.ai_protocol = 0;
	hints.ai_canonname = NULL;
	hints.ai_addr = NULL;
	hints.ai_next = NULL;
	
	int tmp;

	tmp = getaddrinfo(bindObj->address, bindObj->port, &hints, &result);
	if (tmp < 0) {
		error("networking: networking: could't get addrinfo: %s", gai_strerror(tmp));
		warn("networking: Not listening on %s:%s", bindObj->address == NULL ? "0.0.0.0" : bindObj->address, bindObj->port);
		return NULL;
	}

	for (rp = result; rp != NULL; rp = rp->ai_next) {
		tmp = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

		if (tmp == -1)
			continue;
		
		bindObj->_private.socketFd = tmp;

		if (bind(tmp, rp->ai_addr, rp->ai_addrlen) == 0)
			break;

		close(tmp);
	}

	if (rp == NULL) {
		error("networking: Could not bind.");
		warn("networking: Not listening on %s:%s", bindObj->address == NULL ? "0.0.0.0" : bindObj->address, bindObj->port);
		return NULL;
	}

	if (listen(tmp, LISTEN_BACKLOG) < 0) {
		error("networking: Could not listen.");
		warn("networking: Not listening on %s:%s", bindObj->address == NULL ? "0.0.0.0" : bindObj->address, bindObj->port);
		return NULL;
	}

	freeaddrinfo(result);

	while(true) {
		struct sockaddr_in client;
		socklen_t clientSize = sizeof (struct sockaddr_in);

		tmp = accept(bindObj->_private.socketFd, (struct sockaddr *) &client, &clientSize);
		if (tmp < 0) {
			switch(errno) {
				case ENETDOWN:
				case EPROTO:
				case ENOPROTOOPT:
				case EHOSTDOWN:
				case ENONET:
				case EHOSTUNREACH:
				case EOPNOTSUPP:
				case ENETUNREACH:
				case EAGAIN:
				
				case ECONNABORTED:
				case EINTR:
					// retry
					break;

				default:
					error("networking: Could not accept connection: %s", strerror(errno));
					warn("networking: Not listening on %s:%s", bindObj->address == NULL ? "0.0.0.0" : bindObj->address, bindObj->port);
					return NULL;
			}

			continue;
		}
		
		struct connection* connection = malloc(sizeof (struct connection));
		if (connection == NULL) {
			error("networking: Couldn't allocate connection objekt: %s", strerror(errno));
			continue;
		}

		connection->state = OPENED;
		connection->client = client;
		connection->bind = bindObj;
		connection->fd = tmp;
		connection->metaData = (struct metaData) {
			.path = NULL,
			.queryString = NULL
		};
		connection->headers = (struct headers) {
			.number = 0,
			.headers = NULL
		};
		connection->currentHeaderLength = 0;
		connection->currentHeader = NULL;
		connection->handler = NULL;
		connection->inUse = 0;
		updateTiming(connection, false);

		linked_push(&connectionList, connection);
	}
}

void initNetworking(struct networkingConfig _networkingConfig) {
	networkingConfig = _networkingConfig;

	connectionList = linked_create();
	connectionsToFree = linked_create();

	timer_t timer = timer_createThreadTimer(&cleanup);
	if (timer == NULL) {
		critical("networking: Couldn't create cleaup timer.");
		return;
	}
	if (timer_startInterval(timer, CLEANUP_INTERVAl) < 0) {
		critical("networking: Couldn't start cleaup timer.");
		return;
	}

	if (pthread_create(&dataThreadId, NULL, &dataThread, NULL) != 0) {
		critical("networking: Couldn't start data thread.");
		return;
	}

	signal_block(SIGIO);

	for(int i = 0; i < networkingConfig.binds.number; i++) {
		struct bind* bind = &(networkingConfig.binds.binds[i]);
		if (pthread_create(&(bind->_private.threadId), NULL, &listenThread, bind) != 0) {
			critical("networking: Couldn't start data thread.");
			return;
		}
	}
}
