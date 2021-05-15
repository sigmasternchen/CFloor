#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <sys/select.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include "networking.h"
#include "linked.h"
#include "logging.h"
#include "signals.h"
#include "headers.h"
#include "util.h"
#include "config.h"
#include "files.h"
#include "cgi.h"

bool global = true;
bool overall = true;

void checkBool(bool ok, const char* check) {
	const char* result;
	if (ok) {
		result = "[  OK  ]";
	} else {
		result = "[FAILED]";
		global = false;
	}

	printf("%s:%*s%s\n", check, (int) (30 - strlen(check)), "", result);
}
void checkInt(int value, int compare, const char* check) {
	checkBool(value == compare, check);
}
void checkString(const char* value, const char* compare, const char* check) {
	checkBool(strcmp(value, compare) == 0, check);
}
void checkVoid(const void* value, const void* compare, const char* check) {
	checkBool(value == compare, check);
}
void checkNull(void* value, const char* check) {
	checkBool(value != NULL, check);
}

void showError() {
	fprintf(stderr, "Error: %s\n", strerror(errno));
}

void testUtil() {
	const char* original = "Hello World";
	char* clone = strdup(original);

	strremove(clone, 4, 1);
	checkString(clone, "Hell World", "remove: middle");
	strremove(clone, 4, 6);
	checkString(clone, "Hell", "remove: end");
	strremove(clone, 0, 2);
	checkString(clone, "ll", "remove: start");
	
	free(clone);

	char* tmp;
	tmp = symbolicRealpath("/hello/world/");
	checkString(tmp, "/hello/world/", "realpath: no mod");
	free(tmp);
	tmp = symbolicRealpath("//hello//world//");
	checkString(tmp, "/hello/world/", "realpath: //");
	free(tmp);
	tmp = symbolicRealpath("hello/world/");
	checkString(tmp, "/hello/world/", "realpath: no /");
	free(tmp);
	tmp = symbolicRealpath("/hello/././world/");
	checkString(tmp, "/hello/world/", "realpath: ./");
	free(tmp);
	tmp = symbolicRealpath("/hello/../world/");
	checkString(tmp, "/world/", "realpath: norm ..");
	free(tmp);
	tmp = symbolicRealpath("hello/../../world/");
	checkString(tmp, "../world/", "realpath: over ..");
	free(tmp);
	tmp = symbolicRealpath("/hello/../../../world/");
	checkString(tmp, "../../world/", "realpath: double over ..");
	free(tmp);
}

void testLinkedList() {
	linkedList_t list = linked_create();
	
	const char* testString = "Test";

	checkInt(linked_length(&list), 0, "empty list length");
	checkInt(linked_push(&list, testString), 0, "insert position");
	checkInt(linked_length(&list), 1, "list length");
	link_t* link = linked_get(&list, 0);
	checkNull(link, "get not null");
	checkVoid(link->data, testString, "test string value");
	checkInt(link->inUse, 1, "inUse counter value");
	linked_release(link);
	checkInt(link->inUse, 0, "inUse counter value");

	checkInt(linked_push(&list, (void*) 1), 1, "insert position");
	checkInt(linked_push(&list, (void*) 2), 2, "insert position");
	checkInt(linked_push(&list, (void*) 3), 3, "insert position");
	checkInt(linked_length(&list), 4, "list length");

	link = linked_first(&list);
	checkInt(linked_unlink(link), 0, "unlink first result");
	linked_release(link);
	checkInt(linked_length(&list), 3, "link length");

	link = linked_first(&list);
	checkNull(link, "get not null");
	checkInt((long) link->data, 1, "get value");

	link = linked_next(link);
	checkNull(link, "get not null");
	checkInt((long) link->data, 2, "get value");

	checkInt(linked_unlink(link), 0, "unlink not first result");

	link = linked_next(link);
	checkNull(link, "get not null");
	checkInt((long) link->data, 3, "get value");

	linked_release(link);

	checkInt(linked_length(&list), 2, "list length");

	if (pthread_mutex_trylock(&(list.lock)) != 0) {
		checkBool(false, "list unlocked");
	} else {
		checkBool(true, "list unlocked");
		pthread_mutex_unlock(&(list.lock));
	}

	link = list.first;
	while(link != NULL) {
		checkInt(link->inUse, 0, "raw inUse");
		
		if (pthread_mutex_trylock(&(link->lock)) != 0) {
			checkBool(false, "raw unlocked");
		} else {
			checkBool(true, "raw unlocked");
			pthread_mutex_unlock(&(link->lock));
		}
		
		
		link = link->next;
	}

	linked_destroy(&list);
}

bool hasData(int fd) {
	int tmp = poll(&(struct pollfd){ .fd = fd, .events = POLLIN }, 1, 10);

	return tmp == 1;
}

bool handlerHasTriggered = false;
void criticalHandler() {
	printf("This is the critical handler.\n");
	printBacktrace();
}
void testLogging() {
	int pipefd[2];
	if (pipe(pipefd) < 0) {
		showError();
		return;
	}

	FILE* pipeWrite = fdopen(pipefd[1], "w");
	if (pipeWrite == NULL) {
		showError();
		return;
	}
	setbuf(pipeWrite, NULL);
	FILE* pipeRead = fdopen(pipefd[0], "r");
	if (pipeRead == NULL) {
		showError();
		return;
	}
	setbuf(pipeRead, NULL);

	setLogging(pipeWrite, DEFAULT_LOGLEVEL, false);
	setLogging(stderr, DEFAULT_LOGLEVEL, true);

	info("This info should not be displayed.");
	checkBool(!hasData(pipefd[0]), "no data read (info)");

	warn("This warning should be displayed.");
	checkBool(hasData(pipefd[0]), "data read (warn)");
	fflush(pipeRead);

	error("This error should be displayed.");
	checkBool(hasData(pipefd[0]), "data read (error)");
	fflush(pipeRead);

	setCriticalHandler(&criticalHandler);

	critical("This critical should be displayed.");
	checkBool(hasData(pipefd[0]), "data read (crititcal)");
	fflush(pipeRead);

	fclose(pipeWrite);
	fclose(pipeRead);
}

volatile int counter = 0;
void timerThread() {
	counter++;
}
void testTimers() {
	timer_t timer = timer_createThreadTimer(&timerThread);
	if (timer == NULL) {
		showError();
		return;
	}
	if (timer_startInterval(timer, 10) < 0) {
		showError();
		return;
	}
	sleep(1);
	timer_stop(timer);
	timer_destroy(timer);

	checkBool(counter >= 99 && counter <= 101, "interval count");
}

void testHeaders() {
	struct headers headers = (struct headers) {
		.number = 0
	};

	char* tmp = "test:  Hello World  ";
	checkInt(headers_parse(&headers, tmp, strlen(tmp)), 0, "parse ok");
	tmp = "blablabla";
	checkInt(headers_parse(&headers, tmp, strlen(tmp)), HEADERS_PARSE_ERROR, "parse error");
	tmp = "test2: Hello World2";
	checkInt(headers_parse(&headers, tmp, strlen(tmp)), 1, "parse ok");
	tmp = "";
	checkInt(headers_parse(&headers, tmp, strlen(tmp)), HEADERS_END, "header end");

	checkString(headers_get(&headers, "test"), "Hello World", "value check");

	headers_free(&headers);
}

void testConfig() {
	FILE* file;

	#ifdef SSL_SUPPORT
		file = fopen("tests/test-with-ssl.conf", "r");
	#else
		file = fopen("tests/test.conf", "r");
	#endif

	struct config* config = config_parse(file);
	
	checkNull(config, "null check");

	#ifdef SSL_SUPPORT
		checkInt(config->nrBinds, 2, "bind no check");
	#else
		checkInt(config->nrBinds, 1, "bind no check");
	#endif

	checkString(config->binds[0]->addr, "0.0.0.0", "bind addr check");
	checkString(config->binds[0]->port, "80", "bind port check");
	checkInt(config->binds[0]->nrSites, 1, "site no check");
	checkInt(config->binds[0]->sites[0]->nrHostnames, 1, "site hostname no check");
	checkString(config->binds[0]->sites[0]->hostnames[0], "example.com", "site hostname check");
	checkString(config->binds[0]->sites[0]->documentRoot, "/", "site document root check");
	checkInt(config->binds[0]->sites[0]->nrHandlers, 1, "handler no check");
	checkString(config->binds[0]->sites[0]->handlers[0]->dir, "/", "handler dir check");
	checkInt(config->binds[0]->sites[0]->handlers[0]->type, FILE_HANDLER_NO, "handler type no check");
	checkVoid(config->binds[0]->sites[0]->handlers[0]->handler, &fileHandler, "handler ptr check");
	checkString(config->binds[0]->sites[0]->handlers[0]->settings.fileSettings.documentRoot, "/", "handler settings root check");
	checkInt(config->binds[0]->sites[0]->handlers[0]->settings.fileSettings.indexfiles.number, 1, "handler settings index no");
	checkString(config->binds[0]->sites[0]->handlers[0]->settings.fileSettings.indexfiles.files[0], "index.html", "handler settings index check");
	checkString(config->logging.accessLogfile, "access.log", "access log file check");
	checkString(config->logging.serverLogfile, "server.log", "server log file check");
	printf("%s\n", config->logging.serverLogfile);
	checkInt(config->logging.serverVerbosity, INFO, "server log verbosity check");

	#ifdef SSL_SUPPORT
		checkString(config->binds[1]->addr, "0.0.0.0", "bind addr check");
		checkString(config->binds[1]->port, "443", "bind port check");
		checkNull(config->binds[1]->ssl, "ssl null check");
		checkString(config->binds[1]->ssl->privateKey, "ssl.key", "ssl key check");
		checkString(config->binds[1]->ssl->certificate, "ssl.crt", "ssl cert check");
		checkInt(config->binds[1]->nrSites, 1, "site no check");
		checkInt(config->binds[1]->sites[0]->nrHostnames, 1, "site hostname no check");
		checkString(config->binds[1]->sites[0]->hostnames[0], "example.com", "site hostname check");
		checkString(config->binds[1]->sites[0]->documentRoot, "/", "site document root check");
		checkInt(config->binds[1]->sites[0]->nrHandlers, 1, "handler no check");
		checkString(config->binds[1]->sites[0]->handlers[0]->dir, "/", "handler dir check");
		checkInt(config->binds[1]->sites[0]->handlers[0]->type, FILE_HANDLER_NO, "handler type no check");
		checkVoid(config->binds[1]->sites[0]->handlers[0]->handler, &fileHandler, "handler ptr check");
		checkString(config->binds[1]->sites[0]->handlers[0]->settings.fileSettings.documentRoot, "/", "handler settings root check");
		checkInt(config->binds[1]->sites[0]->handlers[0]->settings.fileSettings.indexfiles.number, 1, "handler settings index no");
		checkString(config->binds[1]->sites[0]->handlers[0]->settings.fileSettings.indexfiles.files[0], "index.html", "handler settings index check");
	#endif

	fclose(file);	

	config_destroy(config);
}

#define LOCAL_PORT (1337)
#define LOCAL_PORT_STRING ("1337")

struct {
	handler_t handler;
	struct bind bind;
	int pid;
} serverdata = {
	.bind = {
		.address = "127.0.0.1",
		.port = LOCAL_PORT_STRING,
		.ssl = false
	},
	.pid = 0
};

void stopWebserver() {
	if (serverdata.pid != 0) {
		kill(serverdata.pid, SIGTERM);
		int tmp;
		wait(&tmp);
	}
}

struct handler handlerGetter(struct metaData metaData, const char* host, struct bind* bind) {
	return (struct handler) {
		.handler = serverdata.handler
	};
}


void startWebserver(handler_t handler) {
	serverdata.handler = handler;
	
	struct headers headers = headers_create();
	headers_mod(&headers, "Server", "CShore 0.1");
	struct networkingConfig netConfig = (struct networkingConfig) {
		binds: {
			number: 1,
			binds: &serverdata.bind 
		},
		connectionTimeout: DEFAULT_CONNECTION_TIMEOUT,
		maxConnections: DEFAULT_MAX_CONNECTIONS,
		defaultHeaders: headers,
		getHandler: handlerGetter
	};
	
	serverdata.pid = fork();
	
	if (serverdata.pid == 0) {
		networking_init(netConfig);
	} else if (serverdata.pid < 0) {
		printf("PANIC!\n");
		exit(1);
	}
	
	usleep(100000);
}

FILE* sendRequest(FILE* stream, enum protocol procotol, enum method method, const char* uri, struct headers headers) {
	if (stream == NULL) {
		int sfd = socket(AF_INET, SOCK_STREAM, 0);
		struct in_addr addr;
		inet_aton("127.0.0.1", &addr);
		struct sockaddr_in sockaddr = {
			.sin_family = AF_INET,
			.sin_port = LOCAL_PORT,
			.sin_addr = addr
		};
		int fd = connect(sfd, &sockaddr, sizeof(struct sockaddr_in));
		stream = fdopen(fd, "rw");
	}
	
	const char* protocolString = "HTTP/1.0";
	if (procotol == HTTP11)
		protocolString = "HTTP/1.1";
		
	const char* methodString = "GET";
	switch(method) {
		case GET:
			methodString = "GET";
			break;
		case HEAD:
			methodString = "HEAD";
			break;
		case POST:
			methodString = "POST";
			break;
		case PUT:
			methodString = "PUT";
			break;
		case DELETE:
			methodString = "DELETE";
			break;
		case CONNECT:
			methodString = "CONNECT";
			break;
		case OPTIONS:
			methodString = "OPTIONS";
			break;
		case TRACE:
			methodString = "TRACE";
			break;
		case PATCH:
			methodString = "PATCH";
			break;
		default:
			break;
	}
	
	fprintf(stream, "%s %s %s\r\n", methodString, uri, protocolString);
	headers_dump(&headers, stream);
	headers_free(&headers);
	fprintf(stream, "\r\n");
	
	return stream;
}

#define BUFFER_SIZE (1024)
char buffer[BUFFER_SIZE];

const char* readline(FILE* stream) {
	fgets(&(buffer[0]), BUFFER_SIZE, stream);
	return buffer;
}

void testHandler1(struct request request, struct response response) {
	struct headers headers = headers_create();
	headers_mod(&headers, "Content-Length", "0");
	int fd = response.sendHeader(200, &headers, &request);
	headers_free(&headers);
	close(fd);
}

void testIntegration() {
	startWebserver(&testHandler1);
	
	FILE* stream = sendRequest(NULL, HTTP10, GET, "/", headers_create());
	printf("%s\n", readline(stream));
	printf("%s\n", readline(stream));
	printf("%s\n", readline(stream));
	printf("%s\n", readline(stream));
	fclose(stream);
	
	stopWebserver();
}

void test(const char* name, void (*testFunction)()) {
	printf("%s\n", name);
	printf("%.*s\n", (int) strlen(name), 
		"===================================");
	testFunction();
	if (!global)
		overall = false;
	printf("%s: %s\n\n", name, global ? "OK" : "FAILED");
	global = true;
}

int main(int argc, char** argv) {
	test("config", &testConfig);
	test("util", &testUtil);
	test("linked lists", &testLinkedList);
	test("signals", &testTimers);
	test("headers", &testHeaders);
	test("logging", &testLogging);
	test("integration", &testIntegration);


	printf("\nOverall: %s\n", overall ? "OK" : "FAILED");
}
