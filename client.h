/******************************************************************************\
 *  client.h
 *    by Chris Dunlap <cdunlap@llnl.gov>
 *
 *  $Id: client.h,v 1.1 2001/05/04 15:26:40 dun Exp $
\******************************************************************************/


#ifndef _CLIENT_H
#define _CLIENT_H


#include "conman.h"
#include "list.h"


typedef struct client_conf {
    int    sd;
    char  *user;
    char  *rhost;
    int    rport;
    cmd_t  command;
    int    escapeChar;
    int    enableBroadcast;
    int    enableForce;
    int    enableVerbose;
    char  *program;
    char  *log;
    List   consoles;
    int    errnum;
    char  *errmsg;
} client_conf_t;


/*******************\
**  client-conf.c  **
\*******************/

client_conf_t * create_client_conf(void);

void destroy_client_conf(client_conf_t *conf);

void process_client_cmd_line(int argc, char *argv[], client_conf_t *conf);


/*******************\
**  client-sock.c  **
\*******************/

void connect_to_server(client_conf_t *conf);

int send_greeting(client_conf_t *conf);

int send_req(client_conf_t *conf);

int recv_rsp(client_conf_t *conf);

void display_error(client_conf_t *conf);

void display_data(client_conf_t *conf);


/******************\
**  client-tty.c  **
\******************/

void connect_console(client_conf_t *conf);


#endif /* !_CLIENT_H */
