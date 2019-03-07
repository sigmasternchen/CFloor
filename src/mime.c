#include <string.h>
#include <stdio.h>

#include "mime.h"

struct mime {
	const char* mime;
	int number;
	const char** extensions;
} mimelist[] = {
	{
		.mime = "application/pdf",
		.number = 1,
		.extensions = (const char* []) {
			"pdf"
		}
	},{
		.mime = "text/css",
		.number = 1,
		.extensions = (const char* []) {
			"css"
		}
	},{
		.mime = "text/html",
		.number = 2,
		.extensions = (const char* []) {
			"html",
			"htm"
		}
	},{
		.mime = "application/javascript",
		.number = 1,
		.extensions = (const char* []) {
			"js"
		}
	},{
		.mime = "application/json",
		.number = 1,
		.extensions = (const char* []) {
			"json"
		}
	},{
		.mime = "application/xml",
		.number = 1,
		.extensions = (const char* []) {
			"xml"
		}
	},{
		.mime = "image/jpeg",
		.number = 2,
		.extensions = (const char* []) {
			"jpg",
			"jpeg"
		}
	},{
		.mime = "image/gif",
		.number = 1,
		.extensions = (const char* []) {
			"gif"
		}
	},{
		.mime = "image/x-icon",
		.number = 1,
		.extensions = (const char* []) {
			"ico"
		}
	},{
		.mime = "image/png",
		.number = 1,
		.extensions = (const char* []) {
			"png"
		}
	},{
		.mime = "image/svg+xml",
		.number = 1,
		.extensions = (const char* []) {
			"svg"
		}
	},{
		.mime = "audio/mpeg",
		.number = 1,
		.extensions = (const char* []) {
			"mpga"
		}
	},{
		.mime = "video/mpeg",
		.number = 1,
		.extensions = (const char* []) {
			"mpeg"
		}
	},{
		.mime = "audio/mp4",
		.number = 4,
		.extensions = (const char* []) {
			"mp4a",
			"m4a"
		}
	},{
		.mime = "video/mp4",
		.number = 1,
		.extensions = (const char* []) {
			"mp4"
		}
	},{
		.mime = "video/webm",
		.number = 1,
		.extensions = (const char* []) {
			"webm"
		}
	},{
		.mime = "audio/aac",
		.number = 1,
		.extensions = (const char* []) {
			"aac"
		}
	},{
		.mime = "text/plain",
		.number = 1,
		.extensions = (const char* []) {
			"txt"
		}
	},{
		.mime = "audio/ogg",
		.number = 2,
		.extensions = (const char* []) {
			"oga",
			"ogg"
		}
	},{
		.mime = "video/ogg",
		.number = 1,
		.extensions = (const char* []) {
			"ogv"
		}
	},{
		.mime = "image/tiff",
		.number = 2,
		.extensions = (const char* []) {
			"tif",
			"tiff"
		}
	},{
		.mime = "audio/wav",
		.number = 1,
		.extensions = (const char* []) {
			"wav"
		}
	},{
		.mime = "audio/webm",
		.number = 1,
		.extensions = (const char* []) {
			"weba"
		}
	},{
		.mime = "image/webm",
		.number = 1,
		.extensions = (const char* []) {
			"webp"
		}
	},{
		.mime = "application/xhtml+xml",
		.number = 1,
		.extensions = (const char* []) {
			"xhtml"
		}
	},{
		.mime = "audio/aac",
		.number = 1,
		.extensions = (const char* []) {
			"aac"
		}
	},
};

const char* unknownMime = "application/octet-stream";


const char* getMineFromFileName(const char* filename) {
	const char* extension = NULL;
	const char* tmp = filename;

	while ((tmp = strcasestr(tmp, ".")) != NULL) {
		tmp += 1;
		extension = tmp;
	}
	if (extension == NULL)
		return unknownMime;

	for (int i = 0; i < (sizeof(mimelist) / sizeof(mimelist[0])); i++) {
		for(int j = 0; j < mimelist[i].number; j++) {
			if (strcmp(extension, mimelist[i].extensions[j]) == 0)
				return mimelist[i].mime;
		}
	}

	return unknownMime;
}
