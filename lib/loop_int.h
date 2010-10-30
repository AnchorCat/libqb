/*
 * Copyright (C) 2010 Red Hat, Inc.
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

#ifndef QB_LOOP_INT_DEFINED
#define QB_LOOP_INT_DEFINED

#include <qb/qbloop.h>

struct qb_loop;
struct qb_loop_item;

struct qb_loop_item {
	struct qb_list_head list;
	struct qb_loop_source *source;
	void *user_data;
};

struct qb_loop_level {
	enum qb_loop_priority priority;
	int32_t to_process;
	struct qb_list_head wait_head;
	struct qb_list_head job_head;
	struct qb_loop *l;
};

struct qb_loop_source {
	struct qb_loop *l;
	void (*dispatch_and_take_back)(struct qb_loop_item *i,
			 enum qb_loop_priority p);
	int32_t (*poll)(struct qb_loop_source* s, int32_t ms_timeout);
};

struct qb_loop {
	struct qb_loop_level level[3];
	int32_t stop_requested;
	struct qb_loop_source * timer_source;
	struct qb_loop_source * job_source;
	struct qb_loop_source * fd_source;
};

struct qb_loop_source *
qb_loop_jobs_create(struct qb_loop *l);

struct qb_loop_source*
qb_loop_timer_create(struct qb_loop *l);

struct qb_loop_source*
qb_loop_poll_create(struct qb_loop *l);

void qb_loop_jobs_destroy(struct qb_loop *l);

void qb_loop_timer_destroy(struct qb_loop *l);

void qb_loop_poll_destroy(struct qb_loop *l);


int32_t qb_loop_timer_msec_duration_to_expire(struct qb_loop_source *timer_source);


#endif /* QB_LOOP_INT_DEFINED */

