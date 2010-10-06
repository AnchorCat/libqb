/*
 * Copyright (C) 2010 Red Hat, Inc.
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

#ifndef QB_DEFS_H_DEFINED
#define QB_DEFS_H_DEFINED

/**
 * @file qbdefs.h
 * @author Angus Salkeld <asalkeld@redhat.com>
 *
 * These are some convience macros and defines.
 */

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

/*
 * simple math macros
 */
#define QB_ROUNDUP(x, y)	((((x) + ((y) - 1)) / (y)) * (y))
#define QB_ABS(i)		(((i) < 0) ? -(i) : (i))
#define QB_MAX(a, b)		(((a) > (b)) ? (a) : (b))
#define QB_MIN(a, b)		(((a) < (b)) ? (a) : (b))

/*
 * the usual boolean defines
 */
#define	QB_FALSE		0
#define	QB_TRUE			(!QB_FALSE)

/*
 * handy time based converters.
 */
#define QB_TIME_MS_IN_SEC   1000ULL
#define QB_TIME_US_IN_SEC   1000000ULL
#define QB_TIME_NS_IN_SEC   1000000000ULL
#define QB_TIME_US_IN_MSEC  1000ULL
#define QB_TIME_NS_IN_MSEC  1000000ULL
#define QB_TIME_NS_IN_USEC  1000ULL

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif /* __cplusplus */
/* *INDENT-ON* */

#endif /* QB_DEFS_H_DEFINED */
