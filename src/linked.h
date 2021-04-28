#ifndef LINKED_H
#define LINKED_H

#include <stdbool.h>
#include <signal.h>
#include <pthread.h>

typedef struct linkedList {
	pthread_mutex_t lock;
	struct link* first;
} linkedList_t;

typedef struct link {
	void* data;
	pthread_mutex_t lock;
	volatile sig_atomic_t inUse;
	bool unlinked;
	struct link* next;
	struct link* prev;
	struct linkedList* list;	
} link_t;

linkedList_t linked_create();
size_t linked_push(linkedList_t* list, const void* data);
link_t* linked_first(linkedList_t* list);
link_t* linked_next(link_t* link);
size_t linked_length(linkedList_t* list);
bool linked_release(link_t* link);
link_t* linked_get(linkedList_t* list, size_t index);
int linked_unlink(link_t* link);
void linked_destroy(linkedList_t* list);

#endif
