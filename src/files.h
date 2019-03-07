#ifndef FILES_H
#define FILES_H

#include <stdbool.h>

#include "files.h"
#include "misc.h"

struct fileSettings {
	const char* documentRoot;
	bool index;
};

void fileHandler(struct request request, struct response response);

char* normalizePath(struct request request, struct response response, const char* documentRoot);

#endif
