#include <stdbool.h>
#include <stdlib.h>

#include "networking.h"

struct networkingConfig networkingConfig;

struct connection connections[MAX_CONNECTIONS];

static inline struct connection emptyConnection() {
	return (struct connection) {
		.used = false,
		.connectionState = CLOSED,
		.metaData = {
			.path = NULL,
			.queryString = NULL
		},
		.headers = {
			.number = 0,
			.headers = NULL
		},
		.currentHeaderLength = 0,
		.fd = -1,
		/*
		 * ignored:
		 * .metaData.method
		 * .metaData.httpVersion
		 * .currentHeader	
		 * .timing.*
		 */
	};
}

void initNetworking(struct networkingConfig _networkingConfig) {
	networkingConfig = _networkingConfig;

	for (int i = 0; i < MAX_CONNECTIONS; i++) {
		connections[i] = emptyConnection();
	}
}

void newConnection() {

}
