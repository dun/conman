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


/*  FIXME:
 *  This code does not fully handle the following cases:
 *  - when the watched directory does not yet exist
 *  - when the watched directory is later deleted
 *  In the event of the watched directory not yet existing, this code should
 *    watch the parent directory for its subsequent creation; if the parent
 *    does not yet exist, it should watch the parent's parent, etc., up to the
 *    root.  This might be best accomplished by initially registering all of
 *    parent directories up to the root for the directory being watched.
 *  In the event of the watched directory being deleted (event IN_IGNORED),
 *    once the existing watch has been removed, this should degenerate into the
 *    case above where the watched directory does not yet exist.
 *
 *  Currently, inotify works as expected as long as the parent directory being
 *    watched persists for the lifetime of the daemon.  But once that
 *    directory's inode is removed, the daemon falls back to using timers to
 *    periodically resurrect downed objects.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */


/*****************************************************************************
 *  Stubbed Routines for building without <sys/inotify.h>.
 *
 *  These routines preserve type-checking while allowing any decent compiler
 *    to optimize the case of simply returning a constant integer such that
 *    no function call overhead is incurred.
 *****************************************************************************/

#if ! HAVE_SYS_INOTIFY_H

#include "inevent.h"


int
inevent_add (const char *filename, inevent_cb_f cb_fnc, void *cb_arg)
{
    return (-1);
}


int
inevent_remove (const char *filename)
{
    return (-1);
}


int
inevent_get_fd (void)
{
    return (-1);
}


int inevent_process (void)
{
    return (-1);
}


/*****************************************************************************
 *  Routines for building with <sys/inotify.h> (Linux 2.6.13+).
 *****************************************************************************/

#else  /* HAVE_SYS_INOTIFY_H */

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>
#include "inevent.h"
#include "list.h"
#include "log.h"
#include "util-file.h"


/*****************************************************************************
 *  Constants
 *****************************************************************************/

/*  Number of bytes for the average inotify event (w/ 16 bytes for name).
 */
#define INEVENT_SIZE            ((sizeof (struct inotify_event)) + 16)

/*  Number of average inotify events to process per read invocation.
 */
#define INEVENT_NUM             128

/*  Number of bytes to allocate for the inotify event buffer.
 */
#define INEVENT_BUF_LEN         ((INEVENT_SIZE) * (INEVENT_NUM))


/*****************************************************************************
 *  Internal Data Types
 *****************************************************************************/

struct inevent {
    char         *pathname;             /* pathname being watched            */
    char         *dirname;              /* directory component of pathname   */
    char         *filename;             /* filename component of pathname    */
    inevent_cb_f  cb_fnc;               /* callback function                 */
    void         *cb_arg;               /* callback function arg             */
    int           wd;                   /* inotify watch descriptor          */
};

typedef struct inevent inevent_t;


/*****************************************************************************
 *  Private Function Prototypes
 *****************************************************************************/

static int _inevent_init (void);

static void _inevent_fini (void);

static inevent_t * _inevent_create (const char *pathname,
    inevent_cb_f cb_fnc, void *cb_arg);

static void _inevent_destroy (inevent_t *inevent_ptr);

static int _list_find_by_path (const inevent_t *inevent_ptr,
    const char *pathname);

static int _list_find_by_wd (const inevent_t *inevent_ptr,
    const int *wd_ptr);

static int _list_find_by_event (const inevent_t *inevent_ptr,
    const struct inotify_event *event_ptr);


/*****************************************************************************
 *  Internal Data Variables
 *****************************************************************************/

static int  inevent_fd = -1;            /* inotify file descriptor           */
static List inevent_list = NULL;        /* list of inevent structs           */


/*****************************************************************************
 *  Public Functions
 *****************************************************************************/

int
inevent_add (const char *pathname, inevent_cb_f cb_fnc, void *cb_arg)
{
/*  Adds an inotify event for [pathname], causing [cb_fnc] to be invoked with
 *    [cb_arg] whenever the specified file is created.
 *  Returns 0 on success, or -1 on error.
 */
    inevent_t *inevent_ptr;

    if (pathname == NULL) {
        log_msg (LOG_ERR, "inotify event pathname not specified");
        return (-1);
    }
    if (cb_fnc == NULL) {
        log_msg (LOG_ERR, "inotify event callback not specified");
        return (-1);
    }
    if (pathname[0] != '/') {
        log_msg (LOG_ERR, "inotify event path \"%s\" is not absolute",
            pathname);
        return (-1);
    }
    if (inevent_fd == -1) {
        if (_inevent_init () < 0) {
            log_msg (LOG_ERR, "unable to initialize inotify: %s",
                strerror (errno));
            return (-1);
        }
    }
    if (list_find_first (inevent_list, (ListFindF) _list_find_by_path,
            (void *) pathname)) {
        log_msg (LOG_ERR, "inotify event path \"%s\" already specified",
            pathname);
        return (-1);
    }
    inevent_ptr = _inevent_create (pathname, cb_fnc, cb_arg);
    if (inevent_ptr == NULL) {
        return (-1);
    }
    list_append (inevent_list, inevent_ptr);
    return (0);
}


int
inevent_remove (const char *pathname)
{
/*  Removes the inotify event (if present) for [pathname].
 *  Returns 0 on success, or -1 on error.
 */
    ListIterator  li = NULL;
    inevent_t    *inevent_ptr;
    int           wd_cnt;

    if (pathname == NULL) {
        return (0);
    }
    if (inevent_list == NULL) {
        return (0);
    }
    li = list_iterator_create (inevent_list);
    inevent_ptr = list_find (li, (ListFindF) _list_find_by_path,
            (void *) pathname);
    if (inevent_ptr == NULL) {
        log_msg (LOG_ERR, "inotify event path \"%s\" not registered",
                pathname);
        list_iterator_destroy (li);
        return (0);
    }
    (void) list_remove (li);

    list_iterator_reset (li);
    wd_cnt = 0;
    while (list_find (li, (ListFindF) _list_find_by_wd, &(inevent_ptr->wd))) {
        wd_cnt++;
    }
    list_iterator_destroy (li);

    /*  If no other inevents were found with a matching wd, then this inevent
     *    is the only one associated with this watch descriptor.  As such, the
     *    watch associated with this watch descriptor can be removed since no
     *    other objects are relying on it.
     *  Note that multiple files may share the same watch descriptor since it
     *    is the file's directory that is watched.
     */
    if ((inevent_ptr->wd >= 0) && (wd_cnt == 0)) {
        (void) inotify_rm_watch (inevent_fd, inevent_ptr->wd);
        DPRINTF((10, "Removed inotify watch wd=%d for directory \"%s\".\n",
                inevent_ptr->wd, inevent_ptr->dirname));
    }
    _inevent_destroy (inevent_ptr);

    if (list_is_empty (inevent_list)) {
        _inevent_fini ();
    }
    return (0);
}


int
inevent_get_fd (void)
{
/*  Returns the file descriptor associated with the inotify event queue,
 *    or -1 on error.
 */
    return (inevent_fd);
}


int
inevent_process (void)
{
/*  Processes the callback functions for all available events in the inotify
 *    event queue.
 *  Returns the number of events processed on success, or -1 on error.
 */
    char buf [INEVENT_BUF_LEN];
    int  len;
    int  n = 0;

    if (inevent_fd == -1) {
        return (-1);
    }

retry_read:
    len = read (inevent_fd, buf, sizeof (buf));
    if (len < 0) {
        if (errno == EINTR) {
            goto retry_read;
        }
        log_msg (LOG_ERR, "unable to read inotify fd: %s", strerror (errno));
        return (-1);
    }
    else if (len == 0) {
        log_msg (LOG_ERR, "inotify read buffer is too small");
        return (-1);
    }
    else {
        unsigned int i = 0;
        uint32_t     event_mask = IN_CREATE | IN_MOVED_TO;

        while (i < (unsigned int) len) {

            struct inotify_event *event_ptr;
            inevent_t            *inevent_ptr;

            event_ptr = (struct inotify_event *) &buf[i];

            DPRINTF((15,
                "Received inotify event wd=%d mask=0x%x len=%u name=\"%s\".\n",
                event_ptr->wd, event_ptr->mask, event_ptr->len,
                (event_ptr->len > 0 ? event_ptr->name : "")));

            if (event_ptr->mask & IN_IGNORED) {

                (void) list_delete_all (inevent_list,
                    (ListFindF) _list_find_by_wd, &(event_ptr->wd));
            }
            else if ((event_ptr->mask & event_mask) && (event_ptr->len > 0)) {

                inevent_ptr = list_find_first (inevent_list,
                    (ListFindF) _list_find_by_event, event_ptr);

                if ((inevent_ptr != NULL) && (inevent_ptr->cb_fnc != NULL)) {
                    inevent_ptr->cb_fnc (inevent_ptr->cb_arg);
                }
            }
            i += sizeof (struct inotify_event) + event_ptr->len;
            n++;
        }
    }
    return (n);
}


/*****************************************************************************
 *  Private Functions
 *****************************************************************************/

static int
_inevent_init (void)
{
/*  Initializes the inotify event subsystem.
 *  Returns 0 on success, or -1 on error (with errno set).
 */
    assert (inevent_fd == -1);
    assert (inevent_list == NULL);

    if (inevent_fd == -1) {
        inevent_fd = inotify_init ();
        if (inevent_fd == -1) {
            goto err;
        }
        set_fd_closed_on_exec (inevent_fd);
        set_fd_nonblocking (inevent_fd);
    }
    if (inevent_list == NULL) {
        inevent_list = list_create ((ListDelF) _inevent_destroy);
        if (inevent_list == NULL) {
            goto err;
        }
    }
    DPRINTF((5, "Initialized inotify event subsystem.\n"));
    return (inevent_fd);

err:
    _inevent_fini ();
    return (-1);
}


static void
_inevent_fini (void)
{
/*  Shuts down the inotify event subsystem.
 */
    assert (inevent_fd >= 0);
    assert (inevent_list != NULL);

    if (inevent_fd >= 0) {
        (void) close (inevent_fd);
        inevent_fd = -1;
    }
    if (inevent_list != NULL) {
        list_destroy (inevent_list);
        inevent_list = NULL;
    }
    DPRINTF((5, "Shut down inotify event subsystem.\n"));
    return;
}


static inevent_t *
_inevent_create (const char *pathname, inevent_cb_f cb_fnc, void *cb_arg)
{
/*  Creates an inotify event object for [cb_fnc] to be invoked with [cb_arg]
 *    whenever the file specified by [pathname] is created.
 *  Returns a pointer to the new object on success, or NULL on error
 *    (with errno set).
 */
    inevent_t *inevent_ptr = NULL;
    char      *p;
    uint32_t   event_mask = IN_CREATE | IN_MOVED_TO;

    assert (pathname != NULL);
    assert (pathname[0] == '/');
    assert (cb_fnc != NULL);

    inevent_ptr = malloc (sizeof (*inevent_ptr));
    if (inevent_ptr == NULL) {
        goto err;
    }
    memset (inevent_ptr, 0, sizeof (*inevent_ptr));
    inevent_ptr->wd = -1;

    inevent_ptr->pathname = strdup (pathname);
    if (inevent_ptr->pathname == NULL) {
        goto err;
    }
    inevent_ptr->dirname = strdup (pathname);
    if (inevent_ptr->dirname == NULL) {
        goto err;
    }
    p = strrchr (inevent_ptr->dirname, '/');
    inevent_ptr->filename = strdup (p + 1);
    if (inevent_ptr->filename == NULL) {
        goto err;
    }
    if (p == inevent_ptr->dirname) {    /* dirname is root directory ("/") */
        *++p = '\0';
    }
    else {
        *p = '\0';
    }
    inevent_ptr->cb_fnc = cb_fnc;
    inevent_ptr->cb_arg = cb_arg;

    inevent_ptr->wd = inotify_add_watch (inevent_fd, inevent_ptr->dirname,
            event_mask);
    if (inevent_ptr->wd == -1) {
        goto err;
    }

    DPRINTF((10, "Added inotify watch wd=%d for \"%s\".\n",
            inevent_ptr->wd, inevent_ptr->pathname));
    return (inevent_ptr);

err:
    _inevent_destroy (inevent_ptr);
    return (NULL);
}


static void
_inevent_destroy (inevent_t *inevent_ptr)
{
/*  Destroys the inotify event object referenced by [inevent_ptr].
 */
    assert (inevent_ptr != NULL);

    if (inevent_ptr == NULL) {
        return;
    }
    DPRINTF((10, "Removed inotify watch wd=%d for \"%s\".\n",
            inevent_ptr->wd, inevent_ptr->pathname));

    if (inevent_ptr->pathname != NULL) {
        free (inevent_ptr->pathname);
    }
    if (inevent_ptr->dirname != NULL) {
        free (inevent_ptr->dirname);
    }
    if (inevent_ptr->filename != NULL) {
        free (inevent_ptr->filename);
    }
    free (inevent_ptr);
    return;
}


static int
_list_find_by_path (const inevent_t *inevent_ptr, const char *pathname)
{
/*  List function helper to match items in a list of inevent_t pointers using
 *    the pathname [pathname] as the key.
 *  Returns non-zero if the key is found; o/w, returns zero.
 */
    assert (inevent_ptr != NULL);
    assert (pathname != NULL);

    return (strcmp (inevent_ptr->pathname, pathname) == 0);
}


static int
_list_find_by_wd (const inevent_t *inevent_ptr, const int *wd_ptr)
{
/*  List function helper to match items in a list of inevent_t pointers using
 *    a pointer to an inotify watch descriptor [wd_ptr] as the key.
 *  Returns non-zero if the key is found; o/w, returns zero.
 */
    assert (inevent_ptr != NULL);
    assert (wd_ptr != NULL);

    return (inevent_ptr->wd == *wd_ptr);
}


static int
_list_find_by_event (const inevent_t *inevent_ptr,
                     const struct inotify_event *event_ptr)
{
/*  List function helper to match items in a list of inevent_t pointers using
 *    a pointer to an inotify_event struct [event_ptr] as the key.
 *  Returns non-zero if the key is found; o/w, returns zero.
 */
    assert (inevent_ptr != NULL);
    assert (inevent_ptr->filename != NULL);
    assert (event_ptr != NULL);
    assert (event_ptr->len > 0);
    assert (event_ptr->name != NULL);

    return ((inevent_ptr->wd == event_ptr->wd) &&
            (strcmp (inevent_ptr->filename, event_ptr->name) == 0));
}


#endif /* HAVE_SYS_INOTIFY_H */
