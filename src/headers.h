#ifndef HEADERS_H
#define HEADERS_H

struct header {
	char* key;
	char* value;
};

struct headers {
	int number;
	struct header* headers;
};

#endif
