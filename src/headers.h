#ifndef HEADERS_H
#define HEADERS_H

#include <stdio.h>

#include "misc.h"

#define HEADERS_SUCCESS (0)
#define HEADERS_PARSE_ERROR (-1)
#define HEADERS_ALLOC_ERROR (-2)
#define HEADERS_END (-3)

struct header {
	char* key;
	char* value;
};

struct headers {
	int number;
	struct header* headers;
};

struct headers headers_create();
const char* headers_get(struct headers* headers, const char* key);
int headers_mod(struct headers* headers, const char* key, const char* value);
int headers_parse(struct headers* headers, const char* currentHeader, size_t length);
void headers_free(struct headers* headers);
void headers_dump(struct headers* headers, FILE* stream);

int headers_metadata(struct metaData* metaData, char* header);

#endif
