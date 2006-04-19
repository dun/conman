/*****************************************************************************
 *  $Id$
 *****************************************************************************
 *  This file is part of ConMan, a remote console management program.
 *  For details, see <http://www.llnl.gov/linux/conman/>.
 *
 *  Copyright (C) 2001-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *  UCRL-CODE-2002-009.
 *
 *  This is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 *****************************************************************************/


#ifndef _TPOLL_H
#define _TPOLL_H

#include <poll.h>
#include <sys/time.h>


/*****************************************************************************
 *  Data Types
 *****************************************************************************/

typedef struct tpoll * tpoll_t;
/*
 *  Opaque data pointer for a tpoll object.
 */

typedef void (*callback_f) (void *arg);
/*
 *  Function prototype for a timer callback function.
 */

typedef enum {
/*
 *  Data type for tpoll_zero() [how] parameter.
 */
    TPOLL_ZERO_FDS    = 0x01,           /* zero fds but not timers */
    TPOLL_ZERO_TIMERS = 0x02,           /* zero timers but not fds */
    TPOLL_ZERO_ALL    = 0x03,           /* zero both fds and timers */
} tpoll_zero_t;


/*****************************************************************************
 *  Functions
 *****************************************************************************/

tpoll_t tpoll_create (int n);

void tpoll_destroy (tpoll_t tp);

int tpoll_zero (tpoll_t tp, tpoll_zero_t how);

int tpoll_clear (tpoll_t tp, int fd, short int events);

int tpoll_is_set (tpoll_t tp, int fd, short int events);

int tpoll_set (tpoll_t tp, int fd, short int events);

int tpoll_timeout_absolute (tpoll_t tp, callback_f cb, void *arg,
    const struct timeval *tvp);

int tpoll_timeout_relative (tpoll_t tp, callback_f cb, void *arg, int ms);

int tpoll_timeout_cancel (tpoll_t tp, int id);

int tpoll (tpoll_t tp, int ms);


#endif /* !_TPOLL_H */
