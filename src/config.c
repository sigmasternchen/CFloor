#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <stdlib.h>

#include "config.h"
#include "logging.h"
#include "util.h"
#include "networking.h"

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


	#define BIND (0)
	#define BIND_VALUE (1)
	#define BIND_BRACKETS_OPEN (2)
	#define BIND_BRACKETS_CLOSE (3)
	#define BIND_CONTENT (4)
	#define SITE_BRACKETS_OPEN (5)
	#define SITE_BRACKETS_CLOSE (6)
	#define SITE_CONTENT (7)
	#define SITE_HOST_EQUALS (8)
	#define SITE_HOST_VALUE (9)
	#define SITE_ROOT_EQUALS (10)
	#define SITE_ROOT_VALUE (11)
	#define HANDLER_VALUE (12)
	#define HANDLER_BRACKETS_OPEN (13)
	#define HANDLER_BRACKETS_CLOSE (14)
	#define HANDLER_CONTENT (15)
	#define TYPE_EQUALS (16)
	#define TYPE_VALUE (17)
	#define INDEX_EQUALS (18)
	#define INDEX_VALUE (19)
	int state = BIND;

	struct config_bind* currentBind = NULL;
	struct config_site* currentSite = NULL;
	struct config_handler* currentHandler = NULL;

	char currentToken[MAX_TOKEN_LENGTH];
	int currentTokenLength = 0;
	int line = 0;

	int c;
	while((c = fgetc(file)) != EOF) {
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
			int currentLine = line;

			if (c == '\n')
				line++;

			if (currentTokenLength == 0)
				continue;
						
			currentToken[currentTokenLength++] = '\0';

			switch(state) {
				case BIND:
					if (strcmp(currentToken, "bind") != 0) {
						error("config: Unexpected token '%s' on line %d. 'bind' expected", currentToken, currentLine);
						freeEverything(toFree, toFreeLength);
						return NULL;
					}

					currentBind = malloc(sizeof(struct config_bind));
					if (currentBind == NULL) {
						error("config: couldn't allocate for bind struct: %s", strerror(errno));
						freeEverything(toFree, toFreeLength);
						return NULL;
					}
					replaceOrAdd(toFree, &toFreeLength, NULL, currentBind);

					config->nrBinds++;
					struct config_bind** tmp = realloc(config->binds, config->nrBinds * sizeof(struct config_bind*));
					if (tmp == NULL) {
						error("config: couldn't reallocate for bind array: %s", strerror(errno));
						freeEverything(toFree, toFreeLength);
						return NULL;
					}

					replaceOrAdd(toFree, &toFreeLength, config->binds, tmp);
					config->binds = tmp;
					config->binds[config->nrBinds - 1] = currentBind;

					currentBind->nrSites = 0;
					currentBind->sites = NULL;
					currentBind->addr = NULL;
					currentBind->port = NULL;

					state = BIND_VALUE;

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
						host = strclone("0.0.0.0");
					else
						host = strclone(currentToken);

					if (host == NULL) {
						error("config: error cloning host string");
						freeEverything(toFree, toFreeLength);
						return NULL;
					}
					replaceOrAdd(toFree, &toFreeLength, NULL, host);

					char* port = strclone(seperator + 1);
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
				case BIND_BRACKETS_CLOSE:
					if (strcmp(currentToken, "}") != 0) {
						error("config: Unexpected token '%s' on line %d. '}' expected", currentToken, currentLine);
						freeEverything(toFree, toFreeLength);
						return NULL;
					}

					state = BIND;

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
					} else {
						error("config: Unknown property '%s' on line %d.", currentToken, currentLine);
						freeEverything(toFree, toFreeLength);
						return NULL;
					}
					break;
				case SITE_BRACKETS_OPEN:
					if (strcmp(currentToken, "{") != 0) {
						error("config: Unexpected token '%s' on line %d. '{' expected", currentToken, currentLine);
						freeEverything(toFree, toFreeLength);
						return NULL;
					}

					state = SITE_CONTENT;
					break;
				case SITE_BRACKETS_CLOSE:
					if (strcmp(currentToken, "}") != 0) {
						error("config: Unexpected token '%s' on line %d. '}' expected", currentToken, currentLine);
						freeEverything(toFree, toFreeLength);
						return NULL;
					}

					state = BIND_CONTENT;
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

						state = SITE_BRACKETS_OPEN;
					} else if (strcmp(currentToken, "host") == 0 || strcmp(currentToken, "alias") == 0) {
						state = SITE_HOST_EQUALS;
					} else if (strcmp(currentToken, "root") == 0) {
						state = SITE_ROOT_EQUALS;
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
				case SITE_HOST_VALUE: ;
					char** tmpArray = realloc(currentSite->hostnames, ++currentSite->nrHostnames * sizeof(char*));
					if (tmp == NULL) {
						error("config: error allocating hostname array");
						freeEverything(toFree, toFreeLength);
						return NULL;
					}
					replaceOrAdd(toFree, &toFreeLength, currentSite->hostnames, tmpArray);
					currentSite->hostnames = tmpArray;
					
					char* clone = strclone(currentToken);
					if (clone == NULL) {
						error("config: error cloning hostname string");
						freeEverything(toFree, toFreeLength);
						return NULL;
					}
					replaceOrAdd(toFree, &toFreeLength, NULL, clone);
					
					currentSite->hostnames[currentSite->nrHostnames - 1] = clone;

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
					clone = strclone(currentToken);
					if (clone == NULL) {
						error("config: error cloning document root string");
						freeEverything(toFree, toFreeLength);
						return NULL;
					}
					replaceOrAdd(toFree, &toFreeLength, NULL, clone);
					
					currentSite->documentRoot = clone;

					state = SITE_CONTENT;
					break;
				case HANDLER_VALUE:
					currentHandler->dir = strclone(currentToken);
					if (clone == NULL) {
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
				case HANDLER_BRACKETS_CLOSE:
					if (strcmp(currentToken, "{") != 0) {
						error("config: Unexpected token '%s' on line %d. '{' expected", currentToken, currentLine);
						freeEverything(toFree, toFreeLength);
						return NULL;
					}

					state = SITE_CONTENT;
					break;
				case HANDLER_CONTENT:
					if (strcmp(currentToken, "type") == 0) {
						state = TYPE_EQUALS;
					} else if (strcmp(currentToken, "index") == 0) {
						state = INDEX_EQUALS;
					}  else {
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
						error("config: unexpected 'index'; this is not a file handler");
						freeEverything(toFree, toFreeLength);
						return NULL;
					}

					struct fileSettings* settings = &(currentHandler->settings.fileSettings);

					tmpArray = realloc(settings->indexfiles.files, ++(settings->indexfiles.number) * sizeof(char*));
					if (tmp == NULL) {
						error("config: error allocating hostname array");
						freeEverything(toFree, toFreeLength);
						return NULL;
					}
					replaceOrAdd(toFree, &toFreeLength, settings->indexfiles.files, tmpArray);
					settings->indexfiles.files = tmpArray;

					clone = strclone(currentToken);
					if (clone == NULL) {
						error("config: error cloning hostname string");
						freeEverything(toFree, toFreeLength);
						return NULL;
					}
					replaceOrAdd(toFree, &toFreeLength, NULL, clone);
					
					settings->indexfiles.files[settings->indexfiles.number - 1] = clone;

					state = HANDLER_CONTENT;
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

	if (state != BIND) {
		error("config: unexpected EOF");
		freeEverything(toFree, toFreeLength);
		return NULL;
	}

	for (int i = 0; i < config->nrBinds; i++) {
		currentBind = config->binds[i];
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
						fileSettings->documentRoot = documentRoot;
						break;
					case CGI_HANDLER_NO: ;
						struct cgiSettings* cgiSettings = &(currentHandler->settings.cgiSettings);
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

struct networkingConfig config_getNetworkingConfig(struct config* config) {
	struct networkingConfig networkingConfig = {};


	return networkingConfig;
}
struct handler config_getHandler(struct config* config, struct metaData metaData, const char* host, struct bind* bind) {
	struct handler handler = {};
	
	return handler;
}

void config_destroy(struct config* config) {

}
