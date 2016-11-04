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
#include <sys/types.h>                  /* include before in.h for bsd */
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "client.h"
#include "common.h"
#include "lex.h"
#include "log.h"
#include "util-file.h"
#include "util-net.h"
#include "util-str.h"


static void parse_rsp_ok(Lex l, client_conf_t *conf);
static void parse_rsp_err(Lex l, client_conf_t *conf);


int connect_to_server(client_conf_t *conf)
{
    int sd;
    struct sockaddr_in saddr;
    char buf[MAX_LINE];
    char *p;

    assert(conf->req->host != NULL);
    assert(conf->req->port > 0);

    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        log_err(errno, "Unable to create socket");

    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(conf->req->port);
    if (host_name_to_addr4(conf->req->host, &saddr.sin_addr) < 0) {
        conf->errnum = CONMAN_ERR_LOCAL;
        conf->errmsg = create_format_string(
            "Unable to resolve host <%s>", conf->req->host);
        (void) close(sd);
        return(-1);
    }

    if (host_name_to_cname(conf->req->host, buf, sizeof(buf)) == NULL) {
        conf->req->fqdn = create_string(conf->req->host);
    }
    else {
        conf->req->fqdn = create_string(buf);
        free(conf->req->host);
        if ((p = strchr(buf, '.')))
            *p = '\0';
        conf->req->host = create_string(buf);
    }

    if (connect(sd, (struct sockaddr *) &saddr, sizeof(saddr)) < 0) {
        conf->errnum = CONMAN_ERR_LOCAL;
        conf->errmsg = create_format_string(
            "Unable to connect to <%s:%d>: %s",
            conf->req->fqdn, conf->req->port, strerror(errno));
        (void) close(sd);
        return(-1);
    }
    conf->req->sd = sd;
    return(0);
}


int send_greeting(client_conf_t *conf)
{
    char buf[MAX_SOCK_LINE] = "";       /* init buf for appending with NUL */
    int n;

    assert(conf->req->sd >= 0);
    assert(conf->req->user != NULL);

    n = append_format_string(buf, sizeof(buf), "%s %s='%s'",
        LEX_TOK2STR(proto_strs, CONMAN_TOK_HELLO),
        LEX_TOK2STR(proto_strs, CONMAN_TOK_USER),
        lex_encode(conf->req->user));

    if (conf->req->tty) {
        n = append_format_string(buf, sizeof(buf), " %s='%s'",
            LEX_TOK2STR(proto_strs, CONMAN_TOK_TTY),
            lex_encode(conf->req->tty));
    }

    n = append_format_string(buf, sizeof(buf), "\n");

    if (n < 0) {
        conf->errnum = CONMAN_ERR_LOCAL;
        conf->errmsg = create_string(
            "Overran request buffer for sending greeting");
        return(-1);
    }

    if (write_n(conf->req->sd, buf, strlen(buf)) < 0) {
        conf->errnum = CONMAN_ERR_LOCAL;
        conf->errmsg = create_format_string(
            "Unable to send greeting to <%s:%d>: %s",
            conf->req->host, conf->req->port, strerror(errno));
        return(-1);
    }

    if (recv_rsp(conf) < 0) {
        if (conf->errnum == CONMAN_ERR_AUTHENTICATE) {
            /*
             *  FIXME: NOT_IMPLEMENTED_YET
             */
            if (close(conf->req->sd) < 0)
                log_err(errno, "Unable to close connection to <%s:%d>",
                    conf->req->host, conf->req->port);
            conf->req->sd = -1;
        }
        return(-1);
    }
    return(0);
}


int send_req(client_conf_t *conf)
{
    char buf[MAX_SOCK_LINE] = "";       /* init buf for appending with NUL */
    int n;
    char *cmd = NULL;
    char *str;

    assert(conf->req->sd >= 0);

    switch(conf->req->command) {
    case CONMAN_CMD_QUERY:
        cmd = LEX_TOK2STR(proto_strs, CONMAN_TOK_QUERY);
        break;
    case CONMAN_CMD_MONITOR:
        cmd = LEX_TOK2STR(proto_strs, CONMAN_TOK_MONITOR);
        break;
    case CONMAN_CMD_CONNECT:
        cmd = LEX_TOK2STR(proto_strs, CONMAN_TOK_CONNECT);
        break;
    default:
        log_err(0, "INTERNAL: Invalid command=%d", conf->req->command);
        break;
    }

    n = append_format_string(buf, sizeof(buf), "%s", cmd);

    if (conf->req->enableQuiet) {
        n = append_format_string(buf, sizeof(buf), " %s=%s",
            LEX_TOK2STR(proto_strs, CONMAN_TOK_OPTION),
            LEX_TOK2STR(proto_strs, CONMAN_TOK_QUIET));
    }
    if (conf->req->enableRegex) {
        n = append_format_string(buf, sizeof(buf), " %s=%s",
            LEX_TOK2STR(proto_strs, CONMAN_TOK_OPTION),
            LEX_TOK2STR(proto_strs, CONMAN_TOK_REGEX));
    }
    if (conf->req->command == CONMAN_CMD_CONNECT) {
        if (conf->req->enableForce) {
            n = append_format_string(buf, sizeof(buf), " %s=%s",
                LEX_TOK2STR(proto_strs, CONMAN_TOK_OPTION),
                LEX_TOK2STR(proto_strs, CONMAN_TOK_FORCE));
        }
        if (conf->req->enableJoin) {
            n = append_format_string(buf, sizeof(buf), " %s=%s",
                LEX_TOK2STR(proto_strs, CONMAN_TOK_OPTION),
                LEX_TOK2STR(proto_strs, CONMAN_TOK_JOIN));
        }
        if (conf->req->enableBroadcast) {
            n = append_format_string(buf, sizeof(buf), " %s=%s",
                LEX_TOK2STR(proto_strs, CONMAN_TOK_OPTION),
                LEX_TOK2STR(proto_strs, CONMAN_TOK_BROADCAST));
        }
    }

    /*  Empty the consoles list here because it will be filled in
     *    with the actual console names in recv_rsp().
     */
    while ((str = list_pop(conf->req->consoles))) {
        n = append_format_string(buf, sizeof(buf), " %s='%s'",
            LEX_TOK2STR(proto_strs, CONMAN_TOK_CONSOLE),
            lex_encode(str));
        free(str);
    }

    n = append_format_string(buf, sizeof(buf), "\n");

    if (n < 0) {
        conf->errnum = CONMAN_ERR_LOCAL;
        conf->errmsg = create_string("Overran request buffer");
        return(-1);
    }

    if (write_n(conf->req->sd, buf, strlen(buf)) < 0) {
        conf->errnum = CONMAN_ERR_LOCAL;
        conf->errmsg = create_format_string(
            "Unable to send greeting to <%s:%d>: %s",
            conf->req->host, conf->req->port, strerror(errno));
        return(-1);
    }

    /*  For QUERY commands, the write-half of the socket
     *    connection can be closed once the request is sent.
     */
    if (conf->req->command == CONMAN_CMD_QUERY) {
        if (shutdown(conf->req->sd, SHUT_WR) < 0) {
            conf->errnum = CONMAN_ERR_LOCAL;
            conf->errmsg = create_format_string(
                "Unable to close write-half of connection to <%s:%d>: %s",
                conf->req->host, conf->req->port, strerror(errno));
            return(-1);
        }
    }
    return(0);
}


int recv_rsp(client_conf_t *conf)
{
    char buf[MAX_SOCK_LINE];
    int n;
    Lex l;
    int done = 0;
    int tok;

    assert(conf->req->sd >= 0);

    if ((n = read_line(conf->req->sd, buf, sizeof(buf))) < 0) {
        conf->errnum = CONMAN_ERR_LOCAL;
        conf->errmsg = create_format_string("Unable to read response"
            " from <%s:%d>:\n  %s (blocked by TCP-Wrappers?)",
            conf->req->host, conf->req->port, strerror(errno));
        return(-1);
    }
    else if (n == 0) {
        conf->errnum = CONMAN_ERR_LOCAL;
        conf->errmsg = create_format_string("Connection terminated by <%s:%d>",
            conf->req->host, conf->req->port);
        return(-1);
    }

    l = lex_create(buf, proto_strs);
    while (!done) {
        tok = lex_next(l);
        switch(tok) {
        case CONMAN_TOK_OK:             /* OK, so ignore rest of line */
            parse_rsp_ok(l, conf);
            done = 1;
            break;
        case CONMAN_TOK_ERROR:
            parse_rsp_err(l, conf);
            done = -1;
            break;
        case LEX_EOF:
        case LEX_EOL:
            done = -1;
            break;
        default:                        /* ignore unrecognized tokens */
            break;
        }
    }
    lex_destroy(l);

    if (done == 1)
        return(0);
    if (conf->errnum == CONMAN_ERR_NONE) {
        conf->errnum = CONMAN_ERR_LOCAL;
        conf->errmsg = create_format_string("Received invalid reponse from"
            " <%s:%d>", conf->req->host, conf->req->port);
    }
    return(-1);
}


static void parse_rsp_ok(Lex l, client_conf_t *conf)
{
    int tok;
    int done = 0;
    char *str;

    while (!done) {
        tok = lex_next(l);
        switch (tok) {
        case CONMAN_TOK_CONSOLE:
            if ((lex_next(l) == '=') && (lex_next(l) == LEX_STR)) {
                if ((str = lex_decode(create_string(lex_text(l)))))
                    list_append(conf->req->consoles, str);
            }
            break;
        case CONMAN_TOK_OPTION:
            if (lex_next(l) == '=') {
                if (lex_next(l) == CONMAN_TOK_RESET)
                    conf->req->enableReset = 1;
            }
            break;
        case LEX_EOF:
        case LEX_EOL:
            done = 1;
            break;
        default:
            break;                      /* ignore unrecognized tokens */
        }
    }
    return;
}


static void parse_rsp_err(Lex l, client_conf_t *conf)
{
    int tok;
    int done = 0;
    int err = 0;
    char buf[MAX_LINE] = "";

    while (!done) {
        tok = lex_next(l);
        switch (tok) {
        case CONMAN_TOK_CODE:
            if ((lex_next(l) == '=') && (lex_next(l) == LEX_INT))
                err = atoi(lex_text(l));
            break;
        case CONMAN_TOK_MESSAGE:
            if ((lex_next(l) == '=') && (lex_next(l) == LEX_STR))
                strlcpy(buf, lex_text(l), sizeof(buf));
            break;
        case LEX_EOF:
        case LEX_EOL:
            done = 1;
            break;
        default:
            break;                      /* ignore unrecognized tokens */
        }
    }
    conf->errnum = err;
    if (*buf)
        conf->errmsg = lex_decode(create_string(buf));
    return;
}


void display_error(client_conf_t *conf)
{
    char *p;

    assert(conf->errnum > 0);

    p = create_format_string("ERROR: %s.\n\n",
        (conf->errmsg ? conf->errmsg : "Unspecified"));
    if (write_n(STDERR_FILENO, p, strlen(p)) < 0)
        log_err(errno, "Unable to write to stderr");
    if (conf->logd >= 0)
        if (write_n(conf->logd, p, strlen(p)) < 0)
            log_err(errno, "Unable to write to \"%s\"", conf->log);
    free(p);

    if (conf->errnum != CONMAN_ERR_LOCAL)
        display_data(conf, STDERR_FILENO);

    if ((conf->errnum == CONMAN_ERR_TOO_MANY_CONSOLES)
      && (!conf->req->enableBroadcast))
        p = "\nDo you want to broadcast (-b) to multiple consoles?\n\n";
    else if ((conf->errnum == CONMAN_ERR_BUSY_CONSOLES)
      && ((!conf->req->enableForce) && (!conf->req->enableJoin)))
        p = "\nDo you want to force (-f) or join (-j) the connection?\n\n";
    else
        p = NULL;

    if (p) {
        if (write_n(STDERR_FILENO, p, strlen(p)) < 0)
            log_err(errno, "Unable to write to stderr");
        if (conf->logd >= 0)
            if (write_n(conf->logd, p, strlen(p)) < 0)
                log_err(errno, "Unable to write to \"%s\"", conf->log);
    }

    exit(2);
}


void display_data(client_conf_t *conf, int fd)
{
    char buf[MAX_BUF_SIZE];
    int n;

    assert(fd >= 0);

    if (conf->req->sd < 0)
        return;

    for (;;) {
        n = read(conf->req->sd, buf, sizeof(buf));
        if (n < 0)
            log_err(errno, "Unable to read from <%s:%d>",
                conf->req->host, conf->req->port);
        if (n == 0)
            break;
        if (write_n(fd, buf, n) < 0)
            log_err(errno, "Unable to write to fd=%d", fd);
        if (conf->logd >= 0)
            if (write_n(conf->logd, buf, n) < 0)
                log_err(errno, "Unable to write to \"%s\"", conf->log);
    }
    return;
}


void display_consoles(client_conf_t *conf, int fd)
{
    ListIterator i;
    char *p;
    char buf[MAX_LINE];
    int n;

    i = list_iterator_create(conf->req->consoles);
    while ((p = list_next(i))) {
        n = snprintf(buf, sizeof(buf), "%s\n", p);
        if ((n < 0) || ((size_t) n >= sizeof(buf)))
            log_err(0, "Got console list buffer overrun");
        if (write_n(fd, buf, n) < 0)
            log_err(errno, "Unable to write to fd=%d", fd);
        if (conf->logd >= 0)
            if (write_n(conf->logd, buf, n) < 0)
                log_err(errno, "Unable to write to \"%s\"", conf->log);
    }
    list_iterator_destroy(i);
    return;
}
