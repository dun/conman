/*****************************************************************************\
 *  $Id: client.h,v 1.14 2002/02/08 18:12:25 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
\*****************************************************************************/


#ifndef _CLIENT_H
#define _CLIENT_H


#include <termios.h>
#include "common.h"


typedef struct client_conf {
    req_t          *req;                /* client request info               */
    int             escapeChar;         /* char to issue client escape seq   */
    char           *log;                /* connection logfile name           */
    int             logd;               /* connection logfile descriptor     */
    int             errnum;             /* error number from issuing command */
    char           *errmsg;             /* error msg from issuing command    */
    struct termios  tty;                /* saved "cooked" terminal mode      */
    unsigned        enableVerbose:1;    /* true if verbose output requested  */
    unsigned        isClosedByClient:1; /* true if socket closed by client   */
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

int connect_to_server(client_conf_t *conf);

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

char * write_esc_char(char c, char *dst);


#endif /* !_CLIENT_H */
