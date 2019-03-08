#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include "cgi.h"
#include "misc.h"
#include "util.h"
#include "files.h"
#include "status.h"
#include "logging.h"
#include "headers.h"
#define EXIT_EXEC_FAILED (255)

static inline void setEnvStatic(const char* envname, const char* value) {
	if (setenv(envname, value, true) < 0)
		exit(EXIT_EXEC_FAILED);
}

static inline void setEnvFromHeader(struct headers* headers, const char* envname, const char* headerKey) {
	const char* tmp = headers_get(headers, headerKey);
	setEnvStatic(envname, tmp == NULL ? "" : tmp);
}

void cgiHandler(struct request request, struct response response) {
	struct cgiSettings* settings = (struct cgiSettings*) request.userData.ptr;
	const char* documentRoot = settings->documentRoot;

	char* path = normalizePath(request, response, documentRoot);
	if (path == NULL)
		return;

	if (access(path, F_OK | X_OK) < 0) {
		free(path);
		
		switch(errno) {
			case EACCES:
				warn("cgi: file is not executable");
				status(request, response, 403);
				return;
			default:
				// this should not happen
				error("cgi: Couldn't access file: %s", strerror(errno));
				status(request, response, 500);
				return;
		}
	}

	struct stat statObj;
	if (stat(path, &statObj) < 0) {
		free(path);

		error("cgi: Couldn't stat file: %s", strerror(errno));
		status(request, response, 500);		
		return;
	}

	if (!S_ISREG(statObj.st_mode)) {
		free(path);
		
		error("cgi: Not a regular file");
		status(request, response, 403);
		return;
	}


	int pipefd[2];

	if (pipe(&(pipefd[0])) < 0) {
		error("cgi: failed to create pipe: %s", strerror(errno));
		status(request, response, 500);		

		free(path);
		return;
	}

	pid_t pid = fork();
	if (pid < 0) {
		error("cgi: failed to fork: %s", strerror(errno));
		status(request, response, 500);
	} else if (pid == 0) {
		// child
		// logging is thread-only
		// so no logging from here on out.
			
		close(pipefd[0]);

		if (dup2(request.fd, 0) < 0) {
			exit(EXIT_EXEC_FAILED);
		}
		if (dup2(pipefd[1], 1) < 0) {
			exit(EXIT_EXEC_FAILED);
		}

		struct headers* headers = request.headers;

		setEnvStatic("GATEWAY_INTERFACE", "CGI/1.1");
		setEnvStatic("DOCUMENT_ROOT", documentRoot);
		setEnvStatic("HTTPS", request.bind.tls ? "on" : "off");
		setEnvStatic("QUERY_STRING", request.metaData.queryString);
		setEnvStatic("REQUEST_METHOD", methodString(request.metaData));
		setEnvStatic("REQUEST_URI", request.metaData.uri);
		setEnvStatic("REMOTE_ADDR", request.peer.addr);
		setEnvStatic("REMOTE_HOST", request.peer.name);
		setEnvStatic("REMOTE_PORT", request.peer.portStr);
		setEnvStatic("SCRIPT_NAME", request.metaData.path);
		setEnvStatic("SERVER_PROTOCOL", protocolString(request.metaData));

		setEnvStatic("SERVER_NAME", ""); // TODO maybe bind addr?
		setEnvStatic("SERVER_ADMIN", ""); // TODO
		setEnvStatic("SERVER_ADDR", ""); // TODO
		setEnvStatic("SERVER_PORT", ""); // TODO
		setEnvStatic("SERVER_SIGNATURE", ""); // TODO
		setEnvStatic("SERVER_SOFTWARE", ""); // TODO
		
		setEnvStatic("REDIRECT_REMOTE_USER", ""); // TODO not implemented
		setEnvStatic("REMOTE_USER", ""); // TODO not implemented

		setEnvFromHeader(headers, "HTTP_ACCEPT_CHARSET", "Accept-Charset");
		setEnvFromHeader(headers, "HTTP_ACCEPT_ENCODING", "Accept-Encoding");
		setEnvFromHeader(headers, "HTTP_ACCEPT_LANGUAGE", "Accept-Language");
		setEnvFromHeader(headers, "HTTP_CONNECTION", "Accept-Connection");
		setEnvFromHeader(headers, "HTTP_ACCEPT", "Accept");
		setEnvFromHeader(headers, "HTTP_HOST", "Host");
		setEnvFromHeader(headers, "HTTP_USER_AGENT", "User-Agent");
		setEnvFromHeader(headers, "HTTP_COOKIE", "Cookie");
		setEnvFromHeader(headers, "CONTENT_TYPE", "Content-Type");
		setEnvFromHeader(headers, "CONTENT_LENGTH", "Content-Length");
		
		execl(path, path, NULL);

		exit(EXIT_EXEC_FAILED);
	} else {
		// this is the parent, but the child can't talk for itself
		info("cgi: child started successfully");

		close(pipefd[1]);
		
		#define LOCAL_BUFFER_LENGTH (512)

		char buffer[LOCAL_BUFFER_LENGTH];
		int bufferIndex = 0;

		bool wasLineEnd = false;
		bool finished = false;

		struct headers headers = headers_create();

		char c;
		while((read(pipefd[0], &c, 1)) > 0) {
			bool malformed = false;

			if (bufferIndex >= LOCAL_BUFFER_LENGTH - 1)
				malformed = true;
			else if (c == '\r')
				continue;
			else if (c == '\n') {
				if (wasLineEnd) {
					finished = true;
					break;
				}

				buffer[bufferIndex] = '\0';

				int tmp = headers_parse(&headers, &(buffer[0]), bufferIndex);
				if (tmp < 0) {
					debug("cgi: error parsing header: '%s'", buffer);
					malformed = true;
				}

				bufferIndex = 0;

				wasLineEnd = true;
			} else {
				wasLineEnd = false;

				buffer[bufferIndex++] = c;
			}

			if (malformed) {
				error("cgi: response malformed");

				finished = false;
				break; 
			}
		}

		int statusCode = 200;

		const char* statusLine = headers_get(&headers, "Status");
		if (statusLine != NULL) {
			char* endptr;

			statusCode = strtol(statusLine, &endptr, 10);

			if ((statusCode < 100) || (statusCode > 600)) {
				error("cgi: malformed status code: %s", statusLine);
				finished = false;
			} else {
				headers_remove(&headers, "Status");
			}
		}

		if (!finished) {
			error("cgi: error while reading header");

			kill(pid, SIGTERM);
			waitpid(pid, &statusCode, 0);

			close(pipefd[0]);

			status(request, response, 500);
			
			free(path);
			headers_free(&headers);
			
			return;
		}

		int fd = response.sendHeader(statusCode, &headers, &request);

		headers_free(&headers);

		free(path);

		pthread_t copyThread;

		startCopyThread(pipefd[0], fd, true, &copyThread);

		if (waitpid(pid, &statusCode, 0) < 1) {
			error("cgi: error while waiting for child: %s", strerror(errno));
			status(request, response, 500);

			pthread_cancel(copyThread);

			return;
		}

		// WIFEXISTED has to be true
		statusCode = WEXITSTATUS(statusCode);

		if (statusCode == EXIT_EXEC_FAILED) {
			error("cgi: child exit code indicates that exec failed");
			status(request, response, 500);
		}

		debug("cgi: fork returned with status %d", statusCode);

		pthread_join(copyThread, NULL);

		return;
	}
}
