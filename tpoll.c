/*****************************************************************************
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
 ***************************************************************************** 
 *  Based on the implementation in Jon C. Snader's
 *    "Effective TCP/IP Programming" (Tip #20).
 ***************************************************************************** 
 *  Refer to "tpoll.h" for documentation on public functions.
 *****************************************************************************/


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include "log.h"
#include "tpoll.h"


/******************************************************************************
 *  Out of Memory
 *****************************************************************************/

#ifdef WITH_OOMF
#  undef out_of_memory
   extern void * out_of_memory(void);
#else /* !WITH_OOMF */
#  ifndef out_of_memory
#    define out_of_memory() (NULL)
#  endif /* !out_of_memory */
#endif /* WITH_OOMF */


/******************************************************************************
 *  Constants
 *****************************************************************************/

#define TPOLL_ALLOC 4                   /* FIXME */
#define TIMER_ALLOC 10


/******************************************************************************
 *  Internal Data Types
 *****************************************************************************/

struct tpoll {
    struct pollfd  *fds;                /* poll fd array                     */
    int             max_fd;             /* max fd in array in use            */
    int             num_fds;            /* num of pollfd structs allocated   */
};

struct timer {
    int             id;                 /* timer ID                          */
    callback_f      fnc;                /* callback function                 */
    void           *arg;                /* callback function arg             */
    struct timeval  tv;                 /* time at which timer expires       */
    struct timer   *next;               /* next timer in list                */
};

typedef struct timer * Timer;


/******************************************************************************
 *  Internal Prototypes
 *****************************************************************************/

static int _tpoll_inc_nofile (int max, int min);

static int _tpoll_grow (tpoll_t tp, int num_fds_req);

static Timer _alloc_timer (void);


/******************************************************************************
 *  Internal Variables
 *****************************************************************************/

static Timer active = NULL;
static Timer inactive = NULL;


/******************************************************************************
 *  Functions
 *****************************************************************************/

tpoll_t
tpoll_create (int n)
{
/*  Creates a new object for multiplexing I/O over at least [n]
 *    file descriptors.  If [n] is 0, the default size will be used.
 *    The internal tables as well as the limit on the maximum number
 *    of files the process can have open will grow as needed.
 *  Returns an opaque ptr to this new object, or NULL on error.
 */
    tpoll_t tp;
    int     i;

    assert (TPOLL_ALLOC > 0);

    if (n <= 0) {
        n = TPOLL_ALLOC;
    }
    if ((n = _tpoll_inc_nofile (n, n)) < 0) {
        return (NULL);
    }
    if (!(tp = malloc (sizeof (*tp)))) {
        return (out_of_memory ());
    }
    if (!(tp->fds = malloc (n * sizeof (struct pollfd)))) {
        free (tp);
        return (out_of_memory ());
    }
    tp->num_fds = n;
    tp->max_fd = -1;

    memset (tp->fds, 0, tp->num_fds * sizeof (struct pollfd));
    for (i = 0; i < tp->num_fds; i++) {
        tp->fds[i].fd = -1;
    }
    return (tp);
}


void
tpoll_destroy (tpoll_t tp)
{
/*  Destroys the [tp] object for multiplexing I/O and its associated data.
 */
    if (!tp) {
        return;
    }
    if (tp->fds) {
        free (tp->fds);
        tp->fds = NULL;
    }
    tp->num_fds = 0;
    tp->max_fd = -1;
    free (tp);
    return;
}


int
tpoll_clear (tpoll_t tp, int fd, int events)
{
/*  Removes [events] from any existing events for [fd] within the [tp] object.
 *  Returns 0 on success, or -1 on error.
 */
    int i;

    if (!tp) {
        return (-1);
    }
    if (fd < 0) {
        return (-1);
    }
    if (fd > tp->max_fd) {
        return (0);
    }
    tp->fds[fd].events &= ~events;

    if (tp->fds[fd].events != 0) {
        return (0);
    }
    tp->fds[fd].revents = 0;
    tp->fds[fd].fd = -1;

    if (tp->max_fd != fd) {
        return (0);
    }
    tp->max_fd = -1;
    for (i = fd - 1; i >= 0; i--) {
        if (tp->fds[i].fd >= 0) {
            tp->max_fd = i;
            break;
        }
    }
    return (0);
}


int
tpoll_is_set (tpoll_t tp, int fd, int events)
{
/*  Tests whether any of the bitwise-OR'd [events] have occurred for [fd]
 *    within the [tp] object.
 *  Returns true if any of the specified [events] have occurred,
 *    or 0 otherwise.
 */
    if (!tp) {
        return (-1);
    }
    if (fd < 0) {
        return (-1);
    }
    if (fd > tp->max_fd) {
        return (0);
    }
    return (tp->fds[fd].revents & events);
}


int
tpoll_set (tpoll_t tp, int fd, int events)
{
/*  Adds [events] to any existing events for [fd] within the [tp] object.
 *    The internal tables as well as the limit on the maximum number
 *    of files the process can have open will grow as needed.
 *  Returns 0 on success, or -1 on error.
 */
    if (!tp) {
        return (-1);
    }
    if (fd < 0) {
        return (-1);
    }
    if (fd >= tp->num_fds) {
        if (_tpoll_grow (tp, fd + 1) < 0) {
            return (-1);
        }
    }
    if (fd > tp->max_fd) {
        tp->max_fd = fd;
    }
    tp->fds[fd].fd = fd;
    tp->fds[fd].events |= events;
    return (0);
}


void
tpoll_zero (tpoll_t tp)
{
/*  Re-initializes the [tp] object to the null set.
 */
    int i;

    if (!tp) {
        return;
    }
    memset (tp->fds, 0, tp->num_fds * sizeof (struct pollfd));
    for (i = 0; i < tp->num_fds; i++) {
        tp->fds[i].fd = -1;
    }
    tp->max_fd = -1;
    return;
}


int
tpoll (tpoll_t tp)
{
/*  Similar to poll(), but file descriptors and timers are specified by
 *    auxiliary functions.
 *  Dispatches any timer events that have expired.
 *  Returns the number of events ready, or -1 or error.
 */
    struct timeval  tv_now;
    Timer           t;
    int             ms_timeout;
    int             n;

    if (!tp) {
        errno = EINVAL;
        return (-1);
    }
    for (;;) {
        if (gettimeofday (&tv_now, NULL) < 0) {
            log_err (LOG_ERR, "Unable to get time of day");
        }
        /*  Dispatch timer events that have expired.
         */
        while (active && !timercmp (&tv_now, &active->tv, <)) {
            t = active;
            active = active->next;
            /*
             *  The timer must be removed from the active list before
             *    the callback function is invoked in case it calls
             *    tpoll_untimeout().  O/w, the current timer would be removed
             *    by tpoll_untimeout(), and the next timer on the active list
             *    would be removed when the callback function returned.
             */
            t->fnc (t->arg);
            t->next = inactive;
            inactive = t;
        }
        if (active) {
            /*
             *  Calculate time (in milliseconds) until next timer event.
             */
            if (gettimeofday (&tv_now, NULL) < 0) {
                log_err (LOG_ERR, "Unable to get time of day");
            }
            ms_timeout = ((active->tv.tv_sec  - tv_now.tv_sec)  * 1000) +
                         ((active->tv.tv_usec - tv_now.tv_usec) / 1000);
        }
        else if (tp->max_fd >= 0) {
            /*
             *  No additional timers, but I/O events exist.
             */
            ms_timeout = -1;
        }
        else {
            /*
             *  No additional timers and no I/O events exist.
             */
            return (0);
        }
        /*  Kludge to support mux_io() pthread brain-damage.
         *  Specify a timeout to poll() to prevent the following scenario:
         *    Suppose poll() blocks after a new thread is spawned to handle a
         *    new connecting client.  This thread then adds a client obj to the
         *    conf->objs list to be handled by mux_io().  But read-activity on
         *    this client's socket will not unblock poll() because this fd does
         *    not yet exist in poll()'s fd array.
         */
        ms_timeout = 1000;

        if ((n = poll (tp->fds, tp->max_fd + 1, ms_timeout)) < 0) {
            return (-1);
        }
        if (n > 0) {
            return (n);
        }
        /*  Kludge to support mux_io() pthread brain-damage.
         */
        break;
    }
    return (0);
}


int
timeout (callback_f cb, void *arg, int ms)
{
/*  Sets a timer event for tpoll() specifying how long (in milliseconds)
 *    the timer should run before it expires.  At expiration, the callback
 *    function will be invoked with the specified arg.
 *  Returns a timer ID > 0 for use with untimeout(),
 *    or -1 on memory allocation failure.
 */
    struct timeval tv;

    assert (cb != NULL);
    assert (ms >= 0);

    if (gettimeofday (&tv, NULL) < 0) {
        log_err (LOG_ERR, "Unable to get time of day");
    }
    tv.tv_usec += ms * 1000;
    if (tv.tv_usec >= 1000000) {
        tv.tv_sec += tv.tv_usec / 1000000;
        tv.tv_usec %= 1000000;
    }
    return (abtimeout (cb, arg, &tv));
}


int
abtimeout (callback_f cb, void *arg, const struct timeval *tvp)
{
/*  Sets an "absolute" timer event for tpoll() specifying when the timer
 *    should expire.  At expiration, the callback function will be invoked
 *    with the specified arg.
 *  Returns a timer ID > 0 for use with untimeout(),
 *    or -1 on memory allocation failure.
 */
    static int  id = 1;
    Timer       t;
    Timer       tCurr;
    Timer      *tPrevPtr;

    assert (cb != NULL);
    assert (tvp != NULL);

    if (!(t = _alloc_timer ())) {
        return (-1);
    }
    t->id = id++;
    if (id <= 0) {
        id = 1;
    }
    t->fnc = cb;
    t->arg = arg;
    t->tv = *tvp;

    tPrevPtr = &active;
    tCurr = active;
    while (tCurr && !timercmp (&t->tv, &tCurr->tv, <)) {
        tPrevPtr = &tCurr->next;
        tCurr = tCurr->next;
    }
    *tPrevPtr = t;
    t->next = tCurr;
    return (t->id);
}


void
untimeout (int id)
{
/*  Cancels a timer event before it expires.
 */
    Timer  tCurr;
    Timer *tPrevPtr;

    if (id <= 0) {
        return;
    }
    tPrevPtr = &active;
    tCurr = active;
    while (tCurr && (id != tCurr->id)) {
        tPrevPtr = &tCurr->next;
        tCurr = tCurr->next;
    }
    if (!tCurr) {                       /* timer id not active */
        return;
    }
    *tPrevPtr = tCurr->next;
    tCurr->next = inactive;
    inactive = tCurr;
    return;
}


/******************************************************************************
 *  Internal Functions
 *****************************************************************************/

static int
_tpoll_inc_nofile (int max, int min)
{
/*  Attempts to increase the maximum number of files the process can have open
 *    to at least [min] but no more than [max].
 *  Returns the new limit on the number of open files, or -1 on error.
 */
    struct rlimit limit;

    if (max <= 0) {
        return (-1);
    }
    if (min > max) {
        min = max;
    }
    if (getrlimit (RLIMIT_NOFILE, &limit) < 0) {
        log_err (errno, "Unable to get the num open file limit");
    }
    if (max <= limit.rlim_cur) {
        return (max);
    }
    if ((max > limit.rlim_max) && (min < limit.rlim_max)) {
        limit.rlim_cur = limit.rlim_max;
    }
    else {
        limit.rlim_cur = max;
        limit.rlim_max = (max > limit.rlim_max) ? max : limit.rlim_max;
    }
    if (setrlimit (RLIMIT_NOFILE, &limit) < 0) {
        log_msg (LOG_ERR, "Unable to increase the num open file limit to %d",
            limit.rlim_cur);
        return (-1);
    }
    log_msg (LOG_INFO, "Increased the num open file limit to %d",
        limit.rlim_cur);
    return (limit.rlim_cur);
}


static int
_tpoll_grow (tpoll_t tp, int num_fds_req)
{
/*  Attempts to grow [tp]'s pollfd array to at least [num_fds_req] structs.
 *  Returns 0 if the request is successful, -1 if not.
 */
    struct pollfd *fds_tmp;
    struct pollfd *fds_new;
    int            num_fds_tmp;
    int            num_fds_new;
    int            i;

    assert (tp != NULL);
    assert (num_fds_req > 0);

    if (num_fds_req <= tp->num_fds) {
        return (0);
    }
    num_fds_tmp = tp->num_fds;
    while ((num_fds_tmp < num_fds_req) && (num_fds_tmp > 0)) {
        num_fds_tmp *= 2;
    }
    if (num_fds_tmp < num_fds_req) {
        num_fds_tmp = num_fds_req;
    }
    if ((num_fds_tmp = _tpoll_inc_nofile (num_fds_tmp, num_fds_req)) < 0) {
        return (-1);
    }
    if (!(fds_tmp = realloc (tp->fds, num_fds_tmp * sizeof (struct pollfd)))) {
        return (-1);
    }
    fds_new = fds_tmp + tp->num_fds;
    num_fds_new = num_fds_tmp - tp->num_fds;
    memset (fds_new, 0, num_fds_new * sizeof (struct pollfd));
    for (i = tp->num_fds; i < num_fds_tmp; i++) {
        fds_tmp[i].fd = -1;
    }
    tp->fds = fds_tmp;
    tp->num_fds = num_fds_tmp;
    return (0);
}


static Timer
_alloc_timer (void)
{
/*  Returns a timer, or out_of_memory() if memory allocation fails.
 */
    Timer t;
    Timer tLast;

    if (!inactive) {
        if (!(inactive = malloc (TIMER_ALLOC * sizeof (struct timer)))) {
            return (out_of_memory ());
        }
        tLast = inactive + TIMER_ALLOC - 1;
        for (t = inactive; t < tLast; t++) {
            t->next = t + 1;
        }
        tLast->next = NULL;
    }
    t = inactive;
    inactive = inactive->next;
    return (t);
}
