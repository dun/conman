/******************************************************************************\
 *  server-sock.c  
 *    by Chris Dunlap <cdunlap@llnl.gov>
 *
 *  $Id: server-sock.c,v 1.1 2001/05/04 15:26:41 dun Exp $
\******************************************************************************/


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "conman.h"
#include "errors.h"
#include "server.h"
#include "util.h"


typedef struct request {
    int    sd;
    char  *ip;
    char  *host;
    char  *user;
    cmd_t  command;
    List   consoles;
    char  *program;
    int    enableBroadcast;
    int    enableForce;
} req_t;


static req_t * create_req(int sd);
static void destroy_req(req_t *req);
static int recv_greeting(req_t *req);
static void parse_greeting(Lex l, req_t *req);
static int recv_req(req_t *req);
static void parse_command(Lex l, req_t *req);
static int query_consoles(server_conf_t *conf, req_t *req);
static int validate_req(req_t *req);
static int send_rsp(req_t *req, int errnum, char *errmsg);
static void perform_query_cmd(req_t *req);
static void perform_monitor_cmd(req_t *req);
static void perform_connect_cmd(req_t *req);
static void perform_broadcast_cmd(req_t *req);


void process_client(server_conf_t *conf)
{
/*  cf. Stevens APUE, section 15.6.
 */
    int rc;
    int sd;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    char buf[MAX_LINE];
    req_t *req;

    if ((rc = pthread_detach(pthread_self())) != 0)
        err_msg(rc, "pthread_detach() failed");

    while ((sd = accept(conf->ld, &addr, &addrlen)) < 0) {
        if (errno == EINTR)
            continue;
        if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
            return;
        if (errno == ECONNABORTED)
            return;
        err_msg(errno, "accept() failed");
    }

    req = create_req(sd);

    if (!inet_ntop(AF_INET, &addr.sin_addr, buf, sizeof(buf)))
        err_msg(errno, "inet_ntop() failed");
    req->ip = create_string(buf);

    if (get_hostname_via_addr(&addr.sin_addr, buf, sizeof(buf)))
        req->host = create_string(buf);

    if (recv_greeting(req) < 0)
        goto end;
    if (recv_req(req) < 0)
        goto end;
    if (query_consoles(conf, req) < 0)
        goto end;
    if (validate_req(req) < 0)
        goto end;

    switch(req->command) {
    case QUERY:
        perform_query_cmd(req);
        break;
    case MONITOR:
        perform_monitor_cmd(req);
        break;
    case CONNECT:
        perform_connect_cmd(req);
        break;
    case EXECUTE:
        perform_broadcast_cmd(req);
        break;
    default:
        err_msg(0, "Invalid command (%d) at %s:%d",
            req->command, __FILE__, __LINE__);
        break;
    }

end:
    destroy_req(req);
    return;
}


static req_t * create_req(int sd)
{
    req_t *req;

    assert(sd >= 0);

    if (!(req = malloc(sizeof(req_t))))
        err_msg(0, "Out of memory");
    req->sd = sd;
    req->ip = NULL;
    req->host = NULL;
    req->user = NULL;
    req->command = NONE;

    /*  The "consoles" list will initially contain strings received while
     *    parsing the client's request.  These strings will then be matched
     *    against the server's conf during query_consoles().
     *  The destroy_string() destructor is used here because the initial
     *    list of strings will be destroyed when query_consoles()
     *    replaces it with a list of console objects.
     */
    if (!(req->consoles = list_create((ListDelF) destroy_string)))
        err_msg(0, "Out of memory");

    req->program = NULL;
    req->enableBroadcast = 0;
    req->enableForce = 0;
    return(req);
}


static void destroy_req(req_t *req)
{
    if (req->sd >= 0) {
        if (close(req->sd) < 0)
            err_msg(errno, "close(%d) failed", req->sd);
        req->sd = -1;
    }
    if (req->ip)
        free(req->ip);
    if (req->host)
        free(req->host);
    if (req->user)
        free(req->user);
    list_destroy(req->consoles);
    if (req->program)
        free(req->program);
    free(req);
    return;
}


static int recv_greeting(req_t *req)
{
    char buf[MAX_SOCK_LINE];
    int n;
    Lex l;
    int done = 0;
    int error = 0;
    int tok;

    /*  Read greeting (ie, first line of request):
     *    HELLO USER="<str>"
     */
    if ((n = read_line(req->sd, buf, sizeof(buf))) <= 0) {
        DPRINTF("Error reading greeting from fd=%d (%s): %s\n",
            req->sd, (req->host ? req->host : req->ip), strerror(errno));
        return(-1);
    }

    if (!(l = lex_create(buf, proto_strs)))
        err_msg(0, "Out of memory");

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

    /*  Validate greeting and prepare response.
     */
    if (req->user == NULL) {
        error = 1;
        send_rsp(req, CONMAN_ERR_BAD_REQUEST, 
            "Invalid greeting: no user specified");
    }
    else if (strcmp(req->ip, "127.0.0.1")) {	/* if remote connection */
        error = 1;
        send_rsp(req, CONMAN_ERR_AUTHENTICATE,
            "Authentication required (but not yet implemented)");
    }
    else {
        error = send_rsp(req, CONMAN_ERR_NONE, NULL);
    }

    return(error ? -1 : 0);
}


static void parse_greeting(Lex l, req_t *req)
{
    int done = 0;
    int tok;

    while (!done) {
        tok = lex_next(l);
        switch(tok) {
        case CONMAN_TOK_USER:
            if ((lex_next(l) == '=') && (lex_next(l) == LEX_STR)) {
                if (req->user)
                    free(req->user);
                req->user = create_string(lex_text(l));
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
    char buf[MAX_SOCK_LINE];
    int n;
    Lex l;
    int done = 0;
    int tok;

    if ((n = read_line(req->sd, buf, sizeof(buf))) <= 0) {
        DPRINTF("Error reading request from fd=%d (%s): %s\n",
            req->sd, (req->host ? req->host : req->ip), strerror(errno));
        return(-1);
    }

    if (!(l = lex_create(buf, proto_strs)))
        err_msg(0, "Out of memory");

    while (!done) {
        tok = lex_next(l);
        switch(tok) {
        case CONMAN_TOK_CONNECT:
            req->command = CONNECT;
            parse_command(l, req);
            break;
        case CONMAN_TOK_EXECUTE:
            req->command = EXECUTE;
            parse_command(l, req);
            break;
        case CONMAN_TOK_MONITOR:
            req->command = MONITOR;
            parse_command(l, req);
            break;
        case CONMAN_TOK_QUERY:
            req->command = QUERY;
            parse_command(l, req);
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


static void parse_command(Lex l, req_t *req)
{
    int done = 0;
    int tok;

    while (!done) {
        tok = lex_next(l);
        switch(tok) {
        case CONMAN_TOK_CONSOLE:
            if ((lex_next(l) == '=') && (lex_next(l) == LEX_STR))
                if (!list_append(req->consoles, create_string(lex_text(l))))
                    err_msg(0, "Out of memory");
            break;
        case CONMAN_TOK_OPTION:
            if (lex_next(l) == '=') {
                if (lex_next(l) == CONMAN_TOK_FORCE)
                    req->enableForce = 1;
                else if (lex_prev(l) == CONMAN_TOK_BROADCAST)
                    req->enableBroadcast = 1;
            }
            break;
        case CONMAN_TOK_PROGRAM:
            if ((lex_next(l) == '=') && (lex_next(l) == LEX_STR)) {
                if (req->program)
                    free(req->program);
                req->program = create_string(lex_text(l));
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
    char buf[MAX_BUF_SIZE];
    ListIterator i;
    char *p;
    int rc;
    regex_t rex;
    int n;
    List matches;
    obj_t *obj;


    if (list_is_empty(req->consoles)) {
        DPRINTF("log me!\n");
        return(-1);
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

    /*  If error compiling regex, return ERROR and close connection.
     */
    rc = regcomp(&rex, buf, REG_EXTENDED | REG_ICASE | REG_NOSUB | REG_NEWLINE);
    if (rc != 0) {
        n = regerror(rc, &rex, NULL, 0);
        if (!(p = alloca(n)))
            err_msg(0, "alloca() failed");
        regerror(rc, &rex, p, n);
        send_rsp(req, CONMAN_ERR_BAD_REGEX, p);
        return(-1);
    }

    /*  Search objs for consoles matching the regex.
     */
    if (!(matches = list_create(NULL)))
        err_msg(0, "Out of memory");
    if ((rc = pthread_mutex_lock(&conf->objsLock)) != 0)
        err_msg(rc, "pthread_mutex_lock() failed for objs");
    if (!(i = list_iterator_create(conf->objs)))
        err_msg(0, "Out of memory");
    while ((obj = list_next(i))) {
        if (obj->type != CONSOLE)
            continue;
        if (!regexec(&rex, obj->name, 0, NULL, 0))
            if (!list_append(matches, obj))
                err_msg(0, "Out of memory");
    }
    list_iterator_destroy(i);
    if ((rc = pthread_mutex_unlock(&conf->objsLock)) != 0)
        err_msg(rc, "pthread_mutex_unlock() failed for objs");
    regfree(&rex);

    /*  Replace original consoles-string list with regex-matched obj list.
     */
    list_destroy(req->consoles);
    req->consoles = matches;

    return(0);
}


static int validate_req(req_t *req)
{
    /*  NOT_IMPLEMENTED_YET
     */
    if (list_is_empty(req->consoles)) {
        /* send CONMAN_ERR_NO_CONSOLES */
        return(-1);
    }
    return(0);
}


static int send_rsp(req_t *req, int errnum, char *errmsg)
{
    char buf[MAX_SOCK_LINE];
    int n;

    assert(errnum >= 0);

    /*  Create response message.
     */
    if (errnum == CONMAN_ERR_NONE) {
        n = snprintf(buf, sizeof(buf), "%s\n",
            proto_strs[LEX_UNTOK(CONMAN_TOK_OK)]);
    }
    else {
        n = snprintf(buf, sizeof(buf), "%s %s=%d %s=\"%s\"\n",
            proto_strs[LEX_UNTOK(CONMAN_TOK_ERROR)],
            proto_strs[LEX_UNTOK(CONMAN_TOK_CODE)], errnum,
            proto_strs[LEX_UNTOK(CONMAN_TOK_MESSAGE)],
            (errmsg ? errmsg : "Doh!"));
    }

    /*  Ensure response is properly terminated.
     */
    if (n < 0 || n >= sizeof(buf)) {
        DPRINTF("Buffer overflow during send_rsp().\n");
        buf[MAX_SOCK_LINE-2] = '\n';
        buf[MAX_SOCK_LINE-1] = '\0';
    }

    /*  Write response to socket.
     */
    if (write_n(req->sd, buf, strlen(buf)) < 0) {
        DPRINTF("Error writing to fd=%d (%s): %s\n",
            req->sd, (req->host ? req->host : req->ip), strerror(errno));
        return(-1);
    }

    return(0);
}


static void perform_query_cmd(req_t *req)
{
    ListIterator i;
    obj_t *obj;
    char buf[MAX_BUF_SIZE];

    list_sort(req->consoles, (ListCmpF) compare_objs);
    if (!(i = list_iterator_create(req->consoles)))
        goto end;
    /* FIX_ME: send OK */
    while ((obj = list_next(i))) {
        strlcpy(buf, obj->name, sizeof(buf));
        strlcat(buf, "\n", sizeof(buf));
        if (write_n(req->sd, buf, strlen(buf)) <  0)
            ;				/* FIX_ME: do something here */
    }
end:
    list_iterator_destroy(i);
    return;
}


static void perform_monitor_cmd(req_t *req)
{
    /*  NOT_IMPLEMENTED_YET
     */
    return;
}


static void perform_connect_cmd(req_t *req)
{
    /*  NOT_IMPLEMENTED_YET
     */
    return;
}


static void perform_broadcast_cmd(req_t *req)
{
    /*  NOT_IMPLEMENTED_YET
     */
    return;
}
