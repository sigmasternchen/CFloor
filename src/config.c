#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include "config.h"
#include "logging.h"
#include "util.h"
#include "networking.h"
#include "misc.h"
#include "status.h"

#ifdef SSL_SUPPORT
#include "ssl.h"
#endif

#define LENGTH_OF_TOFREE_ARRAY (256)
#define MAX_TOKEN_LENGTH (128)

void freeEverything(void** array, int length) {
	for (int i = 0; i < length; i++) {
		free(array[i]);
	}
}

void replaceOrAdd(void** array, int* length, void* old, void* new) {
	for (int i = 0; i < *length; i++) {
		if (array[i] == old) {
			array[i] = new;
			return;
		}
	}
	array[*length++] = new;
}

struct config* config_parse(FILE* file) {
	// logger is not yet set but we need error messages
	setLogging(stdout, ERROR, true);

	void* toFree[LENGTH_OF_TOFREE_ARRAY];
	int toFreeLength = 0;

	struct config* config = malloc(sizeof(struct config));
	if (config == NULL) {
		error("config: couldn't allocate for config struct: %s", strerror(errno));
		return NULL;
	}

	replaceOrAdd(toFree, &toFreeLength, NULL, config);
	config->nrBinds = 0;
	config->binds = NULL;
	config->logging.accessLogfile = NULL;
	config->logging.serverLogfile = NULL;
	config->logging.serverVerbosity = CONFIG_DEFAULT_LOGLEVEL;


	#define ROOT (0)
	#define BIND_VALUE (10)
	#define BIND_BRACKETS_OPEN (11)
	#define BIND_CONTENT (12)
	#define SSL_BRACKETS_OPEN (130)
	#define SSL_CONTENT (131)
	#define SSL_KEY_EQUALS (132)
	#define SSL_KEY_VALUE (133)
	#define SSL_CERT_EQUALS (134)
	#define SSL_CERT_VALUE (135)
	#define SITE_BRACKETS_OPEN (140)
	#define SITE_CONTENT (141)
	#define SITE_HOST_EQUALS (142)
	#define SITE_HOST_VALUE (143)
	#define SITE_ROOT_EQUALS (144)
	#define SITE_ROOT_VALUE (145)
	#define HANDLER_VALUE (1460)
	#define HANDLER_BRACKETS_OPEN (1461)
	#define HANDLER_CONTENT (1462)
	#define TYPE_EQUALS (1463)
	#define TYPE_VALUE (1464)
	#define INDEX_EQUALS (1465)
	#define INDEX_VALUE (1466)
	#define LOGGING_BRACKETS_OPEN (20)
	#define LOGGING_CONTENT (21)
	#define LOGGING_ACCESS_FILE_EQUALS (22)
	#define LOGGING_ACCESS_FILE_VALUE (23)
	#define LOGGING_SERVER_FILE_EQUALS (24)
	#define LOGGING_SERVER_FILE_VALUE (25)
	#define LOGGING_SERVER_VERBOSITY_EQUALS (26)
	#define LOGGING_SERVER_VERBOSITY_VALUE (27)
	int state = ROOT;

	struct config_bind* currentBind = NULL;
	struct config_site* currentSite = NULL;
	struct config_handler* currentHandler = NULL;

	char currentToken[MAX_TOKEN_LENGTH];
	int currentTokenLength = 0;
	int line = 1;

	int c;
	while((c = fgetc(file)) != EOF) {
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
			int currentLine = line;

			if (c == '\n')
				line++;

			if (currentTokenLength == 0)
				continue;
						
			currentToken[currentTokenLength] = '\0';

			char* tmp;
			char** tmpArray;

			switch(state) {
				case ROOT:
					if (strcmp(currentToken, "bind") == 0) {
						currentBind = malloc(sizeof(struct config_bind));
						if (currentBind == NULL) {
							error("config: couldn't allocate for bind struct: %s", strerror(errno));
							freeEverything(toFree, toFreeLength);
							return NULL;
						}
						replaceOrAdd(toFree, &toFreeLength, NULL, currentBind);

						config->nrBinds++;
						struct config_bind** tmpBind = realloc(config->binds, config->nrBinds * sizeof(struct config_bind*));
						if (tmpBind == NULL) {
							error("config: couldn't reallocate for bind array: %s", strerror(errno));
							freeEverything(toFree, toFreeLength);
							return NULL;
						}

						replaceOrAdd(toFree, &toFreeLength, config->binds, tmpBind);
						config->binds = tmpBind;
						config->binds[config->nrBinds - 1] = currentBind;

						currentBind->nrSites = 0;
						currentBind->sites = NULL;
						currentBind->addr = NULL;
						currentBind->port = NULL;

						#ifdef SSL_SUPPORT
						currentBind->ssl = NULL;
						#endif

						state = BIND_VALUE;
					} else if (strcmp(currentToken, "logging") == 0) {
						state = LOGGING_BRACKETS_OPEN;
					} else {
						error("config: Unexpected token '%s' on line %d.", currentToken, currentLine);
						freeEverything(toFree, toFreeLength);
						return NULL;
					}

					break;
				case BIND_VALUE: ;
					char* seperator = strstr(currentToken, ":");
					if (seperator == NULL) {
						error("config: wrong bind addr format on line %d.", currentLine);
						freeEverything(toFree, toFreeLength);
						return NULL;
					}
					seperator[0] = '\0';	

					char* host;
					if (strcmp(currentToken, "*") == 0)
						host = strdup("0.0.0.0");
					else
						host = strdup(currentToken);

					if (host == NULL) {
						error("config: error cloning host string");
						freeEverything(toFree, toFreeLength);
						return NULL;
					}
					replaceOrAdd(toFree, &toFreeLength, NULL, host);

					char* port = strdup(seperator + 1);
					if (port == NULL) {
						error("config: error cloning port string");
						freeEverything(toFree, toFreeLength);
						return NULL;
					}
					replaceOrAdd(toFree, &toFreeLength, NULL, port);

					currentBind->addr = host;
					currentBind->port = port;
						
					state = BIND_BRACKETS_OPEN;

					break;
				case BIND_BRACKETS_OPEN:
					if (strcmp(currentToken, "{") != 0) {
						error("config: Unexpected token '%s' on line %d. '{' expected", currentToken, currentLine);
						freeEverything(toFree, toFreeLength);
						return NULL;
					}

					state = BIND_CONTENT;

					break;
				case BIND_CONTENT:
					if (strcmp(currentToken, "site") == 0) {
						currentSite = malloc(sizeof(struct config_site));
						if (currentSite == NULL) {
							error("config: couldn't allocate for site struct: %s", strerror(errno));
							freeEverything(toFree, toFreeLength);
							return NULL;
						}
						replaceOrAdd(toFree, &toFreeLength, NULL, currentSite);
						
						currentBind->nrSites++;
						struct config_site** tmp = realloc(currentBind->sites, currentBind->nrSites * sizeof(struct config_site*));
						if (tmp == NULL) {
							error("config: couldn't reallocate for site array: %s", strerror(errno));
							freeEverything(toFree, toFreeLength);
							return NULL;
						}

						replaceOrAdd(toFree, &toFreeLength, currentBind->sites, tmp);
						currentBind->sites = tmp;
						currentBind->sites[currentBind->nrSites - 1] = currentSite;

						currentSite->nrHostnames = 0;
						currentSite->hostnames = NULL;
						currentSite->nrHandlers = 0;
						currentSite->handlers = NULL;
						currentSite->documentRoot = NULL;

						state = SITE_BRACKETS_OPEN;
					} else if (strcmp(currentToken, "}") == 0) {
						state = ROOT;
					} else if (strcmp(currentToken, "ssl") == 0) {						
						#ifdef SSL_SUPPORT
							if (currentBind->ssl != NULL) {
								error("config: ssl settings for this bind already defined; line %d", currentLine);
								freeEverything(toFree, toFreeLength);
								return NULL;
							}
					
							currentBind->ssl = malloc(sizeof(struct ssl_settings));
							if (currentBind->ssl == NULL) {
								error("config: couldn't allocate for ssl settings: %s", strerror(errno));
								freeEverything(toFree, toFreeLength);
								return NULL;
							}
							replaceOrAdd(toFree, &toFreeLength, NULL, currentBind->ssl);

							currentBind->ssl->privateKey = NULL;
							currentBind->ssl->certificate = NULL;

							state = SSL_BRACKETS_OPEN;
						#else
								error("config: not compiled with ssl support");
								freeEverything(toFree, toFreeLength);
								return NULL;
						#endif
					} else {
						error("config: Unknown property '%s' on line %d.", currentToken, currentLine);
						freeEverything(toFree, toFreeLength);
						return NULL;
					}
					break;
				#ifdef SSL_SUPPORT
					case SSL_BRACKETS_OPEN:
						if (strcmp(currentToken, "{") != 0) {
							error("config: Unexpected token '%s' on line %d. '{' expected", currentToken, currentLine);
							freeEverything(toFree, toFreeLength);
							return NULL;
						}

						state = SSL_CONTENT;
						break;
					case SSL_CONTENT:
						if (strcmp(currentToken, "key") == 0) {
							state = SSL_KEY_EQUALS;
						} else if (strcmp(currentToken, "cert") == 0) {
							state = SSL_CERT_EQUALS;
						} else if (strcmp(currentToken, "}") == 0) {
							state = BIND_CONTENT;
						} else {
							error("config: Unknown property '%s' on line %d.", currentToken, currentLine);
							freeEverything(toFree, toFreeLength);
							return NULL;
						}
						break;
					case SSL_KEY_EQUALS:
						if (strcmp(currentToken, "=") != 0) {
							error("config: Unexpected token '%s' on line %d. '=' expected", currentToken, currentLine);
							freeEverything(toFree, toFreeLength);
							return NULL;
						}
						state = SSL_KEY_VALUE;
						break;
					case SSL_KEY_VALUE: ;
						tmp = strdup(currentToken);
						if (tmp == NULL) {
							error("config: error cloning ssl key string");
							freeEverything(toFree, toFreeLength);
							return NULL;
						}
						replaceOrAdd(toFree, &toFreeLength, NULL, tmp);
						
						currentBind->ssl->privateKey = tmp;

						state = SSL_CONTENT;
						break;
					case SSL_CERT_EQUALS:
						if (strcmp(currentToken, "=") != 0) {
							error("config: Unexpected token '%s' on line %d. '=' expected", currentToken, currentLine);
							freeEverything(toFree, toFreeLength);
							return NULL;
						}
						state = SSL_CERT_VALUE;
						break;
					case SSL_CERT_VALUE: ;
						tmp = strdup(currentToken);
						if (tmp == NULL) {
							error("config: error cloning ssl cert string");
							freeEverything(toFree, toFreeLength);
							return NULL;
						}
						replaceOrAdd(toFree, &toFreeLength, NULL, tmp);
						
						currentBind->ssl->certificate = tmp;

						state = SSL_CONTENT;
						break;
				#endif
				case SITE_BRACKETS_OPEN:
					if (strcmp(currentToken, "{") != 0) {
						error("config: Unexpected token '%s' on line %d. '{' expected", currentToken, currentLine);
						freeEverything(toFree, toFreeLength);
						return NULL;
					}

					state = SITE_CONTENT;
					break;
				case SITE_CONTENT:
					if (strcmp(currentToken, "handler") == 0) {
						currentHandler = malloc(sizeof(struct config_handler));
						if (currentHandler == NULL) {
							error("config: couldn't allocate for handler struct: %s", strerror(errno));
							freeEverything(toFree, toFreeLength);
							return NULL;
						}
						replaceOrAdd(toFree, &toFreeLength, NULL, currentHandler);
						
						currentSite->nrHandlers++;
						struct config_handler** tmp = realloc(currentSite->handlers, currentSite->nrHandlers * sizeof(struct config_handler*));
						if (tmp == NULL) {
							error("config: couldn't reallocate for handler array: %s", strerror(errno));
							freeEverything(toFree, toFreeLength);
							return NULL;
						}

						replaceOrAdd(toFree, &toFreeLength, currentSite->handlers, tmp);
						currentSite->handlers = tmp;
						currentSite->handlers[currentSite->nrHandlers - 1] = currentHandler;

						currentHandler->dir = NULL;
						currentHandler->type = -1;
						currentHandler->handler = NULL;

						memset(&(currentHandler->settings), 0, sizeof(union config_handler_settings));

						state = HANDLER_VALUE;
					} else if (strcmp(currentToken, "hostname") == 0 || strcmp(currentToken, "alias") == 0) {
						state = SITE_HOST_EQUALS;
					} else if (strcmp(currentToken, "root") == 0) {
						state = SITE_ROOT_EQUALS;
					} else if (strcmp(currentToken, "}") == 0) {
						state = BIND_CONTENT;
					} else {
						error("config: Unknown property '%s' on line %d.", currentToken, currentLine);
						freeEverything(toFree, toFreeLength);
						return NULL;
					}
					break;
				case SITE_HOST_EQUALS:
					if (strcmp(currentToken, "=") != 0) {
						error("config: Unexpected token '%s' on line %d. '=' expected", currentToken, currentLine);
						freeEverything(toFree, toFreeLength);
						return NULL;
					}
					state = SITE_HOST_VALUE;
					break;
				case SITE_HOST_VALUE:
					tmpArray = realloc(currentSite->hostnames, ++currentSite->nrHostnames * sizeof(char*));
					if (tmpArray == NULL) {
						error("config: error allocating hostname array");
						freeEverything(toFree, toFreeLength);
						return NULL;
					}
					replaceOrAdd(toFree, &toFreeLength, currentSite->hostnames, tmpArray);
					currentSite->hostnames = tmpArray;
					
					tmp = strdup(currentToken);
					if (tmp == NULL) {
						error("config: error cloning hostname string");
						freeEverything(toFree, toFreeLength);
						return NULL;
					}
					replaceOrAdd(toFree, &toFreeLength, NULL, tmp);
					
					currentSite->hostnames[currentSite->nrHostnames - 1] = tmp;

					state = SITE_CONTENT;
					break;
				case SITE_ROOT_EQUALS: 
					if (strcmp(currentToken, "=") != 0) {
						error("config: Unexpected token '%s' on line %d. '=' expected", currentToken, currentLine);
						freeEverything(toFree, toFreeLength);
						return NULL;
					}
					state = SITE_ROOT_VALUE;
					break;
				case SITE_ROOT_VALUE:
					tmp = realpath(currentToken, NULL);
					if (tmp == NULL) {
						error("config: error getting realpath of document root: %s", strerror(errno));
						freeEverything(toFree, toFreeLength);
						return NULL;
					}
					replaceOrAdd(toFree, &toFreeLength, NULL, tmp);
					
					currentSite->documentRoot = tmp;

					state = SITE_CONTENT;
					break;
				case HANDLER_VALUE:
					currentHandler->dir = strdup(currentToken);
					if (currentHandler->dir == NULL) {
						error("config: error cloning handle directory string");
						freeEverything(toFree, toFreeLength);
						return NULL;
					}

					state = HANDLER_BRACKETS_OPEN;
					break;
				case HANDLER_BRACKETS_OPEN:
					if (strcmp(currentToken, "{") != 0) {
						error("config: Unexpected token '%s' on line %d. '{' expected", currentToken, currentLine);
						freeEverything(toFree, toFreeLength);
						return NULL;
					}

					state = HANDLER_CONTENT;
					break;
				case HANDLER_CONTENT:
					if (strcmp(currentToken, "type") == 0) {
						state = TYPE_EQUALS;
					} else if (strcmp(currentToken, "index") == 0) {
						state = INDEX_EQUALS;
					} else if (strcmp(currentToken, "}") == 0) {
						state = SITE_CONTENT;
					} else {
						error("config: Unknown property '%s' on line %d.", currentToken, currentLine);
						freeEverything(toFree, toFreeLength);
						return NULL;
					}
					break;
				case TYPE_EQUALS:
					if (strcmp(currentToken, "=") != 0) {
						error("config: Unexpected token '%s' on line %d. '=' expected", currentToken, currentLine);
						freeEverything(toFree, toFreeLength);
						return NULL;
					}
					state = TYPE_VALUE;
					break;
				case TYPE_VALUE:
					if (strcmp(currentToken, "file") == 0) {
						currentHandler->type = FILE_HANDLER_NO;
					} else if (strcmp(currentToken, "cgi") == 0) {
						currentHandler->type = CGI_HANDLER_NO;
					} else {
						error("config: Unknown handler type '%s' on line %d.", currentToken, currentLine);
						freeEverything(toFree, toFreeLength);
						return NULL;
	
					}
					state = HANDLER_CONTENT;
					break;
				case INDEX_EQUALS:
					if (strcmp(currentToken, "=") != 0) {
						error("config: Unexpected token '%s' on line %d. '=' expected", currentToken, currentLine);
						freeEverything(toFree, toFreeLength);
						return NULL;
					}
					state = INDEX_VALUE;
					break;
				case INDEX_VALUE:
					if (currentHandler->type != FILE_HANDLER_NO) {
						error("config: unexpected 'index' on line %d; this is not a file handler", currentLine);
						freeEverything(toFree, toFreeLength);
						return NULL;
					}

					struct fileSettings* settings = &(currentHandler->settings.fileSettings);

					tmpArray = realloc(settings->indexfiles.files, ++(settings->indexfiles.number) * sizeof(char*));
					if (tmpArray == NULL) {
						error("config: error allocating hostname array");
						freeEverything(toFree, toFreeLength);
						return NULL;
					}
					replaceOrAdd(toFree, &toFreeLength, settings->indexfiles.files, tmpArray);
					settings->indexfiles.files = tmpArray;

					tmp = strdup(currentToken);
					if (tmp == NULL) {
						error("config: error cloning hostname string");
						freeEverything(toFree, toFreeLength);
						return NULL;
					}
					replaceOrAdd(toFree, &toFreeLength, NULL, tmp);
					
					settings->indexfiles.files[settings->indexfiles.number - 1] = tmp;

					state = HANDLER_CONTENT;
					break;
				case LOGGING_BRACKETS_OPEN:
					if (strcmp(currentToken, "{") != 0) {
						error("config: Unexpected token '%s' on line %d. '{' expected", currentToken, currentLine);
						freeEverything(toFree, toFreeLength);
						return NULL;
					}

					state = LOGGING_CONTENT;
					break;
				case LOGGING_CONTENT:
					if (strcmp(currentToken, "access") == 0) {
						state = LOGGING_ACCESS_FILE_EQUALS;
					} else if (strcmp(currentToken, "server") == 0) {
						state = LOGGING_SERVER_FILE_EQUALS;
					} else if (strcmp(currentToken, "verbosity") == 0) {
						state = LOGGING_SERVER_VERBOSITY_EQUALS;
					} else if (strcmp(currentToken, "}") == 0) {
						state = ROOT;
					} else {
						error("config: Unknown property '%s' on line %d.", currentToken, currentLine);
						freeEverything(toFree, toFreeLength);
						return NULL;
					}
					break;
				case LOGGING_ACCESS_FILE_EQUALS:
					if (strcmp(currentToken, "=") != 0) {
						error("config: Unexpected token '%s' on line %d. '=' expected", currentToken, currentLine);
						freeEverything(toFree, toFreeLength);
						return NULL;
					}
					state = LOGGING_ACCESS_FILE_VALUE;
					break;
				case LOGGING_ACCESS_FILE_VALUE:
					config->logging.accessLogfile = strdup(currentToken);
					if (config->logging.accessLogfile == NULL) {
						error("config: error cloning access log file string");
						freeEverything(toFree, toFreeLength);
						return NULL;
					}

					state = LOGGING_CONTENT;
					break;
				case LOGGING_SERVER_FILE_EQUALS:
					if (strcmp(currentToken, "=") != 0) {
						error("config: Unexpected token '%s' on line %d. '=' expected", currentToken, currentLine);
						freeEverything(toFree, toFreeLength);
						return NULL;
					}
					state = LOGGING_SERVER_FILE_VALUE;
					break;
				case LOGGING_SERVER_FILE_VALUE:
					config->logging.serverLogfile = strdup(currentToken);
					if (config->logging.serverLogfile == NULL) {
						error("config: error cloning server log file string");
						freeEverything(toFree, toFreeLength);
						return NULL;
					}

					state = LOGGING_CONTENT;
					break;
				case LOGGING_SERVER_VERBOSITY_EQUALS:
					if (strcmp(currentToken, "=") != 0) {
						error("config: Unexpected token '%s' on line %d. '=' expected", currentToken, currentLine);
						freeEverything(toFree, toFreeLength);
						return NULL;
					}
					state = LOGGING_SERVER_VERBOSITY_VALUE;
					break;
				case LOGGING_SERVER_VERBOSITY_VALUE:
					config->logging.serverVerbosity = strtologlevel(currentToken);

					if (config->logging.serverVerbosity == UNKNOWN) {
						error("config: Unexpected token '%s' on line %d.", currentToken, currentLine);
						freeEverything(toFree, toFreeLength);
						return NULL;
					}
					state = LOGGING_CONTENT;
					break;
				default:
					assert(false);
			}
			currentTokenLength = 0;
		} else {
			if (currentTokenLength >= MAX_TOKEN_LENGTH - 1) {
				error("config: Token on line %d is too long.", line);
				freeEverything(toFree, toFreeLength);
				return NULL;
			}
			currentToken[currentTokenLength++] = c;
		}
	}

	if (state != ROOT) {
		error("config: unexpected EOF");
		freeEverything(toFree, toFreeLength);
		return NULL;
	}

	for (int i = 0; i < config->nrBinds; i++) {
		currentBind = config->binds[i];

		#ifdef SSL_SUPPORT
			if (currentBind->ssl != NULL) {
				if (currentBind->ssl->privateKey == NULL) {
					error("config: ssl private key missing for %s:%s", currentBind->addr, currentBind->port);
					freeEverything(toFree, toFreeLength);
					return NULL;
				}
				if (currentBind->ssl->certificate == NULL) {
					error("config: ssl certificate missing for %s:%s", currentBind->addr, currentBind->port);
					freeEverything(toFree, toFreeLength);
					return NULL;
				}

				if (ssl_initSettings(currentBind->ssl) < 0) {
					error("config: error setting up ssl settings");
					freeEverything(toFree, toFreeLength);
					return NULL;
				}
			}
		#endif

		for(int j = 0; j < currentBind->nrSites; j++) {
			currentSite = currentBind->sites[j];
			
			const char* documentRoot = currentSite->documentRoot;
			if (documentRoot == NULL) {
				error("config: no document root given for %s:%s", currentBind->addr, currentBind->port);
				freeEverything(toFree, toFreeLength);
				return NULL;
			}

			for (int k = 0; k < currentSite->nrHandlers; k++) {
				currentHandler = currentSite->handlers[k];

				switch(currentHandler->type) {
					case FILE_HANDLER_NO: ;
						struct fileSettings* fileSettings = &(currentHandler->settings.fileSettings);
						currentHandler->handler = &fileHandler;
						fileSettings->documentRoot = documentRoot;
						break;
					case CGI_HANDLER_NO: ;
						struct cgiSettings* cgiSettings = &(currentHandler->settings.cgiSettings);
						currentHandler->handler = &cgiHandler;
						cgiSettings->documentRoot = documentRoot;
						break;
					default:
						error("config: unknown handler for %s:%s", currentBind->addr, currentBind->port);
						freeEverything(toFree, toFreeLength);
						return NULL;
				}
			} 
		}
	}

	return config;
}

struct networkingConfig* config_getNetworkingConfig(struct config* config, struct networkingConfig* networkingConfig) {
	struct bind* binds = malloc(config->nrBinds * sizeof(struct bind));
	if (binds == NULL) {
		error("config: couldn't malloc for bind array: %s", strerror(errno));
		return NULL;
	}

	for (int i = 0; i < config->nrBinds; i++) {
		binds[i] = (struct bind) {
			.address = config->binds[i]->addr,
			.port = config->binds[i]->port,
			.settings = {
				.ptr = config
			},
			#ifdef SSL_SUPPORT
				.ssl = (config->binds[i]->ssl != NULL),
				.ssl_settings = config->binds[i]->ssl
			#else
				.ssl = false
			#endif
		};
	};

	networkingConfig->binds = (struct binds) {
		.number = config->nrBinds,
		.binds = binds
	};
	networkingConfig->maxConnections = DEFAULT_MAX_CONNECTIONS;
	networkingConfig->connectionTimeout = DEFAULT_CONNECTION_TIMEOUT;
	networkingConfig->getHandler = &config_getHandler;
	networkingConfig->defaultHeaders = headers_create();

	return networkingConfig;
}

void config_setLogging(struct config* config) {
	setLogging(stdout, config->logging.serverVerbosity, true);

	if (config->logging.serverLogfile != NULL) {
		FILE* file = fopen(config->logging.serverLogfile, "a");
		if (file == NULL) {
			error("config: failed to open server log file %s: %s", config->logging.serverLogfile, strerror(errno));
		} else {
			setbuf(file, NULL);
			setLogging(file, config->logging.serverVerbosity, false);
		}
	}

	if (config->logging.accessLogfile != NULL) {
		FILE* file = fopen(config->logging.accessLogfile, "a");
		if (file == NULL) {
			error("config: failed to open access log file %s: %s", config->logging.accessLogfile, strerror(errno));
		} else {
			setbuf(file, NULL);
			setLogging(file, HTTP_ACCESS, false);
		}
	}
}

struct handler config_getHandler(struct metaData metaData, const char* host, struct bind* bind) {
	struct config* config = (struct config*) (bind->settings.ptr);
	struct handler handler = {};

	struct config_bind* config_bind = NULL;

	for (int i = 0; i < config->nrBinds; i++) {
		if (strcmp(config->binds[i]->addr, bind->address) == 0 && strcmp(config->binds[i]->port, bind->port) == 0) {
			config_bind = config->binds[i];
			break;
		}
	}

	if (config_bind == NULL) {
		error("config: this bind does not exist: %s:%s", bind->address, bind->port);
		handler.handler = status500;
		return handler;
	}

	bool isWildcard = false;
	struct config_site* config_site = NULL;

	for (int i = 0; i < config_bind->nrSites; i++) {
		if (config_bind->sites[i]->nrHostnames == 0) {
			if (config_site != NULL && !isWildcard)
				continue;

			config_site = config_bind->sites[i];
			isWildcard = true;
			continue;
		}

		for (int j = 0; j < config_bind->sites[i]->nrHostnames; j++) {
			if (strcmp(host, config_bind->sites[i]->hostnames[j]) == 0) {
				config_site = config_bind->sites[i];
				break;
			}
			if (strcmp(config_bind->sites[i]->hostnames[j], "*") == 0) {
				if (config_site != NULL && !isWildcard)
					continue;

				config_site = config_bind->sites[i];
				isWildcard = true;
				break;
			}
		}

		if (config_site != NULL)
			break;
	}
	
	if (config_site == NULL) {
		error("config: the site '%s' does not exist for bind %s:%s", host, bind->address, bind->port);
		handler.handler = status500;
		return handler;
	}

	int matchLength = 0;
	struct config_handler* config_handler = NULL;	

	for (int i = 0; i < config_site->nrHandlers; i++) {
		char* dir = config_site->handlers[i]->dir;
		int dirLength = strlen(dir);
		if (dirLength <= matchLength)
			continue;

		if (isInDir(metaData.path, dir)) {
			matchLength = dirLength;
			config_handler = config_site->handlers[i];
		}		
	}

	if (config_handler == NULL) {
		error("config: no handler for %s on %s:%s", metaData.uri, bind->address, bind->port);
		handler.handler = status500;
		return handler;
	}

	handler.handler = config_handler->handler;
	handler.data.ptr = &(config_handler->settings);


	return handler;
}

void config_destroy(struct config* config) {
	if (config == NULL)
		return;

	struct config_bind* currentBind = NULL;
	struct config_site* currentSite = NULL;
	struct config_handler* currentHandler = NULL;

	for (int i = 0; i < config->nrBinds; i++) {
		currentBind = config->binds[i];

		free(currentBind->addr);
		free(currentBind->port);

		#ifdef SSL_SUPPORT
			if (currentBind->ssl != NULL) {
				free(currentBind->ssl->privateKey);
				free(currentBind->ssl->certificate);
				free(currentBind->ssl);
			}
		#endif

		for(int j = 0; j < currentBind->nrSites; j++) {
			currentSite = currentBind->sites[j];

			if (currentSite->documentRoot != NULL)
				free(currentSite->documentRoot);

			for (int k = 0; k < currentSite->nrHostnames; k++) {
				if (currentSite->hostnames[k] != NULL)
					free(currentSite->hostnames[k]);
			}
			if (currentSite->hostnames != NULL)
				free(currentSite->hostnames);

			for (int k = 0; k < currentSite->nrHandlers; k++) {
				currentHandler = currentSite->handlers[k];

				if (currentHandler->dir != NULL)
					free(currentHandler->dir);

				switch(currentHandler->type) {
					case FILE_HANDLER_NO: ;
						struct fileSettings fileSettings = currentHandler->settings.fileSettings;

						for (int l = 0; l < fileSettings.indexfiles.number; l++) {
							free(fileSettings.indexfiles.files[l]);
						}

						if (fileSettings.indexfiles.files != NULL)
							free(fileSettings.indexfiles.files);

						break;
					case CGI_HANDLER_NO: ;
						//struct cgiSettings cgiSettings = currentHandler->settings.cgiSettings;
	
						break;
					default:
						break;
				}

				free(currentHandler);
			}
			if (currentSite->handlers != NULL)
				free(currentSite->handlers);
			free(currentSite);
		}
		if (currentBind->sites != NULL)
			free(currentBind->sites);
		free(currentBind);
	}
	if (config->binds != NULL)
		free(config->binds);
	free(config);
}
