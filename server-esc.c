/******************************************************************************\
 *  $Id: server-esc.c,v 1.7 2001/08/14 23:18:36 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
\******************************************************************************/


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <assert.h>
#include <errno.h>
#include <string.h>
#include "common.h"
#include "errors.h"
#include "list.h"
#include "server.h"
#include "util.h"


static void perform_serial_break(obj_t *client);
static void perform_log_replay(obj_t *client);
static void perform_quiet_toggle(obj_t *client);
static void perform_suspend(obj_t *client);


int process_escape_chars(obj_t *client, void *src, int len)
{
/*  Processes the buffer (src) of length (len) received from the client
 *    for escape character sequences.
 *  Escape characters are removed (unstuffed) from the buffer
 *    and immediately processed.
 *  Returns the new length of the modified buffer.
 */
    const unsigned char *last = (unsigned char *) src + len;
    unsigned char *p, *q;

    assert(client->type == CLIENT);
    assert(client->fd >= 0);

    if (!src || len <= 0)
        return(0);

    for (p=q=src; p<last; p++) {

        if (client->aux.client.gotEscape) {
            client->aux.client.gotEscape = 0;
            switch (*p) {
            case ESC_CHAR:
                *q++ = *p;
                break;
            case ESC_CHAR_BREAK:
                perform_serial_break(client);
                break;
            case ESC_CHAR_LOG:
                perform_log_replay(client);
                break;
            case ESC_CHAR_QUIET:
                perform_quiet_toggle(client);
                break;
            case ESC_CHAR_SUSPEND:
                perform_suspend(client);
                break;
            default:
                log_msg(10, "Received invalid escape '%c' from %s.",
                    *p, client->name);
                break;
            }
        }
        else if (*p == ESC_CHAR) {
            client->aux.client.gotEscape = 1;
        }
        else {
            *q++ = *p;
        }
    }
    assert((q >= (unsigned char *) src) && (q <= p));
    len = q - (unsigned char *) src;
    return(len);
}


static void perform_serial_break(obj_t *client)
{
/*  Transmits a serial-break to each of the consoles written to by the client.
 */
    ListIterator i;
    obj_t *console;

    assert(client->type == CLIENT);
    if (!(i = list_iterator_create(client->readers)))
        err_msg(0, "Out of memory");
    while ((console = list_next(i))) {
        assert(console->type == CONSOLE);
        DPRINTF("Performing serial-break on console [%s].\n", console->name);
        if (tcsendbreak(console->fd, 0) < 0)
            err_msg(errno, "tcsendbreak() failed for console [%s]",
                console->name);
    }
    list_iterator_destroy(i);
    return;
}


static void perform_log_replay(obj_t *client)
{
/*  Kinda like TiVo's Instant Replay.  :)
 *  Replays the last bytes from the console logfile (if present) associated
 *    with this client (in either a R/O or R/W session, but not a B/C session).
 *
 *  The maximum amount of data that can be written into an object's
 *    circular-buffer via write_obj_data() is (MAX_BUF_SIZE - 1) bytes.
 *    But writing this much data may likely overwrite data in the buffer that
 *    has not been flushed to the object's file descriptor via write_to_obj().
 *    Therefore, it is recommended that (CONMAN_REPLAY_LEN <= MAX_BUF_SIZE / 2).
 */
    obj_t *console;
    obj_t *logfile;
    unsigned char buf[MAX_BUF_SIZE - 1];
    unsigned char *ptr = buf;
    int len = sizeof(buf);
    unsigned char *p;
    int n, m;
    int rc;

    assert(client->type == CLIENT);

    /*  Broadcast sessions are "write-only", so the log-replay is a no-op.
     */
    if (list_is_empty(client->writers))
        return;

    /*  The client will have exactly one writer in either a R/O or R/W session.
     */
    assert(list_count(client->writers) == 1);
    console = list_peek(client->writers);
    assert(console->type == CONSOLE);
    logfile = console->aux.console.logfile;

    if (!logfile) {
        n = snprintf(ptr, len, "%sConsole [%s] is not being logged%s",
            CONMAN_MSG_PREFIX, console->name, CONMAN_MSG_SUFFIX);
        if ((n < 0) || (n >= len)) {
            log_msg(10, "Insufficient buffer to replay console %s log for %s.",
                console->name, client->name);
            return;
        }
    }
    else {
        assert(logfile->type == LOGFILE);
        n = snprintf(ptr, len, "%sBegin log replay of console [%s]%s",
            CONMAN_MSG_PREFIX, console->name, CONMAN_MSG_SUFFIX);
        if ((n < 0) || (n >= len) || (sizeof(buf) <= 2*n - 2)) {
            log_msg(10, "Insufficient buffer to replay console %s log for %s.",
                console->name, client->name);
            return;
        }
        ptr += n;
        /*
         *  Since we now know the length of the "begin" message, reserve
         *    space in 'buf' for the "end" message by doubling its length.
         */
        len -= 2*n - 2;

        if ((rc = pthread_mutex_lock(&logfile->bufLock)) != 0)
            err_msg(rc, "pthread_mutex_lock() failed for [%s]",
                logfile->name);

        /*  Compute the number of bytes to replay.
         *  If the console's circular-buffer has not yet wrapped around,
         *    don't wrap back into uncharted buffer territory.
         */
        if (!logfile->gotBufWrap)
            n = MIN(CONMAN_REPLAY_LEN, logfile->bufInPtr - logfile->buf);
        else
            n = MIN(CONMAN_REPLAY_LEN, MAX_BUF_SIZE - 1);
        n = MIN(n, len);

        p = logfile->bufInPtr - n;
        if (p >= logfile->buf) {	/* no wrap needed */
            memcpy(ptr, p, n);
            ptr += n;
        }
        else {				/* wrap backwards */
            m = logfile->buf - p;
            p = &logfile->buf[MAX_BUF_SIZE] - m;
            memcpy(ptr, p, m);
            ptr += m;
            n -= m;
            memcpy(ptr, logfile->buf, n);
            ptr += n;
        }

        if ((rc = pthread_mutex_unlock(&logfile->bufLock)) != 0)
            err_msg(rc, "pthread_mutex_unlock() failed for [%s]",
                logfile->name);

        /*  Must recompute 'len' since we already subtracted space reserved
         *    for this string.  We could get away with just sprintf() here.
         */
        len = &buf[sizeof(buf)] - ptr;
        n = snprintf(ptr, len, "%sEnd log replay of console [%s]%s",
            CONMAN_MSG_PREFIX, console->name, CONMAN_MSG_SUFFIX);
        assert((n >= 0) && (n < len));
    }

    DPRINTF("Performing log replay on console [%s].\n", console->name);
    write_obj_data(client, buf, strlen(buf), 0);
    return;
}


static void perform_quiet_toggle(obj_t *client)
{
/*  Toggles whether informational messages are suppressed by the client.
 */
    int rc;

    assert(client->type == CLIENT);

    if ((rc = pthread_mutex_lock(&client->bufLock)) != 0)
        err_msg(rc, "pthread_mutex_lock() failed for [%s]", client->name);

    client->aux.client.req->enableQuiet ^= 1;
    DPRINTF("Toggled quiet-mode for client [%s].\n", client->name);

    if ((rc = pthread_mutex_unlock(&client->bufLock)) != 0)
        err_msg(rc, "pthread_mutex_unlock() failed for [%s]", client->name);
    return;
}


static void perform_suspend(obj_t *client)
{
/*  Toggles whether output to the client is suspended/resumed.
 *  Note that while a client is suspended, data may still be written
 *    into its circular-buffer; if the client does not resume before
 *    the buffer wraps around, data will be lost.
 */
    int rc;
    int gotSuspend;

    assert(client->type == CLIENT);

    if ((rc = pthread_mutex_lock(&client->bufLock)) != 0)
        err_msg(rc, "pthread_mutex_lock() failed for [%s]", client->name);

    gotSuspend = client->aux.client.gotSuspend ^= 1;

    if ((rc = pthread_mutex_unlock(&client->bufLock)) != 0)
        err_msg(rc, "pthread_mutex_unlock() failed for [%s]", client->name);

    if (gotSuspend)
        DPRINTF("Suspending output to client [%s].\n", client->name);
    else
        DPRINTF("Resuming output to client [%s].\n", client->name);
    return;
}
