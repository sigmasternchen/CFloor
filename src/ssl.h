#ifndef SSL_H
#define SSL_H

#include <pthread.h>

#include <openssl/ssl.h>

struct ssl_connection {
	SSL* instance;
	int readfd;
	int writefd;
	int _readfd;
	int _writefd;
	pthread_t _threads[2];
};

struct ssl_settings {
	const char* privateKey;
	const char* certificate;
	struct {
		SSL_CTX* ctx;
	} _private;
};

void ssl_init();
void ssl_destroy();

int ssl_initSettings(struct ssl_settings* settings);
struct ssl_connection* ssl_initConnection(struct ssl_settings* settings, int socket);
void ssl_closeConnection(struct ssl_connection* connection);

#endif
