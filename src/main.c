#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "networking.h"
#include "logging.h"
#include "headers.h"
#include "files.h"
#include "cgi.h"
#include "util.h"

struct handlerSettings {
	struct fileSettings fileSettings;
	struct cgiSettings cgiSettings;
	const char* cgiBin;
};

struct handler handlerGetter(struct metaData metaData, const char* host, struct bind* bind) {
	struct handlerSettings* settings = (struct handlerSettings*) bind->settings.ptr;

	info("%s", metaData.path);

	union userData data;

	if (isInDir(metaData.path, settings->cgiBin)) {
		data.ptr = &(settings->cgiSettings);

		return (struct handler) {
			.handler = &cgiHandler,
			.data = data
		};
	} else {
		data.ptr = &(settings->fileSettings);

		return (struct handler) {
			.handler = &fileHandler,
			.data = data
		};
	}
}

int main(int argc, char** argv) {
	setLogging(stderr, DEBUG, true);
	setCriticalHandler(NULL);

	char* documentRoot = realpath("./home/", NULL);

	struct handlerSettings handlerSettings = {
		.fileSettings =  {
			.documentRoot = documentRoot,
			.index = true,
			.indexfiles = {
				.number = 2,
				.files = (const char* []) {
					"index.html",
					"index.htm"
				}
			}	
		},
		.cgiSettings = {
			.documentRoot = documentRoot
		},
		.cgiBin = "/cgi-bin/"
	};
	union userData settingsData;
	settingsData.ptr = &handlerSettings;

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
					.port = "1337",
					.settings = settingsData
				}
			}
		},
		.defaultHeaders = headers,
		.getHandler = &handlerGetter
	};

	networking_init(config);

	while(true) {
		sleep(0xffff);
	}

	headers_free(&headers);
	free(documentRoot);
}
