#ifndef FILES_H
#define FILES_H

#include <stdbool.h>

#include "files.h"
#include "misc.h"

void files_init(const char* documentRoot, bool index);

void fileHandler(struct request request, struct response response);

#endif
