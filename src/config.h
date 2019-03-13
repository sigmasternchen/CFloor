#ifndef CONFIG_H
#define CONFIG_H

#include <stdio.h>

#include "networking.h"
#include "misc.h"
#include "files.h"
#include "cgi.h"
#include "logging.h"

#ifdef SSL_SUPPORT
	#include "ssl.h"
#endif

#define CONFIG_DEFAULT_LOGLEVEL (DEFAULT_LOGLEVEL)

struct config {
	int nrBinds;
	struct config_bind {
		char* addr;
		char* port;
		
		#ifdef SSL_SUPPORT
			struct ssl_settings* ssl;
		#endif

		int nrSites;
		struct config_site {
			int nrHostnames;
			char** hostnames;
			char* documentRoot;
			int nrHandlers;
			struct config_handler {
				char* dir;
				int type;
				handler_t handler;
				union config_handler_settings {
					struct fileSettings fileSettings;
					struct cgiSettings cgiSettings;
				} settings;
			} **handlers;
		} **sites;
	} **binds;
	struct config_logging {
		char* accessLogfile;
		char* serverLogfile;
		loglevel_t serverVerbosity;
	} logging;
};

/*
config format

bind [addr]:[port] {
	ssl {
		key = file
		cert = certfile
	}
	site {
		hostname|alias = "[host]"
		root = "/"
		handler "/" {
			type = cgi|file
			index = "index.html"
			index = "index.htm"
		}
	}
}
logging {
	access = file
	server = file
	verboseity = debug|info|warn|error
}


*/

struct config* config_parse(FILE* file);

struct networkingConfig config_getNetworkingConfig(struct config* config);
void config_setLogging(struct config* config);
struct handler config_getHandler(struct config* config, struct metaData metaData, const char* host, struct bind* bind);

void config_destroy(struct config* config);

#endif
