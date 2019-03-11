#include <stdio.h>
#include <stding.h>
#include <errno.h>
#include <assert.h>

#include "config.h"
#include "logging.h"

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
					struct config_bind* tmp = realloc(config->binds, config->nrBinds * sizeof(struct config_bind));
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

					state = BIND_VALUE;

					break;
				case BIND_VALUE:
					break;
				case BIND_BRACKETS_OPEN:
					break;
				case BIND_CONTENT:
					break;
				case SITE_BRACKETS_OPEN:
					break;
				case SITE_BRACKETS_CLOSE:
					break;
				case SITE_CONTENT:
					break;
				case SITE_HOST_EQUALS:
					break;
				case SITE_HOST_VALUE:
					break;
				case SITE_ROOT_EQUALS: 
					break;
				case SITE_ROOT_VALUE:
					break;
				case HANDLER_VALUE:
					break;
				case HANDLER_BRACKETS_OPEN:
					break;
				case HANDLER_BRACKETS_CLOSE:
					break;
				case HANDLER_CONTENT:
					break;
				case TYPE_EQUALS:
					break;
				case TYPE_VALUE:
					break;
				case INDEX_EQUALS:
					break;
				case INDEX_VALUE:
					break;
				default:
					assert(false);
			}
			currentTokenLength = 0;
		} else {
			currentToken[currentTokenLength++] = c;
		}
	}

	return config;
}

struct networkingConfig config_getNetworkingConfig(struct config* config) {

}
struct handler config_getHandler(struct config* config, struct metaData metaData, const char* host, struct bind* bind) {

}

void config_destroy(struct config* config) {

}
