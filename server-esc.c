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

#include <arpa/telnet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
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

extern tpoll_t tp_global;               /* defined in server.c */


static void perform_serial_break(obj_t *client);
static void perform_del_char_seq(obj_t *client);
static void perform_console_writer_linkage(obj_t *client);
static void perform_log_replay(obj_t *client);
static void perform_quiet_toggle(obj_t *client);
static void perform_reset(obj_t *client);
static void kill_reset_cmd(obj_t *console);
static void perform_suspend(obj_t *client);


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
            case ESC_CHAR_REPLAY:
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

        /*  For process-based consoles, the serial-break will be replaced with
         *    the "&B" character sequence.  The default escape character '&'
         *    will always be used here since the client-server protocol does
         *    not transmit the escape character being used by the client.
         *  This character sequence can be intercepted by the script
         *    controlling the console.  An Expect script spawning a telnet
         *    connection could perform the following action:
         *      interact "&B" { send "\035send brk\r\n" }
         */
        if (is_process_obj(console)) {
            unsigned char brk[2];
            brk[0] = DEFAULT_CLIENT_ESCAPE;
            brk[1] = ESC_CHAR_BREAK;
            write_obj_data(console, &brk, 2, 0);
        }
        else if (is_serial_obj(console)) {
            if (tcsendbreak(console->fd, 0) < 0)
                log_msg(LOG_WARNING,
                    "Unable to send serial-break to console [%s]: %s",
                    console->name, strerror(errno));
        }
        else if (is_telnet_obj(console)) {
            if (send_telnet_cmd(console, BREAK, -1) < 0) {
                log_msg(LOG_WARNING,
                    "Unable to send serial-break to console [%s]",
                    console->name);
            }
        }
#if WITH_FREEIPMI
        else if (is_ipmi_obj(console)) {
            if (send_ipmi_break(console) < 0) {
                log_msg(LOG_WARNING,
                    "Unable to send serial-break to console [%s]",
                    console->name);
            }
        }
#endif /* WITH_FREEIPMI */

        /*  FIXME: How should serial-breaks be handled for unixsock objs?
         */
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
 *    circular-buffer via write_obj_data() is (OBJ_BUF_SIZE - 1) bytes.
 *    But writing this much data may likely overwrite data in the buffer that
 *    has not been flushed to the object's file descriptor via write_to_obj().
 *    Therefore, it is recommended (LOG_REPLAY_LEN <= OBJ_BUF_SIZE / 2).
 */
    obj_t *console;
    obj_t *logfile;
    unsigned char buf[OBJ_BUF_SIZE - 1];
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
        n = snprintf((char *) ptr, len,
            "%sConsole [%s] is not being logged -- cannot replay%s",
            CONMAN_MSG_PREFIX, console->name, CONMAN_MSG_SUFFIX);
        if ((n < 0) || (n >= len)) {
            log_msg(LOG_WARNING,
                "Insufficient buffer to write message to console %s log",
                console->name);
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
            n = MIN(LOG_REPLAY_LEN, logfile->bufInPtr - logfile->buf);
        else
            n = MIN(LOG_REPLAY_LEN, OBJ_BUF_SIZE - 1);
        n = MIN(n, len);

        p = logfile->bufInPtr - n;
        if (p >= logfile->buf) {        /* no wrap needed */
            memcpy(ptr, p, n);
            ptr =  (ptr - buf + n > MAX_BUF_SIZE - 1 ? buf + MAX_BUF_SIZE - 1 : ptr + n);
        }
        else {                          /* wrap backwards */
            m = logfile->buf - p;
            p = &logfile->buf[OBJ_BUF_SIZE] - m;
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
        if( len > sizeof(buf) - 1 ) {  /* Ensure buffer overflow cannot happen */
                len = sizeof(buf) - 1;
        }
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

    if (client->aux.client.req->enableQuiet)
        op = "enabled", action = "suppressed";
    else
        op = "disabled", action = "displayed";

    DPRINTF((5, "Quiet-mode %s for client [%s].\n", op, client->name));
    str = create_format_string("%sQuiet-mode %s -- info msgs will be %s%s",
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
 */
    ListIterator i;
    obj_t *console;
    char cmd[MAX_LINE];
    int fd;

    assert(is_client_obj(client));

    i = list_iterator_create(client->readers);
    while ((console = list_next(i))) {

        assert(is_console_obj(console));

        if (console->resetCmdRef == NULL) {
            continue;
        }
        if (console->resetCmdPid > 0) {
            write_notify_msg(console, LOG_INFO,
                "Ignoring reset of console [%s]: pid %d still active",
                console->name, (int) console->resetCmdPid);
            continue;
        }
        if (format_obj_string(
                cmd, sizeof(cmd), console, console->resetCmdRef) < 0) {
            write_notify_msg(console, LOG_WARNING,
                "Unable to reset console [%s]: command too long",
                console->name);
            continue;
        }
        console->resetCmdPid = fork();
        if (console->resetCmdPid < 0) {
            write_notify_msg(console, LOG_WARNING,
                "Unable to reset console [%s]: fork failed: %s",
                console->name, strerror(errno));
            continue;
        }
        else if (console->resetCmdPid == 0) {
            setpgid(console->resetCmdPid, 0);
            fd = open("/dev/null", O_RDWR);
            (void) dup2(fd, STDIN_FILENO);
            (void) dup2(fd, STDOUT_FILENO);
            (void) dup2(fd, STDERR_FILENO);
            if (fd > STDERR_FILENO) {
                (void) close(fd);
            }
            execl("/bin/sh", "sh", "-c", cmd, (char *) NULL);
            _exit(127);                 /* execl() error */
        }
        /*  Both parent and child call setpgid() to make the child a process
         *    group leader.  One of these calls is redundant, but by doing
         *    both we avoid a race condition.  (cf. APUE 9.4 p244)
         */
        setpgid(console->resetCmdPid, 0);

        write_notify_msg(console, LOG_NOTICE,
            "Console [%s] reset by <%s@%s> (pid %d)",
            console->name, client->aux.client.req->user,
            client->aux.client.req->host, (int) console->resetCmdPid);

        /*  Set a timer to ensure the reset cmd does not exceed its time limit.
         */
        console->resetCmdTimer = tpoll_timeout_relative(tp_global,
            (callback_f) kill_reset_cmd, console, RESET_CMD_TIMEOUT * 1000);
        if (console->resetCmdTimer < 0) {
            write_notify_msg(console, LOG_WARNING,
                "Unable to create timer for reset of console [%s]: %s",
                console->name, strerror(errno));
        }
    }
    list_iterator_destroy(i);
    return;
}


static void kill_reset_cmd(obj_t *console)
{
/*  Terminates the "ResetCmd" process associated with 'console' if it has
 *    exceeded its time limit.
 */
    pid_t pid;

    assert(console != NULL);

    pid = console->resetCmdPid;
    console->resetCmdPid = 0;
    console->resetCmdTimer = 0;

    if (pid <= 0) {
        return;
    }
    if (kill(pid, 0) < 0) {             /* process is no longer running */
        return;
    }
    if (kill(-pid, SIGKILL) == 0) {     /* kill entire process group */
        log_msg(LOG_NOTICE,
            "Console [%s] reset terminated after %ds (pid %d)",
            console->name, RESET_CMD_TIMEOUT, (int) pid);
    }
    else {
        log_msg(LOG_WARNING,
            "Unable to terminate console [%s] reset after %ds (pid %d): %s",
            console->name, RESET_CMD_TIMEOUT, (int) pid, strerror(errno));
    }
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

    if (client->aux.client.gotSuspend) {
        tpoll_clear(tp_global, client->fd, POLLOUT);
    }
    else {
        tpoll_set(tp_global, client->fd, POLLOUT);
    }

    /*  FIXME: Do check_console_state() here looking for downed telnets.
     *    Should it check the state of all readers & writers?
     */
    DPRINTF((5, "%s output to client [%s].\n",
        (gotSuspend ? "Suspending" : "Resuming"), client->name));
    return;
}
