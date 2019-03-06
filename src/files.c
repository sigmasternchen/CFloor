#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "files.h"
#include "misc.h"
#include "logging.h"
#include "status.h"
#include "mime.h"

const char* documentRoot = NULL;
bool indexes = false;

void files_init(const char* _documentRoot, bool _index) {
	documentRoot = _documentRoot;
	indexes = _index;
}

void showIndex(int fd, const char* path) {
	FILE* stream = fdopen(fd, "w");
	if (stream == NULL) {
		error("status: Couldn't get stream from fd: %s", strerror(errno));
		return;
	}

	// TODO index

	fclose(stream);
}


void fuckyouHandler(struct request request, struct response response) {
	struct headers headers = headers_create();
	headers_mod(&headers, "Content-Type", "text/plain");
	int fd = response.sendHeader(400, &headers, &request);
	headers_free(&headers);

	FILE* stream = fdopen(fd, "w");
	if (stream == NULL) {
		error("files: Couldn't get stream from fd: %s", strerror(errno));
		return;
	}

	fprintf(stream, "Status 400\nYou know... I'm not an idiot...\n");

	fclose(stream);
}

void fileHandler(struct request request, struct response response) {
	if (documentRoot == NULL) {
		error("files: No document root given.");
		status(request, response, 500);
		return;
	}
	
	char* path = request.metaData.path;

	char* tmp = malloc(strlen(path) + 1 + strlen(documentRoot) + 1);
	if (tmp == NULL) {
		error("files: Couldn't malloc for path construction: %s", strerror(errno));
		status(request, response, 500);
		return;
	}
	strcpy(tmp, documentRoot);
	strcat(tmp, "/");
	strcat(tmp, path);
	path = realpath(tmp, NULL);
	if (path == NULL) {
		free(tmp);
		error("files: Couldn't get constructed realpath: %s", strerror(errno));
		status(request, response, 500);
		return;
	}
	free(tmp);

	info("files: file path is: %s", path);

	if (strncmp(documentRoot, path, strlen(documentRoot)) != 0) {
		free(path);
		warn("files: Requested path not in document root.");
		fuckyouHandler(request, response);

		return;
	}

	if (access(path, F_OK | R_OK) < 0) {
		free(path);
		
		switch(errno) {
			case EACCES:
				status(request, response, 403);
				return;
			case ENOENT:
			case ENOTDIR:
				status(request, response, 404);
				return;
			default:
				warn("files: Couldn't access file: %s", strerror(errno));
				status(request, response, 500);
				return;
		}
	}

	struct stat statObj;
	if (stat(path, &statObj) < 0) {
		free(path);

		error("files: Couldn't stat file: %s", strerror(errno));
		status(request, response, 500);		
		return;
	}

	if (S_ISDIR(statObj.st_mode)) {
		struct headers headers = headers_create();
		headers_mod(&headers, "Content-Type", "text/html; charset=utf-8");
		int fd = response.sendHeader(200, &headers, &request);
		headers_free(&headers);

		// if indexes

		showIndex(fd, path);
	} else if (S_ISREG(statObj.st_mode)) {
		int filefd = open(path, O_RDONLY);
		if (filefd < 0) {
			free(path);
			status(request, response, 500);
			return;
		}

		off_t size = statObj.st_size;
		int length = strlenOfNumber(size);
		tmp = malloc(length + 1);
		if (tmp == NULL) {
			free(path);
			close(filefd);
			error("files: Couldn't allocate for content length: %s", strerror(errno));
			status(request, response, 500);
			return;
		}
		snprintf(tmp, length + 1, "%ld", size);

		struct headers headers = headers_create();
		headers_mod(&headers, "Content-Type", getMineFromFileName(path));
		headers_mod(&headers, "Content-Length", tmp);
		free(tmp);

		int sockfd = response.sendHeader(200, &headers, &request);
		headers_free(&headers);

		char c;

		while(read(filefd, &c, 1))
			write(sockfd, &c, 1);

		close(filefd);
		close(sockfd);		
	} else {
		status(request, response, 500);
	}

	free(path);
}
