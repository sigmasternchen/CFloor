#ifndef FILES_H
#define FILES_H

#include <stdbool.h>

#include "files.h"
#include "misc.h"

#define FILE_HANDLER_NO (0)

struct fileSettings {
	const char* documentRoot;
	bool index;
	struct {
		int number;
		char** files;
	} indexfiles;
};

void fileHandler(struct request request, struct response response);

char* normalizePath(struct request request, struct response response, const char* documentRoot);

#endif
