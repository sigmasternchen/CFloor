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

#include "networking.h"
#include "linked.h"
#include "logging.h"
#include "signals.h"
#include "headers.h"
#include "util.h"

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
	char* clone = strclone(original);

	checkNull(clone, "clone: null check");
	checkString(clone, original, "clone: value check");

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

	link = list.first;
	while(link != NULL) {
		checkInt(link->inUse, 0, "raw inUse");
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
	test("util", &testUtil);
	test("linked lists", &testLinkedList);
	test("logging", &testLogging);
	test("signals", &testTimers);
	test("headers", &testHeaders);


	printf("\nOverall: %s\n", overall ? "OK" : "FAILED");
}
