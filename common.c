/******************************************************************************\
 *  $Id: common.c,v 1.4 2001/05/31 18:20:19 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
\******************************************************************************/


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "common.h"
#include "errors.h"
#include "util.h"


char *proto_strs[] = {
/*
 *  Keep strings in sync w/ conman.h:proto_toks enum.
 */
    "OK",
    "ERROR",
    "HELLO",
    "QUERY",
    "MONITOR",
    "CONNECT",
    "EXECUTE",
    "CODE",
    "MESSAGE",
    "USER",
    "TTY",
    "CONSOLE",
    "PROGRAM",
    "OPTION",
    "BROADCAST",
    "FORCE",
    "JOIN",
    NULL
};


req_t * create_req(void)
{
/*  Creates a request struct.
 *  Returns the new struct, or NULL on error (ie, out of memory).
 */
    req_t *req;

    if (!(req = malloc(sizeof(req_t)))) {
        return(NULL);
    }
    req->sd = -1;
    req->user = NULL;
    req->tty = NULL;
    req->host = NULL;
    req->ip = NULL;
    req->port = 0;
    req->command = NONE;
    req->enableBroadcast = 0;
    req->enableForce = 0;
    req->enableJoin = 0;
    req->program = NULL;
    if (!(req->consoles = list_create((ListDelF) destroy_string))) {
        free(req);
        return(NULL);
    }
    return(req);
}


void destroy_req(req_t *req)
{
/*  Destroys a request struct.
 */
    if (!req)
        return;

    if (req->sd >= 0) {
        if (close(req->sd) < 0)
            err_msg(errno, "close(%d) failed", req->sd);
        req->sd = -1;
    }
    if (req->user)
        free(req->user);
    if (req->tty)
        free(req->tty);
    if (req->host)
        free(req->host);
    if (req->ip)
        free(req->ip);
    if (req->program)
        free(req->program);
    if (req->consoles)
        list_destroy(req->consoles);

    free(req);
    return;
}
