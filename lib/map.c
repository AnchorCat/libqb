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

#include "os_base.h"
#include <qb/qbmap.h>
#include "map_int.h"

void
qb_map_put(struct qb_map *map, const char *key, const void *value)
{
	map->put(map, key, value);
}

void *
qb_map_get(struct qb_map *map, const char *key)
{
	return map->get(map, key);
}

int32_t
qb_map_rm(struct qb_map *map, const char *key)
{
	return map->rm(map, key);
}

size_t
qb_map_count_get(struct qb_map *map)
{
	return map->count_get(map);
}

void
qb_map_foreach(struct qb_map *map, qb_transverse_func func, void *user_data)
{
	const char* key;
	void* value;
	qb_map_iter_t* i = qb_map_iter_create(map);

	for (key = qb_map_iter_next(i, &value);
	     key;
	     key = qb_map_iter_next(i, &value)) {
		if (func(key, value, user_data)) {
			goto clean_up;
		}
	}
clean_up:
	qb_map_iter_free(i);
}

qb_map_iter_t*
qb_map_iter_create(struct qb_map *map)
{
	return map->iter_create(map, NULL);
}

qb_map_iter_t*
qb_map_pref_iter_create(qb_map_t *map, const char* prefix)
{
	return map->iter_create(map, prefix);
}

const char*
qb_map_iter_next(struct qb_map_iter* i, void** value)
{
	return i->m->iter_next(i, value);
}

void
qb_map_iter_free(qb_map_iter_t* i)
{
	i->m->iter_free(i);
}

void
qb_map_destroy(struct qb_map *map)
{
	map->destroy(map);
}
