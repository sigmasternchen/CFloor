#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "status.h"
#include "misc.h"
#include "logging.h"

struct statusStrings getStatusStrings(int status) {
	switch(status) {
		case 100:
			return (struct statusStrings) {
				.statusString = "Continue",
				.statusFormat = ""
			};
		case 101:
			return (struct statusStrings) {
				.statusString = "Switching Protocols",
				.statusFormat = ""
			};
		case 102:
			return (struct statusStrings) {
				.statusString = "Processing",
				.statusFormat = ""
			};

		case 200:
			return (struct statusStrings) {
				.statusString = "OK",
				.statusFormat = ""
			};
		case 201:
			return (struct statusStrings) {
				.statusString = "Created",
				.statusFormat = ""
			};
		case 202:
			return (struct statusStrings) {
				.statusString = "Accepted",
				.statusFormat = ""
			};
		case 203:
			return (struct statusStrings) {
				.statusString = "Non-Authoritative Information",
				.statusFormat = ""
			};
		case 204:
			return (struct statusStrings) {
				.statusString = "No Content",
				.statusFormat = ""
			};
		case 205:
			return (struct statusStrings) {
				.statusString = "Reset Content",
				.statusFormat = ""
			};
		case 206:
			return (struct statusStrings) {
				.statusString = "Partial Content",
				.statusFormat = ""
			};
		case 207:
			return (struct statusStrings) {
				.statusString = "Multi-Status",
				.statusFormat = ""
			};
		case 208:
			return (struct statusStrings) {
				.statusString = "Already Reported",
				.statusFormat = ""
			};
		case 226:
			return (struct statusStrings) {
				.statusString = "IM Used",
				.statusFormat = ""
			};

		case 300:
			return (struct statusStrings) {
				.statusString = "Multiple Choices",
				.statusFormat = ""
			};
		case 301:
			return (struct statusStrings) {
				.statusString = "Moved Permanently",
				.statusFormat = ""
			};
		case 302:
			return (struct statusStrings) {
				.statusString = "Found (Moved Temporarily)",
				.statusFormat = ""
			};
		case 303:
			return (struct statusStrings) {
				.statusString = "See Other",
				.statusFormat = ""
			};
		case 304:
			return (struct statusStrings) {
				.statusString = "Not Modified",
				.statusFormat = ""
			};
		case 305:
			return (struct statusStrings) {
				.statusString = "Use Proxy",
				.statusFormat = ""
			};
		case 306:
			return (struct statusStrings) {
				.statusString = "(reserved)",
				.statusFormat = ""
			};
		case 307:
			return (struct statusStrings) {
				.statusString = "Temporary Redirect",
				.statusFormat = ""
			};
		case 308:
			return (struct statusStrings) {
				.statusString = "Permanent Redirect",
				.statusFormat = ""
			};

		case 400:
			return (struct statusStrings) {
				.statusString = "Bad Request",
				.statusFormat = ""
			};
		case 401:
			return (struct statusStrings) {
				.statusString = "Unauthorized",
				.statusFormat = ""
			};
		case 402:
			return (struct statusStrings) {
				.statusString = "Payment Required",
				.statusFormat = ""
			};
		case 403:
			return (struct statusStrings) {
				.statusString = "Forbidden",
				.statusFormat = "You don't have the permission to access %s."
			};
		case 404:
			return (struct statusStrings) {
				.statusString = "Not found",
				.statusFormat = "The file %s was not found on this server."
			};
		case 405:
			return (struct statusStrings) {
				.statusString = "Method Not Allowed",
				.statusFormat = ""
			};
		case 406:
			return (struct statusStrings) {
				.statusString = "Not Acceptable",
				.statusFormat = ""
			};
		case 407:
			return (struct statusStrings) {
				.statusString = "Proxy Auhentication Required",
				.statusFormat = ""
			};
		case 408:
			return (struct statusStrings) {
				.statusString = "Request Timeout",
				.statusFormat = ""
			};
		case 409:
			return (struct statusStrings) {
				.statusString = "Conflict",
				.statusFormat = ""
			};
		case 410:
			return (struct statusStrings) {
				.statusString = "Gone",
				.statusFormat = ""
			};
		case 411:
			return (struct statusStrings) {
				.statusString = "Length Required",
				.statusFormat = ""
			};
		case 412:
			return (struct statusStrings) {
				.statusString = "Precondition Failed",
				.statusFormat = ""
			};
		case 413:
			return (struct statusStrings) {
				.statusString = "Request Entity Too Large",
				.statusFormat = ""
			};
		case 414:
			return (struct statusStrings) {
				.statusString = "URI Too Long",
				.statusFormat = ""
			};
		case 415:
			return (struct statusStrings) {
				.statusString = "Unsupported Media Type",
				.statusFormat = ""
			};
		case 416:
			return (struct statusStrings) {
				.statusString = "Requested Range Not Satisfiable",
				.statusFormat = ""
			};
		case 417:
			return (struct statusStrings) {
				.statusString = "Expectation Failed",
				.statusFormat = ""
			};
		case 420:
			return (struct statusStrings) {
				.statusString = "Policy Not Fulfilled",
				.statusFormat = ""
			};
		case 421:
			return (struct statusStrings) {
				.statusString = "Misdirected Request",
				.statusFormat = ""
			};
		case 422:
			return (struct statusStrings) {
				.statusString = "Unprocessable Entity",
				.statusFormat = ""
			};
		case 423:
			return (struct statusStrings) {
				.statusString = "Locked",
				.statusFormat = ""
			};
		case 424:
			return (struct statusStrings) {
				.statusString = "Failed Dependency",
				.statusFormat = ""
			};
		case 426:
			return (struct statusStrings) {
				.statusString = "Upgrade Required",
				.statusFormat = ""
			};
		case 428:
			return (struct statusStrings) {
				.statusString = "Precondition Required",
				.statusFormat = ""
			};
		case 429:
			return (struct statusStrings) {
				.statusString = "Too Many Requests",
				.statusFormat = ""
			};
		case 431:
			return (struct statusStrings) {
				.statusString = "Request Header Fields Too Large",
				.statusFormat = ""
			};
		case 451:
			return (struct statusStrings) {
				.statusString = "Unavailable For Legal Reasons",
				.statusFormat = ""
			};

		case 500:
			return (struct statusStrings) {
				.statusString = "Internal Server Error",
				.statusFormat = ""
			};
		case 501:
			return (struct statusStrings) {
				.statusString = "Not Implemented",
				.statusFormat = ""
			};
		case 502:
			return (struct statusStrings) {
				.statusString = "Bad Gateway",
				.statusFormat = ""
			};
		case 503:
			return (struct statusStrings) {
				.statusString = "Service Unavailable",
				.statusFormat = ""
			};
		case 504:
			return (struct statusStrings) {
				.statusString = "Gateway Timeout",
				.statusFormat = ""
			};
		case 505:
			return (struct statusStrings) {
				.statusString = "HTTP Version Not Supported",
				.statusFormat = ""
			};
		case 506:
			return (struct statusStrings) {
				.statusString = "Variant Also Negotiates",
				.statusFormat = ""
			};
		case 507:
			return (struct statusStrings) {
				.statusString = "Insufficient Storage",
				.statusFormat = ""
			};
		case 508:
			return (struct statusStrings) {
				.statusString = "Loop Detected",
				.statusFormat = ""
			};
		case 509:
			return (struct statusStrings) {
				.statusString = "Bandwidth Limit Exceeded",
				.statusFormat = ""
			};
		case 510:
			return (struct statusStrings) {
				.statusString = "Not Extended",
				.statusFormat = ""
			};
		case 511:
			return (struct statusStrings) {
				.statusString = "Notwork Authentication Required",
				.statusFormat = ""
			};

		default:
			return (struct statusStrings) {
				.statusString = "Unknown Status Code",
				.statusFormat = "This is pretty bad."
			};
	}
}

void status(struct request request, struct response response, int status) {
	struct headers headers = headers_create();
	headers_mod(&headers, "Content-Type", "text/html; charset=utf-8");
	int fd = response.sendHeader(status, &headers, &request);
	headers_free(&headers);

	FILE* stream = fdopen(fd, "w");
	if (stream == NULL) {
		error("status: Couldn't get stream from fd: %s", strerror(errno));
		return;
	}

	struct statusStrings string = getStatusStrings(status);

	fprintf(stream, "<!DOCTYPE html>\n");
	fprintf(stream, "<html>\n");
	fprintf(stream, "	<head>\n");
	fprintf(stream, "		<title>%s</title>\n", string.statusString);
	fprintf(stream, "	</head>\n");
	fprintf(stream, "	<body>\n");
	fprintf(stream, "		<h1>%s</h1>\n", string.statusString);
	fprintf(stream, string.statusFormat, request.metaData.path);
	fprintf(stream, "		<hr />\n");
	fprintf(stream, "	</body>\n");
	fprintf(stream, "</html>\n");

	fclose(stream);
}

void status500(struct request request, struct response response) {
	status(request, response, 500);
}
