/*****************************************************************************\
 *  $Id: server-esc.c,v 1.34.2.2 2003/04/04 05:57:07 dun Exp $
 *****************************************************************************
 *  Copyright (C) 2001-2002 The Regents of the University of California.
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


#define TELCMDS
#define TELOPTS

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <arpa/telnet.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "list.h"
#include "log.h"
#include "server.h"
#include "util-str.h"
#include "util.h"
#include "wrapper.h"


static void perform_serial_break(obj_t *client);
static void perform_del_char_seq(obj_t *client);
static void perform_console_writer_linkage(obj_t *client);
static void perform_log_replay(obj_t *client);
static void perform_quiet_toggle(obj_t *client);
static void perform_reset(obj_t *client);
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
            case ESC_CHAR_DEL:          /* XXX: gnats:100 del char kludge */
                perform_del_char_seq(client);
                break;
            case ESC_CHAR_FORCE:
                client->aux.client.req->enableForce = 1;
                client->aux.client.req->enableJoin = 0;
                perform_console_writer_linkage(client);
                break;
            case ESC_CHAR_JOIN:
                client->aux.client.req->enableForce = 0;
                client->aux.client.req->enableJoin = 1;
                perform_console_writer_linkage(client);
                break;
            case ESC_CHAR_LOG:
                perform_log_replay(client);
                break;
            case ESC_CHAR_MONITOR:
                client->aux.client.req->enableForce = 0;
                client->aux.client.req->enableJoin = 0;
                perform_console_writer_linkage(client);
                break;
            case ESC_CHAR_QUIET:
                perform_quiet_toggle(client);
                break;
            case ESC_CHAR_RESET:
                perform_reset(client);
                break;
            case ESC_CHAR_SUSPEND:
                perform_suspend(client);
                break;
            default:
                log_msg(LOG_WARNING, "Received invalid escape '%c' from %s",
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
        DPRINTF((5, "Performing serial-break on console [%s].\n",
            console->name));

        if (is_serial_obj(console)) {
            if (tcsendbreak(console->fd, 0) < 0)
                log_err(errno, "Unable to send serial-break to console [%s]",
                    console->name);
        }
        else if (is_telnet_obj(console)) {
            send_telnet_cmd(console, BREAK, -1);
        }
    }
    list_iterator_destroy(i);
    return;
}


static void perform_del_char_seq(obj_t *client)
{
/*  Transmits a del char to each of the consoles written to by the client.
 *
 *  XXX: gnats:100 del char kludge
 */
    ListIterator i;
    obj_t *console;
    unsigned char del = 0x7F;

    assert(is_client_obj(client));

    i = list_iterator_create(client->readers);
    while ((console = list_next(i))) {

        assert(is_console_obj(console));
        DPRINTF((5, "Performing DEL-char sequence on console [%s].\n",
            console->name));

        write_obj_data(console, &del, 1, 0);
    }
    list_iterator_destroy(i);
    return;
}


static void perform_console_writer_linkage(obj_t *client)
{
/*  Converts the client's console session between read-only and read-write.
 */
    obj_t *console;
    int gotWrite;

    assert(is_client_obj(client));

    /*  Broadcast sessions are treated as a no-op.
     */
    if (client->aux.client.req->enableBroadcast)
        return;
    assert(list_count(client->readers) <= 1);

    /*  A R/O or R/W client will have exactly one console writer.
     */
    assert(list_count(client->writers) == 1);
    console = list_peek(client->writers);
    assert(is_console_obj(console));

    /*  A R/O client will have no readers,
     *    while a R/W client will have only one reader.
     */
    gotWrite = list_count(client->readers);

    if (gotWrite) {
        client->aux.client.req->command = CONMAN_CMD_MONITOR;
        unlink_objs(client, console);
    }
    else {
        client->aux.client.req->command = CONMAN_CMD_CONNECT;
        link_objs(client, console);
    }
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
 *    Therefore, it is recommended (CONMAN_REPLAY_LEN <= MAX_BUF_SIZE / 2).
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
    logfile = get_console_logfile_obj(console);

    if (!logfile) {
        n = snprintf((char *) ptr, len, "%sConsole [%s] is not being logged%s",
            CONMAN_MSG_PREFIX, console->name, CONMAN_MSG_SUFFIX);
        if ((n < 0) || (n >= len)) {
            log_msg(LOG_WARNING,
                "Insufficient buffer to replay console %s log for %s",
                console->name, client->name);
            return;
        }
        ptr += n;
    }
    else {
        assert(is_logfile_obj(logfile));
        n = snprintf((char *) ptr, len, "%sBegin log replay of console [%s]%s",
            CONMAN_MSG_PREFIX, console->name, CONMAN_MSG_SUFFIX);
        if ((n < 0) || (n >= len) || (sizeof(buf) <= 2*n - 2)) {
            log_msg(LOG_WARNING,
                "Insufficient buffer to replay console %s log for %s",
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
        if (p >= logfile->buf) {        /* no wrap needed */
            memcpy(ptr, p, n);
            ptr += n;
        }
        else {                          /* wrap backwards */
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
        n = snprintf((char *) ptr, len, "%sEnd log replay of console [%s]%s",
            CONMAN_MSG_PREFIX, console->name, CONMAN_MSG_SUFFIX);
        assert((n >= 0) && (n < len));
        ptr += n;
    }

    DPRINTF((5, "Performing log replay on console [%s].\n", console->name));
    write_obj_data(client, buf, ptr - buf, 0);
    return;
}


static void perform_quiet_toggle(obj_t *client)
{
/*  Toggles whether informational messages are suppressed by the client.
 */
    const char *op, *action;
    char *str;

    assert(is_client_obj(client));

    client->aux.client.req->enableQuiet ^= 1;
    DPRINTF((5, "Toggled quiet-mode for client [%s].\n", client->name));

    if (client->aux.client.req->enableQuiet)
        op = "Enabled", action = "suppressed";
    else
        op = "Disabled", action = "displayed";
    str = create_format_string("%s%s quiet-mode -- info msgs will be %s%s",
        CONMAN_MSG_PREFIX, op, action, CONMAN_MSG_SUFFIX);
    /*
     *  Technically, this is an informational message.  But, it is marked as
     *    a non-info msg to write_obj_data() in order to ensure it is written
     *    to the client regardless of the "quiet-mode" setting.
     */
    write_obj_data(client, str, strlen(str), 0);
    free(str);
    return;
}


static void perform_reset(obj_t *client)
{
/*  Resets all consoles for which this client has write-access.
 *
 *  Actually, this routine cannot perform the reset command because
 *    the command string is stored within the server_conf struct.
 *    Therefore, this routine sets a reset flag for each affected
 *    console.  And mux_io() will do the dirty deed when we return.
 */
    ListIterator i;
    obj_t *console;
    char *tty;
    char *now;
    char buf[MAX_LINE];

    assert(is_client_obj(client));

    tty = client->aux.client.req->tty;
    now = create_short_time_string(0);
    i = list_iterator_create(client->readers);
    while ((console = list_next(i))) {
        assert(is_console_obj(console));
        if (console->gotReset)          /* prior reset not yet processed */
            continue;
        console->gotReset = 1;
        log_msg(LOG_NOTICE, "Console [%s] reset by <%s@%s>", console->name,
            client->aux.client.req->user, client->aux.client.req->host);
        snprintf(buf, sizeof(buf),
            "%sConsole [%s] reset by <%s@%s>%s%s at %s%s",
            CONMAN_MSG_PREFIX, console->name,
            client->aux.client.req->user, client->aux.client.req->host,
            (tty ? " on " : ""), (tty ? tty : ""), now, CONMAN_MSG_SUFFIX);
        strcpy(&buf[sizeof(buf) - 3], "\r\n");
        notify_console_objs(console, buf);
    }
    list_iterator_destroy(i);
    free(now);
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

    gotSuspend = client->aux.client.gotSuspend ^= 1;
    /*
     *  FIXME: Do check_console_state() here looking for downed telnets.
     *    Should it check the state of all readers & writers?
     */
    DPRINTF((5, "%s output to client [%s].\n",
        (gotSuspend ? "Suspending" : "Resuming"), client->name));
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
    assert(telnet->aux.telnet.conState == CONMAN_TELCON_UP);

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
            log_err(0, "Reached invalid state %#.2x%.2x for console [%s]",
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
    assert(cmd > 0);

    /*  This is a no-op if the telnet connection is not yet established.
     */
    if ((telnet->fd < 0) || (telnet->aux.telnet.conState != CONMAN_TELCON_UP))
        return(0);

    *p++ = IAC;
    if (!TELCMD_OK(cmd)) {
        log_msg(LOG_WARNING, "Invalid telnet cmd=%#.2x for console [%s]",
            cmd, telnet->name);
        return(-1);
    }
    *p++ = cmd;
    if ((cmd == DONT) || (cmd == DO) || (cmd == WONT) || (cmd == WILL)) {
        if (!TELOPT_OK(opt)) {
            log_msg(LOG_WARNING,
                "Invalid telnet cmd %s opt=%#.2x for console [%s]",
                telcmds[cmd - TELCMD_FIRST], opt, telnet->name);
            return(-1);
        }
        *p++ = opt;
    }

    assert((p > buf) && ((p - buf) <= sizeof(buf)));
    if (write_obj_data(telnet, buf, p - buf, 0) <= 0)
        return(-1);
    DPRINTF((10, "Sent telnet cmd %s%s%s to console [%s].\n",
        telcmds[cmd - TELCMD_FIRST], ((p - buf > 2) ? " " : ""),
        ((p - buf > 2) ? telopts[opt - TELOPT_FIRST] : ""), telnet->name));
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
    assert(telnet->aux.telnet.conState == CONMAN_TELCON_UP);

    if (!TELCMD_OK(cmd)) {
        /*  FIXME: Changed from LOG_WARNING to LOG_DEBUG. 20030403 */
        log_msg(LOG_DEBUG,
            "Received invalid telnet cmd %#.2x from console [%s]",
            cmd, telnet->name);
        return(-1);
    }
    DPRINTF((10, "Received telnet cmd %s%s%s from console [%s].\n",
        telcmds[cmd - TELCMD_FIRST], (TELOPT_OK(opt) ? " " : ""),
        (TELOPT_OK(opt) ? telopts[opt - TELOPT_FIRST] : ""), telnet->name));

    if (!TELOPT_OK(opt)) {
        /*  FIXME: Changed from LOG_WARNING to LOG_DEBUG. 20030403 */
        log_msg(LOG_DEBUG,
            "Received invalid telnet opt %#.2x from console [%s]",
            opt, telnet->name);
        return(-1);
    }
    /*  Perform telnet option negotiation via the Q-Method.
     *
     *  NOT_IMPLEMENTED_YET
     */
    switch(cmd) {
    case DONT:
        /* fall-thru */
    case DO:
        if ((opt != TELOPT_ECHO) && (opt != TELOPT_SGA))
            send_telnet_cmd(telnet, WONT, opt);
        break;
    case WONT:
        /* fall-thru */
    case WILL:
        if ((opt != TELOPT_ECHO) && (opt != TELOPT_SGA))
            send_telnet_cmd(telnet, DONT, opt);
        break;
    default:
        log_msg(LOG_INFO, "Ignoring telnet cmd %s%s%s from console [%s]",
            telcmds[cmd - TELCMD_FIRST], (TELOPT_OK(opt) ? " " : ""),
            (TELOPT_OK(opt) ? telopts[opt - TELOPT_FIRST] : ""), telnet->name);
        break;
    }
    return(0);
}
