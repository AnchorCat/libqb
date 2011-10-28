/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Angus Salkeld <asalkeld@redhat.com>
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

#ifdef HAVE_SYS_SHM_H
#include <sys/shm.h>
#endif
#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif

#include "util_int.h"
#include <qb/qbdefs.h>
#include <qb/qbutil.h>

char *
qb_strerror_r(int errnum, char *buf, size_t buflen)
{
#ifdef QB_LINUX
	return strerror_r(errnum, buf, buflen);
#else
	char * out_buf;

	if (strerror_r(err_num, buffer, sizeof_buffer) == 0 ) {
		out_ptr = buffer;
	} else {
		out_ptr = "";
	}
	return out_buf;
#endif /* QB_LINUX */
}

static int32_t
open_mmap_file(char *path, uint32_t file_flags)
{
	if (strstr(path, "XXXXXX") != NULL) {
		return mkstemp(path);
	}

	return open(path, file_flags, 0600);
}

int32_t
qb_sys_mmap_file_open(char *path, const char *file, size_t bytes,
		       uint32_t file_flags)
{
	int32_t fd;
	int32_t i;
	int32_t res = 0;
	ssize_t written;
	char *buffer = NULL;
	char *is_absolute = strchr(file, '/');;

	if (is_absolute) {
		strcpy(path, file);
	} else {
		snprintf(path, PATH_MAX, "/dev/shm/%s", file);
	}
	fd = open_mmap_file(path, file_flags);
	if (fd < 0 && !is_absolute) {
		res = -errno;
		qb_util_perror(LOG_ERR, "couldn't open file %s", path);

		snprintf(path, PATH_MAX, LOCALSTATEDIR "/run/%s", file);
		fd = open_mmap_file(path, file_flags);
		if (fd < 0) {
			res = -errno;
			qb_util_perror(LOG_ERR, "couldn't open file %s", path);
			return res;
		}
	}

	if (ftruncate(fd, bytes) == -1) {
		res = -errno;
		qb_util_perror(LOG_ERR, "couldn't truncate file %s", path);
		goto unlink_exit;
	}

	if (file_flags & O_CREAT) {
		long page_size = sysconf(_SC_PAGESIZE);
		if (page_size < 0) {
			res = -errno;
			goto unlink_exit;
		}
		buffer = calloc(1, page_size);
		if (buffer == NULL) {
			res = -ENOMEM;
			goto unlink_exit;
		}
		for (i = 0; i < (bytes / page_size); i++) {
retry_write:
			written = write(fd, buffer, page_size);
			if (written == -1 && errno == EINTR) {
				goto retry_write;
			}
			if (written != page_size) {
				res = -ENOSPC;
				free(buffer);
				goto unlink_exit;
			}
		}
		free(buffer);
	}

	return fd;

unlink_exit:
	unlink(path);
	if (fd > 0) {
		close(fd);
	}
	return res;
}


int32_t
qb_sys_circular_mmap(int32_t fd, void **buf, size_t bytes)
{
	void *addr_orig = NULL;
	void *addr;
	void *addr_next;
	int32_t res;
	int flags = MAP_ANONYMOUS;

#ifdef QB_FORCE_SHM_ALIGN
/* On a number of arches any fixed and shared mmap() mapping address
 * must be aligned to 16k. If the first mmap() below is not shared then
 * the first mmap() will succeed because these restrictions do not apply to
 * private mappings. The second mmap() wants a shared memory mapping but
 * the address returned by the first one is only page-aligned and not
 * aligned to 16k.
 */
	flags |= MAP_SHARED;
#else
	flags |= MAP_PRIVATE;
#endif /* QB_FORCE_SHM_ALIGN */

	addr_orig = mmap(NULL, bytes << 1, PROT_NONE, flags, -1, 0);

	if (addr_orig == MAP_FAILED) {
		return -errno;
	}

	addr = mmap(addr_orig, bytes, PROT_READ | PROT_WRITE,
		    MAP_FIXED | MAP_SHARED, fd, 0);

	if (addr != addr_orig) {
		res = -errno;
		goto cleanup_fail;
	}
#ifdef QB_BSD
	madvise(addr_orig, bytes, MADV_NOSYNC);
#endif
	addr_next = ((char *)addr_orig) + bytes;
	addr = mmap(addr_next,
		    bytes, PROT_READ | PROT_WRITE,
		    MAP_FIXED | MAP_SHARED, fd, 0);
	if (addr != addr_next) {
		res = -errno;
		goto cleanup_fail;
	}
#ifdef QB_BSD
	madvise(((char *)addr_orig) + bytes, bytes, MADV_NOSYNC);
#endif

	res = close(fd);
	if (res) {
		goto cleanup_fail;
	}
	*buf = addr_orig;
	return 0;

cleanup_fail:

	if (addr_orig) {
		munmap(addr_orig, bytes << 1);
	}
	close(fd);
	return res;
}

int32_t
qb_sys_fd_nonblock_cloexec_set(int32_t fd)
{
	int32_t res = 0;
	int32_t oldflags = fcntl(fd, F_GETFD, 0);

	if (oldflags < 0) {
		oldflags = 0;
	}
	oldflags |= FD_CLOEXEC;
	res = fcntl(fd, F_SETFD, oldflags);
	if (res == -1) {
		res = -errno;
		qb_util_perror(LOG_ERR,
			       "Could not set close-on-exit on fd:%d", fd);
		return res;
	}

	res = fcntl(fd, F_SETFL, O_NONBLOCK);
	if (res == -1) {
		res = -errno;
		qb_util_log(LOG_ERR, "Could not set non-blocking on fd:%d", fd);
	}

	return res;
}

