/******************************************************************************\
 *  $Id: server-sock.c,v 1.23 2001/08/02 19:39:37 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
\******************************************************************************/


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fnmatch.h>
#include <pthread.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "common.h"
#include "errors.h"
#include "lex.h"
#include "server.h"
#include "util.h"


static void resolve_req_addr(req_t *req, int sd);
static int recv_greeting(req_t *req);
static int parse_greeting(Lex l, req_t *req);
static int recv_req(req_t *req);
static int parse_cmd_opts(Lex l, req_t *req);
static int query_consoles(server_conf_t *conf, req_t *req);
static int query_consoles_via_globbing(
    server_conf_t *conf, req_t *req, List matches);
static int query_consoles_via_regex(
    server_conf_t *conf, req_t *req, List matches);
static int validate_req(req_t *req);
static int check_too_many_consoles(req_t *req);
static int check_busy_consoles(req_t *req);
static int send_rsp(req_t *req, int errnum, char *errmsg);
static void perform_query_cmd(req_t *req);
static void perform_monitor_cmd(req_t *req, server_conf_t *conf);
static void perform_connect_cmd(req_t *req, server_conf_t *conf);


void process_client(client_arg_t *args)
{
/*  The thread responsible for accepting a client connection
 *    and processing the request.
 *  The QUERY cmd is processed entirely by this thread.
 *  The MONITOR and CONNECT cmds are setup and then placed
 *    in the conf->objs list to be handled by mux_io().
 */
    int rc;
    int sd;
    server_conf_t *conf;
    req_t *req;

    assert(args);
    sd = args->sd;
    conf = args->conf;
    free(args);

    DPRINTF("Processing new client.\n");

    if ((rc = pthread_detach(pthread_self())) != 0)
        err_msg(rc, "pthread_detach() failed");

    if (!(req = create_req())) {
        if (close(sd) < 0)
            err_msg(errno, "close() failed on fd=%d", sd);
        return;
    }
    resolve_req_addr(req, sd);

    if (recv_greeting(req) < 0)
        goto err;
    if (recv_req(req) < 0)
        goto err;
    if (query_consoles(conf, req) < 0)
        goto err;
    if (validate_req(req) < 0)
        goto err;

    switch(req->command) {
    case CONNECT:
        perform_connect_cmd(req, conf);
        break;
    case MONITOR:
        perform_monitor_cmd(req, conf);
        break;
    case QUERY:
        perform_query_cmd(req);
        break;
    default:
        /*  This should not happen, as invalid commands
         *    will be detected by validate_req().
         */
        log_msg(0, "INTERNAL ERROR: Received invalid command (%d) at %s:%d",
            req->command, __FILE__, __LINE__);
        break;
    }
    return;

err:
    destroy_req(req);
    return;
}


static void resolve_req_addr(req_t *req, int sd)
{
/*  Resolves the network information associated with the
 *    peer at the other end of the socket connection.
 */
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    char buf[MAX_LINE];
    char *p;

    assert(sd >= 0);

    req->sd = sd;
    if (getpeername(sd, (struct sockaddr *) &addr, &addrlen) < 0)
        err_msg(errno, "getpeername() failed");
    if (!inet_ntop(AF_INET, &addr.sin_addr, buf, sizeof(buf)))
        err_msg(errno, "inet_ntop() failed");
    req->port = ntohs(addr.sin_port);
    req->ip = create_string(buf);
    /*
     *  Attempt to resolve IP address.  If it succeeds, buf contains
     *    host string; if it fails, buf is unchanged with IP addr string.
     *    Either way, copy buf to prevents having to code everything as
     *    (req->host ? req->host : req->ip).
     */
    if ((get_hostname_by_addr(&addr.sin_addr, buf, sizeof(buf)))) {
        req->fqdn = create_string(buf);
        if ((p = strchr(buf, '.')))
            *p = '\0';
        req->host = create_string(buf);
    }
    else {
        req->fqdn = create_string(buf);
        req->host = create_string(buf);
    }
    return;
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
        log_msg(0, "Error reading greeting from %s: %s",
            req->ip, strerror(errno));
        return(-1);
    }
    else if (n == 0) {
        log_msg(0, "Connection terminated by %s", req->ip);
        return(-1);
    }

    DPRINTF("Received greeting: %s", buf); /* xyzzy */

    if (!(l = lex_create(buf, proto_strs))) {
        send_rsp(req, CONMAN_ERR_NO_RESOURCES,
            "Insufficient resources to process request.");
        return(-1);
    }
    while (!done) {
        tok = lex_next(l);
        switch(tok) {
        case CONMAN_TOK_HELLO:
            done = parse_greeting(l, req);
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
    if (done == -1) {
        send_rsp(req, CONMAN_ERR_NO_RESOURCES,
            "Insufficient resources to process request.");
        return(-1);
    }
    if (!req->user) {
        send_rsp(req, CONMAN_ERR_BAD_REQUEST, 
            "Invalid greeting: no user specified");
        return(-1);
    }
#ifdef NDEBUG
    if (strcmp(req->ip, "127.0.0.1")) {
        send_rsp(req, CONMAN_ERR_AUTHENTICATE,
            "Authentication required (but not yet implemented)");
        return(-1);
    }
#endif /* NDEBUG */

    DPRINTF("Received request from <%s@%s>.\n", req->user, req->host);

    /*  Send response to greeting.
     */
    return(send_rsp(req, CONMAN_ERR_NONE, NULL));
}


static int parse_greeting(Lex l, req_t *req)
{
/*  Parses the "HELLO" command from the client:
 *    HELLO USER='<str>' TTY='<str>'
 *  Returns 0 on success, or -1 on error (ie, out of memory).
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
                if (!(req->user = lex_decode(strdup(lex_text(l)))))
                    return(-1);
            }
            break;
        case CONMAN_TOK_TTY:
            if ((lex_next(l) == '=') && (lex_next(l) == LEX_STR)
              && (*lex_text(l) != '\0')) {
                if (req->tty)
                    free(req->tty);
                if (!(req->tty = lex_decode(strdup(lex_text(l)))))
                    return(-1);
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
    return(0);
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
        log_msg(0, "Error reading request from %s: %s",
            req->ip, strerror(errno));
        return(-1);
    }
    else if (n == 0) {
        log_msg(0, "Connection terminated by %s", req->ip);
        return(-1);
    }

    DPRINTF("Received request: %s", buf); /* xyzzy */

    if (!(l = lex_create(buf, proto_strs))) {
        send_rsp(req, CONMAN_ERR_NO_RESOURCES,
            "Insufficient resources to process request.");
        return(-1);
    }
    while (!done) {
        tok = lex_next(l);
        switch(tok) {
        case CONMAN_TOK_CONNECT:
            req->command = CONNECT;
            done = parse_cmd_opts(l, req);
            break;
        case CONMAN_TOK_MONITOR:
            req->command = MONITOR;
            done = parse_cmd_opts(l, req);
            break;
        case CONMAN_TOK_QUERY:
            req->command = QUERY;
            done = parse_cmd_opts(l, req);
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

    if (done == -1) {
        send_rsp(req, CONMAN_ERR_NO_RESOURCES,
            "Insufficient resources to process request.");
        return(-1);
    }
    return(0);
}


static int parse_cmd_opts(Lex l, req_t *req)
{
/*  Parses the command options for the given request.
 *  Returns 0 on success, or -1 on error (ie, out of memory).
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
                if (!(str = lex_decode(strdup(lex_text(l)))))
                    return(-1);
                if (!list_append(req->consoles, str)) {
                    free(str);
                    return(-1);
                }
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
    return(0);
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

    if (list_is_empty(req->consoles) && (req->command != QUERY))
        return(0);

    /*  The NULL destructor is used for 'matches' because the matches list
     *    will only contain refs to objs contained in the conf->objs list.
     *    These objs will be destroyed when the conf->objs list is destroyed.
     */
    if (!(matches = list_create(NULL)))
        err_msg(0, "Out of memory");

    if (req->enableRegex)
        rc = query_consoles_via_regex(conf, req, matches);
    else
        rc = query_consoles_via_globbing(conf, req, matches);

    /*  Replace original list of strings with list of obj_t's.
     */
    list_destroy(req->consoles);
    req->consoles = matches;
    list_sort(req->consoles, (ListCmpF) compare_objs);

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
        if (!list_append(req->consoles, p))
            err_msg(0, "Out of memory");
    }

    if (!(i = list_iterator_create(req->consoles)))
        err_msg(0, "Out of memory");
    if (!(j = list_iterator_create(conf->objs)))
        err_msg(0, "Out of memory");

    /*  Search objs for console names matching console patterns in the request.
     */
    while ((pat = list_next(i))) {
        list_iterator_reset(j);
        while ((obj = list_next(j))) {
            if (obj->type != CONSOLE)
                continue;
            if (!fnmatch(pat, obj->name, 0)
              && !list_find_first(matches, (ListFindF) find_obj, obj)) {
                if (!list_append(matches, obj))
                    err_msg(0, "Out of memory");
            }
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
        if (!list_append(req->consoles, p))
            err_msg(0, "Out of memory");
    }

    /*  Combine console patterns via alternation to create single regex.
     */
    if (!(i = list_iterator_create(req->consoles)))
        err_msg(0, "Out of memory");
    strlcpy(buf, list_next(i), sizeof(buf));
    while ((p = list_next(i))) {
        strlcat(buf, "|", sizeof(buf));
        strlcat(buf, p, sizeof(buf));
    }
    list_iterator_destroy(i);

    /*  Compile regex for searching server's console objs.
     */
    rc = regcomp(&rex, buf, REG_EXTENDED | REG_ICASE);
    if (rc != 0) {
        if (regerror(rc, &rex, buf, sizeof(buf)) > sizeof(buf))
            log_msg(10, "Buffer overflow during regerror()");
        regfree(&rex);
        send_rsp(req, CONMAN_ERR_BAD_REGEX, buf);
        return(-1);
    }

    if (!(i = list_iterator_create(conf->objs)))
        err_msg(0, "Out of memory");

    /*  Search objs for console names matching console patterns in the request.
     */
    while ((obj = list_next(i))) {
        if (obj->type != CONSOLE)
            continue;
        if (!regexec(&rex, obj->name, 1, &match, 0)
          && (match.rm_so == 0) && (match.rm_eo == strlen(obj->name))) {
            if (!list_append(matches, obj))
                err_msg(0, "Out of memory");
        }
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
        send_rsp(req, CONMAN_ERR_NO_CONSOLES, "Found no matching consoles.");
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

    if (req->command == QUERY)
        return(0);
    if (list_count(req->consoles) == 1)
        return(0);
    if ((req->command == CONNECT) && (req->enableBroadcast))
        return(0);

    if (!(i = list_iterator_create(req->consoles))) {
        send_rsp(req, CONMAN_ERR_NO_RESOURCES,
            "Insufficient resources to process request.");
        return(-1);
    }
    snprintf(buf, sizeof(buf), "Found %d matching consoles.",
        list_count(req->consoles));
    send_rsp(req, CONMAN_ERR_TOO_MANY_CONSOLES, buf);

    /*  FIX_ME? Replace with single write_n()?
     */
    while ((obj = list_next(i))) {
        strlcpy(buf, obj->name, sizeof(buf));
        strlcat(buf, "\n", sizeof(buf));
        if (write_n(req->sd, buf, strlen(buf)) < 0) {
            log_msg(0, "Error writing to %s: %s", req->ip, strerror(errno));
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
    int rc;
    time_t t;
    char *delta;
    char buf[MAX_LINE];

    assert(!list_is_empty(req->consoles));

    if ((req->command == QUERY) || (req->command == MONITOR))
        return(0);
    if (req->enableForce || req->enableJoin)
        return(0);

    if (!(busy = list_create(NULL)))
        err_msg(0, "Out of memory");
    if (!(i = list_iterator_create(req->consoles)))
        err_msg(0, "Out of memory");
    while ((console = list_next(i))) {
        assert(console->type == CONSOLE);
        if (!list_is_empty(console->writers))
            if (!list_append(busy, console))
                err_msg(0, "Out of memory");
    }
    list_iterator_destroy(i);

    if (list_is_empty(busy)) {
        list_destroy(busy);
        return(0);
    }

    if (list_count(busy) == 1) {
        snprintf(buf, sizeof(buf), "Found console already in use.");
    }
    else {
        snprintf(buf, sizeof(buf), "Found %d consoles already in use.",
            list_count(busy));
    }
    send_rsp(req, CONMAN_ERR_BUSY_CONSOLES, buf);

    /*  Note: the "busy" list contains object references,
     *    so they DO NOT get destroyed here when removed from the list.
     */
    while ((console = list_pop(busy))) {

        if (!(i = list_iterator_create(console->writers)))
            err_msg(0, "Out of memory");
        while ((writer = list_next(i))) {

            assert(writer->type == CLIENT);

            if ((rc = pthread_mutex_lock(&writer->bufLock)) != 0)
                err_msg(rc, "pthread_mutex_lock() failed for [%s]",
                    writer->name);
            t = writer->aux.client.timeLastRead;
            gotBcast = list_is_empty(writer->writers);
            tty = writer->aux.client.req->tty;
            if ((rc = pthread_mutex_unlock(&writer->bufLock)) != 0)
                err_msg(rc, "pthread_mutex_unlock() failed for [%s]",
                    writer->name);
            delta = create_time_delta_string(t);

            snprintf(buf, sizeof(buf),
                "Console [%s] open %s by %s@%s%s%s (idle %s).\n",
                console->name, (gotBcast ? "B/C" : "R/W"),
                writer->aux.client.req->user, writer->aux.client.req->host,
                (tty ? " on " : ""), (tty ? tty : ""), (delta ? delta : "???"));
            buf[sizeof(buf) - 2] = '\n';
            buf[sizeof(buf) - 1] = '\0';
            if (delta)
                free(delta);
            if (write_n(req->sd, buf, strlen(buf)) < 0) {
                log_msg(0, "Error writing to %s: %s", req->ip, strerror(errno));
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
 *  Otherwise, (errnum) identifies the err_type enumeration (in conman.h)
 *    and (errmsg) is a string describing the error in more detail.
 *  Returns 0 if the response is sent OK, or -1 on error.
 */
    char buf[MAX_SOCK_LINE];
    char tmp[MAX_LINE];			/* tmp buffer for lex-encoding strs */
    int n;

    assert(req->sd >= 0);
    assert(errnum >= 0);

    if (errnum == CONMAN_ERR_NONE) {

        char *ptr = buf;
        int len = sizeof(buf) - 2;	/* reserve space for trailing "\n\0" */
        ListIterator i;
        obj_t *console;

        n = snprintf(buf, sizeof(buf), "%s",
            proto_strs[LEX_UNTOK(CONMAN_TOK_OK)]);
        assert(n >= 0 && n < len);
        ptr += n;
        len -= n;

        /*  Note: if buf[] is overflowed by console strings,
         *    a valid rsp will still be sent to the client,
         *    but some console named may be omitted.
         */
        if ((i = list_iterator_create(req->consoles))) {
            while ((console = list_next(i))) {
                n = strlcpy(tmp, console->name, sizeof(tmp));
                assert(n >= 0 && n < sizeof(tmp));
                n = snprintf(ptr, len, " %s='%s'",
                    proto_strs[LEX_UNTOK(CONMAN_TOK_CONSOLE)], lex_encode(tmp));
                if (n < 0 || n >= len)
                    break;
                ptr += n;
                len -= n;
            }
            list_iterator_destroy(i);
        }

        *ptr++ = '\n';
        *ptr++ = '\0';
    }
    else {
    /*
     *  FIX_ME? Should all errors be logged?
     */
        n = strlcpy(tmp, (errmsg ? errmsg : "Doh!"), sizeof(tmp));
        assert(n >= 0 && n < sizeof(tmp));
        n = snprintf(buf, sizeof(buf), "%s %s=%d %s='%s'\n",
            proto_strs[LEX_UNTOK(CONMAN_TOK_ERROR)],
            proto_strs[LEX_UNTOK(CONMAN_TOK_CODE)], errnum,
            proto_strs[LEX_UNTOK(CONMAN_TOK_MESSAGE)], lex_encode(tmp));
        assert(n >= 0 && n < sizeof(buf));
    }

    /*  Write response to client.
     */
    if (write_n(req->sd, buf, strlen(buf)) < 0) {
        log_msg(0, "Error writing to %s: %s", req->ip, strerror(errno));
        return(-1);
    }

    DPRINTF("Sent response: %s", buf); /* xyzzy */
    return(0);
}


static void perform_query_cmd(req_t *req)
{
/*  Performs the QUERY command, returning a list of consoles that
 *    matches the console patterns given in the client's request.
 *  Since this cmd is processed entirely by this thread,
 *    the client socket connection is closed once it is finished.
 */
    assert(req->sd >= 0);
    assert(req->command == QUERY);
    assert(!list_is_empty(req->consoles));

    send_rsp(req, CONMAN_ERR_NONE, NULL);
    destroy_req(req);
    return;
}


static void perform_monitor_cmd(req_t *req, server_conf_t *conf)
{
/*  Performs the MONITOR command, placing the client in a
 *    "read-only" session with a single console.
 */
    obj_t *client;
    obj_t *console;

    assert(req->sd >= 0);
    assert(req->command == MONITOR);
    assert(list_count(req->consoles) == 1);

    send_rsp(req, CONMAN_ERR_NONE, NULL);
    client = create_client_obj(conf->objs, req);
    console = list_peek(req->consoles);
    assert(console->type == CONSOLE);
    link_objs(console, client);
    return;
}


static void perform_connect_cmd(req_t *req, server_conf_t *conf)
{
/*  Performs the CONNECT command.  If a single console is specified,
 *    the client is placed in a "read-write" session with that console.
 *    Otherwise, the client is placed in a "write-only" broadcast session
 *    affecting multiple consoles.
 */
    obj_t *client;
    obj_t *console;
    ListIterator i;

    assert(req->sd >= 0);
    assert(req->command == CONNECT);

    send_rsp(req, CONMAN_ERR_NONE, NULL);
    client = create_client_obj(conf->objs, req);

    if (list_count(req->consoles) == 1) {
        /*
         *  Unicast connection (R/W).
         */
        console = list_peek(req->consoles);
        assert(console->type == CONSOLE);
        link_objs(client, console);
        link_objs(console, client);
    }
    else {
        /*
         *  Broadcast connection (W/O).
         */
        if (!(i = list_iterator_create(req->consoles)))
            err_msg(0, "Out of memory");
        while ((console = list_next(i))) {
            assert(console->type == CONSOLE);
            link_objs(client, console);
        }
        list_iterator_destroy(i);
    }
    return;
}
