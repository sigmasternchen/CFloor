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
#include <arpa/inet.h>

#include "networking.h"
#include "linked.h"
#include "logging.h"
#include "signals.h"
#include "status.h"
#include "util.h"

#ifdef SSL_SUPPORT
#include "ssl.h"
#endif

struct networkingConfig networkingConfig;

static inline long timespecDiffMs(struct timespec start, struct timespec end) {
	return (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec / 1000000 - start.tv_nsec / 1000000);
}

static inline struct timespec getTime() {
	struct timespec time;

	// no need to check result; none of the errors can happen
	clock_gettime(TIMING_CLOCK, &time);

	return time;
}

static inline long timespacAgeMs(struct timespec start) {
	return timespecDiffMs(start, getTime());
}

void updateTiming(struct connection* connection, bool stateChange) {
	struct timespec time = getTime();

	connection->timing.lastUpdate = time;
	if (stateChange)	
		connection->timing.states[connection->state] = time;
}

linkedList_t connectionList;

linkedList_t connectionsToFree;
void cleanup() {
	signal_block_all();

	link_t* link = linked_first(&connectionList);

	int length = 0;
	int unlinked = 0;

	while(link != NULL) {
		length++;

		bool unlink = false;

		struct connection* connection = link->data;

		long diffms = timespacAgeMs(connection->timing.lastUpdate);

		if (connection->state != OPENED) {
			unlink = true;
		} else if (diffms > networkingConfig.connectionTimeout) {
			connection->state = ABORTED;
		}

		if (unlink) {	
			linked_push(&connectionsToFree, connection);
			unlinked++;
			linked_unlink(link);
		}

		link = linked_next(link);
	}

	debug("cleanup: %d/%d unlinked", unlinked, length);

	link = linked_first(&connectionsToFree);

	length = 0;
	int freed = 0;
	while(link != NULL) {
		length++;
		struct connection* connection = link->data;
		if (connection->inUse == 0) {
			freed++;

			#ifdef SSL_SUPPORT
			if (connection->sslConnection != NULL)
				ssl_closeConnection(connection->sslConnection);
			#endif

			if (connection->metaData.path != NULL)
				free(connection->metaData.path);
			if (connection->metaData.queryString != NULL)
				free(connection->metaData.queryString);
			if (connection->metaData.uri != NULL)
				free(connection->metaData.uri);
			if (connection->currentHeader != NULL)
				free(connection->currentHeader);

			headers_free(&(connection->headers));

			close(connection->readfd);
			close(connection->writefd);

			if (connection->threads.request != PTHREAD_NULL) {
				pthread_cancel(connection->threads.request);
				pthread_join(connection->threads.request, NULL);
			}
			if (connection->threads.response != PTHREAD_NULL) {
				pthread_cancel(connection->threads.response);
				pthread_join(connection->threads.response, NULL);
			}
			if (connection->threads.helper[0] != PTHREAD_NULL) {
				pthread_cancel(connection->threads.helper[0]);
				pthread_join(connection->threads.helper[0], NULL);
			}
			if (connection->threads.helper[1] != PTHREAD_NULL) {
				pthread_cancel(connection->threads.helper[1]);
				pthread_join(connection->threads.helper[1], NULL);
			}

			if (connection->peer.name != NULL)
				free(connection->peer.name);

			free(connection);

			linked_unlink(link);
		}

		link = linked_next(link);
	}

	debug("cleanup: %d/%d freed", freed, length);
}

void setSIGIO(int fd, bool enable) {
	// set socket to non-blocking, asynchronous

	int flags = fcntl(fd, F_GETFL);
	if (flags < 0) {
		warn("networking: couldn't get socket flags");
		// ignore; maybe the socket is dead
		return;
	}

	flags |= O_NONBLOCK;
	if (enable) {
		flags |= O_ASYNC;
	} else {
		flags &= ~O_ASYNC;
	}
	if (fcntl(fd, F_SETFL, flags) < 0) {
		error("networking: couldn't set socket flags");
		return;
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

static inline void stopThread(pthread_t self, pthread_t* thread, bool force) {
	if (pthread_equal(self, *thread))
		return;

	debug("networking: freeing thread");

	if (force) {
		if (pthread_cancel(*thread) < 0)
			error("networking: cancel thread: %s", strerror(errno));
	}
	if (pthread_join(*thread, NULL) < 0) 
		error("networking: join thread: %s", strerror(errno));

	*thread = PTHREAD_NULL;
}

void safeEndConnection(struct connection* connection, bool force) {
	pthread_t self = pthread_self();

	debug("networking: safely shuting down the connection. %d", force);

	stopThread(self, &(connection->threads.request), true);
	stopThread(self, &(connection->threads.response), force);
	stopThread(self, &(connection->threads.helper[0]), force);
	stopThread(self, &(connection->threads.helper[1]), force);

	close(connection->readfd);
	close(connection->writefd);

	connection->inUse--;
}

int sendHeader(int statusCode, struct headers* headers, struct request* request) {
	debug("networking: sending headers");

	struct headers defaultHeaders = networkingConfig.defaultHeaders;

	for(int i = 0; i < defaultHeaders.number; i++) {
		headers_mod(headers, defaultHeaders.headers[i].key, defaultHeaders.headers[i].value);
	} 

	struct connection* connection = (struct connection*) request->_private;
	int fd = connection->threads.responseFd;
	
	FILE* stream = fdopen(dup(fd), "w");	
	if (stream == NULL) {
		error("networking: Couldn't send header: %s", strerror(errno));
		return -1;
	}

	logging(HTTP_ACCESS, "%s %s %d %s", methodString(connection->metaData), connection->metaData.uri, statusCode, headers_get(request->headers, "User-Agent"));

	struct statusStrings strings = getStatusStrings(statusCode);

	fprintf(stream, "%s %d %s\r\n", protocolString(connection->metaData), statusCode, strings.statusString);

	headers_dump(headers, stream);


	fprintf(stream, "\r\n");
	fclose(stream);

	return fd;
}

/*
 * This thread calls the handler.
 */
void* responseThread(void* data) {
	struct connection* connection = (struct connection*) data;

	debug("networking: calling response handler");

	connection->threads.handler.handler((struct request) {
		.metaData = connection->metaData,
		.headers = &(connection->headers),
		.fd = connection->threads.requestFd,
		.peer = connection->peer,
		.userData = connection->threads.handler.data,
		._private = connection 
	}, (struct response) {
		.sendHeader = sendHeader
	});
	
	debug("networking: response handler returned");

	safeEndConnection(connection, false);
	
	close(connection->threads.requestFd);
	close(connection->threads._responseFd);

	close(connection->threads._requestFd);
	close(connection->threads.responseFd);

	return NULL;
}

/*
 * This thread handles finding a handler and handles pipes
 */
void* requestThread(void* data) {
	struct connection* connection = (struct connection*) data;

	signal_block_all();

	struct handler handler = networkingConfig.getHandler(connection->metaData, headers_get(&(connection->headers), "Host"), connection->bind);

	if (handler.handler == NULL) {
		handler.handler = status500;
		handler.data.ptr = NULL;
	}

	connection->threads.handler = handler;

	int pipefd[2];
	if (pipe(&(pipefd[0])) < 0) {
		error("networking: Couldn't create reponse pipe: %s", strerror(errno));
		warn("Aborting request.");
		connection->state = ABORTED;
		connection->inUse--;
		return NULL;
	}

	int request = pipefd[1];
	connection->threads._requestFd = request;
	connection->threads.requestFd = pipefd[0];
	
	if (pipe(&(pipefd[0])) < 0) {
		close(request);
		close(connection->threads.requestFd);

		error("networking: Couldn't create reponse pipe: %s", strerror(errno));
		warn("Aborting request.");
		connection->state = ABORTED;
		connection->inUse--;
		return NULL;
	}

	int response = pipefd[0];
	connection->threads._responseFd = response;
	connection->threads.responseFd = pipefd[1];

	if (startCopyThread(connection->readfd, request, true, &(connection->threads.helper[0])) < 0) {
		close(request);
		close(connection->threads.requestFd);
		close(response);
		close(connection->threads.responseFd);
		
		error("networking: Couldn't start helper thread.");
		warn("networking: Aborting request.");
		connection->state = ABORTED;
		connection->inUse--;
		return NULL;
	}
	if (startCopyThread(response, connection->writefd, false, &(connection->threads.helper[1])) < 0) {
		close(request);
		close(connection->threads.requestFd);
		close(response);
		close(connection->threads.responseFd);	
	
		error("networking: Couldn't start helper thread.");
		warn("networking: Aborting request.");
		connection->state = ABORTED;
		connection->inUse--;
		return NULL;
	}

	if (pthread_create(&(connection->threads.response), NULL, &responseThread, connection) < 0) {
		close(request);
		close(connection->threads.requestFd);
		close(response);
		close(connection->threads.responseFd);

		error("networking: Couldn't start response thread.");
		warn("networking: Aborting request.");
		connection->state = ABORTED;
		connection->inUse--;
		return NULL;
	}

	debug("networking: going to sleep");
	// TODO set timeout via config
	sleep(30);

	error("networking: Timeout of handler.");
	error("networking: Aborting");

	close(request);
	close(connection->threads.requestFd);
	close(response);
	close(connection->threads.responseFd);
	
	safeEndConnection(connection, true);

	return NULL;
}

void startRequestHandler(struct connection* connection) {
	connection->inUse++;

	debug("networking: starting request handler");
	if (pthread_create(&(connection->threads.request), NULL, &requestThread, connection) != 0) {
		error("networking: Couldn't start request thread.");
		warn("networking: Aborting request.");
		connection->state = ABORTED;
		connection->inUse--;
		return;
	}
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
		connection->inUse++;
		int tmp;
		char c;
		char buffer[BUFFER_LENGTH];
		size_t length = 0;
		bool dropConnection = false;
		char last = 0;
		if (connection->currentHeaderLength > 0)
			last = connection->currentHeader[connection->currentHeaderLength - 1];
		while((tmp = read(connection->readfd, &c, 1)) > 0) {
			if (last == '\r' && c == '\n') {
				if (dumpHeaderBuffer(&(buffer[0]), length, connection) < 0) {
					dropConnection = true;
					break;
				}

				// \r is in the buffer
				connection->currentHeaderLength--;
				connection->currentHeader[connection->currentHeaderLength] = '\0';

				int tmp;

				updateTiming(connection, false);

				if (connection->metaData.path == NULL) {
					tmp = headers_metadata(&(connection->metaData), connection->currentHeader);
					if (tmp == HEADERS_ALLOC_ERROR) {
						error("networking: couldn't allocate memory for meta data: %s", strerror(errno));
						warn("networking: aborting request");
						dropConnection = true;
						break;
					} else if (tmp == HEADERS_PARSE_ERROR) {
						error("networking: error while reading header line");
						warn("networking: aborting request");
						dropConnection = true;
						break;
					}
				} else {
					tmp = headers_parse(&(connection->headers), connection->currentHeader, connection->currentHeaderLength);
					if (tmp == HEADERS_END) {						
						free(connection->currentHeader);
						connection->currentHeader = NULL;

						debug("networking: headers complete");
						connection->state = PROCESSING;
						updateTiming(connection, true);
						startRequestHandler(connection);
						break;
					} else if (tmp == HEADERS_ALLOC_ERROR) {
						error("networking: couldn't allocate memory for header: %s", strerror(errno));
						warn("networking: aborting request");
						dropConnection = true;
						break;
					} else if (tmp == HEADERS_PARSE_ERROR) {
						error("networking: failed to parse headers");
						warn("networking: aborting request");
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

			buffer[length] = '\0';
			debug("networking: buffer: '%s'", buffer);
			dropConnection = true;
		}
		if (length > 0) {
			if (dumpHeaderBuffer(&(buffer[0]), length, connection) < 0) {
				dropConnection = true;
			}
		}

		if (dropConnection) {
			if (connection->currentHeader != NULL)
				free(connection->currentHeader);
			connection->currentHeader = NULL;
		
			debug("networking: dropping connection");
			setSIGIO(connection->readfd, false);
			connection->state = ABORTED;
		}

		connection->inUse--;
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

		if (setsockopt(tmp, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int)) < 0) {
			close(tmp);
			continue;
		}
		if (setsockopt(tmp, SOL_SOCKET, SO_REUSEPORT, &(int){ 1 }, sizeof(int)) < 0) {
			close(tmp);
			continue;
		}
		
		bindObj->_private.socketFd = tmp;

		if (bind(tmp, rp->ai_addr, rp->ai_addrlen) == 0)
			break;

		close(tmp);
	}

	if (rp == NULL) {
		error("networking: Could not bind: %s", strerror(errno));
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
		struct sockaddr_storage client;
		socklen_t clientSize = sizeof (client);

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

		
		struct peer peer;
		int family = client.ss_family;
		void* addrPtr;

		if (family == AF_INET) {
			addrPtr = &(((struct sockaddr_in*) &client)->sin_addr);
			peer.port = ntohs(((struct sockaddr_in*) &client)->sin_port);
		} else if (family == AF_INET6) {
			addrPtr = &(((struct sockaddr_in6*) &client)->sin6_addr);
			peer.port = ntohs(((struct sockaddr_in6*) &client)->sin6_port);
		}

		if (inet_ntop(family, addrPtr, &(peer.addr[0]), INET6_ADDRSTRLEN + 1) == NULL) {
			free(connection);
			error("networking: Couldn't set peer addr string: %s", strerror(errno));
			return NULL;
		}

		struct hostent entry;
		struct hostent* result;
		int h_errno;

		#define LOCAL_BUFFER_SIZE (128)	
		char buffer[LOCAL_BUFFER_SIZE];

		gethostbyaddr_r(&client, sizeof(client), family, &entry, &(buffer[0]), LOCAL_BUFFER_SIZE, &result, &h_errno);
		if (result == NULL) {
			peer.name = strclone("");
		} else {
			peer.name = strclone(entry.h_name);
			if (peer.name == NULL) {
				error("networking: Couldn't strclone hostname: %s", strerror(errno));
				peer.name = strclone("");
			}
		}

		snprintf(&(peer.portStr[0]), 5 + 1, "%d", peer.port);

		info("networking: new connection from %s:%s", peer.addr, peer.portStr);

		connection->readfd = tmp;
		connection->writefd = tmp;

		#ifdef SSL_SUPPORT
		if (bindObj->ssl_settings != NULL) {
			struct ssl_connection* sslConnection = ssl_initConnection(bindObj->ssl_settings, tmp);
			if (sslConnection == NULL) {
				free(connection);
				close(tmp);
				error("networking: failed to open ssl connection");
				continue;
			}
	
			connection->sslConnection = sslConnection;
			connection->readfd = sslConnection->readfd;
			connection->writefd = sslConnection->writefd;
		} else {
			connection->sslConnection = NULL;
		}
		#endif

		connection->state = OPENED;
		connection->peer = peer;
		connection->bind = bindObj;
		connection->metaData = (struct metaData) {
			.path = NULL,
			.queryString = NULL
		};
		connection->headers = headers_create();
		connection->threads = (struct threads) {
			/*
			 * This is really hacky. pthread_t is no(t always an) integer.
			 * TODO: better solution
			 */
			.request = PTHREAD_NULL,
			.response = PTHREAD_NULL,
			.handler = {},
			.requestFd = -1,
			.responseFd = -1,
			._requestFd = -1,
			._responseFd = -1
		};
		// TODO see above
		connection->threads.helper[0] = PTHREAD_NULL;
		connection->threads.helper[1] = PTHREAD_NULL;
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

void networking_init(struct networkingConfig _networkingConfig) {
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
	signal_block(SIGALRM);

	for(int i = 0; i < networkingConfig.binds.number; i++) {
		struct bind* bind = &(networkingConfig.binds.binds[i]);
		if (pthread_create(&(bind->_private.threadId), NULL, &listenThread, bind) != 0) {
			critical("networking: Couldn't start data thread.");
			return;
		}
	}
}
