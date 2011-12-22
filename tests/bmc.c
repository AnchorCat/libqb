/*
 * Copyright (c) 2009 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)
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
#include <signal.h>

#include <qb/qbdefs.h>
#include <qb/qbutil.h>
#include <qb/qbipcc.h>

#define ITERATIONS 10000
pid_t mypid;
int32_t blocking = QB_TRUE;
int32_t events = QB_FALSE;
int32_t verbose = 0;
static qb_ipcc_connection_t *conn;
#define MAX_MSG_SIZE (8192*128)
static qb_util_stopwatch_t *sw;

static void bm_finish(const char *operation, int32_t size)
{
	float ops_per_sec;
	float mbs_per_sec;
	float elapsed;

	qb_util_stopwatch_stop(sw);
	elapsed = qb_util_stopwatch_sec_elapsed_get(sw);
	ops_per_sec = ((float)ITERATIONS) / elapsed;
	mbs_per_sec = ((((float)ITERATIONS) * size) / elapsed) / (1024.0 * 1024.0);

	printf("write size, %d, OPs/sec, %9.3f, ", size, ops_per_sec);
	printf("MB/sec, %9.3f\n", mbs_per_sec);
}

struct my_req {
	struct qb_ipc_request_header hdr;
	char message[1024 * 1024];
};

static struct my_req request;

static int32_t bmc_send_nozc(uint32_t size)
{
	struct qb_ipc_response_header res_header;
	int32_t res;

	request.hdr.id = QB_IPC_MSG_USER_START + 3;
	request.hdr.size = sizeof(struct qb_ipc_request_header) + size;

repeat_send:
	res = qb_ipcc_send(conn, &request, request.hdr.size);
	if (res < 0) {
		if (res == -EAGAIN) {
			goto repeat_send;
		} else if (res == -EINVAL || res == -EINTR || res == -ENOTCONN) {
			perror("qb_ipcc_send");
			return -1;
		} else {
			errno = -res;
			perror("qb_ipcc_send");
			goto repeat_send;
		}
	}

	if (blocking) {
		res = qb_ipcc_recv(conn,
				&res_header,
				sizeof(struct qb_ipc_response_header), -1);
		if (res == -EINTR) {
			return -1;
		}
		if (res < 0) {
			perror("qb_ipcc_recv");
		}
		assert(res == sizeof(struct qb_ipc_response_header));
		assert(res_header.id == 13);
		assert(res_header.size == sizeof(struct qb_ipc_response_header));
	}
	if (events) {
		res = qb_ipcc_event_recv(conn,
				&res_header,
				sizeof(struct qb_ipc_response_header), -1);
		if (res == -EINTR) {
			return -1;
		}
		if (res < 0) {
			perror("qb_ipcc_event_recv");
		}
		assert(res == sizeof(struct qb_ipc_response_header));
		assert(res_header.id == 13);
		assert(res_header.size == sizeof(struct qb_ipc_response_header));
	}
	return 0;
}

struct qb_ipc_request_header *global_zcb_buffer;

static void show_usage(const char *name)
{
	printf("usage: \n");
	printf("%s <options>\n", name);
	printf("\n");
	printf("  options:\n");
	printf("\n");
	printf("  -n             non-blocking ipc (default blocking)\n");
	printf("  -e             receive events\n");
	printf("  -v             verbose\n");
	printf("  -h             show this help text\n");
	printf("\n");
}

static void sigterm_handler(int32_t num)
{
	printf("bmc: %s(%d)\n", __func__, num);
	qb_ipcc_disconnect(conn);
	exit(0);
}

static void libqb_log_writer(const char *file_name,
			     int32_t file_line,
			     int32_t severity, const char *msg)
{
	printf("libqb: %s:%d [%d] %s\n", file_name, file_line, severity, msg);
}

int32_t main(int32_t argc, char *argv[])
{
	const char *options = "nevh";
	int32_t opt;
	int32_t i, j;
	size_t size;

	mypid = getpid();

	qb_util_set_log_function(libqb_log_writer);

	while ((opt = getopt(argc, argv, options)) != -1) {
		switch (opt) {
		case 'n':
			blocking = QB_FALSE;
			break;
		case 'e':
			events = QB_TRUE;
			break;
		case 'v':
			verbose++;
			break;
		case 'h':
		default:
			show_usage(argv[0]);
			exit(0);
			break;
		}
	}

	signal(SIGINT, sigterm_handler);
	signal(SIGILL, sigterm_handler);
	signal(SIGTERM, sigterm_handler);
	conn = qb_ipcc_connect("bm1", MAX_MSG_SIZE);
	if (conn == NULL) {
		perror("qb_ipcc_connect");
		exit(1);
	}

	sw =  qb_util_stopwatch_create();
	size = QB_MAX(sizeof(struct qb_ipc_request_header), 64);
	for (j = 0; j < 20; j++) {
		if (size >= MAX_MSG_SIZE)
			break;
		qb_util_stopwatch_start(sw);
		for (i = 0; i < ITERATIONS; i++) {
			if (bmc_send_nozc(size) == -1) {
				break;
			}
		}
		bm_finish("send_nozc", size);
		size *= 2;
	}

	qb_ipcc_disconnect(conn);
	return EXIT_SUCCESS;
}

