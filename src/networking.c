#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

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
		}

		if (unlink) {	
			linked_push(&connectionsToFree, connection);
			unlinked++;
			linked_unlink(link);
		}

		link = linked_next(link);
	}

	//debug("cleanup: %d/%d unlinked", unlinked, length);

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

			headers_free(&(connection->headers));

			close(connection->fd);

			free(connection);

			linked_unlink(link);
		}

		link = linked_next(link);
	}

	//debug("cleanup: %d/%d freed", freed, length);
}

void setSIGIO(int fd, bool enable) {
	// set socket to non-blocking, asynchronous

	int flags = fcntl(fd, F_GETFL);
	if (flags < 0) {
		critical("networking: couldn't get socket flags");
	}

	flags |= O_NONBLOCK;
	if (enable) {
		flags |= O_ASYNC;
	} else {
		flags &= ~O_ASYNC;
	}
	if (fcntl(fd, F_SETFL, flags) < 0) {
		critical("networking: couldn't set socket flags");
	}

	// set signal owner
	fcntl(fd, F_SETOWN, getpid());
}

int dumpHeaderBuffer(char* buffer, size_t size, struct connection* connection) {
	if (connection->currentHeaderLength == 0) {
		connection->currentHeader = malloc(size + 1);
		if (connection->currentHeader == NULL) {
			error("networking: couldn't allocate memory for header: %s", strerror(errno));
			error("networking: aborting request");
			return -1;
		}
	} else {
		char* tmp = realloc(connection->currentHeader, connection->currentHeaderLength + size + 1);
		if (tmp == NULL) {
			error("networking: couldn't allocate memory for header: %s", strerror(errno));
			error("networking: aborting request");
			return -1;
		}
		connection->currentHeader = tmp;
	}
	memcpy(connection->currentHeader + connection->currentHeaderLength, buffer, size);
	connection->currentHeaderLength += size;
	connection->currentHeader[connection->currentHeaderLength] = '\0';

	return 0;
}

#define SUCCESS (0)
#define ALLOC_ERROR (-1)
#define OTHER_ERROR (-2)

int setMetaData(struct metaData* metaData, char* header) {

	char* _method = strtok(header, " ");
	if (_method == NULL)
		return OTHER_ERROR;
	char* _path = strtok(NULL, " ");
	if (_path == NULL)
		return OTHER_ERROR;
	char* _httpVersion = strtok(NULL, " ");
	if (_httpVersion == NULL)
		return OTHER_ERROR;

	char* _null = strtok(NULL, " ");
	if (_null != NULL)
		return OTHER_ERROR;

	_path = strtok(_path, "#");
	int tmp = strlen(_path);
	_path = strtok(_path, "?");
	char* _queryString = "";
	if (tmp > strlen(_path)) {
		_queryString = _path + strlen(_path) + 1;
	}

	enum method method;

	if (strcmp(_method, "GET") == 0)
		method = GET;
	else if (strcmp(_method, "POST") == 0)
		method = POST;
	else if (strcmp(_method, "PUT") == 0)
		method = PUT;
	else
		return OTHER_ERROR;

	enum httpVersion httpVersion;
	if (strcmp(_httpVersion, "HTTP/1.0") == 0)
		httpVersion = HTTP10;
	else if (strcmp(_httpVersion, "HTTP/1.1") == 0)
		httpVersion = HTTP11;
	else
		return OTHER_ERROR;

	char* path = malloc(strlen(_path) + 1);
	if (path == NULL) {
		return ALLOC_ERROR;
	}
	char* queryString = malloc(strlen(_queryString) + 1);
	if (queryString == NULL) {
		free(path);
		return ALLOC_ERROR;
	}
	
	metaData->method = method;
	metaData->httpVersion = httpVersion;
	metaData->path = path;
	metaData->queryString = queryString;

	return SUCCESS;
}

#define BUFFER_LENGTH (64)

pthread_t dataThreadId;
void dataHandler(int signo) {
	debug("networking: data handler got called.");

	for(link_t* link = linked_first(&connectionList); link != NULL; link = linked_next(link)) {
		debug("networking: connection %p", link);
		struct connection* connection = link->data;
		if (connection->state != OPENED)
			continue;
		int tmp;
		char c;
		char buffer[BUFFER_LENGTH];
		size_t length = 0;
		bool dropConnection = false;
		char last = 0;
		if (connection->currentHeaderLength > 0)
			last = connection->currentHeader[connection->currentHeaderLength - 1];
		while((tmp = read(connection->fd, &c, 1)) > 0) {
			if (last == '\r' && c == '\n') {
				if (dumpHeaderBuffer(&(buffer[0]), length, connection) < 0) {
					dropConnection = true;
					break;
				}

				debug("networking: header: %s", connection->currentHeader);

				// \r is in the buffer
				connection->currentHeaderLength--;
				connection->currentHeader[connection->currentHeaderLength] = '\0';

				int tmp;

				if (connection->metaData.path == NULL) {
					tmp = setMetaData(&(connection->metaData), connection->currentHeader);
					if (tmp == ALLOC_ERROR) {
						error("networking: couldn't allocate memory for meta data: %s", strerror(errno));
						error("networking: aborting request");
						dropConnection = true;
						break;
					} else if (tmp == OTHER_ERROR) {
						error("networking: error while reading header line");
						error("networking: aborting request");
						dropConnection = true;
						break;
					}
				} else {
					tmp = headers_parse(&(connection->headers), connection->currentHeader, connection->currentHeaderLength);
					if (tmp == HEADERS_END) {
						debug("networking: headers complete");
						return;
					} else if (tmp == HEADERS_ALLOC_ERROR) {
						error("networking: couldn't allocate memory for header: %s", strerror(errno));
						error("networking: aborting request");
						dropConnection = true;
						break;
					} else if (tmp == HEADERS_PARSE_ERROR) {
						error("networking: failed to parse headers");
						error("networking: aborting request");
						dropConnection = true;
						break;

					}
				}

				connection->currentHeaderLength = 0;
				free(connection->currentHeader);
				connection->currentHeader = NULL;
				length = 0;

				continue;
			}
			
			if (length >= BUFFER_LENGTH) {
				length = 0;
				if (dumpHeaderBuffer(&(buffer[0]), BUFFER_LENGTH, connection) < 0) {
					dropConnection = true;
					break;
				}
			}

			buffer[length++] = c;
			last = c;
		}

		if (tmp < 0) {
			switch(errno) {
				case EAGAIN:
					// no more data to be ready
					// ignore this error
					break;
				default:
					dropConnection = true;
					error("networking: error reading socket: %s", strerror(errno));
					break;
			}
		} else if (tmp == 0) {
			error("networking: connection ended");
			dropConnection = true;
		}
		if (length > 0) {
			if (dumpHeaderBuffer(&(buffer[0]), length, connection) < 0) {
				dropConnection = true;
			}
		}

		if (dropConnection) {
			debug("networking: dropping connection");
			setSIGIO(connection->fd, false);
			connection->state = ABORTED;
		}
	}
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
		connection->inUse = 0;
		updateTiming(connection, false);

		linked_push(&connectionList, connection);
		
		setSIGIO(tmp, true);

		// trigger sigio in case we missed something
		kill(getpid(), SIGIO);
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
