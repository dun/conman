/******************************************************************************\
 *  $Id: server-esc.c,v 1.13 2001/09/23 01:54:52 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
\******************************************************************************/


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#define TELCMDS
#define TELOPTS

#include <arpa/telnet.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include "common.h"
#include "errors.h"
#include "list.h"
#include "server.h"
#include "util.h"
#include "wrapper.h"


static void perform_serial_break(obj_t *client);
static void perform_log_replay(obj_t *client);
static void perform_quiet_toggle(obj_t *client);
static void perform_suspend(obj_t *client);
static int process_telnet_cmd(obj_t *telnet, int cmd, int opt);


int process_client_escapes(obj_t *client, void *src, int len)
{
/*  Processes the buffer (src) of length (len) received from the client
 *    for escape character sequences.
 *  Escape characters are removed (unstuffed) from the buffer
 *    and immediately processed.
 *  Returns the new length of the modified buffer.
 */
    const unsigned char *last = (unsigned char *) src + len;
    unsigned char *p, *q;

    assert(is_client_obj(client));
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
    assert(len >= 0);
    return(len);
}


static void perform_serial_break(obj_t *client)
{
/*  Transmits a serial-break to each of the consoles written to by the client.
 */
    ListIterator i;
    obj_t *console;

    assert(is_client_obj(client));

    i = list_iterator_create(client->readers);
    while ((console = list_next(i))) {

        assert(is_console_obj(console));
        DPRINTF("Performing serial-break on console [%s].\n", console->name);

        if (is_serial_obj(console)) {
            if (tcsendbreak(console->fd, 0) < 0)
                err_msg(errno, "tcsendbreak() failed for console [%s]",
                    console->name);
        }
        else if (is_telnet_obj(console)) {
            send_telnet_cmd(console, BREAK, -1);
        }
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

    assert(is_client_obj(client));

    /*  Broadcast sessions are "write-only", so the log-replay is a no-op.
     */
    if (list_is_empty(client->writers))
        return;

    /*  The client will have exactly one writer in either a R/O or R/W session.
     */
    assert(list_count(client->writers) == 1);
    console = list_peek(client->writers);
    assert(is_console_obj(console));
    if (is_serial_obj(console))
        logfile = console->aux.serial.logfile;
    else if (is_telnet_obj(console))
        logfile = console->aux.telnet.logfile;

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
        assert(is_logfile_obj(logfile));
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

        x_pthread_mutex_lock(&logfile->bufLock);

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

        x_pthread_mutex_unlock(&logfile->bufLock);

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
    assert(is_client_obj(client));
    x_pthread_mutex_lock(&client->bufLock);
    client->aux.client.req->enableQuiet ^= 1;
    x_pthread_mutex_unlock(&client->bufLock);

    DPRINTF("Toggled quiet-mode for client [%s].\n", client->name);
    return;
}


static void perform_suspend(obj_t *client)
{
/*  Toggles whether output to the client is suspended/resumed.
 *  Note that while a client is suspended, data may still be written
 *    into its circular-buffer; if the client does not resume before
 *    the buffer wraps around, data will be lost.
 */
    int gotSuspend;

    assert(is_client_obj(client));
    x_pthread_mutex_lock(&client->bufLock);
    gotSuspend = client->aux.client.gotSuspend ^= 1;
    x_pthread_mutex_unlock(&client->bufLock);

    DPRINTF("%s output to client [%s].\n",
        (gotSuspend ? "Suspending" : "Resuming"), client->name);
    return;
}


int process_telnet_escapes(obj_t *telnet, void *src, int len)
{
/*  Processes the buffer (src) of length (len) received from
 *    a terminal server for IAC escape character sequences.
 *  Escape character sequences are removed from the buffer
 *    and immediately processed.
 *  Returns the new length of the modified buffer.
 */
    const unsigned char *last = (unsigned char *) src + len;
    unsigned char *p, *q;

    assert(is_telnet_obj(telnet));
    assert(telnet->fd >= 0);
    assert(telnet->aux.telnet.conState == TELCON_UP);

    if (!src || len <= 0)
        return(0);

    for (p=q=src; p<last; p++) {
        switch(telnet->aux.telnet.iac) {
        case -1:
            if (*p == IAC)
                telnet->aux.telnet.iac = *p;
            else
                *q++ = *p;
            break;
        case IAC:
            switch (*p) {
            case IAC:
                *q++ = *p;
                telnet->aux.telnet.iac = -1;
                break;
            case DONT:
                /* fall-thru */
            case DO:
                /* fall-thru */
            case WONT:
                /* fall-thru */
            case WILL:
                /* fall-thru */
            case SB:
                telnet->aux.telnet.iac = *p;
                break;
            case SE:
                telnet->aux.telnet.iac = -1;
                break;
            default:
                process_telnet_cmd(telnet, *p, -1);
                telnet->aux.telnet.iac = -1;
                break;
            }
            break;
        case DONT:
            /* fall-thru */
        case DO:
            /* fall-thru */
        case WONT:
            /* fall-thru */
        case WILL:
            process_telnet_cmd(telnet, telnet->aux.telnet.iac, *p);
            telnet->aux.telnet.iac = -1;
            break;
        case SB:
            /*
             *  Ignore subnegotiation opts.  Consume bytes until IAC SE.
             *    Remain in the SB state until an IAC is found; then reset
             *    the state to IAC assuming the next byte will be the SE cmd.
             */
            if (*p == IAC)
                telnet->aux.telnet.iac = *p;
            break;
        default:
            err_msg(0, "Reached invalid state %#.2x%.2x for console [%s]",
                telnet->aux.telnet.iac, *p, telnet->name);
            break;
        }
    }

    assert((q >= (unsigned char *) src) && (q <= p));
    len = q - (unsigned char *) src;
    assert(len >= 0);
    return(len);
}


int send_telnet_cmd(obj_t *telnet, int cmd, int opt)
{
/*  Sends the given telnet (cmd) and (opt) to the (telnet) console;
 *    if (cmd) does not require an option, set (opt) = -1.
 *  Returns 0 if the command is successfully "sent" (ie, written into
 *    the obj's buffer), or -1 on error.
 */
    unsigned char buf[3];
    unsigned char *p = buf;

    assert(is_telnet_obj(telnet));
    assert(telnet->fd >= 0);
    assert(telnet->aux.telnet.conState == TELCON_UP);
    assert(cmd > 0);

    *p++ = IAC;
    if (!TELCMD_OK(cmd)) {
        log_msg(0, "Invalid telnet cmd=%#.2x for console [%s]",
            cmd, telnet->name);
        return(-1);
    }
    *p++ = cmd;
    if ((cmd == DONT) || (cmd == DO) || (cmd == WONT) || (cmd == WILL)) {
        if (!TELOPT_OK(opt)) {
            log_msg(0, "Invalid telnet cmd %s opt=%#.2x for console [%s]",
                telcmds[cmd - TELCMD_FIRST], opt, telnet->name);
            return(-1);
        }
        *p++ = opt;
    }

    assert((p > buf) && ((p - buf) <= sizeof(buf)));
    if (write_obj_data(telnet, buf, p - buf, 0) <= 0)
        return(-1);
    DPRINTF("Sent telnet cmd %s%s%s to console [%s].\n",
        telcmds[cmd - TELCMD_FIRST], ((p - buf > 2) ? " " : ""),
        ((p - buf > 2) ? telopts[opt - TELOPT_FIRST] : ""), telnet->name);
    return(0);
}


static int process_telnet_cmd(obj_t *telnet, int cmd, int opt)
{
/*  Processes the given telnet cmd received from the (telnet) console.
 *  Telnet option negotiation is performed using the Q-Method (rfc1143).
 *  Returns 0 if the command is valid, or -1 on error.
 */
    assert(is_telnet_obj(telnet));
    assert(telnet->fd >= 0);
    assert(telnet->aux.telnet.conState == TELCON_UP);

    if (!TELCMD_OK(cmd)) {
        log_msg(0, "Received invalid telnet cmd %#.2x from console [%s]",
            cmd, telnet->name);
        return(-1);
    }
    DPRINTF("Received telnet cmd %s%s%s from console [%s].\n",
        telcmds[cmd - TELCMD_FIRST], (TELOPT_OK(opt) ? " " : ""),
        (TELOPT_OK(opt) ? telopts[opt - TELOPT_FIRST] : ""), telnet->name);

    switch(cmd) {
    case DONT:
        /* fall-thru */
    case DO:
        /* fall-thru */
    case WONT:
        /* fall-thru */
    case WILL:
        if (!TELOPT_OK(opt)) {
            log_msg(0, "Received invalid telnet opt %#.2x from console [%s]",
                opt, telnet->name);
            return(-1);
        }
        /*  Perform telnet option negotiation via the Q-Method.
         *
         *  NOT_IMPLEMENTED_YET
         */
        break;
    default:
        log_msg(10, "Ignoring telnet cmd %s%s%s from console [%s]",
            telcmds[cmd - TELCMD_FIRST], (TELOPT_OK(opt) ? " " : ""),
            (TELOPT_OK(opt) ? telopts[opt - TELOPT_FIRST] : ""), telnet->name);
        break;
    }
    return(0);
}
