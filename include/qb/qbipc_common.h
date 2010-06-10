/*
 * Copyright (c) 2010 Red Hat, Inc.
 *
 * All rights reserved.
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
#ifndef QB_IPC_COMMON_H_DEFINED
#define QB_IPC_COMMON_H_DEFINED

typedef struct {
	int size __attribute__ ((aligned(8)));
	int id __attribute__ ((aligned(8)));
} qb_ipc_request_header_t __attribute__ ((aligned(8)));

typedef struct {
	int size __attribute__ ((aligned(8)));
	int id __attribute__ ((aligned(8)));
	int32_t error __attribute__ ((aligned(8)));
} qb_ipc_response_header_t __attribute__ ((aligned(8)));

#endif /* QB_IPC_COMMON_H_DEFINED */
