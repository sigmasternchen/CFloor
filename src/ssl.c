#ifdef SSL_SUPPORT

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "ssl.h"
#include "logging.h"
#include "misc.h"

void ssl_init() {
	SSL_load_error_strings();
	SSL_library_init();
	OpenSSL_add_all_algorithms();
}
void ssl_destroy() {
	ERR_free_strings();
	EVP_cleanup();
}

int ssl_initSettings(struct ssl_settings* settings) {
	SSL_CTX* ctx = SSL_CTX_new( SSLv23_server_method());

	SSL_CTX_set_options(ctx, SSL_OP_SINGLE_DH_USE);
	if (!SSL_CTX_use_certificate_file(ctx, settings->certificate, SSL_FILETYPE_PEM)) {
		error("ssl: failed to set cert file for ctx: %s", ERR_error_string(ERR_get_error(), NULL));
		return -1;
	}

	if (!SSL_CTX_use_PrivateKey_file(ctx, settings->privateKey, SSL_FILETYPE_PEM)) {
		error("ssl: failed to set key file for ctx: %s", ERR_error_string(ERR_get_error(), NULL));
		return -1;
	}

	settings->_private.ctx = ctx;

	return 0;
}

void* copyFromSslToFd(void* data) {
	struct ssl_connection* connection = (struct ssl_connection*) data;

	char b;
	int r;

	while(true) {
		while((r = SSL_read(connection->instance, &b, 1)) == 1) {
			write(connection->_readfd, &b, 1);
		}

		int error = SSL_get_error(connection->instance, r);

		debug("ssl: ssl error: %d", error);

		if (error == SSL_ERROR_ZERO_RETURN) {
			debug("ssl: no more data can be read");
			break;
		} else if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
			debug("ssl: trying again");

			usleep(10000);

			continue;
		} else {
			warn("ssl: connection had an error");
			break;
		}
	}

	debug("ssl: read thread finished %d", r);
	close(connection->_readfd);

	return NULL;
}

void* copyFromFdToSsl(void* data) {
	struct ssl_connection* connection = (struct ssl_connection*) data;

	char b;
	int r;
	while((r = read(connection->_writefd, &b, 1)) == 1) {
		SSL_write(connection->instance, &b, 1);
	}

	debug("ssl: write thread finished %d", r);

	return NULL;
}

struct ssl_connection* ssl_initConnection(struct ssl_settings* settings, int socket) {
	struct ssl_connection* connection = malloc(sizeof(struct ssl_connection));
	if (connection == NULL) {
		error("ssl: couldn't allocate for ssl connection: %s", strerror(errno));
		return NULL;
	}

	connection->writefd = -1;
	connection->readfd = -1;
	connection->_writefd = -1;
	connection->_readfd = -1;
	connection->_threads[0] = PTHREAD_NULL;
	connection->_threads[1] = PTHREAD_NULL;

	connection->instance = SSL_new(settings->_private.ctx);
	info("ssl: instance created");

	if (connection->instance == NULL) {
		free(connection);
		error("ssl: failed to create new connection: %s", ERR_error_string(ERR_get_error(), NULL));
		return NULL;
	}

	SSL_set_fd(connection->instance, socket);

	if (SSL_accept(connection->instance) < 0) {
		ssl_closeConnection(connection);
		error("ssl: couldn't accept ssl connection: %s", ERR_error_string(ERR_get_error(), NULL));
		return NULL;
	}

	int pipefd[2];
	if (pipe(&(pipefd[0])) < 0) {
		ssl_closeConnection(connection);
		error("ssl: couldn't create pipe: %s", strerror(errno));
		return NULL;
	}

	connection->writefd = pipefd[1];
	connection->_writefd = pipefd[0];

	if (pipe(&(pipefd[0])) < 0) {
		ssl_closeConnection(connection);
		error("ssl: couldn't create pipe: %s", strerror(errno));
		return NULL;
	}

	connection->_readfd = pipefd[1];
	connection->readfd = pipefd[0];

	if (pthread_create(&(connection->_threads[0]), NULL, &copyFromSslToFd, connection) < 0) {
		ssl_closeConnection(connection);
		error("ssl: couldn't create copyToFd-thread");
		return NULL;
	}
	if (pthread_create(&(connection->_threads[1]), NULL, &copyFromFdToSsl, connection) < 0) {
		ssl_closeConnection(connection);
		error("ssl: couldn't create copyToSsl-thread");
		return NULL;
	}

	info("ssl: copy threads started");

	return connection;
}


void ssl_closeConnection(struct ssl_connection* connection) {
	info("ssl: closing connection");
	close(connection->writefd);
	close(connection->readfd);
	close(connection->_writefd);
	close(connection->_readfd);

	if (connection->_threads[0] != PTHREAD_NULL) {
		pthread_cancel(connection->_threads[0]);
		pthread_join(connection->_threads[0], NULL);
	}

	if (connection->_threads[1] != PTHREAD_NULL) {
		pthread_cancel(connection->_threads[1]);
		pthread_join(connection->_threads[1], NULL);
	} 

	SSL_shutdown(connection->instance);
	SSL_free(connection->instance);
	free(connection);
}

#endif
