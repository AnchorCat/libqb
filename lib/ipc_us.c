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
#include "os_base.h"
#if defined(HAVE_GETPEERUCRED)
#include <ucred.h>
#endif

#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif /* HAVE_SYS_UN_H */
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#include <qb/qbipcs.h>
#include <qb/qbloop.h>
#include <qb/qbdefs.h>

#include "util_int.h"
#include "ipc_int.h"

#define SERVER_BACKLOG 5

#if defined(QB_LINUX) || defined(QB_SOLARIS)
#define QB_SUN_LEN(a) sizeof(*(a))
#else
#define QB_SUN_LEN(a) SUN_LEN(a)
#endif

struct ipc_auth_ugp {
	uid_t uid;
	gid_t gid;
	pid_t pid;
};

static int32_t qb_ipcs_us_connection_acceptor(int fd, int revent, void *data);

#ifdef SO_NOSIGPIPE
static void socket_nosigpipe(int32_t s)
{
	int32_t on = 1;
	setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (void *)&on, sizeof(on));
}
#endif

static int32_t set_cloexec_flag(int32_t fd)
{
	int32_t res;
	char error_str[100];
	int32_t oldflags = fcntl(fd, F_GETFD, 0);

	if (oldflags < 0) {
		oldflags = 0;
	}
	oldflags |= FD_CLOEXEC;
	res = fcntl(fd, F_SETFD, oldflags);
	if (res == -1) {
		res = -errno;
		strerror_r(errno, error_str, 100);
		qb_util_log(LOG_CRIT,
			    "Could not set close-on-exit operation on socket: %s\n",
			    error_str);
	}
	return res;
}

static int32_t set_nonblock_flag(int32_t fd)
{
	int32_t res;
	char error_str[100];

	res = fcntl(fd, F_SETFL, O_NONBLOCK);
	if (res == -1) {
		res = -errno;
		strerror_r(errno, error_str, 100);
		qb_util_log(LOG_CRIT,
			    "Could not set non-blocking operation on socket: %s\n",
			    error_str);
	}
	return res;
}

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

ssize_t qb_ipc_us_send(struct qb_ipc_one_way *one_way, const void *msg, size_t len)
{
	int32_t result;
	struct msghdr msg_send;
	struct iovec iov_send;
	char *rbuf = (char *)msg;
	int32_t processed = 0;

	msg_send.msg_iov = &iov_send;
	msg_send.msg_iovlen = 1;
	msg_send.msg_name = 0;
	msg_send.msg_namelen = 0;

#if !defined(QB_SOLARIS)
	msg_send.msg_control = 0;
	msg_send.msg_controllen = 0;
	msg_send.msg_flags = 0;
#else
	msg_send.msg_accrights = NULL;
	msg_send.msg_accrightslen = 0;
#endif

retry_send:
	iov_send.iov_base = &rbuf[processed];
	iov_send.iov_len = len - processed;

	result = sendmsg(one_way->u.us.sock, &msg_send, MSG_NOSIGNAL);
	if (result == -1) {
		return -errno;
	}

	processed += result;
	if (processed != len) {
		goto retry_send;
	}
	return processed;
}

static ssize_t qb_ipc_us_sendv(struct qb_ipc_one_way *one_way, const struct iovec *iov, size_t iov_len)
{
	int32_t result;
	struct msghdr msg_send;
	int32_t processed = 0;
	size_t len = 0;
	int32_t i;

	for (i = 0; i < iov_len; i++) {
		len += iov[i].iov_len;
	}
	msg_send.msg_iov = (struct iovec*)iov;
	msg_send.msg_iovlen = iov_len;
	msg_send.msg_name = 0;
	msg_send.msg_namelen = 0;

#if !defined(QB_SOLARIS)
	msg_send.msg_control = 0;
	msg_send.msg_controllen = 0;
	msg_send.msg_flags = 0;
#else
	msg_send.msg_accrights = NULL;
	msg_send.msg_accrightslen = 0;
#endif

retry_send:
	result = sendmsg(one_way->u.us.sock, &msg_send, MSG_NOSIGNAL);
	if (result == -1) {
		return -errno;
	}

	processed += result;
	if (processed != len) {
		goto retry_send;
	}
	return processed;
}

static ssize_t qb_ipc_us_recv_msghdr(int32_t s,
				     struct msghdr *hdr,
				     char *msg, size_t len)
{
	int32_t result;
	int32_t processed = 0;

retry_recv:
	hdr->msg_iov->iov_base = &msg[processed];
	hdr->msg_iov->iov_len = len - processed;

	result = recvmsg(s, hdr, MSG_NOSIGNAL | MSG_WAITALL);
	if (result == -1 && errno == EAGAIN) {
		goto retry_recv;
	}
	if (result == -1) {
		return -errno;
	}
#if defined(QB_SOLARIS) || defined(QB_BSD) || defined(QB_DARWIN)
	/* On many OS poll never return POLLHUP or POLLERR.
	 * EOF is detected when recvmsg return 0.
	 */
	if (result == 0) {
		return -errno;	//ENOTCONN
	}
#endif

	processed += result;
	if (processed != len) {
		goto retry_recv;
	}
	assert(processed == len);

	return processed;
}


int32_t qb_ipc_us_recv_ready(struct qb_ipc_one_way *one_way, int32_t ms_timeout)
{
	struct pollfd ufds;
	int32_t poll_events;

	ufds.fd = one_way->u.us.sock;
	ufds.events = POLLIN;
	ufds.revents = 0;

	poll_events = poll (&ufds, 1, ms_timeout);
	if ((poll_events == -1 && errno == EINTR) ||
	    poll_events == 0) {
		return -EAGAIN;
	} else if (poll_events == -1) {
		return -errno;
	} else if (poll_events == 1 && (ufds.revents & (POLLERR|POLLHUP))) {
		return -ESHUTDOWN;
	}
	return 0;
}

ssize_t qb_ipc_us_recv(struct qb_ipc_one_way *one_way,
		       void *msg, size_t len, int32_t timeout)
{
	int32_t result;

 retry_recv:
	result = recv(one_way->u.us.sock, msg, len, MSG_NOSIGNAL | MSG_WAITALL);
	if (result == -1 && errno == EAGAIN) {
		goto retry_recv;
	}
	if (result == -1) {
		return -errno;
	}
#if defined(QB_SOLARIS) || defined(QB_BSD) || defined(QB_DARWIN)
	/* On many OS poll never return POLLHUP or POLLERR.
	 * EOF is detected when recvmsg return 0.
	 */
	if (result == 0) {
		return -errno;	//ENOTCONN
	}
#endif
	return result;

}

static int32_t qb_ipcc_us_sock_connect(const char *socket_name, int32_t * sock_pt)
{
	int32_t request_fd;
	struct sockaddr_un address;
	int32_t res = 0;

#if defined(QB_SOLARIS)
	request_fd = socket(PF_UNIX, SOCK_STREAM, 0);
#else
	request_fd = socket(PF_LOCAL, SOCK_STREAM, 0);
#endif
	if (request_fd == -1) {
		return -errno;
	}
#ifdef SO_NOSIGPIPE
	socket_nosigpipe(request_fd);
#endif /* SO_NOSIGPIPE */
	res = set_cloexec_flag(request_fd);
	if (res < 0) {
		goto error_connect;
	}
	res = set_nonblock_flag(request_fd);
	if (res < 0) {
		goto error_connect;
	}

	memset(&address, 0, sizeof(struct sockaddr_un));
	address.sun_family = AF_UNIX;
#if defined(QB_BSD) || defined(QB_DARWIN)
	address.sun_len = SUN_LEN(&address);
#endif

#if defined(QB_LINUX)
	sprintf(address.sun_path + 1, "%s", socket_name);
#else
	sprintf(address.sun_path, "%s/%s", SOCKETDIR, socket_name);
#endif
	if (connect(request_fd, (struct sockaddr *)&address,
		    QB_SUN_LEN(&address)) == -1) {
		res = -errno;
		goto error_connect;
	}

	*sock_pt = request_fd;
	return 0;

error_connect:
	close(request_fd);
	*sock_pt = -1;

	return res;
}

void qb_ipcc_us_sock_close(int32_t sock)
{
	shutdown(sock, SHUT_RDWR);
	close(sock);
}

int32_t qb_ipcc_us_setup_connect(struct qb_ipcc_connection *c,
				   struct qb_ipc_connection_response *r)
{
	int32_t res;
	struct qb_ipc_connection_request request;

	res = qb_ipcc_us_sock_connect(c->name, &c->setup.u.us.sock);
	if (res != 0) {
		return res;
	}

	request.hdr.id = QB_IPC_MSG_AUTHENTICATE;
	request.hdr.size = sizeof(request);
	request.max_msg_size = c->setup.max_msg_size;
	res = qb_ipc_us_send(&c->setup, &request, request.hdr.size);
	if (res < 0) {
		qb_ipcc_us_sock_close(c->setup.u.us.sock);
		return res;
	}

	res = qb_ipc_us_recv(&c->setup, r, sizeof(struct qb_ipc_connection_response), 0);
	if (res < 0) {
		return res;
	}

	if (r->hdr.error != 0) {
		return r->hdr.error;
	}
	return 0;
}

static void qb_ipcc_us_disconnect(struct qb_ipcc_connection* c)
{
	close(c->request.u.us.sock);
	close(c->event.u.us.sock);
}

int32_t qb_ipcc_us_connect(struct qb_ipcc_connection *c,
			    struct qb_ipc_connection_response *r)
{
	int32_t res;
	struct qb_ipc_event_connection_request request;

	c->needs_sock_for_poll = QB_FALSE;
	c->funcs.send = qb_ipc_us_send;
	c->funcs.sendv = qb_ipc_us_sendv;
	c->funcs.recv = qb_ipc_us_recv;
	c->funcs.fc_get = NULL;
	c->funcs.disconnect = qb_ipcc_us_disconnect;

	c->request.u.us.sock = c->setup.u.us.sock;
	c->response.u.us.sock = c->setup.u.us.sock;
	c->setup.u.us.sock = -1;

	res = qb_ipcc_us_sock_connect(c->name, &c->event.u.us.sock);
	if (res != 0) {
		return res;
	}

	request.hdr.id = QB_IPC_MSG_NEW_EVENT_SOCK;
	request.hdr.size = sizeof(request);
	request.connection = r->connection;
	res = qb_ipc_us_send(&c->event, &request, request.hdr.size);
	if (res < 0) {
		qb_ipcc_us_sock_close(c->event.u.us.sock);
		return res;
	}

	return 0;
}


/*
 **************************************************************************
 * SERVER
 */

int32_t qb_ipcs_us_publish(struct qb_ipcs_service * s)
{
	struct sockaddr_un un_addr;
	int32_t res;
	char error_str[100];

	/*
	 * Create socket for IPC clients, name socket, listen for connections
	 */
#if defined(QB_SOLARIS)
	s->server_sock = socket(PF_UNIX, SOCK_STREAM, 0);
#else
	s->server_sock = socket(PF_LOCAL, SOCK_STREAM, 0);
#endif
	if (s->server_sock == -1) {
		res = -errno;
		strerror_r(errno, error_str, 100);
		qb_util_log(LOG_ERR,
			    "Cannot create server socket: %s\n", error_str);
		return res;
	}

	res = set_cloexec_flag(s->server_sock);
	if (res < 0) {
		goto error_close;
	}
	res = set_nonblock_flag(s->server_sock);
	if (res < 0) {
		goto error_close;
	}

	memset(&un_addr, 0, sizeof(struct sockaddr_un));
	un_addr.sun_family = AF_UNIX;
#if defined(QB_BSD) || defined(QB_DARWIN)
	un_addr.sun_len = SUN_LEN(&un_addr);
#endif

	qb_util_log(LOG_INFO, "server name: %s", s->name);
#if defined(QB_LINUX)
	sprintf(un_addr.sun_path + 1, "%s", s->name);
#else
	{
		struct stat stat_out;
		res = stat(SOCKETDIR, &stat_out);
		if (res == -1 || (res == 0 && !S_ISDIR(stat_out.st_mode))) {
			res = -errno;
			qb_util_log(LOG_CRIT,
				    "Required directory not present %s\n",
				    SOCKETDIR);
			goto error_close;
		}
		sprintf(un_addr.sun_path, "%s/%s", SOCKETDIR, s->name);
		unlink(un_addr.sun_path);
	}
#endif

	res =
		bind(s->server_sock, (struct sockaddr *)&un_addr,
		     QB_SUN_LEN(&un_addr));
	if (res) {
		res = -errno;
		strerror_r(errno, error_str, 100);
		qb_util_log(LOG_CRIT,
			    "Could not bind AF_UNIX (%s): %s.\n",
			    un_addr.sun_path, error_str);
		goto error_close;
	}

	/*
	 * Allow eveyrone to write to the socket since the IPC layer handles
	 * security automatically
	 */
#if !defined(QB_LINUX)
	res = chmod(un_addr.sun_path, S_IRWXU | S_IRWXG | S_IRWXO);
#endif
	if (listen(s->server_sock, SERVER_BACKLOG) == -1) {
		strerror_r(errno, error_str, 100);
		qb_util_log(LOG_ERR, "listen failed: %s.\n", error_str);
	}

	s->poll_fns.dispatch_add(s->poll_priority, s->server_sock,
				 POLLIN | POLLPRI | POLLNVAL,
				 s, qb_ipcs_us_connection_acceptor);
	return 0;

error_close:
	close(s->server_sock);
	return res;
}

int32_t qb_ipcs_us_withdraw(struct qb_ipcs_service * s)
{
	qb_util_log(LOG_INFO, "withdrawing server sockets\n");
	shutdown(s->server_sock, SHUT_RDWR);
	close(s->server_sock);
	return 0;
}

static int32_t handle_new_connection(struct qb_ipcs_service *s,
				     int32_t auth_result,
				     int32_t sock,
				     void *msg, size_t len,
				     struct ipc_auth_ugp *ugp)
{
	struct qb_ipcs_connection *c = NULL;
	struct qb_ipc_connection_request *req = msg;
	int32_t res = auth_result;
	struct qb_ipc_connection_response response;
	char error_str[100];

	if (res != 0) {
		goto send_response;
	}

	c = qb_ipcs_connection_alloc(s);
	c->setup.u.us.sock = sock;
	c->request.max_msg_size = req->max_msg_size;
	c->response.max_msg_size = req->max_msg_size;
	c->event.max_msg_size = req->max_msg_size;
	c->pid = ugp->pid;
	c->euid = ugp->uid;
	c->egid = ugp->gid;

	if (c->service->serv_fns.connection_accept) {
		res = c->service->serv_fns.connection_accept(c,
							     c->euid,
							     c->egid);
	}
	if (res != 0) {
		goto send_response;
	}

	qb_util_log(LOG_INFO, "IPC credentials authenticated");

	memset(&response, 0, sizeof(response));
	if (s->funcs.connect) {
		res = s->funcs.connect(s, c, &response);
		if (res != 0) {
			goto send_response;
		}
	}

	qb_list_add(&c->list, &s->connections);
	c->receive_buf = malloc(c->request.max_msg_size);

	if (s->needs_sock_for_poll) {
		s->poll_fns.dispatch_add(s->poll_priority, c->setup.u.us.sock,
					 POLLIN | POLLPRI | POLLNVAL,
					 c,
					 qb_ipcs_dispatch_connection_request);
	}
	if (s->type == QB_IPC_SOCKET) {
		c->request.u.us.sock = c->setup.u.us.sock;
		c->response.u.us.sock = c->setup.u.us.sock;
		s->poll_fns.dispatch_add(s->poll_priority, c->request.u.us.sock,
					 POLLIN | POLLPRI | POLLNVAL,
					 c,
					 qb_ipcs_dispatch_connection_request);
	}


send_response:
	response.hdr.id = QB_IPC_MSG_AUTHENTICATE;
	response.hdr.size = sizeof(response);
	response.hdr.error = res;
	if (res == 0) {
		response.connection = (intptr_t)c;
		response.connection_type = s->type;
		response.max_msg_size = c->request.max_msg_size;
	}

	qb_ipc_us_send(&c->setup, &response, response.hdr.size);

	if (res == 0) {
		if (s->serv_fns.connection_created) {
			s->serv_fns.connection_created(c);
		}
	} else if (res == -EACCES) {
		qb_util_log(LOG_ERR, "Invalid IPC credentials.");
	} else {
		strerror_r(-response.hdr.error, error_str, 100);
		qb_util_log(LOG_ERR, "Error in connection setup: %s.",
			    error_str);
	}
	if (res != 0 && c) {
		qb_ipcs_disconnect(c);
	} else if (res != 0) {
		qb_ipcc_us_sock_close(sock);
	}
	return res;
}

static void handle_connection_new_sock(struct qb_ipcs_service *s,
				       int32_t sock, void *msg)
{
	struct qb_ipcs_connection *c = NULL;
	struct qb_ipc_event_connection_request *req = msg;

	c = (struct qb_ipcs_connection *)req->connection;
	c->event.u.us.sock = sock;
}

static int32_t qb_ipcs_uc_recv_and_auth(int32_t sock, void *msg, size_t len,
					struct ipc_auth_ugp *ugp)
{
	int32_t res = 0;
	struct msghdr msg_recv;
	struct iovec iov_recv;

#ifdef QB_LINUX
	struct cmsghdr *cmsg;
	char cmsg_cred[CMSG_SPACE(sizeof(struct ucred))];
	int off = 0;
	int on = 1;
	struct ucred *cred;
#endif
	msg_recv.msg_flags = 0;
	msg_recv.msg_iov = &iov_recv;
	msg_recv.msg_iovlen = 1;
	msg_recv.msg_name = 0;
	msg_recv.msg_namelen = 0;
#ifdef QB_LINUX
	msg_recv.msg_control = (void *)cmsg_cred;
	msg_recv.msg_controllen = sizeof(cmsg_cred);
#endif
#ifdef QB_SOLARIS
	msg_recv.msg_accrights = 0;
	msg_recv.msg_accrightslen = 0;
#endif /* QB_SOLARIS */

	iov_recv.iov_base = msg;
	iov_recv.iov_len = len;
#ifdef QB_LINUX
	setsockopt(sock, SOL_SOCKET, SO_PASSCRED, &on, sizeof(on));
#endif

	res = qb_ipc_us_recv_msghdr(sock, &msg_recv, msg, len);
	if (res < 0) {
		goto cleanup_and_return;
	}
	if (res != len) {
		res = -EIO;
		goto cleanup_and_return;
	}
	res = -EBADMSG;

	/*
	 * currently support getpeerucred, getpeereid, and SO_PASSCRED credential
	 * retrieval mechanisms for various Platforms
	 */
#ifdef HAVE_GETPEERUCRED
	/*
	 * Solaris and some BSD systems
	 */
	{
		ucred_t *uc = NULL;

		if (getpeerucred(sock, &uc) == 0) {
			res = 0;
			ugp->uid = ucred_geteuid(uc);
			ugp->gid = ucred_getegid(uc);
			ugp->pid = ucred_getpid(uc);
			ucred_free(uc);
		} else {
			res = -errno;
		}
	}
#elif HAVE_GETPEEREID
	/*
	 * Usually MacOSX systems
	 */
	{
		/*
		 * TODO get the peer's pid.
		 * c->pid = ?;
		 */
		if (getpeereid(sock, &ugp->uid, &ugp->gid) == 0) {
			res = 0;
		} else {
			res = -errno;
		}
	}

#elif SO_PASSCRED
	/*
	 * Usually Linux systems
	 */
	cmsg = CMSG_FIRSTHDR(&msg_recv);
	assert(cmsg);
	cred = (struct ucred *)CMSG_DATA(cmsg);
	if (cred) {
		res = 0;
		ugp->pid = cred->pid;
		ugp->uid = cred->uid;
		ugp->gid = cred->gid;
	} else {
		res = -EBADMSG;
	}
#else /* no credentials */
	res = -ENOTSUP;
#endif /* no credentials */

cleanup_and_return:

#ifdef QB_LINUX
	setsockopt(sock, SOL_SOCKET, SO_PASSCRED, &off, sizeof(off));
#endif

	return res;
}

static int32_t qb_ipcs_us_connection_acceptor(int fd, int revent, void *data)
{
	struct sockaddr_un un_addr;
	int32_t new_fd;
	struct qb_ipcs_service *s = (struct qb_ipcs_service *)data;
	int32_t res;
	struct qb_ipc_connection_request setup_msg;
	struct ipc_auth_ugp ugp;
	socklen_t addrlen = sizeof(struct sockaddr_un);
	char error_str[100];

retry_accept:
	errno = 0;
	new_fd = accept(fd, (struct sockaddr *)&un_addr, &addrlen);
	if (new_fd == -1 && errno == EINTR) {
		goto retry_accept;
	}

	if (new_fd == -1 && errno == EBADF) {
		strerror_r(errno, error_str, 100);
		qb_util_log(LOG_ERR,
			    "Could not accept Library connection:(fd: %d) [%d] %s\n",
			    fd, errno, error_str);
		return -1;
	}
	if (new_fd == -1) {
		strerror_r(errno, error_str, 100);
		qb_util_log(LOG_ERR,
			    "Could not accept Library connection: [%d] %s\n",
			    errno, error_str);
		return 0;	/* This is an error, but -1 would indicate disconnect from poll loop */
	}

	res = set_cloexec_flag(new_fd);
	if (res < 0) {
		close(new_fd);
		return 0;	/* This is an error, but -1 would indicate disconnect from poll loop */
	}
	res = set_nonblock_flag(new_fd);
	if (res < 0) {
		close(new_fd);
		return 0;	/* This is an error, but -1 would indicate disconnect from poll loop */
	}

	res = qb_ipcs_uc_recv_and_auth(new_fd, &setup_msg, sizeof(setup_msg),
				       &ugp);

	if (setup_msg.hdr.id == QB_IPC_MSG_AUTHENTICATE) {
		handle_new_connection(s, res, new_fd, &setup_msg, sizeof(setup_msg),
				       &ugp);
	} else if (setup_msg.hdr.id == QB_IPC_MSG_NEW_EVENT_SOCK) {
		if (res == 0) {
			handle_connection_new_sock(s, new_fd, &setup_msg);
		} else {
			close(new_fd);
		}
	} else {
		close(new_fd);
	}

	return 0;
}

static void qb_ipcs_us_disconnect(struct qb_ipcs_connection *c)
{
//	close(c->setup.u.us.sock);
	close(c->request.u.us.sock);
//	close(c->response.u.us.sock);
	close(c->event.u.us.sock);
}

void qb_ipcs_us_init(struct qb_ipcs_service *s)
{
	s->funcs.connect = NULL;
	s->funcs.disconnect = qb_ipcs_us_disconnect;

	s->funcs.recv = qb_ipc_us_recv;
	s->funcs.peek = NULL;
	s->funcs.reclaim = NULL;
	s->funcs.send = qb_ipc_us_send;
	s->funcs.sendv = qb_ipc_us_sendv;

	s->funcs.fc_set = NULL;
	s->funcs.q_len_get = NULL;

	s->needs_sock_for_poll = QB_FALSE;
}


