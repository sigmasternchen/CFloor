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
#include "util.h"
#include "logging.h"
#include "status.h"
#include "mime.h"


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

void freeDirent(struct dirent** list, int number) {
	if (list == NULL || number == 0)
		return;

	for(int i = 0; i < number; i++) {
		free(list[i]);
	}
	free(list);
}

int showIndex(int fd, const char* path, const char* documentRoot) {
	// TODO check for htmml entities

	const char* relative = path + strlen(documentRoot);

	struct dirent** list;
	int number = scandir(path, &list, &scandirFiler, &scandirSort);

	if (number < 0) {
		error("files: Couldn't read dir: %s", strerror(errno));
		return -1;
	}

	FILE* stream = fdopen(fd, "w");
	if (stream == NULL) {
		freeDirent(list, number);
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

	fclose(stream);

	freeDirent(list, number);

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
	struct fileSettings* settings = (struct fileSettings*) request.userData.ptr;
	const char* documentRoot = settings->documentRoot;
	bool indexes = settings->index;

	char* path = normalizePath(request, response, documentRoot);
	if (path == NULL)
		return;

	struct stat statObj, statObjFile;
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

		char* filepath = NULL;
		for (int i = 0; i < settings->indexfiles.number; i++) {
			filepath = malloc(strlen(path) + 1 + strlen(settings->indexfiles.files[i]) + 1);
			if (filepath == NULL) {
				error("files: Couldn't allocate memory for index file check: %s", strerror(errno));
				warn("files: ignoring");
				continue;
			}
			strcpy(filepath, path);
			strcat(filepath, "/");
			strcat(filepath, settings->indexfiles.files[i]);

			debug("files: searching for index: %s", filepath);

			if (access(filepath, F_OK | R_OK) == 0 && stat(filepath, &statObjFile) == 0) {
				if (S_ISREG(statObjFile.st_mode)) {
					break;
				}
			}
			free(filepath);
			filepath = NULL;
		}

		if (filepath != NULL) {
			debug("files: found index file: %s", filepath);
			overrideStat = true;
			free(path);
			path = filepath;
			statObj = statObjFile;
		} else {

			if (!indexes) {
				free(path);
				status(request, response, 403);
				return;
			}

			struct headers headers = headers_create();
			headers_mod(&headers, "Content-Type", "text/html; charset=utf-8");
			int fd = response.sendHeader(200, &headers, &request);
			headers_free(&headers);

			if (showIndex(fd, path, documentRoot) < 0) {
				// TODO error
			}
		
			done = true;
		}
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
		char* tmp = malloc(length + 1);
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

char* normalizePath(struct request request, struct response response, const char* documentRoot) {
	if (documentRoot == NULL) {
		error("files: No document root given.");
		status(request, response, 500);
		return NULL;
	}

	char* path = request.metaData.path;

	char* tmp = malloc(strlen(path) + 1 + strlen(documentRoot) + 1);
	if (tmp == NULL) {
		error("files: Couldn't malloc for path construction: %s", strerror(errno));
		status(request, response, 500);
		return NULL;
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
				return NULL;
			case ENOENT:
			case ENOTDIR:
				status(request, response, 404);
				return NULL;
			default:
				warn("files: Couldn't get constructed realpath: %s", strerror(errno));
				status(request, response, 500);
				return NULL;
		}
		status(request, response, 500);
		return NULL;
	}
	free(tmp);

	info("files: file path is: %s", path);

	if (strncmp(documentRoot, path, strlen(documentRoot)) != 0) {
		free(path);
		warn("files: Requested path not in document root.");
		fuckyouHandler(request, response);

		return NULL;
	}

	if (access(path, F_OK | R_OK) < 0) {
		free(path);
		
		switch(errno) {
			case EACCES:
				status(request, response, 403);
				return NULL;
			case ENOENT:
			case ENOTDIR:
				status(request, response, 404);
				return NULL;
			default:
				warn("files: Couldn't access file: %s", strerror(errno));
				status(request, response, 500);
				return NULL;
		}
	}

	return path;
}
