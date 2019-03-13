#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "networking.h"
#include "logging.h"
#include "headers.h"
#include "util.h"
#include "signals.h"
#include "config.h"

#ifdef SSL_SUPPORT
#include "ssl.h"
#endif

struct networkingConfig networkingConfig;
struct config* config;

const char* configFile = NULL;

void shutdownHandler() {
	info("main: shutting down");

	headers_free(&(networkingConfig.defaultHeaders));

	config_destroy(config);

	#ifdef SSL_SUPPORT
	ssl_destroy();
	#endif

	exit(0);
}

void sigHandler(int signo) {
	info("main: signal %d", signo);
	shutdownHandler();
}

void setup() {
	setLogging(stdout, ERROR, true);
	setCriticalHandler(&shutdownHandler);

	signal_setup(SIGINT, &sigHandler);
	signal_setup(SIGTERM, &sigHandler);

	networkingConfig.defaultHeaders.number = 0;

	#ifdef SSL_SUPPORT
	ssl_init();
	#endif
}

void help(const char* progname) {
	printf("Usage: %s -c CONFIG_FILE\n", progname);
}

int parseArguments(int argc, char** argv) {
	int opt;
	while((opt = getopt(argc, argv, "c:")) != -1) {
		switch(opt) {
			case 'c':
				configFile = optarg;
				break;
			default:
				help(argv[0]);
				return -1;
		}
	}
	if (optind < argc) {
		help(argv[0]);
		return -1;
	}

	if (configFile == NULL) {
		help(argv[0]);
		return -1;
	}

	return 0;
}

int main(int argc, char** argv) {
	setup();

	if (parseArguments(argc, argv) < 0) {
		shutdownHandler();
		return 0;
	}

	FILE* file = fopen(configFile, "r");
	if (file == NULL) {
		error("main: couldn't open config file: %s", strerror(errno));
		shutdownHandler();
		return 0;
	}

	config = config_parse(file);

	fclose(file);

	if (config == NULL) {
		shutdownHandler();
		return 0;
	}
	config_setLogging(config);

	if (config_getNetworkingConfig(config, &networkingConfig) == NULL) {
		shutdownHandler();
		return 0;
	}

	networking_init(networkingConfig);

	while(true) {
		sleep(0xffff);
	}

	shutdownHandler();

	return 0;
}
