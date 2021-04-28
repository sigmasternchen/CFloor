#include <stdbool.h>
#include <stdlib.h>
#include <pthread.h>

#include "linked.h"

linkedList_t linked_create() {
	linkedList_t list = {
		.lock = PTHREAD_MUTEX_INITIALIZER,
		.first = NULL
	};
	return list;
}

size_t linked_push(linkedList_t* list, const void* data) {
	link_t* new = malloc(sizeof (link_t));
	if (new == NULL)
		return -1;

	new->data = (void*) data;
	pthread_mutex_init(&new->lock, NULL);
	new->inUse = 0;
	new->list = list;
	new->next = NULL;
	new->unlinked = false;

	size_t index = 0;

	link_t* current = NULL;

	pthread_mutex_lock(&(list->lock));
	current = list->first;
	if (current == NULL) {
		new->prev = NULL;
		list->first = new;
		pthread_mutex_unlock(&(list->lock));
		return index;
	}
	pthread_mutex_lock(&(current->lock));
	pthread_mutex_unlock(&(list->lock));

	index++;
	while(current->next != NULL) {
		link_t* tmp = current->next;
		pthread_mutex_lock(&(current->next->lock));
		pthread_mutex_unlock(&(current->lock));
		current = tmp;
		index++;
	}

	new->prev = current;
	current->next = new;
	
	pthread_mutex_unlock(&(current->lock));

	return index;
}

link_t* linked_first(linkedList_t* list) {	
	pthread_mutex_lock(&(list->lock));
	if (list->first == NULL) {
		pthread_mutex_unlock(&(list->lock));
		return NULL;
	}
	
	link_t* link = list->first;
	pthread_mutex_lock(&(link->lock));
	pthread_mutex_unlock(&(list->lock));
	link->inUse++;
	pthread_mutex_unlock(&(link->lock));

	return link;
}

// link has to be locked
void linked_free(link_t* link) {
	pthread_mutex_unlock(&(link->lock));
	pthread_mutex_destroy(&(link->lock));
	free(link);
}

// link has to be locked
bool linked_release(link_t* link) {
	link->inUse--;

	if (link->unlinked && (link->inUse == 0)) {
		linked_free(link);
		return true;
	}
	return false;
}

link_t* linked_next(link_t* link) {
	pthread_mutex_lock(&(link->lock));
	link_t* next = link->next;

	if (next != NULL) {
		pthread_mutex_lock(&(next->lock));
		next->inUse++;
		pthread_mutex_unlock(&(next->lock));
	}
	
	if (!linked_release(link)) {
		pthread_mutex_unlock(&(link->lock));
	}

	return next;
}

link_t* linked_get(linkedList_t* list, size_t index) {
	link_t* link = linked_first(list);

	while(link != NULL) {		
		if (index-- == 0) {
			return link;
		}
	 
		link = linked_next(link);
	}

	return NULL;
}

size_t linked_length(linkedList_t* list) {
	link_t* link = linked_first(list);

	size_t length = 0;

	while(link != NULL) {
		length++;
		link = linked_next(link);
	}

	return length;
}

int linked_unlink(link_t* link) {
	// need to get prev sem before link sem to avoid dead lock
	pthread_mutex_t* prevLock;
	link_t** prevNext = NULL;

	while(true) {
		/*
		 * Try to lock prev. If locked prev was modified try again.
		 * Abort if link is unlinked.
		 */
		pthread_mutex_lock(&(link->lock));
		link_t* prev = link->prev;
		pthread_mutex_unlock(&(link->lock));
		if (prev == NULL) {
			pthread_mutex_lock(&(link->list->lock));
			
			if (link->list->first != link) {
				pthread_mutex_unlock(&(link->list->lock));
			
				if (link->unlinked) {
					return -1;
				}
				continue;
			}
			
			prevLock = &(link->list->lock);
			prevNext = &(link->list->first);
			break;
		} else {
			pthread_mutex_lock(&(prev->lock));
			
			if (prev->next != link) {
				pthread_mutex_unlock(&(prev->lock));
				
				if (link->unlinked) {
					return - 1;
				}
				continue;
			}
			prevLock = &(prev->lock);
			prevNext = &(prev->next);
			break;
		}
	}

	// prev is locked, no danger of a deadlock -> lock current link

	pthread_mutex_lock(&(link->lock));

	if (link->unlinked) {
		// already unlinked; release all locks and return
	
		pthread_mutex_unlock(&(link->lock));
		pthread_mutex_unlock(prevLock);
		return - 1;
	}

	if (link->next) {
		pthread_mutex_lock(&(link->next->lock));
		// we don't need to check for changes because link is already locked

		// while we are here we can modify next
		link->next->prev = link->prev;

		// we don't need link->next anymore
		pthread_mutex_unlock(&(link->next->lock));
	}

	link->unlinked = true;

	*prevNext = link->next;

	// all changes to prev are done -> unlock

	pthread_mutex_unlock(prevLock);
	
	// everything is done
	// check if the link is used; if so: unlock; if not: free 
	if (link->inUse == 0) {
		linked_free(link);
	} else {
		pthread_mutex_unlock(&(link->lock));
	}

	return 0;
}

void linked_destroy(linkedList_t* list) {
	// no new links can be added after locking the list
	
	pthread_mutex_lock(&(list->lock));

	link_t* link = list->first;

	while(link != NULL) {
		pthread_mutex_lock(&(link->lock));
		link_t* next = link->next;
		// no new links can be added, so we can unlock link (next can't be changed)
		pthread_mutex_unlock(&(link->lock));
		linked_unlink(link);
		link = next;
	}
	
	pthread_mutex_unlock(&(list->lock));
	pthread_mutex_destroy(&(list->lock));
}

