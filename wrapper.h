/*****************************************************************************
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *  Copyright (C) 2007-2016 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2001-2007 The Regents of the University of California.
 *  UCRL-CODE-2002-009.
 *
 *  This file is part of ConMan: The Console Manager.
 *  For details, see <https://dun.github.io/conman/>.
 *
 *  ConMan is free software: you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation, either version 3 of the License, or (at your option)
 *  any later version.
 *
 *  ConMan is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with ConMan.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************/


#ifndef _WRAPPER_H
#define _WRAPPER_H

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <errno.h>
#include "log.h"


#if WITH_PTHREADS

#  define x_pthread_mutex_init(MUTEX,ATTR)                                    \
     do {                                                                     \
         if ((errno = pthread_mutex_init((MUTEX), (ATTR))) != 0)              \
             log_err(errno, "pthread_mutex_init() failed");                   \
     } while (0)

#  define x_pthread_mutex_lock(MUTEX)                                         \
     do {                                                                     \
         if ((errno = pthread_mutex_lock(MUTEX)) != 0)                        \
             log_err(errno, "pthread_mutex_lock() failed");                   \
     } while (0)

#  define x_pthread_mutex_unlock(MUTEX)                                       \
     do {                                                                     \
         if ((errno = pthread_mutex_unlock(MUTEX)) != 0)                      \
             log_err(errno, "pthread_mutex_unlock() failed");                 \
     } while (0)

#  define x_pthread_mutex_destroy(MUTEX)                                      \
     do {                                                                     \
         if ((errno = pthread_mutex_destroy(MUTEX)) != 0)                     \
             log_err(errno, "pthread_mutex_destroy() failed");                \
     } while (0)

#  define x_pthread_detach(THREAD)                                            \
     do {                                                                     \
         if ((errno = pthread_detach(THREAD)) != 0)                           \
             log_err(errno, "pthread_detach() failed");                       \
     } while (0)

#else /* !WITH_PTHREADS */

#  define x_pthread_mutex_init(MUTEX,ATTR)
#  define x_pthread_mutex_lock(MUTEX)
#  define x_pthread_mutex_unlock(MUTEX)
#  define x_pthread_mutex_destroy(MUTEX)
#  define x_pthread_detach(THREAD)

#endif /* WITH_PTHREADS */


#endif /* !_WRAPPER_H */
