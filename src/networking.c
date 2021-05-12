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

		pthread_mutex_lock(&(connection->lock));
		if (connection->state == KEEP_ALIVE) {
			// KEEP_ALIVE means that the connection is persistent but there is an active handler
			// don't unlink; don't abort connection
		} else if (connection->state != OPENED) {
			unlink = true;
		} else if (diffms > networkingConfig.connectionTimeout) {
			// the connection is open too long without data from the client
			// TODO: add custom timeout if connection isPersistent
			connection->state = ABORTED;
		}
		pthread_mutex_unlock(&(connection->lock));

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
		
		pthread_mutex_lock(&(connection->lock));
		if (connection->inUse == 0) {	
			linked_unlink(link);
		
			freed++;

			if (connection->threads.request != PTHREAD_NULL) {
				pthread_cancel(connection->threads.request);
				pthread_join(connection->threads.request, NULL);
			}
			if (connection->threads.response != PTHREAD_NULL) {
				pthread_cancel(connection->threads.response);
				pthread_join(connection->threads.response, NULL);
			}
			if (connection->threads.encoder != PTHREAD_NULL) {
				pthread_cancel(connection->threads.encoder);
				pthread_join(connection->threads.encoder, NULL);
			}


			if (connection->state == ABORTED && connection->writefd >= 0) {
				// either the client took too long to send the headers
				// or this connection was persistent and the client didn't produce a request
				// either way: since the writefd is still available 
				//             let's send a 408 status before closing the connection
				
				int dupfd = dup(connection->writefd);
				if (dupfd < 0) {
					error("networking: couldn't dup fd for 408 handling: %s", strerror(errno));
					goto CONTINUE_CLEANUP;
				}
				FILE* stream = fdopen(dupfd, "w");
				if (stream == NULL) {
					error("networking: couldn't create FILE for 408 handling: %s", strerror(errno));
					close(dupfd);
					goto CONTINUE_CLEANUP;
				}
				
				struct headers headers = headers_create();
				for(int i = 0; i < networkingConfig.defaultHeaders.number; i++) {
					headers_mod(&headers, networkingConfig.defaultHeaders.headers[i].key, networkingConfig.defaultHeaders.headers[i].value);
				} 
				
				// set connection close header as required by the standard
				headers_mod(&headers, "Connection", "close");
				// no content
				headers_mod(&headers, "Content-Length", "0");
				
				fprintf(stream, "%s %d %s\r\n", protocolString(connection->metaData), 408, getStatusStrings(408).statusString);

				headers_dump(&headers, stream);
				headers_free(&headers);

				fprintf(stream, "\r\n");
				fclose(stream); // will close dup as well
			}
			CONTINUE_CLEANUP:

			#ifdef SSL_SUPPORT
			if (connection->sslConnection != NULL)
				ssl_closeConnection(connection->sslConnection);
			#endif
		
			if (connection->readfd >= 0)
				close(connection->readfd);
			if (connection->writefd >= 0)
				close(connection->writefd);

			if (connection->metaData.path != NULL)
				free(connection->metaData.path);
			if (connection->metaData.queryString != NULL)
				free(connection->metaData.queryString);
			if (connection->metaData.uri != NULL)
				free(connection->metaData.uri);
			if (connection->currentHeader != NULL)
				free(connection->currentHeader);

			headers_free(&(connection->headers));

			if (connection->peer.name != NULL)
				free(connection->peer.name);

			pthread_mutex_unlock(&(connection->lock));
			pthread_mutex_destroy(&(connection->lock));

			free(connection);
		} else {	
			pthread_mutex_unlock(&(connection->lock));
		}

		link = linked_next(link);
	}

	debug("cleanup: %d/%d freed", freed, length);
}

void setNonBlocking(int fd, bool nonBlocking) {
	int flags = fcntl(fd, F_GETFL);
	if (flags < 0) {
		warn("networking: couldn't get socket flags");
		// ignore; maybe the socket is dead
		return;
	}
	
	if (nonBlocking) {
		flags |= O_NONBLOCK;
	} else {
		flags &= ~O_NONBLOCK;
	}
	if (fcntl(fd, F_SETFL, flags) < 0) {
		error("networking: couldn't set socket flags");
		return;
	}

}

void setSIGIO(int fd, bool enable) {
	// set socket to asynchronous

	int flags = fcntl(fd, F_GETFL);
	if (flags < 0) {
		warn("networking: couldn't get socket flags");
		// ignore; maybe the socket is dead
		return;
	}
	
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

	debug("networking: safely shuting down the connection%s.", force ? " (forced)" : "");

	stopThread(self, &(connection->threads.request), true);
	stopThread(self, &(connection->threads.response), force);

	// close socket
	int tmp = connection->readfd;
	connection->readfd = -1;
	close(tmp);
	tmp = connection->writefd;
	connection->writefd = -1;
	close(tmp);

	pthread_mutex_lock(&(connection->lock));
	//connection->state = CLOSED;
	connection->inUse--;
	pthread_mutex_unlock(&(connection->lock));
}

struct chunkedEncodingData {
	struct connection* connection;
	int readfd;
};

/*
 * This thread handles chunked transfer encoding for persistent connections
 */
void* chunkedTransferEncodingThread(void* _data) {
	#define ENCODING_MAX_CHUNK_SIZE (512)

	struct chunkedEncodingData* data = (struct chunkedEncodingData*) _data;
	
	int dupFd = dup(data->connection->writefd);
	if (dupFd < 0) {
		error("networking: couldn't dup for chunked encoding");
		
		// closing pipe should cause the handler to terminate
		close(data->readfd);
		return NULL;
	}
	FILE* writeFile = fdopen(dupFd, "w");
	if (writeFile == NULL) {
		error("networking: couldn't open FILE stream for chunked encoding");
		
		// closing pipe should cause the handler to terminate
		close(data->readfd);
		return NULL;
	}
	
	debug("networking: chunked transfer encoding: using max chunk size of %lld", ENCODING_MAX_CHUNK_SIZE);
	
	char c[ENCODING_MAX_CHUNK_SIZE];

	size_t total = 0;
	size_t chunks = 0;
	int tmp;
	while((tmp = read(data->readfd, c, ENCODING_MAX_CHUNK_SIZE)) > 0) {
		fprintf(writeFile, "%x\r\n", tmp);
		fwrite(c, sizeof(char), tmp, writeFile);
		fputs("\r\n", writeFile);
		total += tmp;
		chunks++;
	}
	
	debug("networking: chunked transfer encoding: %lld bytes send in %lld chunks", total, chunks);
	
	if (0 == tmp) {
		// send last chunk flag
		fputs("0\r\n\r\n", writeFile);
	} else {
		error("networking: error reading from chunked input: %s", strerror(errno));
	}
	
	close(data->readfd);
	fclose(writeFile); // close dup, fd will stay open
	free(data);
	
	return NULL;
}

void minimalErrorResponse(struct headers* headers, struct connection* connection) {

	// fix headers
	headers_remove(headers, "Content-Encoding");
	headers_mod(headers, "Connection", "close");
	headers_mod(headers, "Content-Length", 0);
	
	FILE* stream = fdopen(connection->writefd, "w");
	if (stream == NULL) {
		// all is lost
		error("networking: this is fine... %s", strerror(errno));
		
		pthread_mutex_lock(&(connection->lock));
		
		// invalidate socket fd and close
		int tmp = connection->writefd;
		connection->writefd = -1;
		close(tmp);
	} else {
		fprintf(stream, "%s %d %s\r\n", protocolString(connection->metaData), 500, getStatusStrings(500).statusString);
		headers_dump(headers, stream);
		fprintf(stream, "\r\n");
		
		pthread_mutex_lock(&(connection->lock));
		
		// invalidate socket fd before closing
		connection->writefd = -1;
		fclose(stream);
	}

	if (connection->isPersistent) {
		// this conenction is persistent
		// => set this connection to non-persistent and close it.
		connection->state = PROCESSING;
		connection->isPersistent = false;
	}
	
	pthread_mutex_unlock(&(connection->lock));
}

int sendHeader(int statusCode, struct headers* headers, struct request* request) {
	debug("networking: sending headers");
	
	struct connection* connection = (struct connection*) request->_private;

	struct headers defaultHeaders = networkingConfig.defaultHeaders;

	for(int i = 0; i < defaultHeaders.number; i++) {
		headers_mod(headers, defaultHeaders.headers[i].key, defaultHeaders.headers[i].value);
	} 
	
	bool chunkedTransferEncoding = false;
	
	if (connection->isPersistent) {
		headers_mod(headers, "Connection", "keep-alive");
		
		if (headers_get(headers, "Content-Length") == NULL) {
			headers_mod(headers, "Transfer-Encoding", "chunked");
			chunkedTransferEncoding = true;
		}
	} else {
		headers_mod(headers, "Connection", "close");
	}
	
	// fd will be the fd to be returned to the caller
	int fd = connection->writefd;
	
	// tmp is for sending headers
	int tmp = dup(fd);
	if (tmp < 0) {
		error("networking: sendHeader: dup: %s", strerror(errno));
		minimalErrorResponse(headers, connection);
		return -1;
	}
	
	if (chunkedTransferEncoding) {
		int pipefd[2];
		
		if (pipe(pipefd) < 0) {
			close(tmp);
			error("networking: couldn't create pipe for encoding: %s", strerror(errno));
			minimalErrorResponse(headers, connection);
			return -1;
		}
	
		struct chunkedEncodingData* data = malloc(sizeof(struct chunkedEncodingData));
		if (data == NULL) {
			close(tmp);
			close(pipefd[0]);
			close(pipefd[1]);
			error("networking: encoding data: malloc: %s", strerror(errno));
			minimalErrorResponse(headers, connection);
			return -1;
		}
	
		data->connection = connection;
		data->readfd = pipefd[0];
		fd = pipefd[1];
	
		// start encoding thread
		if (pthread_create(&(connection->threads.encoder), NULL, &chunkedTransferEncodingThread, data) < 0) {
			close(tmp);
			close(pipefd[0]);
			close(pipefd[1]);
			free(data);
		
			error("networking: Couldn't start encoding thread.");
			minimalErrorResponse(headers, connection);
			return -1;
		}
	} else {
		fd = dup(fd);
		if (fd < 0) {
			close(tmp);
			error("networking: sendHeader: dup: %s", strerror(errno));
			minimalErrorResponse(headers, connection);
			return -1;
		}
	}
	
	FILE* stream = fdopen(tmp, "w");	
	if (stream == NULL) {
		error("networking: sendHeader: fdopen: %s", strerror(errno));
		minimalErrorResponse(headers, connection);
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
		.fd = connection->readfd,
		.peer = connection->peer,
		.userData = connection->threads.handler.data,
		._private = connection 
	}, (struct response) {
		.sendHeader = sendHeader
	});
	
	debug("networking: response handler returned");

	safeEndConnection(connection, false);

	return NULL;
}

/*
 * This thread handles finding a handler and handler timeout
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

	if (pthread_create(&(connection->threads.response), NULL, &responseThread, connection) < 0) {
		int tmp = connection->readfd;
		connection->readfd = -1;
		close(tmp);
		tmp = connection->writefd;
		connection->writefd = -1;
		close(tmp);

		error("networking: Couldn't start response thread.");
		warn("networking: Aborting request.");
		
		pthread_mutex_lock(&(connection->lock));
		connection->state = ABORTED;
		connection->inUse--;
		pthread_mutex_unlock(&(connection->lock));
		
		return NULL;
	}

	debug("networking: going to sleep");
	// TODO set timeout via config
	sleep(30);

	error("networking: Timeout of handler.");
	error("networking: Aborting");

	
	safeEndConnection(connection, true);

	return NULL;
}

void startRequestHandler(struct connection* connection) {
	debug("networking: starting request handler");
	if (pthread_create(&(connection->threads.request), NULL, &requestThread, connection) != 0) {
		error("networking: Couldn't start request thread.");
		warn("networking: Aborting request.");
		
		pthread_mutex_lock(&(connection->lock));
		connection->state = ABORTED;
		connection->inUse--;
		pthread_mutex_unlock(&(connection->lock));
		
		return;
	}
}

#define BUFFER_LENGTH (64)

pthread_t dataThreadId;

void dataHandler(int signo) {
	debug("networking: data handler got called.");

	for(link_t* link = linked_first(&connectionList); link != NULL; link = linked_next(link)) {
		struct connection* connection = link->data;
		
		pthread_mutex_lock(&(connection->lock));
		// we don't support pipelining, current request has to be finished for the next to start
		if (connection->state != OPENED) {
			pthread_mutex_unlock(&(connection->lock));
			continue;
		}
		connection->inUse++;
		pthread_mutex_unlock(&(connection->lock));
		
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
						
						connection->isPersistent = true;	
						const char* connectionHeader = headers_get(&(connection->headers), "Connection");
						if (connectionHeader == NULL) {
							// assume keep alive
							connection->isPersistent = true;
						} else if (strcasecmp(connectionHeader, "close")) {
							connection->isPersistent = false;
						} else if (strcasecmp(connectionHeader, "keep-alive")) {
							connection->isPersistent = true;
						}
						
						pthread_mutex_lock(&(connection->lock));
						if (connection->isPersistent) {
						} else {
							connection->state = PROCESSING;
						}
						connection->inUse++;
						pthread_mutex_unlock(&(connection->lock));
						
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
			
			pthread_mutex_lock(&(connection->lock));
			connection->state = ABORTED;
			pthread_mutex_unlock(&(connection->lock));
		}

		pthread_mutex_lock(&(connection->lock));
		connection->inUse--;
		pthread_mutex_unlock(&(connection->lock));
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
			peer.name = strdup("");
		} else {
			peer.name = strdup(entry.h_name);
			if (peer.name == NULL) {
				error("networking: Couldn't strdup hostname: %s", strerror(errno));
				peer.name = strdup("");
			}
		}
		
		if (peer.name == NULL) {
			close(tmp);
			free(connection);
			error("networking: bad: strdup on empty string: %s", strerror(errno));
			continue;
		}

		snprintf(&(peer.portStr[0]), 5 + 1, "%d", peer.port);

		info("networking: new connection from %s:%s", peer.addr, peer.portStr);

		#ifdef SSL_SUPPORT
		if (bindObj->ssl_settings != NULL) {			
			setNonBlocking(tmp, false);

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

			setNonBlocking(connection->readfd, true);
			setSIGIO(connection->readfd, true);	
		} else {
			connection->sslConnection = NULL;

			connection->readfd = tmp;
			connection->writefd = dup(tmp);
			if (connection->writefd < 0) {
				error("networking: listen: dup: %s", strerror(errno));
				free(connection);
				close(tmp);
				continue;
			}

			setNonBlocking(tmp, true);
			setSIGIO(tmp, true);	
		}
		#else 
			connection->readfd = tmp;
			connection->writefd = dup(tmp);
			if (connection->writefd < 0) {
				error("networking: listen: dup: %s", strerror(errno));
				free(connection);
				close(tmp);
				continue;
			}

			setNonBlocking(tmp, true);
			setSIGIO(tmp, true);
		#endif

		// lock doesn't yet exist
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
			.encoder = PTHREAD_NULL,
			.handler = {},
		};
		connection->currentHeaderLength = 0;
		connection->currentHeader = NULL;
		connection->inUse = 0;
		pthread_mutex_init(&connection->lock, NULL);
		updateTiming(connection, false);

		linked_push(&connectionList, connection);

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
