/*****************************************************************************\
 *  $Id: client.h,v 1.17 2002/03/30 01:35:38 dun Exp $
 *****************************************************************************
 *  Copyright (C) 2001-2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *  UCRL-CODE-2002-009.
 *  
 *  This file is part of ConMan, a remote console management program.
 *  For details, see <http://www.llnl.gov/linux/conman.html>.
 *  
 *  ConMan is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  ConMan is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with ConMan; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/


#ifndef _CLIENT_H
#define _CLIENT_H


#include <termios.h>
#include "common.h"


typedef struct client_conf {
    char           *prog;               /* name of client program            */
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

void process_client_env_vars(client_conf_t *conf);

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
