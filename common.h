/******************************************************************************\
 *  $Id: common.h,v 1.27 2001/12/28 23:23:44 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
\******************************************************************************/


#ifndef _COMMON_H
#define _COMMON_H

#include <termios.h>
#include "lex.h"
#include "list.h"


/*  Default escape char for the client.
 */
#define DEFAULT_CLIENT_ESCAPE	'&'

/*  Escape char for the server's configuration file.
 */
#define DEFAULT_CONFIG_ESCAPE	'&'

/*  Name of daemon for TCP-Wrappers.
 */
#define CONMAN_DAEMON_NAME	"conmand"

/*  Message prefix/suffix defs for info msgs written to clients & logfiles.
 */
#define CONMAN_MSG_PREFIX	"\r\n<ConMan> "
#define CONMAN_MSG_SUFFIX	".\r\n"

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

/*  Escape codes used to send ctrl info 'tween client & server.
 */
#define ESC_CHAR		0xFF
#define ESC_CHAR_BREAK		'B'
#define ESC_CHAR_CLOSE		'.'
#define ESC_CHAR_HELP		'?'
#define ESC_CHAR_INFO		'I'
#define ESC_CHAR_LOG		'L'
#define ESC_CHAR_QUIET		'Q'
#define ESC_CHAR_RESET		'R'
#define ESC_CHAR_SUSPEND	'Z'

/*  Version string information
 */
#ifndef NDEBUG
#  define FEATURE_DEBUG " DEBUG"
#else
#  define FEATURE_DEBUG ""
#endif /* !NDEBUG */

#ifdef WITH_DMALLOC
#  define FEATURE_DMALLOC " DMALLOC"
#else
#  define FEATURE_DMALLOC ""
#endif /* WITH_DMALLOC */

#ifdef WITH_TCP_WRAPPERS
#  define FEATURE_TCP_WRAPPERS " TCP-WRAPPERS"
#else
#  define FEATURE_TCP_WRAPPERS ""
#endif /* WITH_TCP_WRAPPERS */

#define CLIENT_FEATURES (FEATURE_DEBUG FEATURE_DMALLOC)
#define SERVER_FEATURES (FEATURE_DEBUG FEATURE_DMALLOC FEATURE_TCP_WRAPPERS)

#ifndef HAVE_SOCKLEN_T
typedef unsigned int socklen_t;		/* socklen_t is defined in Posix.1g   */
#endif /* !HAVE_SOCKLEN_T */


typedef enum cmd_type {			/* bit-field limited to 8 values      */
    NONE,
    CONNECT,
    MONITOR,
    QUERY,
} cmd_t;

typedef struct request {
    int       sd;			/* socket descriptor                  */
    char     *user;			/* login name of client user          */
    char     *tty;			/* device name of client terminal     */
    char     *fqdn;			/* queried remote FQDN (or ip) string */
    char     *host;			/* short remote host name (or ip) str */
    char     *ip;			/* queried remote ip addr string      */
    int       port;			/* remote port number                 */
    List      consoles;			/* list of consoles affected by cmd   */
    cmd_t     command:3;		/* ConMan command to perform          */
    unsigned  enableBroadcast:1;	/* true if b-casting to many consoles */
    unsigned  enableForce:1;		/* true if forcing console connection */
    unsigned  enableJoin:1;		/* true if joining console connection */
    unsigned  enableQuiet:1;		/* true if suppressing info messages  */
    unsigned  enableRegex:1;		/* true if console matching via regex */
    unsigned  enableReset:1;		/* true if server supports reset cmd  */
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
    CONMAN_TOK_FORCE,
    CONMAN_TOK_HELLO,
    CONMAN_TOK_JOIN,
    CONMAN_TOK_MESSAGE,
    CONMAN_TOK_MONITOR,
    CONMAN_TOK_OPTION,
    CONMAN_TOK_QUERY,
    CONMAN_TOK_QUIET,
    CONMAN_TOK_REGEX,
    CONMAN_TOK_RESET,
    CONMAN_TOK_TTY,
    CONMAN_TOK_USER,
};

extern char *proto_strs[];		/* defined in common.c */


/**************\
**  common.c  **
\**************/

req_t * create_req(void);

void destroy_req(req_t *req);

void get_tty_mode(struct termios *tty, int fd);

void set_tty_mode(struct termios *tty, int fd);

void get_tty_raw(struct termios *tty, int fd);


#endif /* !_COMMON_H */
