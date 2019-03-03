#include <stdio.h>

#include "networking.h"

handler_t handlerGetter(struct metaData metaData, const char* host) {
	return NULL;
}

int main(int argc, char** argv) {
	initNetworking((struct networkingConfig) {
		.connectionTimeout = 30000,
		.defaultResponse = {
			.number = 0
		},
		.getHandler = &handlerGetter
	});
}
