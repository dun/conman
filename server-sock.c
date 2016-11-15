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

#include <sys/types.h>                  /* include before in.h for bsd */
#include <netinet/in.h>                 /* include before inet.h for bsd */
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fnmatch.h>
#include <pthread.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "common.h"
#include "lex.h"
#include "log.h"
#include "server.h"
#include "util-file.h"
#include "util-net.h"
#include "util-str.h"
#include "wrapper.h"


#if WITH_TCP_WRAPPERS
/*
 *  TCP-Wrappers support.
 */
#include <syslog.h>
#include <tcpd.h>

extern int hosts_ctl(
  char *daemon, char *client_name, char *client_addr, char *client_user);

int allow_severity = LOG_INFO;          /* logging level for accepted reqs */
int deny_severity = LOG_WARNING;        /* logging level for rejected reqs */

#endif /* WITH_TCP_WRAPPERS */


static int resolve_addr(server_conf_t *conf, req_t *req, int sd);
static int recv_greeting(req_t *req);
static void parse_greeting(Lex l, req_t *req);
static int recv_req(req_t *req);
static void parse_cmd_opts(Lex l, req_t *req);
static int query_consoles(server_conf_t *conf, req_t *req);
static int query_consoles_via_globbing(
    server_conf_t *conf, req_t *req, List matches);
static int query_consoles_via_regex(
    server_conf_t *conf, req_t *req, List matches);
static int validate_req(req_t *req);
static int check_too_many_consoles(req_t *req);
static int check_busy_consoles(req_t *req);
static int send_rsp(req_t *req, int errnum, char *errmsg);
static int perform_query_cmd(req_t *req);
static int perform_monitor_cmd(req_t *req, server_conf_t *conf);
static int perform_connect_cmd(req_t *req, server_conf_t *conf);
static void check_console_state(obj_t *console, obj_t *client);


void process_client(client_arg_t *args)
{
/*  The thread responsible for accepting a client connection
 *    and processing the request.
 *  The QUERY cmd is processed entirely by this thread.
 *  The MONITOR and CONNECT cmds are setup and then placed
 *    in the conf->objs list to be handled by mux_io().
 */
    int sd;
    server_conf_t *conf;
    req_t *req;

    /*  Free the tmp struct that was created by accept_client()
     *    in order to pass multiple args to this thread.
     */
    assert(args != NULL);
    sd = args->sd;
    conf = args->conf;
    free(args);

    DPRINTF((5, "Processing new client.\n"));

    x_pthread_detach(pthread_self());

    req = create_req();

    if (resolve_addr(conf, req, sd) < 0)
        goto err;
    if (recv_greeting(req) < 0)
        goto err;
    if (recv_req(req) < 0)
        goto err;
    if (query_consoles(conf, req) < 0)
        goto err;
    if (validate_req(req) < 0)
        goto err;

    /*  send_rsp() needs to know if the reset command is supported.
     *    Since it cannot check resetCmd in the server_conf struct,
     *    we set a flag in the request struct instead.
     */
    if (conf->resetCmd)
        req->enableReset = 1;

    switch(req->command) {
    case CONMAN_CMD_CONNECT:
        if (perform_connect_cmd(req, conf) < 0)
            goto err;
        break;
    case CONMAN_CMD_MONITOR:
        if (perform_monitor_cmd(req, conf) < 0)
            goto err;
        break;
    case CONMAN_CMD_QUERY:
        if (perform_query_cmd(req) < 0)
            goto err;
        break;
    default:
        log_msg(LOG_WARNING, "Received invalid command=%d from <%s@%s:%d>",
            req->command, req->user, req->fqdn, req->port);
        goto err;
    }
    return;

err:
    destroy_req(req);
    return;
}


static int resolve_addr(server_conf_t *conf, req_t *req, int sd)
{
/*  Resolves the network information associated with the
 *    peer at the other end of the socket connection.
 *  Returns 0 if the remote client address is valid, or -1 on error.
 */
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    char buf[MAX_LINE];
    char *p;
    int gotHostName = 0;

    assert(sd >= 0);

    req->sd = sd;
    if (getpeername(sd, (struct sockaddr *) &addr, &addrlen) < 0)
        log_err(errno, "Unable to get address of remote peer");
    if (!inet_ntop(AF_INET, &addr.sin_addr, buf, sizeof(buf)))
        log_err(errno, "Unable to convert network address into string");
    req->port = ntohs(addr.sin_port);
    req->ip = create_string(buf);
    /*
     *  Attempt to resolve IP address.  If it succeeds, buf contains
     *    host string; if it fails, buf is unchanged with IP addr string.
     *    Either way, copy buf to prevent having to code everything as
     *    (req->host ? req->host : req->ip).
     */
    if ((host_addr4_to_name(&addr.sin_addr, buf, sizeof(buf)))) {
        gotHostName = 1;
        req->fqdn = create_string(buf);
        if ((p = strchr(buf, '.')))
            *p = '\0';
        req->host = create_string(buf);
    }
    else {
        req->fqdn = create_string(buf);
        req->host = create_string(buf);
    }

#if WITH_TCP_WRAPPERS
    /*
     *  Check via TCP-Wrappers.
     */
    if (conf->enableTCPWrap) {
        if (hosts_ctl(CONMAN_DAEMON_NAME,
          (gotHostName ? req->fqdn : STRING_UNKNOWN),
          req->ip, STRING_UNKNOWN) == 0) {
            log_msg(LOG_NOTICE,
                "TCP-Wrappers rejected connection from <%s:%d>",
                req->fqdn, req->port);
            return(-1);
        }
    }
#else /* !WITH_TCP_WRAPPERS */
    (void) gotHostName;         /* suppress unused-but-set-variable warning */
#endif /* WITH_TCP_WRAPPERS */

    return(0);
}


static int recv_greeting(req_t *req)
{
/*  Performs the initial handshake with the client
 *    (SOMEDAY including authentication & encryption, if needed).
 *  Returns 0 if the greeting is valid, or -1 on error.
 */
    int n;
    char buf[MAX_SOCK_LINE];
    Lex l;
    int done = 0;
    int tok;

    assert(req->sd >= 0);

    if ((n = read_line(req->sd, buf, sizeof(buf))) < 0) {
        log_msg(LOG_NOTICE, "Unable to read greeting from <%s:%d>: %s",
            req->fqdn, req->port, strerror(errno));
        return(-1);
    }
    else if (n == 0) {
        log_msg(LOG_NOTICE, "Connection terminated by <%s:%d>",
            req->fqdn, req->port);
        return(-1);
    }

    DPRINTF((5, "Received greeting: %s", buf));

    l = lex_create(buf, proto_strs);
    while (!done) {
        tok = lex_next(l);
        switch(tok) {
        case CONMAN_TOK_HELLO:
            parse_greeting(l, req);
            break;
        case LEX_EOF:
        case LEX_EOL:
            done = 1;
            break;
        default:
            break;
        }
    }
    lex_destroy(l);

    /*  Validate greeting.
     */
    if (!req->user) {
        req->user = create_string("unknown");
        send_rsp(req, CONMAN_ERR_BAD_REQUEST,
            "Invalid greeting: no user specified");
        return(-1);
    }

    /*  Send response to greeting.
     */
    return(send_rsp(req, CONMAN_ERR_NONE, NULL));
}


static void parse_greeting(Lex l, req_t *req)
{
/*  Parses the "HELLO" command from the client:
 *    HELLO USER='<str>' TTY='<str>'
 */
    int done = 0;
    int tok;

    while (!done) {
        tok = lex_next(l);
        switch(tok) {
        case CONMAN_TOK_USER:
            if ((lex_next(l) == '=') && (lex_next(l) == LEX_STR)
              && (*lex_text(l) != '\0')) {
                if (req->user)
                    free(req->user);
                req->user = lex_decode(create_string(lex_text(l)));
            }
            break;
        case CONMAN_TOK_TTY:
            if ((lex_next(l) == '=') && (lex_next(l) == LEX_STR)
              && (*lex_text(l) != '\0')) {
                if (req->tty)
                    free(req->tty);
                req->tty = lex_decode(create_string(lex_text(l)));
            }
            break;
        case LEX_EOF:
        case LEX_EOL:
            done = 1;
            break;
        default:
            break;
        }
    }
    return;
}


static int recv_req(req_t *req)
{
/*  Receives the request from the client after the greeting has completed.
 *  Returns 0 if the request is read OK, or -1 on error.
 */
    int n;
    char buf[MAX_SOCK_LINE];
    Lex l;
    int done = 0;
    int tok;

    assert(req->sd >= 0);

    if ((n = read_line(req->sd, buf, sizeof(buf))) < 0) {
        log_msg(LOG_NOTICE, "Unable to read request from <%s:%d>: %s",
            req->fqdn, req->port, strerror(errno));
        return(-1);
    }
    else if (n == 0) {
        log_msg(LOG_NOTICE, "Connection terminated by <%s:%d>",
            req->fqdn, req->port);
        return(-1);
    }

    DPRINTF((5, "Received request: %s", buf));

    l = lex_create(buf, proto_strs);
    while (!done) {
        tok = lex_next(l);
        switch(tok) {
        case CONMAN_TOK_CONNECT:
            req->command = CONMAN_CMD_CONNECT;
            parse_cmd_opts(l, req);
            break;
        case CONMAN_TOK_MONITOR:
            req->command = CONMAN_CMD_MONITOR;
            parse_cmd_opts(l, req);
            break;
        case CONMAN_TOK_QUERY:
            req->command = CONMAN_CMD_QUERY;
            parse_cmd_opts(l, req);
            break;
        case LEX_EOF:
        case LEX_EOL:
            done = 1;
            break;
        default:
            break;
        }
    }
    lex_destroy(l);

    return(0);
}


static void parse_cmd_opts(Lex l, req_t *req)
{
/*  Parses the command options for the given request.
 */
    int done = 0;
    int tok;
    char *str;

    while (!done) {
        tok = lex_next(l);
        switch(tok) {
        case CONMAN_TOK_CONSOLE:
            if ((lex_next(l) == '=') && (lex_next(l) == LEX_STR)
              && (*lex_text(l) != '\0')) {
                str = lex_decode(create_string(lex_text(l)));
                list_append(req->consoles, str);
            }
            break;
        case CONMAN_TOK_OPTION:
            if (lex_next(l) == '=') {
                if (lex_next(l) == CONMAN_TOK_BROADCAST)
                    req->enableBroadcast = 1;
                else if (lex_prev(l) == CONMAN_TOK_FORCE)
                    req->enableForce = 1;
                else if (lex_prev(l) == CONMAN_TOK_JOIN)
                    req->enableJoin = 1;
                else if (lex_prev(l) == CONMAN_TOK_QUIET)
                    req->enableQuiet = 1;
                else if (lex_prev(l) == CONMAN_TOK_REGEX)
                    req->enableRegex = 1;
            }
            break;
        case LEX_EOF:
        case LEX_EOL:
            done = 1;
            break;
        default:
            break;
        }
    }
    return;
}


static int query_consoles(server_conf_t *conf, req_t *req)
{
/*  Queries the server's conf to resolve the console names specified
 *    in the client's request.
 *  Returns 0 on success, or -1 on error.
 *    Upon a successful return, the req->consoles list of strings
 *    is replaced with a list of console obj_t's.
 */
    List matches;
    int rc;

    if (list_is_empty(req->consoles) && (req->command != CONMAN_CMD_QUERY))
        return(0);

    /*  The NULL destructor is used for 'matches' because the matches list
     *    will only contain refs to objs contained in the conf->objs list.
     *    These objs will be destroyed when the conf->objs list is destroyed.
     */
    matches = list_create(NULL);

    if (req->enableRegex)
        rc = query_consoles_via_regex(conf, req, matches);
    else
        rc = query_consoles_via_globbing(conf, req, matches);

    /*  Replace original list of strings with list of obj_t's.
     */
    list_destroy(req->consoles);
    req->consoles = matches;
    list_sort(req->consoles, (ListCmpF) compare_objs);

    /*  If only one console was selected for a broadcast, then
     *    the session is placed into R/W mode instead of W/O mode.
     *    So update the req accordingly.
     */
    if (list_count(req->consoles) == 1)
        req->enableBroadcast = 0;

    return(rc);
}


static int query_consoles_via_globbing(
    server_conf_t *conf, req_t *req, List matches)
{
/*  Match request patterns against console names using shell-style globbing.
 *  This is less efficient than matching via regular expressions
 *    since the console list must be traversed for each pattern, and the
 *    matches list must be traversed for each match to prevent duplicates.
 */
    char *p;
    ListIterator i, j;
    char *pat;
    obj_t *obj;

    /*  An empty list for the QUERY command matches all consoles.
     */
    if (list_is_empty(req->consoles)) {
        p = create_string("*");
        list_append(req->consoles, p);
    }

    /*  Search objs for console names matching console patterns in the request.
     */
    i = list_iterator_create(req->consoles);
    j = list_iterator_create(conf->objs);
    while ((pat = list_next(i))) {
        list_iterator_reset(j);
        while ((obj = list_next(j))) {
            if (!is_console_obj(obj))
                continue;
            if (!fnmatch(pat, obj->name, 0)
              && !list_find_first(matches, (ListFindF) find_obj, obj))
                list_append(matches, obj);
        }
    }
    list_iterator_destroy(i);
    list_iterator_destroy(j);
    return(0);
}


static int query_consoles_via_regex(
    server_conf_t *conf, req_t *req, List matches)
{
/*  Match request patterns against console names using regular expressions.
 */
    char *p;
    ListIterator i;
    char buf[MAX_SOCK_LINE];
    int rc;
    regex_t rex;
    regmatch_t match;
    obj_t *obj;

    /*  An empty list for the QUERY command matches all consoles.
     */
    if (list_is_empty(req->consoles)) {
        p = create_string(".*");
        list_append(req->consoles, p);
    }

    /*  Combine console patterns via alternation to create single regex.
     */
    i = list_iterator_create(req->consoles);
    strlcpy(buf, list_next(i), sizeof(buf));
    while ((p = list_next(i))) {
        strlcat(buf, "|", sizeof(buf));
        strlcat(buf, p, sizeof(buf));
    }
    list_iterator_destroy(i);

    /*  Initialize 'rex' to silence "uninitialized use" warnings.
     */
    memset(&rex, 0, sizeof(rex));

    /*  Compile regex for searching server's console objs.
     */
    rc = regcomp(&rex, buf, REG_EXTENDED | REG_ICASE);
    if (rc != 0) {
        if (regerror(rc, &rex, buf, sizeof(buf)) > sizeof(buf))
            log_msg(LOG_WARNING, "Got regerror() buffer overrun");
        regfree(&rex);
        send_rsp(req, CONMAN_ERR_BAD_REGEX, buf);
        return(-1);
    }

    /*  Search objs for console names matching console patterns in the request.
     */
    i = list_iterator_create(conf->objs);
    while ((obj = list_next(i))) {
        if (!is_console_obj(obj))
            continue;
        if (!regexec(&rex, obj->name, 1, &match, 0)
          && (match.rm_so == 0)
          && (match.rm_eo == (int) strlen(obj->name)))
            list_append(matches, obj);
    }
    list_iterator_destroy(i);
    regfree(&rex);
    return(0);
}


static int validate_req(req_t *req)
{
/*  Validates the given request.
 *  Returns 0 if the request is valid, or -1 on error.
 */
    if (list_is_empty(req->consoles)) {
        send_rsp(req, CONMAN_ERR_NO_CONSOLES, "Found no matching consoles");
        return(-1);
    }
    if (check_too_many_consoles(req) < 0)
        return(-1);
    if (check_busy_consoles(req) < 0)
        return(-1);

    return(0);
}


static int check_too_many_consoles(req_t *req)
{
/*  Checks to see if the request matches too many consoles
 *    for the given command.
 *  A MONITOR command can only affect a single console, as can a
 *    CONNECT command unless the broadcast option is enabled.
 *  Returns 0 if the request is valid, or -1 on error.
 */
    ListIterator i;
    obj_t *obj;
    char buf[MAX_SOCK_LINE];

    assert(!list_is_empty(req->consoles));

    if (req->command == CONMAN_CMD_QUERY)
        return(0);
    if (list_count(req->consoles) == 1)
        return(0);
    if ((req->command == CONMAN_CMD_CONNECT) && (req->enableBroadcast))
        return(0);

    snprintf(buf, sizeof(buf), "Found %d matching consoles",
        list_count(req->consoles));
    send_rsp(req, CONMAN_ERR_TOO_MANY_CONSOLES, buf);

    /*  FIXME? Replace with single write_n()?
     */
    i = list_iterator_create(req->consoles);
    while ((obj = list_next(i))) {
        strlcpy(buf, obj->name, sizeof(buf));
        strlcat(buf, "\n", sizeof(buf));
        if (write_n(req->sd, buf, strlen(buf)) < 0) {
            log_msg(LOG_NOTICE, "Unable to write to <%s:%d>: %s",
                req->fqdn, req->port, strerror(errno));
            break;
        }
    }
    list_iterator_destroy(i);
    return(-1);
}


static int check_busy_consoles(req_t *req)
{
/*  Checks to see if a "writable" request affects any consoles
 *    that are currently busy (unless the force or join option is enabled).
 *  Returns 0 if the request is valid, or -1 on error.
 */
    List busy;
    ListIterator i;
    obj_t *console;
    obj_t *writer;
    int gotBcast;
    char *tty;
    time_t t;
    char *delta;
    char buf[MAX_LINE];

    assert(!list_is_empty(req->consoles));

    if ((req->command == CONMAN_CMD_QUERY)
      || (req->command == CONMAN_CMD_MONITOR))
        return(0);
    if (req->enableForce || req->enableJoin)
        return(0);

    busy = list_create(NULL);
    i = list_iterator_create(req->consoles);
    while ((console = list_next(i))) {
        assert(is_console_obj(console));
        if (!list_is_empty(console->writers))
            list_append(busy, console);
    }
    list_iterator_destroy(i);

    if (list_is_empty(busy)) {
        list_destroy(busy);
        return(0);
    }

    if (list_count(busy) == 1) {
        snprintf(buf, sizeof(buf), "Found console already in use");
    }
    else {
        snprintf(buf, sizeof(buf), "Found %d consoles already in use",
            list_count(busy));
    }
    send_rsp(req, CONMAN_ERR_BUSY_CONSOLES, buf);

    /*  Note: the "busy" list contains object references,
     *    so they DO NOT get destroyed here when removed from the list.
     */
    while ((console = list_pop(busy))) {

        i = list_iterator_create(console->writers);
        while ((writer = list_next(i))) {

            assert(is_client_obj(writer));
            x_pthread_mutex_lock(&writer->bufLock);
            t = writer->aux.client.timeLastRead;
            gotBcast = list_is_empty(writer->writers);
            tty = writer->aux.client.req->tty;
            x_pthread_mutex_unlock(&writer->bufLock);
            delta = create_time_delta_string(t, -1);

            snprintf(buf, sizeof(buf),
                "Console [%s] open %s by <%s@%s>%s%s (idle %s).\n",
                console->name, (gotBcast ? "B/C" : "R/W"),
                writer->aux.client.req->user, writer->aux.client.req->host,
                (tty ? " on " : ""), (tty ? tty : ""),
                (delta ? delta : "???"));
            buf[sizeof(buf) - 2] = '\n';
            buf[sizeof(buf) - 1] = '\0';
            if (delta)
                free(delta);
            if (write_n(req->sd, buf, strlen(buf)) < 0) {
                log_msg(LOG_NOTICE, "Unable to write to <%s:%d>: %s",
                    req->fqdn, req->port, strerror(errno));
                break;
            }
        }
        list_iterator_destroy(i);
    }
    list_destroy(busy);
    return(-1);
}


static int send_rsp(req_t *req, int errnum, char *errmsg)
{
/*  Sends a response to the given request (req).
 *  If the request is valid and there are no errors,
 *    errnum = CONMAN_ERR_NONE and an "OK" response is sent.
 *  Otherwise, (errnum) identifies the err_type enumeration (in common.h)
 *    and (errmsg) is a string describing the error in more detail.
 *  Returns 0 if the response is sent OK, or -1 on error.
 */
    char buf[MAX_SOCK_LINE] = "";       /* init buf for appending with NUL */
    char tmp[MAX_LINE];                 /* tmp buffer for lex-encoding strs */
    int n;
    ListIterator i;
    obj_t *console;

    assert(req->sd >= 0);
    assert(errnum >= 0);

    if (errnum == CONMAN_ERR_NONE) {

        n = append_format_string(buf, sizeof(buf), "%s",
            LEX_TOK2STR(proto_strs, CONMAN_TOK_OK));
        if (n == -1) {
            goto overrun;
        }
        /*  If consoles have been defined by this point, the "response"
         *    is to the request as opposed to the greeting.
         */
        if (list_count(req->consoles) > 0) {

            if (req->enableReset) {
                n = append_format_string(buf, sizeof(buf), " %s=%s",
                    LEX_TOK2STR(proto_strs, CONMAN_TOK_OPTION),
                    LEX_TOK2STR(proto_strs, CONMAN_TOK_RESET));
                if (n == -1) {
                    goto overrun;
                }
            }
            i = list_iterator_create(req->consoles);
            while ((console = list_next(i))) {
                n = strlcpy(tmp, console->name, sizeof(tmp));
                if ((size_t) n >= sizeof(tmp)) {
                    goto overrun;
                }
                n = append_format_string(buf, sizeof(buf), " %s='%s'",
                    LEX_TOK2STR(proto_strs, CONMAN_TOK_CONSOLE),
                    lex_encode(tmp));
                if (n == -1) {
                    goto overrun;
                }
            }
            list_iterator_destroy(i);
        }

        n = append_format_string(buf, sizeof(buf), "\n");
        if (n == -1) {
            goto overrun;
        }
    }
    else {
        n = strlcpy(tmp, (errmsg ? errmsg : "unspecified error"), sizeof(tmp));
        if ((size_t) n >= sizeof(tmp)) {
            goto overrun;
        }
        n = snprintf(buf, sizeof(buf), "%s %s=%d %s='%s'\n",
            LEX_TOK2STR(proto_strs, CONMAN_TOK_ERROR),
            LEX_TOK2STR(proto_strs, CONMAN_TOK_CODE), errnum,
            LEX_TOK2STR(proto_strs, CONMAN_TOK_MESSAGE), lex_encode(tmp));
        if ((n < 0) || ((size_t) n >= sizeof(buf))) {
            goto overrun;
        }
        log_msg(LOG_NOTICE, "Client <%s@%s:%d> request failed: %s",
            req->user, req->fqdn, req->port, errmsg);
    }

    /*  Write response to client.
     */
    if (write_n(req->sd, buf, strlen(buf)) < 0) {
        log_msg(LOG_NOTICE, "Unable to write to <%s:%d>: %s",
            req->fqdn, req->port, strerror(errno));
        return(-1);
    }

    DPRINTF((5, "Sent response: %s", buf));
    return(0);

overrun:
    log_msg(LOG_WARNING,
        "Client <%s@%s:%d> request terminated due to buffer overrun",
        req->user, req->fqdn, req->port);
    return(-1);
}


static int perform_query_cmd(req_t *req)
{
/*  Performs the QUERY command, returning a list of consoles that
 *    matches the console patterns given in the client's request.
 *  Returns 0 if the command succeeds, or -1 on error.
 *  Since this cmd is processed entirely by this thread,
 *    the client socket connection is closed once it is finished.
 */
    assert(req->sd >= 0);
    assert(req->command == CONMAN_CMD_QUERY);
    assert(!list_is_empty(req->consoles));

    log_msg(LOG_INFO, "Client <%s@%s:%d> issued query",
        req->user, req->fqdn, req->port);

    if (send_rsp(req, CONMAN_ERR_NONE, NULL) < 0) {
        return(-1);
    }
    destroy_req(req);
    return(0);
}


static int perform_monitor_cmd(req_t *req, server_conf_t *conf)
{
/*  Performs the MONITOR command, placing the client in a
 *    "read-only" session with a single console.
 *  Returns 0 if the command succeeds, or -1 on error.
 */
    obj_t *client;
    obj_t *console;

    assert(req->sd >= 0);
    assert(req->command == CONMAN_CMD_MONITOR);
    assert(list_count(req->consoles) == 1);

    if (send_rsp(req, CONMAN_ERR_NONE, NULL) < 0) {
        return(-1);
    }
    client = create_client_obj(conf, req);
    console = list_peek(req->consoles);
    assert(is_console_obj(console));
    link_objs(console, client);
    check_console_state(console, client);

    log_msg(LOG_INFO, "Client <%s@%s:%d> connected to [%s] (read-only)",
        req->user, req->fqdn, req->port, console->name);

    return(0);
}


static int perform_connect_cmd(req_t *req, server_conf_t *conf)
{
/*  Performs the CONNECT command.  If a single console is specified,
 *    the client is placed in a "read-write" session with that console.
 *    Otherwise, the client is placed in a "write-only" broadcast session
 *    affecting multiple consoles.
 *  Returns 0 if the command succeeds, or -1 on error.
 */
    obj_t *client;
    obj_t *console;
    ListIterator i;

    assert(req->sd >= 0);
    assert(req->command == CONMAN_CMD_CONNECT);

    if (send_rsp(req, CONMAN_ERR_NONE, NULL) < 0) {
        return(-1);
    }
    client = create_client_obj(conf, req);

    if (list_count(req->consoles) == 1) {
        /*
         *  Unicast connection (R/W).
         */
        console = list_peek(req->consoles);
        assert(is_console_obj(console));
        link_objs(client, console);
        link_objs(console, client);
        check_console_state(console, client);

        log_msg(LOG_INFO, "Client <%s@%s:%d> connected to [%s]",
            req->user, req->fqdn, req->port, console->name);
    }
    else {
        /*
         *  Broadcast connection (W/O).
         */
        i = list_iterator_create(req->consoles);
        while ((console = list_next(i))) {
            assert(is_console_obj(console));
            link_objs(client, console);
            check_console_state(console, client);
        }
        list_iterator_destroy(i);

        log_msg(LOG_INFO,
            "Client <%s@%s:%d> connected to %d consoles (broadcast)",
            req->user, req->fqdn, req->port, list_count(req->consoles));
    }
    return(0);
}


static void check_console_state(obj_t *console, obj_t *client)
{
/*  Checks the state of the console and warns the client if needed.
 *  Informs the newly-connected client if strange things are afoot.
 *  Attempts an immediate reconnect if the console connection is down.
 */
    char buf[MAX_LINE];

    assert(is_console_obj(console));
    assert(is_client_obj(client));

    if (is_process_obj(console) && (console->fd < 0)) {
        snprintf(buf, sizeof(buf),
            "%sConsole [%s] is currently disconnected from \"%s\"%s",
            CONMAN_MSG_PREFIX, console->name, console->aux.process.prog,
            CONMAN_MSG_SUFFIX);
        strcpy(&buf[sizeof(buf) - 3], "\r\n");
        write_obj_data(client, buf, strlen(buf), 1);
        open_process_obj(console);
    }
    else if (is_serial_obj(console) && (console->fd < 0)) {
        snprintf(buf, sizeof(buf),
            "%sConsole [%s] is currently disconnected from \"%s\"%s",
            CONMAN_MSG_PREFIX, console->name, console->aux.serial.dev,
            CONMAN_MSG_SUFFIX);
        strcpy(&buf[sizeof(buf) - 3], "\r\n");
        write_obj_data(client, buf, strlen(buf), 1);
        open_serial_obj(console);
    }
    else if (is_telnet_obj(console)
            && (console->aux.telnet.state != CONMAN_TELNET_UP)) {
        snprintf(buf, sizeof(buf),
            "%sConsole [%s] is currently disconnected from <%s:%d>%s",
            CONMAN_MSG_PREFIX, console->name, console->aux.telnet.host,
            console->aux.telnet.port, CONMAN_MSG_SUFFIX);
        strcpy(&buf[sizeof(buf) - 3], "\r\n");
        write_obj_data(client, buf, strlen(buf), 1);
        console->aux.telnet.delay = TELNET_MIN_TIMEOUT;
        /*
         *  Do not call connect_telnet_obj() while in the PENDING state since
         *    it would be misinterpreted as the completion of the non-blocking
         *    connect().
         */
        if (console->aux.telnet.state == CONMAN_TELNET_DOWN) {
            open_telnet_obj(console);
        }
    }
    else if (is_unixsock_obj(console) && (console->fd < 0)) {
        assert(console->aux.unixsock.state == CONMAN_UNIXSOCK_DOWN);
        snprintf(buf, sizeof(buf),
            "%sConsole [%s] is currently disconnected from \"%s\"%s",
            CONMAN_MSG_PREFIX, console->name, console->aux.unixsock.dev,
            CONMAN_MSG_SUFFIX);
        strcpy(&buf[sizeof(buf) - 3], "\r\n");
        write_obj_data(client, buf, strlen(buf), 1);
        open_unixsock_obj(console);
    }
#if WITH_FREEIPMI
    else if (is_ipmi_obj(console)
            && (console->aux.ipmi.state != CONMAN_IPMI_UP)) {
        snprintf(buf, sizeof(buf),
            "%sConsole [%s] is currently disconnected from <%s>%s",
            CONMAN_MSG_PREFIX, console->name, console->aux.ipmi.host,
            CONMAN_MSG_SUFFIX);
        strcpy(&buf[sizeof(buf) - 3], "\r\n");
        write_obj_data(client, buf, strlen(buf), 1);
        if (console->aux.ipmi.state == CONMAN_IPMI_DOWN) {
            open_ipmi_obj(console);
        }
    }
#endif /* WITH_FREEIPMI */
    return;
}
