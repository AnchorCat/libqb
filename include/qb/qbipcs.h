/*
 * Copyright (C) 2006-2009 Red Hat, Inc.
 *
 * Author: Steven Dake <sdake@redhat.com>,
 *         Angus Salkeld <asalkeld@redhat.com>
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

#ifndef QB_IPCS_H_DEFINED
#define QB_IPCS_H_DEFINED

#include <stdlib.h>
#include <qb/qbipc_common.h>
#include <qb/qbhdb.h>
#include <qb/qbloop.h>

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

enum qb_ipcs_rate_limit {
	QB_IPCS_RATE_FAST,
	QB_IPCS_RATE_NORMAL,
	QB_IPCS_RATE_SLOW,
	QB_IPCS_RATE_OFF,
};

struct qb_ipcs_connection;
typedef struct qb_ipcs_connection qb_ipcs_connection_t;

typedef qb_handle_t qb_ipcs_service_pt;

typedef int32_t (*qb_ipcs_dispatch_fn_t) (int32_t fd, int32_t revents,
	void *data);

typedef int32_t (*qb_ipcs_dispatch_add_fn)(enum qb_loop_priority p,
					   int32_t fd,
					   int32_t events,
					   void *data,
					   qb_ipcs_dispatch_fn_t fn);
typedef int32_t (*qb_ipcs_dispatch_mod_fn)(enum qb_loop_priority p,
					   int32_t fd,
					   int32_t events,
					   void *data,
					   qb_ipcs_dispatch_fn_t fn);
typedef int32_t (*qb_ipcs_dispatch_del_fn)(int32_t fd);


struct qb_ipcs_poll_handlers {
	qb_ipcs_dispatch_add_fn dispatch_add;
	qb_ipcs_dispatch_mod_fn dispatch_mod;
	qb_ipcs_dispatch_del_fn dispatch_del;
};

/**
 * This callback is to check wether you want to accept a new connection.
 *
 * The type of checks you should do are authentication, service availabilty
 * or process resource constraints.
 * @return 0 to accept or -errno to indicate a failure (sent back to the client)
 */
typedef int32_t (*qb_ipcs_connection_accept_fn) (qb_ipcs_connection_t *c, uid_t uid, gid_t gid);

/**
 * This is called after a new connection has been created.
 */
typedef void (*qb_ipcs_connection_created_fn) (qb_ipcs_connection_t *c);
/**
 * This is called after a connection has been destroyed.
 */
typedef void (*qb_ipcs_connection_destroyed_fn) (qb_ipcs_connection_t *c);
/**
 * This is the message processing calback.
 * It is called with the message data.
 */
typedef int32_t (*qb_ipcs_msg_process_fn) (qb_ipcs_connection_t *c,
		void *data, size_t size);

struct qb_ipcs_service_handlers {
	qb_ipcs_connection_accept_fn connection_accept;
	qb_ipcs_connection_created_fn connection_created;
	qb_ipcs_msg_process_fn msg_process;
	qb_ipcs_connection_destroyed_fn connection_destroyed;
};

/**
 * Create a new IPC server.
 */
qb_ipcs_service_pt qb_ipcs_create(const char *name,
				  int32_t service_id,
				  enum qb_ipc_type type,
				  struct qb_ipcs_service_handlers *handlers);

/**
 * Set your poll callbacks.
 */
void qb_ipcs_poll_handlers_set(qb_ipcs_service_pt s,
	struct qb_ipcs_poll_handlers *handlers);

/**
 * run the new IPC server.
 */
int32_t qb_ipcs_run(qb_ipcs_service_pt s);

/**
 * Destroy the IPC server.
 */
void qb_ipcs_destroy(qb_ipcs_service_pt s);

void qb_ipcs_request_rate_limit(qb_ipcs_service_pt pt, enum qb_ipcs_rate_limit rl);

/**
 * send a response to a incomming request.
 */
ssize_t qb_ipcs_response_send(qb_ipcs_connection_t *c, const void *data, size_t size);

/**
 * Send an asyncronous event message to the client.
 */
ssize_t qb_ipcs_event_send(qb_ipcs_connection_t *c, const void *data, size_t size);

/**
 * Send an asyncronous event message to the client.
 */
ssize_t qb_ipcs_event_sendv(qb_ipcs_connection_t *c, const struct iovec * iov, size_t iov_len);

/**
 * Increment the connection's reference counter.
 */
void qb_ipcs_connection_ref_inc(qb_ipcs_connection_t *c);

/**
 * Decrement the connection's reference counter.
 */
void qb_ipcs_connection_ref_dec(qb_ipcs_connection_t *c);

/**
 * Get the service id related to this connection's service.
 * (as passed into qb_ipcs_create()
 * @return service id.
 */
int32_t qb_ipcs_service_id_get(qb_ipcs_connection_t *c);

void qb_ipcs_context_set(qb_ipcs_connection_t *c, void *context);

void *qb_ipcs_context_get(qb_ipcs_connection_t *c);


/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* QB_IPCS_H_DEFINED */
