#ifndef CONFIG_H
#define CONFIG_H

#include <stdio.h>

#include "misc.h"

struct config {
	int nrBinds;
	struct config_bind {
		const char* addr;
		const char* port;
		int nrSites;
		struct config_site {
			int nrHostnames;
			const char** hostnames;
			const char* documentRoot;
			int nrHandlers;
			struct config_handler {
				const char* dir;
				handler_t handler;
				union config_handler_settings {
					struct fileSettings fileSettings;
					struct cgiSettings cgiSettings;
				} settings;
			} handlers*;
		} sites*;
	} binds*;
};

/*
config format

bind [addr]:[port] {
	site {
		host|alias = "[host]"
		root = "/"
		handler "/" {
			type = cgi|file
			index = "index.html"
			index = "index.htm"
		}
	}
}


*/

struct config* config_parse(FILE* file);

struct networkingConfig config_getNetworkingConfig(struct config* config);
struct handler config_getHandler(struct config* config, struct metaData metaData, const char* host, struct bind* bind);

void config_destroy(struct config* config);

#endif
