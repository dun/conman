/*****************************************************************************
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *  Copyright (C) 2007-2016 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2001-2007 The Regents of the University of California.
 *  UCRL-CODE-2002-009.
 *
 *  This file is part of ConMan: The Console Manager.
 *  For details, see <https://dun.github.io/conman/>.
 *
 *  ConMan is free software: you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation, either version 3 of the License, or (at your option)
 *  any later version.
 *
 *  ConMan is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with ConMan.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************/


#ifndef _COMMON_H
#define _COMMON_H

#include <termios.h>
#include "lex.h"
#include "list.h"

#if HAVE_PATHS_H
#  include <paths.h>
#endif /* HAVE_PATHS_H */

#ifndef _PATH_STDPATH
#  define _PATH_STDPATH "/usr/bin:/bin:/usr/sbin:/sbin"
#endif /* !_PATH_STDPATH */


/*  Default escape char for the client.
 */
#define DEFAULT_CLIENT_ESCAPE   '&'

/*  Ye old deprecated escape char for the server's configuration file.
 */
#define DEPRECATED_CONF_ESCAPE  '&'
#define DO_CONF_ESCAPE_ERROR    0

/*  Name of daemon for TCP-Wrappers.
 */
#define CONMAN_DAEMON_NAME      "conmand"

/*  Message prefix/suffix defs for info msgs written to clients & logfiles.
 */
#define CONMAN_MSG_PREFIX       "\r\n<ConMan> "
#define CONMAN_MSG_SUFFIX       ".\r\n"

/*  Notes regarding the recommended sizes of various constants:
 *
 *    - OBJ_BUF_SIZE >= LOG_REPLAY_LEN * 2
 *    - OBJ_BUF_SIZE >= MAX_LINE
 *    - MAX_BUF_SIZE >= MAX_LINE
 *    - MAX_SOCK_LINE >= MAX_LINE
 */
#define OBJ_BUF_SIZE            16384
#define LOG_REPLAY_LEN          4096
#define MAX_BUF_SIZE            4096
#define MAX_SOCK_LINE           131072
#define MAX_LINE                1024

/*  Escape codes used to send ctrl info 'tween client & server.
 */
#define ESC_CHAR                0xFF
#define ESC_CHAR_BREAK          'B'
#define ESC_CHAR_CLOSE          '.'
#define ESC_CHAR_DEL            'D'     /* XXX: gnats:100 del char kludge */
#define ESC_CHAR_ECHO           'E'
#define ESC_CHAR_FORCE          'F'
#define ESC_CHAR_HELP           '?'
#define ESC_CHAR_INFO           'I'
#define ESC_CHAR_JOIN           'J'
#define ESC_CHAR_REPLAY         'L'
#define ESC_CHAR_MONITOR        'M'
#define ESC_CHAR_QUIET          'Q'
#define ESC_CHAR_RESET          'R'
#define ESC_CHAR_SUSPEND        'Z'

/*  Version string information
 */
#ifndef NDEBUG
#  define FEATURE_DEBUG " DEBUG"
#else
#  define FEATURE_DEBUG ""
#endif /* !NDEBUG */

#if WITH_DMALLOC
#  define FEATURE_DMALLOC " DMALLOC"
#else
#  define FEATURE_DMALLOC ""
#endif /* WITH_DMALLOC */

#if WITH_FREEIPMI
#  define FEATURE_FREEIPMI " FREEIPMI"
#else
#  define FEATURE_FREEIPMI ""
#endif /* WITH_FREEIPMI */

#if WITH_TCP_WRAPPERS
#  define FEATURE_TCP_WRAPPERS " TCP-WRAPPERS"
#else
#  define FEATURE_TCP_WRAPPERS ""
#endif /* WITH_TCP_WRAPPERS */

#define CLIENT_FEATURES \
    (FEATURE_DEBUG FEATURE_DMALLOC)
#define SERVER_FEATURES \
    (FEATURE_DEBUG FEATURE_DMALLOC FEATURE_FREEIPMI FEATURE_TCP_WRAPPERS)

#if ! HAVE_SOCKLEN_T
typedef int socklen_t;                  /* socklen_t is uint32_t in Posix.1g */
#endif /* !HAVE_SOCKLEN_T */


typedef enum cmd_type {                 /* ConMan command (2 bits)           */
    CONMAN_CMD_NONE,
    CONMAN_CMD_CONNECT,
    CONMAN_CMD_MONITOR,
    CONMAN_CMD_QUERY
} cmd_t;

typedef struct request {
    int       sd;                       /* socket descriptor                 */
    char     *user;                     /* login name of client user         */
    char     *tty;                      /* device name of client terminal    */
    char     *fqdn;                     /* queried remote FQDN (or ip) str   */
    char     *host;                     /* short remote hostname (or ip) str */
    char     *ip;                       /* queried remote ip addr string     */
    int       port;                     /* remote port number                */
    List      consoles;                 /* list of consoles affected by cmd  */
    unsigned  command:2;                /* ConMan command to perform (cmd_t) */
    unsigned  enableBroadcast:1;        /* true if b-casting to >1 consoles  */
    unsigned  enableEcho:1;             /* true if echoing standard input    */
    unsigned  enableForce:1;            /* true if forcing console conn      */
    unsigned  enableJoin:1;             /* true if joining console conn      */
    unsigned  enableQuiet:1;            /* true if suppressing info messages */
    unsigned  enableRegex:1;            /* true if regex console matching    */
    unsigned  enableReset:1;            /* true if server supports reset cmd */
} req_t;


enum err_type {
    CONMAN_ERR_NONE,
    CONMAN_ERR_LOCAL,
    CONMAN_ERR_BAD_REQUEST,
    CONMAN_ERR_BAD_REGEX,
    CONMAN_ERR_AUTHENTICATE,
    CONMAN_ERR_NO_CONSOLES,
    CONMAN_ERR_TOO_MANY_CONSOLES,
    CONMAN_ERR_BUSY_CONSOLES
};

enum proto_toks {
/*
 *  Keep enums in sync w/ common.c:proto_strs[].
 */
    CONMAN_TOK_BROADCAST = LEX_TOK_OFFSET,
    CONMAN_TOK_CODE,
    CONMAN_TOK_CONNECT,
    CONMAN_TOK_CONSOLE,
    CONMAN_TOK_ERROR,
    CONMAN_TOK_FORCE,
    CONMAN_TOK_HELLO,
    CONMAN_TOK_JOIN,
    CONMAN_TOK_MESSAGE,
    CONMAN_TOK_MONITOR,
    CONMAN_TOK_OK,
    CONMAN_TOK_OPTION,
    CONMAN_TOK_QUERY,
    CONMAN_TOK_QUIET,
    CONMAN_TOK_REGEX,
    CONMAN_TOK_RESET,
    CONMAN_TOK_TTY,
    CONMAN_TOK_USER
};

extern char *proto_strs[];              /* defined in common.c */

extern const char *conman_license;      /* defined in common.c */


/**************\
**  common.c  **
\**************/

req_t * create_req(void);

void destroy_req(req_t *req);

void get_tty_mode(struct termios *tty, int fd);

void set_tty_mode(struct termios *tty, int fd);

void get_tty_raw(struct termios *tty, int fd);


#endif /* !_COMMON_H */
