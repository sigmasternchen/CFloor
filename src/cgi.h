#ifndef CGI_H
#define CGI_H

#include "misc.h"

struct cgiSettings {
	const char* documentRoot;
};

void cgiHandler(struct request, struct response);

#endif
