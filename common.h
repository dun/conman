/******************************************************************************\
 *  $Id: common.h,v 1.1 2001/05/29 23:45:24 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
\******************************************************************************/


#ifndef _COMMON_H
#define _COMMON_H

#include "lex.h"
#include "list.h"


#define CONMAN_MSG_PREFIX	"<ConMan>"
#define DEFAULT_CONMAN_HOST	"127.0.0.1"
#define DEFAULT_CONMAN_PORT	7890
#define DEFAULT_CLIENT_ESCAPE	'&'
#define DEFAULT_SERVER_CONF	"/etc/conman.conf"
#define DEFAULT_CONSOLE_BAUD	9600
#define MAX_BUF_SIZE		8192
#define MAX_LINE		1024
#define MAX_SOCK_LINE		8192


typedef enum cmd_type {
    NONE,
    CONNECT,
    EXECUTE,
    MONITOR,
    QUERY,
} cmd_t;

typedef struct request {
    int    sd;				/* socket descriptor                  */
    char  *user;			/* login name of client user          */
    char  *host;			/* remote hostname (or ip) string     */
    char  *ip;				/* remote ip addr string              */
    int    port;			/* remote port number                 */
    cmd_t  command;			/* conman command to perform          */
    int    enableBroadcast;		/* true if b-casting to many consoles */
    int    enableForce;			/* true if forcing console connection */
    int    enableJoin;			/* true if joining console connection */
    char  *program;			/* program name for EXECUTE cmd       */
    List   consoles;			/* list of consoles affected by cmd   */
} req_t;


enum err_type {
    CONMAN_ERR_NONE,
    CONMAN_ERR_LOCAL,
    CONMAN_ERR_BAD_REQUEST,
    CONMAN_ERR_BAD_REGEX,
    CONMAN_ERR_AUTHENTICATE,
    CONMAN_ERR_NO_CONSOLES,
    CONMAN_ERR_TOO_MANY_CONSOLES,
    CONMAN_ERR_BUSY_CONSOLES,
    CONMAN_ERR_NO_PROGRAM,
    CONMAN_ERR_BAD_PROGRAM,
    CONMAN_ERR_NO_RESOURCES,
};

enum proto_toks {
/*
 *  Keep enums in sync w/ common.c:proto_strs[].
 */
    CONMAN_TOK_OK = LEX_TOK_OFFSET,
    CONMAN_TOK_ERROR,
    CONMAN_TOK_HELLO,
    CONMAN_TOK_QUERY,
    CONMAN_TOK_MONITOR,
    CONMAN_TOK_CONNECT,
    CONMAN_TOK_EXECUTE,
    CONMAN_TOK_CODE,
    CONMAN_TOK_MESSAGE,
    CONMAN_TOK_USER,
    CONMAN_TOK_CONSOLE,
    CONMAN_TOK_PROGRAM,
    CONMAN_TOK_OPTION,
    CONMAN_TOK_BROADCAST,
    CONMAN_TOK_FORCE,
    CONMAN_TOK_JOIN,
};

extern char *proto_strs[];		/* defined in common.c */


/**************\
**  common.c  **
\**************/

req_t * create_req(void);

void destroy_req(req_t *req);


#endif /* !_COMMON_H */
