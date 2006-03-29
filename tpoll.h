/*****************************************************************************\
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2001-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *  UCRL-CODE-2002-009.
 *  
 *  This file is part of ConMan, a remote console management program.
 *  For details, see <http://www.llnl.gov/linux/conman/>.
 *  
 *  ConMan is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  ConMan is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with ConMan; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/


#ifndef _TPOLL_H
#define _TPOLL_H

#include <poll.h>
#include <sys/time.h>


/******************************************************************************
 *  Notes
 *****************************************************************************/

/*  These routines are not thread-safe.
 *  Only invoke them from within a single thread.
 *
 *  When a timer memory allocation request fails, out_of_memory() is returned.
 *  By default, this is a macro definition that returns NULL; this macro may
 *  be redefined to invoke another routine instead.  Furthermore, if WITH_OOMF
 *  is defined, this macro will not be defined and the allocation routine
 *  will expect an external Out-Of-Memory Function to be defined.
 */


/******************************************************************************
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


/******************************************************************************
 *  Functions
 *****************************************************************************/

tpoll_t tpoll_create (int n);

void tpoll_destroy (tpoll_t tp);

int tpoll_clear (tpoll_t tp, int fd, int events);

int tpoll_is_set (tpoll_t tp, int fd, int events);

int tpoll_set (tpoll_t tp, int fd, int events);

void tpoll_zero (tpoll_t tp);

int tpoll (tpoll_t tp);

int timeout (callback_f cb, void *arg, int ms);

int abtimeout (callback_f cb, void *arg, const struct timeval *tvp);

void untimeout (int id);


#endif /* !_TPOLL_H */
