/******************************************************************************\
 *  $Id: wrapper.h,v 1.1 2001/09/16 23:45:05 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
\******************************************************************************/


#ifndef _WRAPPER_H
#define _WRAPPER_H


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <errno.h>
#include "errors.h"


#define x_pthread_mutex_init(MUTEX,ATTR)                                       \
     do {                                                                      \
         if ((errno = pthread_mutex_init((MUTEX), (ATTR))) != 0)               \
             err_msg(errno, "pthread_mutex_init() failed");                    \
     } while (0)

#define x_pthread_mutex_lock(MUTEX)                                            \
     do {                                                                      \
         if ((errno = pthread_mutex_lock(MUTEX)) != 0)                         \
             err_msg(errno, "pthread_mutex_lock() failed");                    \
     } while (0)

#define x_pthread_mutex_unlock(MUTEX)                                          \
     do {                                                                      \
         if ((errno = pthread_mutex_unlock(MUTEX)) != 0)                       \
             err_msg(errno, "pthread_mutex_unlock() failed");                  \
     } while (0)

#define x_pthread_mutex_destroy(MUTEX)                                         \
     do {                                                                      \
         if ((errno = pthread_mutex_destroy(MUTEX)) != 0)                      \
             err_msg(errno, "pthread_mutex_destroy() failed");                 \
     } while (0)

#define x_pthread_detach(THREAD)                                               \
     do {                                                                      \
         if ((errno = pthread_detach(THREAD)) != 0)                            \
             err_msg(errno, "pthread_detach() failed");                        \
     } while (0)


#endif /* !_WRAPPER_H */
