#ifndef STATUS_H
#define STATUS_H

#include "misc.h"

struct statusStrings {
	const char* statusString;
	const char* statusFormat;
};

struct statusStrings getStatusStrings(int status);

void status500(struct request request, struct response response);
void status(struct request request, struct response response, int status);

#endif
