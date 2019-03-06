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
#include <dirent.h>

#include "files.h"
#include "misc.h"
#include "logging.h"
#include "status.h"
#include "mime.h"

const char* documentRoot = NULL;
bool indexes = false;

// _documentRoot had to be a realpath
void files_init(const char* _documentRoot, bool _index) {
	documentRoot = _documentRoot;
	indexes = _index;
}

int scandirFiler(const struct dirent* entry) {
	if (strcmp(entry->d_name, "..") == 0)
		return 1;
	if (strncmp(entry->d_name, ".", 1) == 0)
		return 0;
	return 1;
}

int scandirSort(const struct dirent** a, const struct dirent** b) {
	if ((*a)->d_type == DT_DIR && (*b)->d_type != DT_DIR)
		return -1; 
	if ((*a)->d_type != DT_DIR && (*b)->d_type == DT_DIR)
		return 1;

	return strcmp((*a)->d_name, (*b)->d_name); 
}

int showIndex(int fd, const char* path) {
	const char* relative = path + strlen(documentRoot);

	struct dirent** list;
	int number = scandir(path, &list, &scandirFiler, &scandirSort);

	if (number < 0) {
		error("files: Couldn't read dir: %s", strerror(errno));
		return -1;
	}

	FILE* stream = fdopen(fd, "w");
	if (stream == NULL) {
		free(list);
		error("files: Couldn't get stream from fd: %s", strerror(errno));
		return -1;
	}

	fprintf(stream, "<!DOCTYPE html>\n");
	fprintf(stream, "<html>\n");
	fprintf(stream, "	<head>\n");
	fprintf(stream, "		<title>Index of %s/</title>\n", relative);
	fprintf(stream, "	</head>\n");
	fprintf(stream, "	<body>\n");
	fprintf(stream, "		<h1>Index of %s/</h1>\n", relative);
	fprintf(stream, "		<table>\n");
	fprintf(stream, "			<tr>\n");
	fprintf(stream, "				<th>Type</th>\n");
	fprintf(stream, "				<th>File</th>\n");
	fprintf(stream, "			</tr>\n");

	for(int i = 0; i < number; i++) {
		struct dirent* entry = list[i];
		if (strcmp(entry->d_name, "..") == 0 && strcmp(relative, "") == 0)
			continue;
		fprintf(stream, "			<tr>\n");
		fprintf(stream, "				<td>%s</td>\n", (entry->d_type == DT_DIR) ? "D" : "");
		fprintf(stream, "				<td><a href='%s/%s'>%s</a></td>\n", relative, entry->d_name, entry->d_name);
		fprintf(stream, "			</tr>\n");
	}
	
	fprintf(stream, "		<table>\n");
	fprintf(stream, "	</body>\n");
	fprintf(stream, "</html>\n");

	free(list);
	fclose(stream);

	return 0;
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
		switch(errno) {
			case EACCES:
				status(request, response, 403);
				return;
			case ENOENT:
			case ENOTDIR:
				status(request, response, 404);
				return;
			default:
				warn("files: Couldn't get constructed realpath: %s", strerror(errno));
				status(request, response, 500);
				return;
		}
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

	bool done = false;
	bool overrideStat = false;

	if (S_ISDIR(statObj.st_mode)) {
		// TODO check for index files

		if (!indexes) {
			status(request, response, 403);
			return;
		}

		struct headers headers = headers_create();
		headers_mod(&headers, "Content-Type", "text/html; charset=utf-8");
		int fd = response.sendHeader(200, &headers, &request);
		headers_free(&headers);

		if (showIndex(fd, path) < 0) {
			// TODO error
		}
		
		done = true;
	}

	if (S_ISREG(statObj.st_mode) || overrideStat) {
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

		done = true;	
	}

	if (!done) {
		status(request, response, 500);
	}

	free(path);
}
