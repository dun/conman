/******************************************************************************\
 *  client.h
 *    by Chris Dunlap <cdunlap@llnl.gov>
 *
 *  $Id: client.h,v 1.5 2001/05/21 22:52:39 dun Exp $
\******************************************************************************/


#ifndef _CLIENT_H
#define _CLIENT_H


#include "conman.h"
#include "list.h"


typedef struct client_conf {
    int    sd;				/* server socket descriptor           */
    char  *user;			/* name of local user                 */
    char  *dhost;			/* conman daemon host name            */
    int    dport;			/* conman daemon port number          */
    cmd_t  command;			/* command to send to conman daemon   */
    int    escapeChar;			/* char to issue client escape seq    */
    int    enableBroadcast;		/* true if b-casting to many consoles */
    int    enableForce;			/* true if forcing console connection */
    int    enableVerbose;		/* true if to be more verbose to user */
    char  *program;			/* program name for EXECUTE cmd       */
    char  *log;				/* connection logfile name            */
    int    ld;				/* connection logfile descriptor      */
    List   consoles;			/* list of consoles affected by cmd   */
    int    closedByClient;		/* true if client closed console conn */
    int    errnum;			/* error number from issuing command  */
    char  *errmsg;			/* error message from issuing command */
} client_conf_t;


/*******************\
**  client-conf.c  **
\*******************/

client_conf_t * create_client_conf(void);

void destroy_client_conf(client_conf_t *conf);

void process_client_cmd_line(int argc, char *argv[], client_conf_t *conf);

void open_client_log(client_conf_t *conf);

void close_client_log(client_conf_t *conf);


/*******************\
**  client-sock.c  **
\*******************/

void connect_to_server(client_conf_t *conf);

int send_greeting(client_conf_t *conf);

int send_req(client_conf_t *conf);

int recv_rsp(client_conf_t *conf);

void display_error(client_conf_t *conf);

void display_data(client_conf_t *conf, int fd);

void display_consoles(client_conf_t *conf, int fd);


/******************\
**  client-tty.c  **
\******************/

void connect_console(client_conf_t *conf);

char * write_esc_char(char c, char *p);


#endif /* !_CLIENT_H */
