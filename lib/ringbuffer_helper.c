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
#include <config.h>

#include "ringbuffer.h"

#if _POSIX_THREAD_PROCESS_SHARED > 0
static int32_t my_posix_sem_create(qb_ringbuffer_t * rb, uint32_t flags)
{
	int32_t pshared = 0;
	if (flags & QB_RB_FLAG_SHARED_PROCESS) {
		if ((flags & QB_RB_FLAG_CREATE) == 0) {
			return 0;
		}
		pshared = 1;
	}
	return sem_init(&rb->shared_hdr->posix_sem, pshared, 0);
}
#else
static int32_t my_sysv_sem_create(qb_ringbuffer_t * rb, uint32_t flags)
{
	union semun options;
	int32_t res;
	key_t sem_key;

	sem_key = ftok(rb->shared_hdr->hdr_path, (rb->shared_hdr->size + 1));

	if (sem_key == -1) {
		qb_util_log(LOG_ERR, "couldn't get a sem id %s",
			    strerror(errno));
		return -1;
	}

	if (flags & QB_RB_FLAG_CREATE) {
		rb->sem_id = semget(sem_key, 1, IPC_CREAT | IPC_EXCL | 0600);
		if (rb->sem_id == -1) {
			qb_util_log(LOG_ERR, "couldn't create a semaphore %s",
				    strerror(errno));
			return -1;
		}
		options.val = 0;
		res = semctl(rb->sem_id, 0, SETVAL, options);
	} else {
		rb->sem_id = semget(sem_key, 0, 0600);
		if (rb->sem_id == -1) {
			qb_util_log(LOG_ERR, "couldn't get a sem id %s",
				    strerror(errno));
			return -1;
		}
		res = 0;
	}
	qb_util_log(LOG_INFO, "sem key:%d, id:%d, value:%d",
		    sem_key, rb->sem_id, semctl(rb->sem_id, 0, GETVAL, 0));

	return res;
}
#endif

int32_t my_sem_create(qb_ringbuffer_t * rb, uint32_t flags)
{
	if ((rb->flags & QB_RB_FLAG_SHARED_PROCESS) == 0) {
		return 0;
	}
#if _POSIX_THREAD_PROCESS_SHARED > 0
	return my_posix_sem_create(rb, flags);
#else
	return my_sysv_sem_create(rb, flags);
#endif
}

#if _POSIX_THREAD_PROCESS_SHARED > 0
static int32_t my_posix_sem_post(qb_ringbuffer_t * rb)
{
	return sem_post(&rb->shared_hdr->posix_sem);
}
#else
static int32_t my_sysv_sem_post(qb_ringbuffer_t * rb)
{
	struct sembuf sops[1];

	if ((rb->flags & QB_RB_FLAG_SHARED_PROCESS) == 0) {
		return 0;
	}

	sops[0].sem_num = 0;
	sops[0].sem_op = 1;
	sops[0].sem_flg = 0;

semop_again:
	if (semop(rb->sem_id, sops, 1) == -1) {
		if (errno == EINTR) {
			goto semop_again;
		} else {
			qb_util_log(LOG_ERR,
				    "could not increment semaphore : %s",
				    strerror(errno));
		}

		return -1;
	}
	return 0;
}
#endif

int32_t my_sem_post(qb_ringbuffer_t * rb)
{
	if ((rb->flags & QB_RB_FLAG_SHARED_PROCESS) == 0) {
		return 0;
	}
#if _POSIX_THREAD_PROCESS_SHARED > 0
	return my_posix_sem_post(rb);
#else
	return my_sysv_sem_post(rb);
#endif
}

#if _POSIX_THREAD_PROCESS_SHARED > 0
static int32_t my_posix_sem_timedwait(qb_ringbuffer_t * rb, int32_t ms_timeout)
{
	struct timespec ts_timeout;

	if (ms_timeout >= 0) {
		ts_timeout.tv_sec = time(NULL);
		ts_timeout.tv_sec += (ms_timeout / 1000);
		ts_timeout.tv_nsec = (ms_timeout % 1000) * RB_NS_IN_MSEC;
		return sem_timedwait(&rb->shared_hdr->posix_sem, &ts_timeout);
	} else {
		return sem_wait(&rb->shared_hdr->posix_sem);
	}
}

#else

static int32_t my_sysv_sem_timedwait(qb_ringbuffer_t * rb, int32_t ms_timeout)
{
	struct sembuf sops[1];
	struct timespec ts_timeout;
	struct timespec *ts_pt;

	if (ms_timeout >= 0) {
		/*
		 * Note: sem_timedwait takes an absolute time where as semtimedop
		 * takes a relative time.
		 */
		ts_timeout.tv_sec = 0;
		ts_timeout.tv_sec += (ms_timeout / 1000);
		ts_timeout.tv_nsec = (ms_timeout % 1000) * RB_NS_IN_MSEC;
		ts_pt = &ts_timeout;
	} else {
		ts_pt = NULL;
	}

	/*
	 * wait for sem post.
	 */
	sops[0].sem_num = 0;
	sops[0].sem_op = -1;
	sops[0].sem_flg = 0;

semop_again:
	if (semtimedop(rb->sem_id, sops, 1, ts_pt) == -1) {
		if (errno == EINTR) {
			goto semop_again;
		} else if (errno == EAGAIN) {
			/* make consistent with sem_timedwait */
			errno = ETIMEDOUT;
		} else {
			qb_util_log(LOG_ERR,
				    "error waiting for semaphore : %s",
				    strerror(errno));
		}

		return -1;
	}

	return 0;
}
#endif

int32_t my_sem_timedwait(qb_ringbuffer_t * rb, int32_t ms_timeout)
{
	if ((rb->flags & QB_RB_FLAG_SHARED_PROCESS) == 0) {
		return 0;
	}
#if _POSIX_THREAD_PROCESS_SHARED > 0
	return my_posix_sem_timedwait(rb, ms_timeout);
#else
	return my_sysv_sem_timedwait(rb, ms_timeout);
#endif
}

int32_t my_sem_destroy(qb_ringbuffer_t * rb)
{
#if _POSIX_THREAD_PROCESS_SHARED > 0
	return sem_destroy(&rb->shared_hdr->posix_sem);
#else
	return semctl(rb->sem_id, 0, IPC_RMID, 0);
#endif
}

#if _POSIX_THREAD_PROCESS_SHARED > 0
static int32_t my_posix_lock_it_create(qb_ringbuffer_t * rb, uint32_t flags)
{
	if (flags & QB_RB_FLAG_CREATE) {
		return pthread_spin_init(&rb->shared_hdr->spinlock,
					 PTHREAD_PROCESS_SHARED);
	} else {
		return 0;
	}
}
#else
static int32_t my_sysv_lock_it_create(qb_ringbuffer_t * rb, uint32_t flags)
{
	union semun options;
	int32_t res;
	key_t sem_key;

	sem_key = ftok(rb->shared_hdr->hdr_path, rb->shared_hdr->size);

	if (sem_key == -1) {
		qb_util_log(LOG_ERR, "couldn't get a sem id %s",
			    strerror(errno));
		return -1;
	}

	if (flags & QB_RB_FLAG_CREATE) {
		rb->lock_id = semget(sem_key, 1, IPC_CREAT | IPC_EXCL | 0600);
		if (rb->lock_id == -1) {
			qb_util_log(LOG_ERR, "couldn't create a semaphore %s",
				    strerror(errno));
			return -1;
		}
		options.val = 0;
		res = semctl(rb->lock_id, 0, SETVAL, options);
	} else {
		rb->lock_id = semget(sem_key, 0, 0600);
		if (rb->lock_id == -1) {
			qb_util_log(LOG_ERR, "couldn't get a sem id %s",
				    strerror(errno));
			return -1;
		}
		res = 0;
	}
	qb_util_log(LOG_INFO, "sem key:%d, id:%d, value:%d",
		    sem_key, rb->lock_id, semctl(rb->lock_id, 0, GETVAL, 0));

	return res;
}
#endif

int32_t my_lock_it_create(qb_ringbuffer_t * rb, uint32_t flags)
{
	if ((rb->flags & QB_RB_FLAG_SHARED_PROCESS) == 0) {
		return 0;
	}
#if _POSIX_THREAD_PROCESS_SHARED > 0
	return my_posix_lock_it_create(rb, flags);
#else
	return my_sysv_lock_it_create(rb, flags);
#endif
}

#if _POSIX_THREAD_PROCESS_SHARED > 0
static int32_t my_posix_lock_it(qb_ringbuffer_t * rb)
{
	return pthread_spin_lock(&rb->shared_hdr->spinlock);
}
#else
static int32_t my_sysv_lock_it(qb_ringbuffer_t * rb)
{
	struct sembuf sops[2];

	/*
	 * atomically wait for sem to get to 0 and then incr.
	 */
	sops[0].sem_num = 0;
	sops[0].sem_op = 0;
	sops[0].sem_flg = 0;

	sops[1].sem_num = 0;
	sops[1].sem_op = 1;
	sops[1].sem_flg = 0;

semop_again:
	if (semop(rb->lock_id, sops, 2) == -1) {
		if (errno == EINTR) {
			goto semop_again;
		} else {
			qb_util_log(LOG_ERR, "could not lock it : %s",
				    strerror(errno));
		}
		return -1;
	}
	return 0;
}
#endif

int32_t my_lock_it(qb_ringbuffer_t * rb)
{
	if ((rb->flags & QB_RB_FLAG_SHARED_PROCESS) == 0) {
		return 0;
	}
#if _POSIX_THREAD_PROCESS_SHARED > 0
	return my_posix_lock_it(rb);
#else
	return my_sysv_lock_it(rb);
#endif
}

#if _POSIX_THREAD_PROCESS_SHARED > 0
static int32_t my_posix_unlock_it(qb_ringbuffer_t * rb)
{
	return pthread_spin_unlock(&rb->shared_hdr->spinlock);
}
#else
static int32_t my_sysv_unlock_it(qb_ringbuffer_t * rb)
{
	struct sembuf lock_it;

	lock_it.sem_num = 0;
	lock_it.sem_op = -1;
	lock_it.sem_flg = IPC_NOWAIT;

semop_again:
	if (semop(rb->lock_id, &lock_it, 1) == -1) {
		if (errno == EINTR) {
			goto semop_again;
		} else {
			qb_util_log(LOG_ERR, "could not unlock it : %s",
				    strerror(errno));
		}
		return -1;
	}
	return 0;
}
#endif

int32_t my_unlock_it(qb_ringbuffer_t * rb)
{
	if ((rb->flags & QB_RB_FLAG_SHARED_PROCESS) == 0) {
		return 0;
	}
#if _POSIX_THREAD_PROCESS_SHARED > 0
	return my_posix_unlock_it(rb);
#else
	return my_sysv_unlock_it(rb);
#endif
}

int32_t my_lock_it_destroy(qb_ringbuffer_t * rb)
{
	if ((rb->flags & QB_RB_FLAG_SHARED_PROCESS) == 0) {
		return 0;
	}
#if _POSIX_THREAD_PROCESS_SHARED > 0
	return pthread_spin_destroy(&rb->shared_hdr->spinlock);
#else
	return semctl(rb->lock_id, 0, IPC_RMID, 0);
#endif
}
