/******************************************************************************\
 *  $Id: util.h,v 1.12 2001/09/13 20:36:44 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
\******************************************************************************/


#ifndef _UTIL_H
#define _UTIL_H


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <errno.h>
#include <pthread.h>


typedef void * (*PthreadFunc)(void *);

typedef void SigFunc(int);


#ifndef MAX
#  define MAX(x,y) (((x) >= (y)) ? (x) : (y))
#endif /* !MAX */
#ifndef MIN
#  define MIN(x,y) (((x) <= (y)) ? (x) : (y))
#endif /* !MIN */

#define init_mutex(MUTEX)                                                      \
     do {                                                                      \
         if ((errno = pthread_mutex_init(MUTEX, NULL)) != 0)                   \
             err_msg(errno, "pthread_mutex_init() failed");                    \
     } while (0)

#define lock_mutex(MUTEX)                                                      \
     do {                                                                      \
         if ((errno = pthread_mutex_lock(MUTEX)) != 0)                         \
             err_msg(errno, "pthread_mutex_lock() failed");                    \
     } while (0)

#define unlock_mutex(MUTEX)                                                    \
     do {                                                                      \
         if ((errno = pthread_mutex_unlock(MUTEX)) != 0)                       \
             err_msg(errno, "pthread_mutex_unlock() failed");                  \
     } while (0)

#define destroy_mutex(MUTEX)                                                   \
     do {                                                                      \
         if ((errno = pthread_mutex_destroy(MUTEX)) != 0)                      \
             err_msg(errno, "pthread_mutex_destroy() failed");                 \
     } while (0)


SigFunc * posix_signal(int signum, SigFunc *f);
/*
 *  A wrapper for the historical signal() function to do things the Posix way.
 */


#endif /* !_UTIL_H */
