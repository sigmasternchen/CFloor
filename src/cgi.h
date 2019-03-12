#ifndef CGI_H
#define CGI_H

#include "misc.h"

#define CGI_HANDLER_NO (1)

struct cgiSettings {
	const char* documentRoot;
};

void cgiHandler(struct request, struct response);

#endif
