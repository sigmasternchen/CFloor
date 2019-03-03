#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "networking.h"
#include "linked.h"

bool global = true;

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

	linked_destroy(&list);
}

handler_t handlerGetter(struct metaData metaData, const char* host) {
	return NULL;
}

void testNetworking() {
	initNetworking((struct networkingConfig) {
		.connectionTimeout = 30000,
		.defaultResponse = {
			.number = 0
		},
		.getHandler = &handlerGetter
	});
}

int main(int argc, char** argv) {
	bool overall = true;

	printf("linked lists\n");
	printf("============\n\n");
	testLinkedList();
	if (!global)
		overall = false;
	printf("linked lists: %s\n\n", global ? "OK" : "FAILED");
	global = true;


	printf("\nOverall: %s\n", overall ? "OK" : "FAILED");
}
