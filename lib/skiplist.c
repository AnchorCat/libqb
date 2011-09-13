/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * Author: Angus Salkeld <asalkeld@redhat.com>
 *
 * This file is part of libqb.
 *
 * libqb is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * libqb is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libqb.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <os_base.h>
#include <assert.h>

#include <qb/qbdefs.h>
#include <qb/qbmap.h>
#include "map_int.h"

#define SKIPLIST_LEVEL_MAX 8
#define SKIPLIST_LEVEL_MIN 0
/* The amount of possible levels */
#define SKIPLIST_LEVEL_COUNT (SKIPLIST_LEVEL_MAX - SKIPLIST_LEVEL_MIN + 1)

struct skiplist_iter {
	struct qb_map_iter i;
	struct skiplist_node *n;
};

struct skiplist_node {
	const char *key;
	void *value;
	int8_t level;
	uint32_t refcount;

	/* An array of @level + 1 node pointers */
	struct skiplist_node **forward;
};

struct skiplist {
	struct qb_map map;

	size_t length;
	int8_t level;
	struct skiplist_node *header;
};

/* An array of nodes that need to be updated after an insert or delete operation
 */
typedef struct skiplist_node *skiplist_update_t[SKIPLIST_LEVEL_COUNT];

static int32_t skiplist_rm(struct qb_map *list, const char *key);

static int8_t
skiplist_level_generate(void)
{
	/* This constant is found by 1 / P, where P = 0.25. */
	enum { P_INVERSE = 4 };

	/* The original algorithm's random number is in the range [0, 1), so
	 * max M = 1. Its ceiling C = M * P = 1 * P = P.
	 *
	 * Our random number is in the range [0, UINT16_MAX], so M = UINT16_MAX. Therefore,
	 * C = UINT16_MAX * P = UINT16_MAX / P_INVERSE.
	 */
	enum { P_CEIL = UINT16_MAX / P_INVERSE };
	int8_t level = SKIPLIST_LEVEL_MIN;

	while ((uint16_t) (rand()) < P_CEIL)
		level++;

	if (level < SKIPLIST_LEVEL_MAX)
		return level;

	return SKIPLIST_LEVEL_MAX;
}

static struct skiplist_node *
skiplist_node_next(const struct skiplist_node *node)
{
	const struct skiplist_node *n = node;
	do {
		n = n->forward[SKIPLIST_LEVEL_MIN];
	} while (n && n->refcount == 0);
	return (struct skiplist_node *)n;
}

/*
 * Create a new level @level node with value @value. The node should eventually
 * be destroyed with @skiplist_node_destroy.
 * 
 * return: a new node on success and NULL otherwise.
 */
static struct skiplist_node *
skiplist_node_new(const int8_t level, const char* key, const void *value)
{
	struct skiplist_node *new_node = (struct skiplist_node *)
	    (malloc(sizeof(struct skiplist_node)));

	if (!new_node)
		return NULL;

	new_node->value = (void*)value;
	new_node->key = key;
	new_node->level = level;
	new_node->refcount = 1;

	/* A level 0 node still needs to hold 1 forward pointer, etc. */
	new_node->forward = (struct skiplist_node **)
	    (calloc(level + 1, sizeof(struct skiplist_node *)));

	if (new_node->forward)
		return new_node;

	free(new_node);

	return NULL;
}

static struct skiplist_node *
skiplist_header_node_new(void)
{
	return skiplist_node_new(SKIPLIST_LEVEL_MAX, NULL, NULL);
}

static void
skiplist_node_destroy(struct skiplist_node *node, struct skiplist *list)
{
	if (list->map.value_destroy_func && node != list->header) {
		list->map.value_destroy_func(node->value);
	}

	if (list->map.key_destroy_func && node != list->header) {
		list->map.key_destroy_func((void*)node->key);
	}

	free(node->forward);
	free(node);
}

static void
skiplist_node_deref(struct skiplist_node *node, struct skiplist *list)
{
	node->refcount--;
	if (node->refcount == 0) {
		skiplist_node_destroy(node, list);
	}
}

static void
skiplist_destroy(struct qb_map *map)
{
	struct skiplist *list = (struct skiplist *)map;
	struct skiplist_node *cur_node = list->header;
	struct skiplist_node *fwd_node;

	do {
		fwd_node = skiplist_node_next(cur_node);
		skiplist_node_destroy(cur_node, list);
	} while ((cur_node = fwd_node));
	free(list);
}

/* An operation to perform after comparing a user value or search with a
 * node's value
 */
typedef enum {
	OP_GOTO_NEXT_LEVEL,
	OP_GOTO_NEXT_NODE,
	OP_FINISH,
} op_t;

static op_t
op_search(const struct skiplist *list,
	  const struct skiplist_node *fwd_node, const void *search)
{
	int32_t cmp;
	
	if (!fwd_node)
		return OP_GOTO_NEXT_LEVEL;

	cmp = strcmp(fwd_node->key, search);
	if (cmp < 0) {
		return OP_GOTO_NEXT_NODE;
	} else if (cmp == 0) {
		return OP_FINISH;
	}

	return OP_GOTO_NEXT_LEVEL;
}

static void
skiplist_put(struct qb_map * map, const char *key, const void *value)
{
	struct skiplist *list = (struct skiplist *)map;
	struct skiplist_node *new_node;
	int8_t level = list->level;
	skiplist_update_t update;
	int8_t update_level;
	int8_t new_node_level;
	struct skiplist_node *cur_node = list->header;

	while ((update_level = level) >= SKIPLIST_LEVEL_MIN) {
		struct skiplist_node *fwd_node = cur_node->forward[level];

		switch (op_search(list, fwd_node, key)) {
		case OP_FINISH:
			if (map->key_destroy_func && fwd_node != list->header) {
				map->key_destroy_func((void*)fwd_node->key);
			}
			if (map->value_destroy_func && fwd_node != list->header) {
				map->value_destroy_func(fwd_node->value);
			}

			fwd_node->value = (void*)value;
			fwd_node->key = (void*)key;
			return;

		case OP_GOTO_NEXT_NODE:
			cur_node = fwd_node;
			break;
		case OP_GOTO_NEXT_LEVEL:
			level--;
		}

		update[update_level] = cur_node;
	}

	new_node_level = skiplist_level_generate();

	if (new_node_level > list->level) {
		for (level = list->level + 1; level <= new_node_level; level++)
			update[level] = list->header;

		list->level = new_node_level;
	}

	new_node = skiplist_node_new(new_node_level, key, value);

	assert(new_node != NULL);

	/* Drop @new_node into @list. */
	for (level = SKIPLIST_LEVEL_MIN; level <= new_node_level; level++) {
		new_node->forward[level] = update[level]->forward[level];
		update[level]->forward[level] = new_node;
	}

	list->length++;
}

static int32_t
skiplist_rm(struct qb_map *map, const char *key)
{
	struct skiplist *list = (struct skiplist *)map;
	struct skiplist_node *found_node;
	struct skiplist_node *cur_node = list->header;
	int8_t level = list->level;
	int8_t update_level;
	skiplist_update_t update;

	while ((update_level = level) >= SKIPLIST_LEVEL_MIN) {
		struct skiplist_node *fwd_node = cur_node->forward[level];

		switch (op_search(list, fwd_node, key)) {
		case OP_GOTO_NEXT_NODE:
			cur_node = fwd_node;
			break;
		case OP_GOTO_NEXT_LEVEL:
		default:
			level--;
			break;
		}

		update[update_level] = cur_node;
	}

	/* The immediate forward node should be the matching node... */
	found_node = skiplist_node_next(cur_node);

	/* ...unless we're at the end of the list or the value doesn't exist. */
	if (!found_node || strcmp(found_node->key, key) != 0) {
		return QB_FALSE;
	}

	/* Splice found_node out of list. */
	for (level = SKIPLIST_LEVEL_MIN; level <= list->level; level++)
		if (update[level]->forward[level] == found_node)
			update[level]->forward[level] = found_node->forward[level];

	skiplist_node_deref(found_node, list);

	/* Remove unused levels from @list -- stop removing levels as soon as a
	 * used level is found. Unused levels can occur if @found_node had the 
	 * highest level.
	 */
	for (level = list->level; level >= SKIPLIST_LEVEL_MIN; level--) {
		if (list->header->forward[level])
			break;

		list->level--;
	}

	list->length--;

	return QB_TRUE;
}

static void *
skiplist_get(struct qb_map *map, const char *key)
{
	struct skiplist *list = (struct skiplist *)map;
	struct skiplist_node *cur_node = list->header;
	int8_t level = list->level;

	while (level >= SKIPLIST_LEVEL_MIN) {
		struct skiplist_node *fwd_node = cur_node->forward[level];

		switch (op_search(list, fwd_node, key)) {
		case OP_FINISH:
			return fwd_node->value;
		case OP_GOTO_NEXT_NODE:
			cur_node = fwd_node;
			break;
		case OP_GOTO_NEXT_LEVEL:
			level--;
		}
	}

	return NULL;
}

static qb_map_iter_t*
skiplist_iter_create(struct qb_map * map, const char* prefix)
{
	struct skiplist_iter *i = malloc(sizeof(struct skiplist_iter));
	struct skiplist *list = (struct skiplist *)map;
	i->i.m = map;
	i->n = list->header;
	i->n->refcount++;
	return (qb_map_iter_t*)i;
}

static const char*
skiplist_iter_next(qb_map_iter_t* i, void** value)
{
	struct skiplist_iter *si = (struct skiplist_iter*)i;
	struct skiplist_node *p = si->n;

	if (p == NULL) {
		return NULL;
	}
	si->n = skiplist_node_next(p);
	if (si->n == NULL) {
		skiplist_node_deref(p, (struct skiplist *)i->m);
		return NULL;
	}
	si->n->refcount++;
	skiplist_node_deref(p, (struct skiplist *)i->m);
	*value = si->n->value;
	return si->n->key;
}

static void
skiplist_iter_free(qb_map_iter_t* i)
{
	free(i);
}

static size_t
skiplist_count_get(struct qb_map * map)
{
	struct skiplist *list = (struct skiplist *)map;
	return list->length;
}

qb_map_t *
qb_skiplist_create(qb_destroy_notifier_func key_destroy_func,
		   qb_destroy_notifier_func value_destroy_func)
{
	struct skiplist *sl = calloc(1, sizeof(struct skiplist));
	
	srand(time(NULL));

	sl->map.key_destroy_func = key_destroy_func;
	sl->map.value_destroy_func = value_destroy_func;

	sl->map.put = skiplist_put;
	sl->map.get = skiplist_get;
	sl->map.rm = skiplist_rm;
	sl->map.count_get = skiplist_count_get;
	sl->map.iter_create = skiplist_iter_create;
	sl->map.iter_next = skiplist_iter_next;
	sl->map.iter_free = skiplist_iter_free;
	sl->map.destroy = skiplist_destroy;

	sl->level = SKIPLIST_LEVEL_MIN;
	sl->length = 0;
	sl->header = skiplist_header_node_new();

	return (qb_map_t *) sl;
}

