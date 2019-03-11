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
#include "signals.h"

#ifdef SSL_SUPPORT
#include "ssl.h"
#endif

struct headers headers;
char* documentRoot = NULL;
FILE* accesslog;

void shutdownHandler() {
	info("main: shutting down");

	headers_free(&headers);

	if (documentRoot != NULL)
		free(documentRoot);

	#ifdef SSL_SUPPORT
	ssl_destroy();
	#endif

	fclose(accesslog);

	exit(0);
}

void sigHandler(int signo) {
	info("main: signal %d", signo);
	shutdownHandler();
}

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
	accesslog = fopen("access.log", "a");
	setbuf(accesslog, NULL);

	setLogging(stderr, DEBUG, true);
	setLogging(accesslog, HTTP_ACCESS, false);
	setCriticalHandler(NULL);

	signal_setup(SIGINT, &sigHandler);
	signal_setup(SIGTERM, &sigHandler);

	documentRoot = realpath("./home/", NULL);

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

	headers = headers_create();
	headers_mod(&headers, "Server", "CFloor 0.1");

	#ifdef SSL_SUPPORT
	ssl_init();

	struct ssl_settings ssl_settings = (struct ssl_settings) {
		.privateKey = "certs/hiro.key",
		.certificate = "certs/hiro.crt"
	};
	
	if (ssl_initSettings(&(ssl_settings)) < 0) {
		error("main: error setting up ssl settings");
		return 1;
	}
	#endif


	struct networkingConfig config = {
		.maxConnections = 1024,
		.connectionTimeout = 2000,
		.binds = {
			.number = 1,
			.binds = (struct bind[]) {
				{
					.address = "0.0.0.0",
					.port = "1337",
					.settings = settingsData,

					#ifdef SSL_SUPPORT
					.ssl_settings = &ssl_settings
					#endif
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

	shutdownHandler();
}
