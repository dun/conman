/******************************************************************************\
 *  $Id: common.h,v 1.6 2001/06/12 16:17:48 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
\******************************************************************************/


#ifndef _COMMON_H
#define _COMMON_H

#include "lex.h"
#include "list.h"


#define DEFAULT_CONMAN_HOST	"127.0.0.1"
#define DEFAULT_CONMAN_PORT	7890
#define DEFAULT_SERVER_CONF	"/etc/conman.conf"
#define DEFAULT_CONSOLE_BAUD	9600
#define DEFAULT_CLIENT_ESCAPE	'&'

#define CONMAN_MSG_PREFIX	"<ConMan> "
#define CONMAN_MSG_SUFFIX	"."

/*  Notes regarding the recommended sizes of various constants:
 *
 *    - MAX_BUF_SIZE >= CONMAN_REPLAY_LEN * 2
 *    - MAX_BUF_SIZE >= MAX_LINE
 *    - MAX_SOCK_LINE >= MAX_LINE
 */
#define CONMAN_REPLAY_LEN	4096
#define MAX_BUF_SIZE		8192
#define MAX_SOCK_LINE		8192
#define MAX_LINE		1024

#define ESC_CHAR		0xFF
#define ESC_CHAR_BREAK		'B'
#define ESC_CHAR_CLOSE		'.'
#define ESC_CHAR_HELP		'?'
#define ESC_CHAR_LOG		'L'
#define ESC_CHAR_SUSPEND	'Z'

#ifndef NDEBUG
#define DEBUG_STRING " (debug)"
#else
#define DEBUG_STRING ""
#endif /* !NDEBUG */


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
    char  *tty;				/* device name of client terminal     */
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
    CONMAN_TOK_BROADCAST,
    CONMAN_TOK_CODE,
    CONMAN_TOK_CONNECT,
    CONMAN_TOK_CONSOLE,
    CONMAN_TOK_EXECUTE,
    CONMAN_TOK_FORCE,
    CONMAN_TOK_HELLO,
    CONMAN_TOK_JOIN,
    CONMAN_TOK_MESSAGE,
    CONMAN_TOK_MONITOR,
    CONMAN_TOK_OPTION,
    CONMAN_TOK_PROGRAM,
    CONMAN_TOK_QUERY,
    CONMAN_TOK_TTY,
    CONMAN_TOK_USER,
};

extern char *proto_strs[];		/* defined in common.c */


/**************\
**  common.c  **
\**************/

req_t * create_req(void);

void destroy_req(req_t *req);


#endif /* !_COMMON_H */
