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

#define TELCMDS
#define TELOPTS

#include <sys/types.h>                  /* include before in.h for bsd */
#include <netinet/in.h>                 /* include before telnet.h for bsd */
#include <arpa/telnet.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "list.h"
#include "log.h"
#include "server.h"
#include "tpoll.h"
#include "util-file.h"
#include "util-net.h"
#include "util-str.h"
#include "util.h"

#define OPTBUFLEN 8                     /* "OPT:nnn" + \0 */


static int connect_telnet_obj(obj_t *telnet);
static void disconnect_telnet_obj(obj_t *telnet);
static void reset_telnet_delay(obj_t *telnet);
static int process_telnet_cmd(obj_t *telnet, int cmd, int opt);
static char * opt2str(int opt, char *buf, int buflen);

extern tpoll_t tp_global;               /* defined in server.c */


int is_telnet_dev(const char *dev, char **host_ref, int *port_ref)
{
    char  buf[MAX_LINE];
    char *p;
    int   n;

    assert(dev != NULL);

    if (strlcpy(buf, dev, sizeof(buf)) >= sizeof(buf)) {
        return(0);
    }
    if (!(p = strchr(buf, ':'))) {
        return(0);
    }
    if ((n = strspn(p+1, "0123456789")) == 0) {
        return(0);
    }
    if (p[ n + 1 ] != '\0') {
        return(0);
    }
    *p++ = '\0';
    if (host_ref) {
        *host_ref = create_string(buf);
    }
    if (port_ref) {
        *port_ref = atoi(p);
    }
    return(1);
}


obj_t * create_telnet_obj(server_conf_t *conf, char *name,
    char *host, int port, char *errbuf, int errlen)
{
/*  Creates a new terminal server object and adds it to the master objs list.
 *  Note: a non-blocking connect will later be initiated for the remote host
 *    by main:open_objs:reopen_obj:open_telnet_obj:connect_telnet_obj().
 *  Returns the new object, or NULL on error.
 */
    ListIterator i;
    obj_t *telnet;

    assert(conf != NULL);
    assert((name != NULL) && (name[0] != '\0'));
    assert((host != NULL) && (host[0] != '\0'));

    if (port <= 0) {
        if ((errbuf != NULL) && (errlen > 0)) {
            snprintf(errbuf, errlen,
                "console [%s] specifies invalid port \"%d\"", name, port);
        }
        return(NULL);
    }
    /*  Check for duplicate console names.
     */
    i = list_iterator_create(conf->objs);
    while ((telnet = list_next(i))) {
        if (is_console_obj(telnet) && !strcmp(telnet->name, name)) {
            if ((errbuf != NULL) && (errlen > 0)) {
                snprintf(errbuf, errlen,
                    "console [%s] specifies duplicate console name", name);
            }
            break;
        }
    }
    list_iterator_destroy(i);
    if (telnet != NULL) {
        return(NULL);
    }
    telnet = create_obj(conf, name, -1, CONMAN_OBJ_TELNET);
    telnet->aux.telnet.host = create_string(host);
    telnet->aux.telnet.port = port;
    telnet->aux.telnet.logfile = NULL;
    telnet->aux.telnet.timer = -1;
    telnet->aux.telnet.delay = TELNET_MIN_TIMEOUT;
    telnet->aux.telnet.iac = -1;
    telnet->aux.telnet.state = CONMAN_TELNET_DOWN;
    /*
     *  Dup 'enableKeepAlive' to prevent passing 'conf'
     *    to connect_telnet_obj().
     */
    telnet->aux.telnet.enableKeepAlive = conf->enableKeepAlive;

    /*  Add obj to the master conf->objs list.
     */
    list_append(conf->objs, telnet);

    return(telnet);
}


int open_telnet_obj(obj_t *telnet)
{
/*  (Re)opens the specified 'telnet' obj.
 *  Returns 0 if the connection is successfully completed; o/w, returns -1.
 */
    int rc = 0;

    assert(telnet != NULL);
    assert(is_telnet_obj(telnet));

    if (telnet->aux.telnet.state == CONMAN_TELNET_UP) {
        disconnect_telnet_obj(telnet);
    }
    else {
        rc = connect_telnet_obj(telnet);
    }
    DPRINTF((9, "Opened [%s] telnet: fd=%d host=%s port=%d state=%d.\n",
        telnet->name, telnet->fd, telnet->aux.telnet.host,
        telnet->aux.telnet.port, (int) telnet->aux.telnet.state));
    return(rc);
}


static int connect_telnet_obj(obj_t *telnet)
{
/*  Establishes a non-blocking connect with the specified (telnet) obj.
 *  Returns 0 if the connection is successfully completed; o/w, returns -1.
 */
    struct sockaddr_in saddr;
    const int on = 1;

    assert(telnet->aux.telnet.state != CONMAN_TELNET_UP);

    if (telnet->aux.telnet.timer >= 0) {
        (void) tpoll_timeout_cancel(tp_global, telnet->aux.telnet.timer);
        telnet->aux.telnet.timer = -1;
    }
    if (telnet->aux.telnet.state == CONMAN_TELNET_DOWN) {
        /*
         *  Initiate a non-blocking connection attempt.
         */
        memset(&saddr, 0, sizeof(saddr));
        saddr.sin_family = AF_INET;
        saddr.sin_port = htons(telnet->aux.telnet.port);
        if (host_name_to_addr4(telnet->aux.telnet.host, &saddr.sin_addr) < 0) {
            log_msg(LOG_WARNING, "Unable to resolve hostname \"%s\" for [%s]",
                telnet->aux.telnet.host, telnet->name);
            telnet->aux.telnet.timer = tpoll_timeout_relative(tp_global,
                (callback_f) connect_telnet_obj, telnet,
                RESOLVE_RETRY_TIMEOUT * 1000);
            return(-1);
        }
        if ((telnet->fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            log_err(errno, "Unable to create socket for [%s]", telnet->name);
        }
        if (setsockopt(telnet->fd, SOL_SOCKET, SO_OOBINLINE,
                (const void *) &on, sizeof(on)) < 0) {
            log_err(errno, "Unable to set OOBINLINE socket option");
        }
        if (telnet->aux.telnet.enableKeepAlive) {
            if (setsockopt(telnet->fd, SOL_SOCKET, SO_KEEPALIVE,
                    (const void *) &on, sizeof(on)) < 0) {
                log_err(errno, "Unable to set KEEPALIVE socket option");
            }
        }
        set_fd_nonblocking(telnet->fd);
        set_fd_closed_on_exec(telnet->fd);

        DPRINTF((10, "Connecting to <%s:%d> for [%s].\n",
            telnet->aux.telnet.host, telnet->aux.telnet.port, telnet->name));

        if (connect(telnet->fd,
                (struct sockaddr *) &saddr, sizeof(saddr)) < 0) {
            if (errno == EINPROGRESS) {
                telnet->aux.telnet.state = CONMAN_TELNET_PENDING;
                tpoll_set(tp_global, telnet->fd, POLLIN | POLLOUT);
            }
            else {
                disconnect_telnet_obj(telnet);
            }
            return(-1);
        }
    }
    else if (telnet->aux.telnet.state == CONMAN_TELNET_PENDING) {
        /*
         *  Did the non-blocking connect complete successfully?
         *    (cf. Stevens UNPv1 15.3 p409)
         */
        int err = 0;
        socklen_t len = sizeof(err);
        int rc;

        rc = getsockopt(telnet->fd, SOL_SOCKET, SO_ERROR, (void *) &err, &len);
        /*
         *  If an error occurred, Berkeley-derived implementations
         *    return 0 with the pending error in 'err'.  But Solaris
         *    returns -1 with the pending error in 'errno'.  Sigh...
         */
        if (rc < 0) {
            err = errno;
        }
        if (err) {
            disconnect_telnet_obj(telnet);
            return(-1);
        }
        tpoll_clear(tp_global, telnet->fd, POLLOUT);
        DPRINTF((10, "Completing connection to <%s:%d> for [%s].\n",
            telnet->aux.telnet.host, telnet->aux.telnet.port, telnet->name));
    }
    else {
        log_err(0, "Console [%s] is in unexpected telnet state=%d",
            telnet->aux.telnet.state);
    }
    telnet->gotEOF = 0;
    telnet->aux.telnet.state = CONMAN_TELNET_UP;
    tpoll_set(tp_global, telnet->fd, POLLIN);

    /*  Notify linked objs when transitioning into an UP state.
     */
    write_notify_msg(telnet, LOG_INFO, "Console [%s] connected to <%s:%d>",
        telnet->name, telnet->aux.telnet.host, telnet->aux.telnet.port);
    /*
     *  Require the connection to be up for a minimum length of time
     *    before resetting the reconnect delay back to zero.  This protects
     *    against the server spinning on reconnects if the connection is
     *    being automatically terminated by something like TCP-Wrappers.
     *  If the connection is terminated before the timer expires,
     *    disconnect_telnet_obj() will cancel the timer and the
     *    exponential backoff will continue.
     */
    telnet->aux.telnet.timer = tpoll_timeout_relative(tp_global,
        (callback_f) reset_telnet_delay, telnet, TELNET_MIN_TIMEOUT * 1000);

    send_telnet_cmd(telnet, DO, TELOPT_BINARY);
    send_telnet_cmd(telnet, DO, TELOPT_ECHO);
    send_telnet_cmd(telnet, DO, TELOPT_SGA);
    send_telnet_cmd(telnet, WILL, TELOPT_BINARY);
    send_telnet_cmd(telnet, WILL, TELOPT_SGA);

    return(0);
}


static void disconnect_telnet_obj(obj_t *telnet)
{
/*  Closes the existing connection with the specified (telnet) obj
 *    and sets a timer for establishing a new connection.
 */
    DPRINTF((10, "Disconnecting from <%s:%d> for [%s].\n",
        telnet->aux.telnet.host, telnet->aux.telnet.port, telnet->name));

    if (telnet->aux.telnet.timer >= 0) {
        (void) tpoll_timeout_cancel(tp_global, telnet->aux.telnet.timer);
        telnet->aux.telnet.timer = -1;
    }
    if (telnet->fd >= 0) {
        tpoll_clear(tp_global, telnet->fd, POLLIN | POLLOUT);
        if (close(telnet->fd) < 0)
            log_msg(LOG_WARNING,
                "Unable to close connection to <%s:%d> for [%s]: %s",
                telnet->aux.telnet.host, telnet->aux.telnet.port,
                telnet->name, strerror(errno));
        telnet->fd = -1;
    }
    /*  Notify linked objs when transitioning from an UP state.
     */
    if (telnet->aux.telnet.state == CONMAN_TELNET_UP) {
        write_notify_msg(telnet, LOG_INFO,
            "Console [%s] disconnected from <%s:%d>",
            telnet->name, telnet->aux.telnet.host, telnet->aux.telnet.port);
    }
    telnet->aux.telnet.state = CONMAN_TELNET_DOWN;
    /*
     *  Set timer for establishing new connection using exponential backoff.
     */
    telnet->aux.telnet.timer = tpoll_timeout_relative(tp_global,
        (callback_f) connect_telnet_obj, telnet,
        telnet->aux.telnet.delay * 1000);
    if (telnet->aux.telnet.delay == 0) {
        telnet->aux.telnet.delay = TELNET_MIN_TIMEOUT;
    }
    else if (telnet->aux.telnet.delay < TELNET_MAX_TIMEOUT) {
        telnet->aux.telnet.delay =
            MIN(telnet->aux.telnet.delay * 2, TELNET_MAX_TIMEOUT);
    }
    return;
}


static void reset_telnet_delay(obj_t *telnet)
{
/*  Resets the telnet obj's delay between reconnect attempts.
 *  By resetting this delay after a minimum length of time, the server is
 *    protected against spinning on reconnects where the connection is
 *    being automatically terminated by something like TCP-Wrappers.
 */
    assert(is_telnet_obj(telnet));

    telnet->aux.telnet.delay = 0;
    /*
     *  Also reset the timer ID since this routine is only invoked
     *    by a timer when it expires.
     */
    telnet->aux.telnet.timer = -1;
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
    assert(telnet->aux.telnet.state == CONMAN_TELNET_UP);

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
    char opt_buf[OPTBUFLEN];

    assert(is_telnet_obj(telnet));
    assert(cmd > 0);

    /*  This is a no-op if the telnet connection is not yet established.
     */
    if ((telnet->fd < 0) || (telnet->aux.telnet.state != CONMAN_TELNET_UP))
        return(0);

    *p++ = IAC;
    if (!TELCMD_OK(cmd)) {
        log_msg(LOG_WARNING, "Invalid telnet cmd=%#.2x for console [%s]",
            cmd, telnet->name);
        return(-1);
    }
    *p++ = cmd;
    if ((cmd == DONT) || (cmd == DO) || (cmd == WONT) || (cmd == WILL)) {
#if 0
        /*  Disabled since my TELOPT_OK doesn't recognize newer telnet opts.
         */
        if (!TELOPT_OK(opt)) {
            log_msg(LOG_WARNING,
                "Invalid telnet cmd %s opt=%#.2x for console [%s]",
                telcmds[cmd - TELCMD_FIRST], opt, telnet->name);
            return(-1);
        }
#endif
        *p++ = opt;
    }

    assert((p > buf) && ((size_t) (p - buf) <= sizeof(buf)));
    if (write_obj_data(telnet, buf, p - buf, 0) <= 0)
        return(-1);

    (void) opt_buf;                     /* suppress unused-variable warning */
    DPRINTF((10, "Sent telnet cmd %s %s to console [%s].\n",
        telcmds[cmd - TELCMD_FIRST],
        (TELOPT_OK(opt)
            ? telopts[opt - TELOPT_FIRST]
            : opt2str(opt, opt_buf, sizeof(opt_buf))),
        telnet->name));
    return(0);
}


static int process_telnet_cmd(obj_t *telnet, int cmd, int opt)
{
/*  Processes the given telnet cmd received from the (telnet) console.
 *  Returns 0 if the command is valid, or -1 on error.
 */
    char opt_buf[OPTBUFLEN];

    assert(is_telnet_obj(telnet));
    assert(telnet->fd >= 0);
    assert(telnet->aux.telnet.state == CONMAN_TELNET_UP);

    if (!TELCMD_OK(cmd)) {
        log_msg(LOG_DEBUG,
            "Received invalid telnet cmd %#.2x from console [%s]",
            cmd, telnet->name);
        return(-1);
    }
    DPRINTF((10, "Received telnet cmd %s %s from console [%s].\n",
        telcmds[cmd - TELCMD_FIRST],
        (TELOPT_OK(opt)
            ? telopts[opt - TELOPT_FIRST]
            : opt2str(opt, opt_buf, sizeof(opt_buf))),
        telnet->name));

#if 0
    /*  Disabled since my TELOPT_OK doesn't recognize newer telnet opts.
     */
    if (!TELOPT_OK(opt)) {
        log_msg(LOG_DEBUG,
            "Received invalid telnet opt %#.2x from console [%s]",
            opt, telnet->name);
        return(-1);
    }
#endif

    /*  FIXME: Perform telnet option negotiation via rfc1143 Q-Method.
     */

    switch(cmd) {
    case DONT:
        break;
    case DO:
        if (    (opt != TELOPT_BINARY) &&
                (opt != TELOPT_SGA))
        {
            send_telnet_cmd(telnet, WONT, opt);
        }
        break;
    case WONT:
        if (    (opt == TELOPT_BINARY) ||
                (opt == TELOPT_ECHO)   ||
                (opt == TELOPT_SGA))
        {
            log_msg(LOG_NOTICE,
                "Received telnet cmd %s %s from console [%s]",
                telcmds[cmd - TELCMD_FIRST],
                telopts[opt - TELOPT_FIRST],
                telnet->name);
        }
        break;
    case WILL:
        if (    (opt != TELOPT_BINARY) &&
                (opt != TELOPT_ECHO)   &&
                (opt != TELOPT_SGA))
        {
            send_telnet_cmd(telnet, DONT, opt);
        }
        break;
    default:
        log_msg(LOG_INFO, "Ignoring telnet cmd %s %s from console [%s]",
            telcmds[cmd - TELCMD_FIRST],
            (TELOPT_OK(opt)
                ? telopts[opt - TELOPT_FIRST]
                : opt2str(opt, opt_buf, sizeof(opt_buf))),
            telnet->name);
        break;
    }
    return(0);
}


static char * opt2str(int opt, char *buf, int buflen)
{
    if ((buf != NULL) && (buflen > 0)) {
        snprintf(buf, buflen, "OPT:%d", opt);
    }
    return(buf);
}
