#include <stdio.h>

#include "networking.h"
#include "linked.h"

handler_t handlerGetter(struct metaData metaData, const char* host) {
	return NULL;
}

void testLinkedList() {
	linkedList_t list = linked_create();
	linked_push(&list, "Entry 0");
	linked_push(&list, "Entry 1");
	linked_push(&list, "Entry 2");
	linked_push(&list, "Entry 3");

	link_t* current = linked_first(&list);
	while(current != NULL) {
		printf("%s\n", (char*) current->data);
		current = linked_next(current);
	}

	current = linked_get(&list, 2);
	printf("%s\n", (char*) current->data);
	linked_unlink(current);
	linked_release(current);

	current = linked_first(&list);
	while(current != NULL) {
		printf("%s\n", (char*) current->data);
		current = linked_next(current);
	}

	linked_destroy(&list);
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
	testLinkedList();
}
