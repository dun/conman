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


#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>
#include "bool.h"
#include "log.h"
#include "tpoll.h"


/*****************************************************************************
 *  Notes
 *****************************************************************************
 *  Based on ideas from:
 *  - David R. Butenhof's "Programming with POSIX Threads" (Section 3.3.4)
 *  - Jon C. Snader's "Effective TCP/IP Programming" (Tip #20)
 *
 *  This implementation is thread-safe.
 *
 *  This implementation assumes the set of file descriptors being polled is
 *  densely populated up through the maximum file descriptor of interest; as
 *  such, the fd_array[] is indexed by the file descriptor.  If this assumption
 *  is not the case, performance can be increased by adding new file
 *  descriptors to the first empty slot in fd_array[], and maintaining a hash
 *  to map file descriptors onto the corresponding fd_array[] index.
 *
 *  This implementation assumes the number of concurrent active timers is
 *  moderate; as such, active timers are stored in a linked-list in order of
 *  increasing timevals (ie, the head of the list (timers_active) is the next
 *  timer to expire).  It does not scale well to a large number of timers
 *  because insertion and deletion are O(n), although dispatching is O(1).
 *  Other possible implementations are heaps [Sedgewick 1998] which are
 *  O(log n) for insertion, deletion, and dispatch; or hashed timing wheels
 *  [Varghese and Lauck 1996] which can be as efficient as O(1) for insertion,
 *  deletion, and dispatch.
 */


/*****************************************************************************
 *  Constants
 *****************************************************************************/

#define TPOLL_ALLOC     256


/*****************************************************************************
 *  Internal Data Types
 *****************************************************************************/

typedef struct tpoll_timer * _tpoll_timer_t;

struct tpoll {
    struct pollfd   *fd_array;          /* poll fd array                     */
    int              fd_pipe[ 2 ];      /* signal pipe for unblocking poll() */
    int              num_fds_alloc;     /* num pollfd structs allocated      */
    int              num_fds_used;      /* num pollfd structs in use         */
    int              max_fd;            /* max fd in array in use            */
    _tpoll_timer_t   timers_active;     /* sorted list of active timers      */
    int              timers_next_id;    /* next id to be assigned to a timer */
    pthread_mutex_t  mutex;             /* locking primitive                 */
    bool             is_blocked;        /* flag set when blocking on poll()  */
    bool             is_realloced;      /* flag set after fd_array[] realloc */
    bool             is_signaled;       /* flag set when fd_pipe is signaled */
    bool             is_mutex_inited;   /* flag set when mutex initialized   */
};

struct tpoll_timer {
    int              id;                /* timer ID                          */
    callback_f       fnc;               /* callback function                 */
    void            *arg;               /* callback function arg             */
    struct timeval   tv;                /* expiration time                   */
    _tpoll_timer_t   next;              /* next timer in list                */
};


/*****************************************************************************
 *  Internal Prototypes
 *****************************************************************************/

static void _tpoll_init (tpoll_t tp, tpoll_zero_t how);

static void _tpoll_signal_send (tpoll_t tp);

static void _tpoll_signal_recv (tpoll_t tp);

static int _tpoll_grow (tpoll_t tp, int num_fds_req);

static void _tpoll_get_timeval (struct timeval *tvp, int ms);

static int _tpoll_diff_timeval (struct timeval *tvp1, struct timeval *tvp0);


/*****************************************************************************
 *  Functions
 *****************************************************************************/

tpoll_t
tpoll_create (int n)
{
/*  Creates a new tpoll object for multiplexing timers as well as I/O over at
 *    least [n] file descriptors.  If [n] is 0, the default size will be used.
 *  Returns an opaque pointer to this new object, or NULL on error.
 */
    tpoll_t tp = NULL;
    int     i;
    int     fval;
    int     e;

    assert (TPOLL_ALLOC > 0);

    if (n <= 0) {
        n = TPOLL_ALLOC;
    }
    if (!(tp = malloc (sizeof (struct tpoll)))) {
        goto err;
    }
    tp->fd_pipe[ 0 ] = tp->fd_pipe[ 1 ] = -1;
    tp->timers_active = NULL;
    tp->is_blocked = false;
    tp->is_realloced = false;
    tp->is_signaled = false;
    tp->is_mutex_inited = false;

    if (!(tp->fd_array = malloc (n * sizeof (struct pollfd)))) {
        goto err;
    }
    tp->num_fds_alloc = n;

    if (pipe (tp->fd_pipe) < 0) {
        goto err;
    }
    for (i = 0; i < 2; i++) {
        if ((fval = fcntl (tp->fd_pipe[ i ], F_GETFL, 0)) < 0) {
            goto err;
        }
        if (fcntl (tp->fd_pipe[ i ], F_SETFL, fval | O_NONBLOCK) < 0) {
            goto err;
        }
        if (fcntl (tp->fd_pipe[ i ], F_SETFD, FD_CLOEXEC) < 0) {
            goto err;
        }
    }
    if ((e = pthread_mutex_init (&tp->mutex, NULL)) != 0) {
        errno = e;
        goto err;
    }
    tp->is_mutex_inited = true;

    /*  The mutex is not locked here before calling _tpoll_init() because the
     *    object handle (tp) has not yet been returned.
     */
    _tpoll_init (tp, TPOLL_ZERO_ALL);
    return (tp);

err:
    tpoll_destroy (tp);
    return (NULL);
}


void
tpoll_destroy (tpoll_t tp)
{
/*  Destroys the tpoll object [tp] and cancels all of its associated timers.
 */
    int            i;
    _tpoll_timer_t t;
    int            e;

    if (!tp) {
        return;
    }
    if (tp->is_mutex_inited) {
        if ((e = pthread_mutex_lock (&tp->mutex)) != 0) {
            log_err (errno = e, "Unable to lock tpoll mutex");
        }
    }
    if (tp->fd_array) {
        free (tp->fd_array);
        tp->fd_array = NULL;
    }
    for (i = 0; i < 2; i++) {
        if (tp->fd_pipe[ i ] > -1) {
            (void) close (tp->fd_pipe[ i ]);
            tp->fd_pipe[ i ] = -1;
        }
    }
    while (tp->timers_active) {
        t = tp->timers_active;
        tp->timers_active = t->next;
        free (t);
    }
    if (tp->is_mutex_inited) {
        if ((e = pthread_mutex_unlock (&tp->mutex)) != 0) {
            log_err (errno = e, "Unable to unlock tpoll mutex");
        }
        if ((e = pthread_mutex_destroy (&tp->mutex)) != 0) {
            log_err (errno = e, "Unable to destroy tpoll mutex");
        }
        tp->is_mutex_inited = false;
    }
    free (tp);
    return;
}


int
tpoll_zero (tpoll_t tp, tpoll_zero_t how)
{
/*  Re-initializes the tpoll object [tp].
 *    If [how] is TPOLL_ZERO_ALL, everything is reset.
 *    If [how] is TPOLL_ZERO_FDS, only the file descriptor events are reset.
 *    If [how] is TPOLL_ZERO_TIMERS, only the timers are canceled.
 *    If [how] is anything else, no action is taken.
 *  Returns 0 on success, or -1 on error.
 */
    int e;

    if (!tp) {
        errno = EINVAL;
        return (-1);
    }
    if (how & ~TPOLL_ZERO_ALL) {
        errno = EINVAL;
        return (-1);
    }
    if ((e = pthread_mutex_lock (&tp->mutex)) != 0) {
        log_err (errno = e, "Unable to lock tpoll mutex");
    }
    _tpoll_init (tp, how);
    _tpoll_signal_send (tp);

    DPRINTF((21, "tpoll_zero how=%d.\n", how));
    if ((e = pthread_mutex_unlock (&tp->mutex)) != 0) {
        log_err (errno = e, "Unable to unlock tpoll mutex");
    }
    return (0);
}


int
tpoll_clear (tpoll_t tp, int fd, short int events)
{
/*  Removes the bitwise-OR'd [events] from any existing events for file
 *    descriptor [fd] within the tpoll object [tp].
 *  Returns 0 on success, or -1 on error.
 */
    short int events_new = 0;
    int       i;
    int       e;

    if (!tp) {
        errno = EINVAL;
        return (-1);
    }
    if (fd < 0) {
        errno = EINVAL;
        return (-1);
    }
    if (events == 0) {
        return (0);
    }
    if ((e = pthread_mutex_lock (&tp->mutex)) != 0) {
        log_err (errno = e, "Unable to lock tpoll mutex");
    }
    if ((fd <= tp->max_fd) && (tp->fd_array[ fd ].fd > -1)) {

        assert (tp->fd_array[ fd ].fd == fd);
        events_new = tp->fd_array[ fd ].events & ~events;
        if (tp->fd_array[ fd ].events != events_new) {

            tp->fd_array[ fd ].events = events_new;

            if (events_new == 0) {
                tp->fd_array[ fd ].revents = 0;
                tp->fd_array[ fd ].fd = -1;
                tp->num_fds_used--;

                if (tp->max_fd == fd) {
                    for (i = fd - 1; i >= 0; i--) {
                        if (tp->fd_array[ i ].fd > -1) {
                            break;
                        }
                    }
                    tp->max_fd = i;
                }
            }
            _tpoll_signal_send (tp);
        }
    }
    DPRINTF((21, "tpoll_clear fd=%d e=0x%02x r=0x%02x.\n",
        fd, events, events_new));
    if ((e = pthread_mutex_unlock (&tp->mutex)) != 0) {
        log_err (errno = e, "Unable to unlock tpoll mutex");
    }
    return (0);
}


int
tpoll_is_set (tpoll_t tp, int fd, short int events)
{
/*  Tests whether any of the bitwise-OR'd [events] have occurred for file
 *    descriptor [fd] within the tpoll object [tp].
 *  Returns >0  if any of the specified [events] have occurred,
 *    0 if none of the specified [events] have occurred, or -1 on error.
 */
    int rc;
    int e;

    if (!tp) {
        errno = EINVAL;
        return (-1);
    }
    if (fd < 0) {
        errno = EINVAL;
        return (-1);
    }
    if ((e = pthread_mutex_lock (&tp->mutex)) != 0) {
        log_err (errno = e, "Unable to lock tpoll mutex");
    }
    if (fd > tp->max_fd) {
        rc = 0;
    }
    else if (tp->fd_array[ fd ].fd < 0) {
        rc = 0;
    }
    else {
        assert (tp->fd_array[ fd ].fd == fd);
        rc = tp->fd_array[ fd ].revents & events;
    }
    DPRINTF((21, "tpoll_is_set fd=%d e=0x%02x r=0x%02x rc=%d.\n",
        fd, events, tp->fd_array[ fd ].revents, rc));
    if ((e = pthread_mutex_unlock (&tp->mutex)) != 0) {
        log_err (errno = e, "Unable to unlock tpoll mutex");
    }
    return (rc);
}


int
tpoll_set (tpoll_t tp, int fd, short int events)
{
/*  Adds the bitwise-OR'd [events] to any existing events for file descriptor
 *    [fd] within the tpoll object [tp].
 *  The internal fd table will grow as needed.
 *  Returns 0 on success, or -1 on error.
 */
    int       rc;
    short int events_new = 0;
    int       e;

    if (!tp) {
        errno = EINVAL;
        return (-1);
    }
    if (fd < 0) {
        errno = EINVAL;
        return (-1);
    }
    if (events == 0) {
        return (0);
    }
    if ((e = pthread_mutex_lock (&tp->mutex)) != 0) {
        log_err (errno = e, "Unable to lock tpoll mutex");
    }
    if ((fd >= tp->num_fds_alloc) && (_tpoll_grow (tp, fd + 1) < 0)) {
        rc = -1;
    }
    else {
        if (tp->fd_array[ fd ].fd < 0) {
            assert (tp->fd_array[ fd ].events == 0);
            assert (tp->fd_array[ fd ].revents == 0);
            tp->fd_array[ fd ].fd = fd;
            tp->num_fds_used++;
            if (fd > tp->max_fd) {
                tp->max_fd = fd;
            }
            events_new = events;
        }
        else {
            events_new = tp->fd_array[ fd ].events | events;
        }
        if (tp->fd_array[ fd ].events != events_new) {
            tp->fd_array[ fd ].events = events_new;
            _tpoll_signal_send (tp);
        }
        rc = 0;
    }
    DPRINTF((21, "tpoll_set fd=%d e=0x%02x r=0x%02x.\n",
        fd, events, events_new));
    if ((e = pthread_mutex_unlock (&tp->mutex)) != 0) {
        log_err (errno = e, "Unable to unlock tpoll mutex");
    }
    return (rc);
}


int
tpoll_timeout_absolute (tpoll_t tp, callback_f cb, void *arg,
    const struct timeval *tvp)
{
/*  Sets an "absolute" timer event for the tpoll object [tp] specifying when
 *    the timer should expire.  At expiration time [tvp], the callback
 *    function [cb] will be invoked with the argument [arg].
 *  Returns a timer ID > 0 for use with tpoll_timeout_cancel(), or -1 on error.
 */
    _tpoll_timer_t  t;
    _tpoll_timer_t *t_ptr;
    int             rc;
    int             e;

    if (!tp) {
        errno = EINVAL;
        return (-1);
    }
    if (!cb) {
        errno = EINVAL;
        return (-1);
    }
    if (!tvp) {
        errno = EINVAL;
        return (-1);
    }
    if (!(t = malloc (sizeof (struct tpoll_timer)))) {
        return (-1);
    }
    t->fnc = cb;
    t->arg = arg;
    t->tv = *tvp;

    if ((e = pthread_mutex_lock (&tp->mutex)) != 0) {
        log_err (errno = e, "Unable to lock tpoll mutex");
    }
    rc = t->id = tp->timers_next_id++;
    if (tp->timers_next_id <= 0) {
        tp->timers_next_id = 1;
    }
    t_ptr = &tp->timers_active;
    while (*t_ptr && !timercmp (tvp, &(*t_ptr)->tv, <)) {
        t_ptr = &((*t_ptr)->next);
    }
    if (*t_ptr == tp->timers_active) {
        _tpoll_signal_send (tp);
    }
    t->next = *t_ptr;
    *t_ptr = t;

    DPRINTF((22, "tpoll timer set id=%d.\n", t->id));
    if ((e = pthread_mutex_unlock (&tp->mutex)) != 0) {
        log_err (errno = e, "Unable to unlock tpoll mutex");
    }
    return (rc);
}


int
tpoll_timeout_relative (tpoll_t tp, callback_f cb, void *arg, int ms)
{
/*  Sets a "relative" timer event for the tpoll object [tp] specifying the
 *    duration (in milliseconds [ms]) before it expires.  At expiration, the
 *    callback function [cb] will be invoked with the argument [arg].
 *  Returns a timer ID > 0 for use with tpoll_timeout_cancel(), or -1 on error.
 */
    struct timeval tv;

    _tpoll_get_timeval (&tv, ms);
    return (tpoll_timeout_absolute (tp, cb, arg, &tv));
}


int
tpoll_timeout_cancel (tpoll_t tp, int id)
{
/*  Cancels the timer event [id] from the tpoll object [tp].
 *  Returns 1 if the timer was canceled, 0 if the timer was not found,
 *    or -1 on error.
 */
    _tpoll_timer_t  t;
    _tpoll_timer_t *t_ptr;
    int             rc;
    int             e;

    if (!tp) {
        errno = EINVAL;
        return (-1);
    }
    if (id <= 0) {
        errno = EINVAL;
        return (-1);
    }
    if ((e = pthread_mutex_lock (&tp->mutex)) != 0) {
        log_err (errno = e, "Unable to lock tpoll mutex");
    }
    t_ptr = &tp->timers_active;
    while (*t_ptr && (id != (*t_ptr)->id)) {
        t_ptr = &((*t_ptr)->next);
    }
    if (!*t_ptr) {
        rc = 0;
    }
    else {
        DPRINTF((22, "tpoll timer cancel id=%d.\n", (*t_ptr)->id));
        if (*t_ptr == tp->timers_active) {
            _tpoll_signal_send (tp);
        }
        t = *t_ptr;
        *t_ptr = t->next;
        free (t);
        rc = 1;
    }
    if ((e = pthread_mutex_unlock (&tp->mutex)) != 0) {
        log_err (errno = e, "Unable to unlock tpoll mutex");
    }
    return (rc);
}


int
tpoll (tpoll_t tp, int ms)
{
/*  Similar to poll(), but file descriptors and timers are specified by
 *    auxiliary functions.
 *  Examines the tpoll object [tp] to see if any file descriptors are ready
 *    for I/O, and dispatches any timer events that have expired.
 *  Blocks until I/O is ready on one or more file descriptors, or [ms]
 *    milliseconds have passed.  Blocks indefinitely if I/O events are
 *    specified and [ms] is -1.  While blocked, timers are still dispatched
 *    once they expire.
 *  Returns immediately if the [ms] timeout is 0, or if no I/O events are
 *    specified and no timers remain and [ms] is -1.
 *  Returns the number of file descriptors with I/O ready, 0 on timeout,
 *    or -1 or error.
 */
    struct timeval  tv_timeout;
    struct timeval  tv_now;
    _tpoll_timer_t  t;
    int             timeout;
    int             ms_diff;
    int             n;
    int             e;

    if (!tp) {
        errno = EINVAL;
        return (-1);
    }
    if (ms > 0) {
        _tpoll_get_timeval (&tv_timeout, ms);
    }
    if ((e = pthread_mutex_lock (&tp->mutex)) != 0) {
        log_err (errno = e, "Unable to lock tpoll mutex");
    }
    DPRINTF((23, "tpoll enter ms=%d nfd=%d mfd=%d.\n",
        ms, tp->num_fds_used, tp->max_fd));
    _tpoll_get_timeval (&tv_now, 0);

    for (;;) {
        /*
         *  Dispatch timer events that have expired.
         */
        while (tp->timers_active
                && !timercmp (&tp->timers_active->tv, &tv_now, >)) {

            t = tp->timers_active;
            tp->timers_active = t->next;
            DPRINTF((22, "tpoll timer dispatch id=%d.\n", t->id));
            /*
             *  Release the mutex while performing the callback function
             *    in case the callback wants to set/cancel another timer.
             */
            if ((e = pthread_mutex_unlock (&tp->mutex)) != 0) {
                log_err (errno = e, "Unable to unlock tpoll mutex");
            }
            t->fnc (t->arg);
            free (t);

            if ((e = pthread_mutex_lock (&tp->mutex)) != 0) {
                log_err (errno = e, "Unable to lock tpoll mutex");
            }
        }
        /*  Compute timeout for poll().
         */
        if (ms == 0) {
            timeout = 0;
        }
        else if ((ms < 0) && !tp->timers_active) {
            if (tp->num_fds_used > 0) {
                timeout = -1;           /* fd events but no more timers */
            }
            else {
                timeout = 0;            /* no fd events and no more timers */
            }
        }
        else {
            _tpoll_get_timeval (&tv_now, 0);

            if (ms < 0) {
                assert (tp->timers_active != NULL);
                ms_diff =
                    _tpoll_diff_timeval (&tp->timers_active->tv, &tv_now);
            }
            else if (!tp->timers_active) {
                assert (ms > 0);
                ms_diff =
                    _tpoll_diff_timeval (&tv_timeout, &tv_now);
            }
            else if (!timercmp (&tp->timers_active->tv, &tv_timeout, >)) {
                assert (ms > 0);
                ms_diff =
                    _tpoll_diff_timeval (&tp->timers_active->tv, &tv_now);
            }
            else {
                assert (ms > 0);
                ms_diff =
                    _tpoll_diff_timeval (&tv_timeout, &tv_now);
            }
            timeout = (ms_diff > 0) ? ms_diff : 0;
        }
        /*  Poll for events, discarding any on the "signaling pipe".
         */
        tp->is_blocked = true;

        if ((e = pthread_mutex_unlock (&tp->mutex)) != 0) {
            log_err (errno = e, "Unable to unlock tpoll mutex");
        }
        DPRINTF((25, "tpoll poll enter ms=%d mfd=%d.\n", timeout, tp->max_fd));
        n = poll (tp->fd_array, tp->max_fd + 1, timeout);
        DPRINTF((25, "tpoll poll return n=%d.\n", n));

        if ((e = pthread_mutex_lock (&tp->mutex)) != 0) {
            log_err (errno = e, "Unable to lock tpoll mutex");
        }
        tp->is_blocked = false;

        if (n < 0) {
            break;
        }
        if (tp->is_realloced) {
            DPRINTF((25, "tpoll is_realloced.\n"));
            tp->is_realloced = false;
            _tpoll_signal_recv (tp);
            continue;
        }
        if (tp->fd_array[ tp->fd_pipe[ 0 ] ].revents & POLLIN) {
            _tpoll_signal_recv (tp);
            n--;
        }
        if (n > 0) {
            assert (tp->num_fds_used > 0);
            break;
        }
        if ((ms == 0)
                || ((ms < 0) && !tp->num_fds_used && !tp->timers_active)) {
            break;
        }
        _tpoll_get_timeval (&tv_now, 0);
        if ((ms > 0) && !timercmp (&tv_timeout, &tv_now, >)) {
            break;
        }
    }
    DPRINTF((23, "tpoll return n=%d.\n", n));
    if ((e = pthread_mutex_unlock (&tp->mutex)) != 0) {
        log_err (errno = e, "Unable to unlock tpoll mutex");
    }
    return (n);
}


/*****************************************************************************
 *  Internal Functions
 *****************************************************************************/

static void
_tpoll_init (tpoll_t tp, tpoll_zero_t how)
{
/*  Initializes the tpoll object [tp] to the empty set.
 *  This routine assumes the [tp] mutex is already locked.
 */
    int            i;
    _tpoll_timer_t t;

    assert (tp != NULL);
    assert (tp->fd_pipe[ 0 ] > -1);
    assert (tp->num_fds_alloc > 0);
    assert ((how & ~TPOLL_ZERO_ALL) == 0);

    if (how & TPOLL_ZERO_FDS) {
        memset (tp->fd_array, 0, tp->num_fds_alloc * sizeof (struct pollfd));
        for (i = 0; i < tp->num_fds_alloc; i++) {
            tp->fd_array[ i ].fd = -1;
        }
        tp->fd_array[ tp->fd_pipe[ 0 ] ].fd = tp->fd_pipe[ 0 ];
        tp->fd_array[ tp->fd_pipe[ 0 ] ].events = POLLIN;
        tp->max_fd = tp->fd_pipe[ 0 ];
        tp->num_fds_used = 0;
    }
    if (how & TPOLL_ZERO_TIMERS) {
        while (tp->timers_active) {
            t = tp->timers_active;
            tp->timers_active = t->next;
            free (t);
        }
        tp->timers_next_id = 1;
    }
    return;
}


static void
_tpoll_signal_send (tpoll_t tp)
{
/*  Signals the tpoll object [tp] that an fd or timer or somesuch has changed
 *    and poll() needs to unblock and re-examine its state.
 *  This routine assumes the [tp] mutex is already locked.
 */
    int           n;
    unsigned char c = 0;

    assert (tp != NULL);
    assert (tp->fd_pipe[ 1 ] > -1);

    if (tp->is_signaled || !tp->is_blocked) {
        return;
    }
    for (;;) {
        n = write (tp->fd_pipe[ 1 ], &c, 1);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                break;
            }
            log_err (errno, "Unable to write signal to tpoll");
        }
        else if (n == 0) {
            log_err (0, "Got an unexpected 0 writing to tpoll's pipe");
        }
        break;
    }
    tp->is_signaled = true;
    DPRINTF((24, "tpoll signal sent.\n"));
    return;
}


static void
_tpoll_signal_recv (tpoll_t tp)
{
/*  Drains all signals sent to the tpoll object [tp].
 *  This routine assumes the [tp] mutex is already locked.
 */
    int           n;
    unsigned char c[ 2 ];

    assert (tp != NULL);
    assert (tp->fd_pipe[ 0 ] > -1);
    assert (tp->fd_array[ tp->fd_pipe[ 0 ] ].fd == tp->fd_pipe[ 0 ]);

    if (!tp->is_signaled) {
        return;
    }
    for (;;) {
        n = read (tp->fd_pipe[ 0 ], &c, sizeof (c));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                break;
            }
            log_err (errno, "Unable to read signal from tpoll");
        }
        else if (n == 0) {
            log_err (0, "Got an unexpected EOF reading from tpoll's pipe");
        }
        else if (n == sizeof (c)) {
            assert (0);                 /* is_signaled should prevent this */
            continue;
        }
        break;
    }
    tp->is_signaled = false;
    DPRINTF((24, "tpoll signal received.\n"));
    return;
}


static int
_tpoll_grow (tpoll_t tp, int num_fds_req)
{
/*  Attempts to grow [tp]'s pollfd array to at least [num_fds_req] structs.
 *  Returns 0 if the request is successful, -1 if not.
 *  This routine assumes the [tp] mutex is already locked.
 */
    struct pollfd *fd_array_tmp;
    struct pollfd *fd_array_new;
    int            num_fds_tmp;
    int            num_fds_new;
    int            i;

    assert (tp != NULL);
    assert (num_fds_req > 0);

    if (num_fds_req <= tp->num_fds_alloc) {
        return (0);
    }
    num_fds_tmp = tp->num_fds_alloc;
    while ((num_fds_tmp < num_fds_req) && (num_fds_tmp > 0)) {
        num_fds_tmp *= 2;
    }
    if (num_fds_tmp < num_fds_req) {
        num_fds_tmp = num_fds_req;
    }
    /*  Force tpoll()'s poll() to unblock before we realloc the fd_array.
     *  Then tpoll() will have to re-acquire the mutex before continuing.
     *  Since we currently have the mutex, we can now safely realloc fd_array.
     */
    _tpoll_signal_send (tp);
    if (!(fd_array_tmp =
            realloc (tp->fd_array, num_fds_tmp * sizeof (struct pollfd)))) {
        return (-1);
    }
    fd_array_new = fd_array_tmp + tp->num_fds_alloc;
    num_fds_new = num_fds_tmp - tp->num_fds_alloc;
    memset (fd_array_new, 0, num_fds_new * sizeof (struct pollfd));
    for (i = tp->num_fds_alloc; i < num_fds_tmp; i++) {
        fd_array_tmp[ i ].fd = -1;
    }
    tp->is_realloced = true;
    tp->fd_array = fd_array_tmp;
    tp->num_fds_alloc = num_fds_tmp;
    return (0);
}


static void
_tpoll_get_timeval (struct timeval *tvp, int ms)
{
/*  Sets [tvp] to the current time.
 *  If [ms] > 0, adds the number of milliseconds [ms] to [tvp].
 */
    assert (tvp != NULL);

    if (gettimeofday (tvp, NULL) < 0) {
        log_err (0, "Unable to get time of day");
    }
    if (ms > 0) {
        tvp->tv_sec += ms / 1000;
        tvp->tv_usec += (ms % 1000) * 1000;
        if (tvp->tv_usec >= 1000000) {
            tvp->tv_sec += tvp->tv_usec / 1000000;
            tvp->tv_usec %= 1000000;
        }
    }
    return;
}


static int
_tpoll_diff_timeval (struct timeval *tvp1, struct timeval *tvp0)
{
/*  Returns the millisecond difference between [tvp1] and [tvp0].
 *  If either is NULL, the current time will be used in its place.
 */
    struct timeval tv;
    int            ms;

    if (!tvp0 || !tvp1) {
        if (gettimeofday (&tv, NULL) < 0) {
            log_err (0, "Unable to get time of day");
        }
        if (!tvp0) {
            tvp0 = &tv;
        }
        if (!tvp1) {
            tvp1 = &tv;
        }
    }
    ms = ( (tvp1->tv_sec  - tvp0->tv_sec)  * 1000 ) +
         ( (tvp1->tv_usec - tvp0->tv_usec) / 1000 ) ;
    /*
     *  Round to the next millisecond.
     */
    if ((tvp1->tv_sec >= tvp0->tv_sec)
            && (tvp1->tv_usec > tvp0->tv_usec)) {
        ms++;
    }
    else if ((tvp1->tv_sec <= tvp0->tv_sec)
            && (tvp1->tv_usec < tvp0->tv_usec)) {
        ms--;
    }
    return (ms);
}
