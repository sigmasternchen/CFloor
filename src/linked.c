#include <semaphore.h>
#include <stdbool.h>
#include <stdlib.h>

#include "linked.h"

linkedList_t linked_create() {
	linkedList_t list = {
		.first = NULL
	};
	sem_init(&(list.modify_sem), 0, 1);
	return list;
}

size_t linked_push(linkedList_t* list, void* data) {
	link_t* new = malloc(sizeof (link_t));
	if (new == NULL)
		return -1;

	new->data = data;
	sem_init(&(new->modify_sem), 0, 1);
	new->inUse = 0;
	new->list = list;
	new->next = NULL;
	new->unlinked = false;

	size_t index = 0;

	link_t* current = NULL;

	sem_wait(&(list->modify_sem));
	current = list->first;
	if (current == NULL) {
		new->prev = NULL;
		list->first = new;
		sem_post(&(list->modify_sem));
		return index;
	}
	sem_post(&(list->modify_sem));

	index++;
	sem_wait(&(current->modify_sem));
	while(current->next != NULL) {
		sem_wait(&(current->next->modify_sem));
		sem_post(&(current->modify_sem));
		current = current->next;
		index++;
	}

	new->prev = current;
	current->next = new;
	
	sem_post(&(current->modify_sem));

	return index;
}

link_t* linked_first(linkedList_t* list) {
	if (list->first == NULL) {
		return NULL;
	}
	link_t* link = list->first;

	link->inUse++;

	return link;
}

void linked_free(link_t* link) {
	free(link);
}

void linked_unlock(link_t* link) {
	link->inUse--;

	if (link->unlinked && link->inUse == 0) {
		linked_free(link);
	}
}

link_t* linked_next(link_t* link) {
	linked_unlock(link);

	if (link->next == NULL) {
		return NULL;
	}
	link = link->next;

	link->inUse++;

	return link;
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
	sem_t* prevModify = NULL;
	link_t** prevNext = NULL;

	while(true) {
		/*
		 * Try to lock prev. If locked prev was modified try again.
		 * Abort if link is unlinked.
		 */

		link_t* prev = link->prev;
		if (prev == NULL) {
			sem_wait(&(link->list->modify_sem));
			if (link->list->first != link) {
				sem_post(&(link->list->modify_sem));
				if (link->unlinked) {
					return -1;
				}
				continue;
			}
			prevModify = &(link->list->modify_sem);
			prevNext = &(link->list->first);
			break;
		} else {
			sem_wait(&(prev->modify_sem));
			if (prev->next != link) {
				sem_post(&(prev->modify_sem));
				if (link->unlinked) {
					return - 1;
				}
				continue;
			}
			prevModify = &(prev->modify_sem);
			prevNext = &(prev->next);
			break;
		}
	}

	sem_wait(&(link->modify_sem));

	if (link->unlinked) {
		sem_post(&(link->modify_sem));
		sem_post(prevModify);
		return - 1;
	}

	if (link->next) {
		sem_wait(&(link->next->modify_sem));
		// we don't need to check for changes because link is already locked

		// while we are here we can modify next
		link->next->prev = link->prev;

		// we don't need this one anymore
		sem_post(&(link->next->modify_sem));
	}

	link->unlinked = true;

	*prevNext = link->next;

	// everything is done

	sem_post(&(link->modify_sem));

	sem_post(prevModify);

	if (link->inUse == 0) {
		linked_free(link);
	}

	return 0;
}

void linked_destroy(linkedList_t* list) {
	link_t* link = linked_first(list);

	while(link != NULL) {
		link_t* next = link->next;
		linked_unlink(link);
		link = next;
	}
}

