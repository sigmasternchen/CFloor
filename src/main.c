#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "networking.h"
#include "logging.h"
#include "headers.h"

handler_t handlerGetter(struct metaData metaData, const char* host, struct bind* bind) {
	return NULL;
}

int main(int argc, char** argv) {
	setLogging(stderr, DEBUG, true);
	setCriticalHandler(NULL);

	struct headers headers = headers_create();
	headers_mod(&headers, "Server", "CFloor 0.1");

	struct networkingConfig config = {
		.maxConnections = 1024,
		.connectionTimeout = 30000,
		.binds = {
			.number = 1,
			.binds = (struct bind[]) {
				{
					.address = "0.0.0.0",
					.port = "1337"
				}
			}
		},
		.defaultHeaders = headers,
		.getHandler = &handlerGetter
	};

	initNetworking(config);

	while(true) {
		sleep(0xffff);
	}

	headers_free(&headers);
}
