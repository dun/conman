/******************************************************************************\
 *  conman.h
 *    by Chris Dunlap <cdunlap@llnl.gov>
 *
 *  $Id: conman.h,v 1.5 2001/05/22 19:39:27 dun Exp $
\******************************************************************************/


#ifndef _CONMAN_H
#define _CONMAN_H

#include "lex.h"


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
    CONMAN_TOK_GOODBYE,
    CONMAN_TOK_CODE,
    CONMAN_TOK_MESSAGE,
    CONMAN_TOK_USER,
    CONMAN_TOK_CONSOLE,
    CONMAN_TOK_PROGRAM,
    CONMAN_TOK_OPTION,
    CONMAN_TOK_BROADCAST,
    CONMAN_TOK_FORCE,
};

extern char *proto_strs[];		/* defined in common.c */


#endif /* !_CONMAN_H */
