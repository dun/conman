/*****************************************************************************\
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2001-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *  UCRL-CODE-2002-009.
 *  
 *  This file is part of ConMan, a remote console management program.
 *  For details, see <http://www.llnl.gov/linux/conman/>.
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


#ifndef _SERVER_H
#define _SERVER_H


#include <arpa/telnet.h>
#include <netinet/in.h>                 /* for struct sockaddr_in            */
#include <pthread.h>
#include <sys/types.h>
#include <termios.h>                    /* for struct termios, speed_t       */
#include <time.h>                       /* for time_t                        */
#include "common.h"
#include "list.h"
#include "tpoll.h"


#define DEFAULT_LOGOPT_SANITIZE         0
#define DEFAULT_LOGOPT_TIMESTAMP        0

#define DEFAULT_SEROPT_BPS              B9600
#define DEFAULT_SEROPT_DATABITS         8
#define DEFAULT_SEROPT_PARITY           0
#define DEFAULT_SEROPT_STOPBITS         1

#define RESET_CMD_TIMEOUT               60

#define TELNET_MIN_TIMEOUT              15
#define TELNET_MAX_TIMEOUT              1800


/*  Under Solaris and Tru64, NTELOPTS is only defined when TELOPTS is defined.
 *    But the TELOPTS definition also defines storage for telopts[] -- and that
 *    can only be done once in the executable.  So we create our own definition
 *    for NTELOPTS for those brain-damaged platforms.  Sigh...
 */
#ifndef NTELOPTS
#  define NTELOPTS (1+TELOPT_LAST)
#endif /* NTELOPTS */


enum obj_type {                         /* type of auxiliary obj (2 bits)    */
    CONMAN_OBJ_CLIENT,
    CONMAN_OBJ_LOGFILE,
    CONMAN_OBJ_SERIAL,
    CONMAN_OBJ_TELNET
};

typedef struct client_obj {             /* CLIENT AUX OBJ DATA:              */
    req_t           *req;               /*  client request info              */
    time_t           timeLastRead;      /*  time last data was read from fd  */
    unsigned         gotEscape:1;       /*  true if last char rcvd was esc   */
    unsigned         gotSuspend:1;      /*  true if suspending client output */
} client_obj_t;

typedef struct logfile_opt {            /* LOGFILE OBJ OPTIONS:              */
    unsigned         enableSanitize:1;  /*  true if logfile being sanitized  */
    unsigned         enableTimestamp:1; /*  true if timestamping each line   */
} logopt_t;

typedef enum logfile_line_state {       /* log CR/LF newline state (2 bits)  */
    CONMAN_LOG_LINE_INIT,
    CONMAN_LOG_LINE_DATA,
    CONMAN_LOG_LINE_CR,
    CONMAN_LOG_LINE_LF
} log_line_state_t;

typedef struct logfile_obj {            /* LOGFILE AUX OBJ DATA:             */
    struct base_obj *console;           /*  con obj ref for name expansion   */
    char            *fmtName;           /*  name with conversion specifiers  */
    logopt_t         opts;              /*  local options                    */
    unsigned         gotProcessing:1;   /*  true if input processing req'd   */
    unsigned         lineState:2;       /*  log_line_state_t CR/LF state     */
} logfile_obj_t;

typedef struct serial_opt {             /* SERIAL OBJ OPTIONS:               */
    speed_t          bps;               /*  bps def for cfset*speed()        */
    int              databits;          /*  databits (5-8)                   */
    int              parity;            /*  parity (0=NONE,1=ODD,2=EVEN)     */
    int              stopbits;          /*  stopbits (1-2)                   */
} seropt_t;

typedef struct serial_obj {             /* SERIAL AUX OBJ DATA:              */
    char            *dev;               /*  local serial device name         */
    seropt_t         opts;              /*  serial options                   */
    struct base_obj *logfile;           /*  log obj ref for console replay   */
    struct termios   tty;               /*  saved cooked tty mode            */
} serial_obj_t;

typedef struct sockaddr_in sockaddr_t;

typedef enum telnet_connect_state {     /* state of n/w connection (2 bits)  */
    CONMAN_TELCON_NONE,
    CONMAN_TELCON_DOWN,
    CONMAN_TELCON_PENDING,
    CONMAN_TELCON_UP
} telcon_state_t;

typedef enum telnet_option_state {      /* rfc1143 Telnet Q-Method opt state */
    CONMAN_TELOPT_NO,
    CONMAN_TELOPT_YES,
    CONMAN_TELOPT_WANT_NO_EMP,
    CONMAN_TELOPT_WANT_NO_OPP,
    CONMAN_TELOPT_WANT_YES_EMP,
    CONMAN_TELOPT_WANT_YES_OPP
} telopt_state_t;

typedef struct telnet_obj {             /* TELNET AUX OBJ DATA:              */
    char            *host;              /*  remote telnetd host name (or ip) */
    int              port;              /*  remote telnetd port number       */
    sockaddr_t       saddr;             /*  n/w address of terminal server   */
    struct base_obj *logfile;           /*  log obj ref for console replay   */
    int              timer;             /*  timer id for reconnects          */
    int              delay;             /*  secs 'til next reconnect attempt */
    int              iac;               /*  -1, or last char if in IAC seq   */
    unsigned char    optState[NTELOPTS];/*  rfc1143 Q-Method option state    */
    unsigned         conState:2;        /*  telcon_state_t of n/w connection */
    unsigned         enableKeepAlive:1; /*  true if using TCP keep-alive     */
} telnet_obj_t;

typedef union aux_obj {
    client_obj_t     client;
    logfile_obj_t    logfile;
    serial_obj_t     serial;
    telnet_obj_t     telnet;
} aux_obj_t;

typedef struct base_obj {               /* BASE OBJ:                         */
    char            *name;              /*  obj name                         */
    int              fd;                /*  file descriptor                  */
    unsigned char    buf[MAX_BUF_SIZE]; /*  circular-buf to be written to fd */
    unsigned char   *bufInPtr;          /*  ptr for data written in to buf   */
    unsigned char   *bufOutPtr;         /*  ptr for data written out to fd   */
    pthread_mutex_t  bufLock;           /*  lock protecting access to buf    */
    List             readers;           /*  list of objs that read from me   */
    List             writers;           /*  list of objs that write to me    */
    unsigned         type:2;            /*  enum obj_type of auxiliary obj   */
    unsigned         gotBufWrap:1;      /*  true if circular-buf has wrapped */
    unsigned         gotEOF:1;          /*  true if obj got EOF on last read */
    unsigned         gotReset:1;        /*  true if resetting a console obj  */
    aux_obj_t        aux;               /*  auxiliary obj data union         */
} obj_t;

typedef struct server_conf {
    char            *cwd;               /* cwd when daemon was started       */
    char            *confFileName;      /* configuration file name           */
    char            *logDirName;        /* dir prefix for relative logfiles  */
    char            *logFileName;       /* file to which logmsgs are written */
    char            *logFmtName;        /* name with conversion specifiers   */
    FILE            *logFilePtr;        /* msg log file ptr, !closed at exit */
    int              logFileLevel;      /* level at which to log msg to file */
    char            *pidFileName;       /* file to which pid is written      */
    char            *resetCmd;          /* cmd to invoke for reset esc-seq   */
    int              syslogFacility;    /* syslog facility or -1 if disabled */
    int              throwSignal;       /* signal num to send running daemon */
    int              tStampMinutes;     /* minutes 'tween logfile timestamps */
    time_t           tStampNext;        /* time next stamp written to logs   */
    int              fd;                /* configuration file descriptor     */
    int              port;              /* port number on which to listen    */
    int              ld;                /* listening socket descriptor       */
    List             objs;              /* list of all server obj_t's        */
    char            *globalLogName;     /* global log name (must contain &)  */
    logopt_t         globalLogopts;     /* global opts for logfile objects   */
    seropt_t         globalSeropts;     /* global opts for serial objects    */
    unsigned         enableKeepAlive:1; /* true if using TCP keep-alive      */
    unsigned         enableLoopBack:1;  /* true if only listening on loopback*/
    unsigned         enableTCPWrap:1;   /* true if TCP-Wrappers is enabled   */
    unsigned         enableVerbose:1;   /* true if verbose output requested  */
    unsigned         enableZeroLogs:1;  /* true if console logs are zero'd   */
} server_conf_t;

typedef struct client_args {
    int              sd;                /* socket descriptor of new client   */
    server_conf_t   *conf;              /* server's configuration            */
} client_arg_t;


/***********\
**  Notes  **
\***********/

/*  Concering object READERS and WRITERS:
 *
 *  - an object's readers are those objects that read from it
 *    (ie, those to which it writes data read from its file descriptor)
 *  - an object's writers are those objects that write to it
 *    (ie, those that write data into its circular write-buffer)
 *
 *  Data is read from an object's file descriptor and immediately written
 *  into the circular write-buffer of each object listed in its readers list.
 *  Data in an object's write-buffer is written out to its file descriptor.
 *
 *  CONSOLE objects: (aka SERIAL/TELNET objects)
 *  - readers list can contain at most one logfile object
 *    and any number of R/O or R/W client objects
 *  - writers list can contain any number of R/W or W/O client objects
 *
 *  LOGFILE objects:
 *  - readers list is empty
 *  - writers list contains exactly one console object
 *
 *  R/O CLIENT objects:
 *  - readers list is empty
 *  - writers list contains exactly one console object
 *
 *  R/W CLIENT objects:
 *  - readers list contains exactly one console object
 *  - writers list contains exactly one console object
 *
 *  W/O CLIENT objects: (aka B/C CLIENT objects)
 *  - readers list contains more than one console object
 *  - writers list is empty
 */


/************\
**  Macros  **
\************/

#define is_client_obj(OBJ)   (OBJ->type == CONMAN_OBJ_CLIENT)
#define is_logfile_obj(OBJ)  (OBJ->type == CONMAN_OBJ_LOGFILE)
#define is_serial_obj(OBJ)   (OBJ->type == CONMAN_OBJ_SERIAL)
#define is_telnet_obj(OBJ)   (OBJ->type == CONMAN_OBJ_TELNET)
#define is_console_obj(OBJ)  (is_serial_obj(OBJ) || is_telnet_obj(OBJ))


/*******************\
**  server-conf.c  **
\*******************/

server_conf_t * create_server_conf(int argc, char *argv[]);

void destroy_server_conf(server_conf_t *conf);


/******************\
**  server-esc.c  **
\******************/

int process_client_escapes(obj_t *client, void *src, int len);

int process_telnet_escapes(obj_t *telnet, void *src, int len);

int send_telnet_cmd(obj_t *telnet, int cmd, int opt);


/**********************\
**  server-logfile.c  **
\**********************/

int parse_logfile_opts(logopt_t *opts, const char *str,
    const char *conf, int line, char *errbuf, int errlen);

int open_logfile_obj(obj_t *logfile, int gotTrunc);

obj_t * get_console_logfile_obj(obj_t *console);

int write_log_data(obj_t *log, const void *src, int len);


/******************\
**  server-obj.c  **
\******************/

obj_t * create_client_obj(server_conf_t *conf, req_t *req);

obj_t * create_logfile_obj(server_conf_t *conf, char *name,
    obj_t *console, logopt_t *opts, char *errbuf, int errlen);

obj_t * create_serial_obj(server_conf_t *conf, char *name,
    char *dev, seropt_t *opts, char *errbuf, int errlen);

int open_serial_obj(obj_t *serial);

obj_t * create_telnet_obj(server_conf_t *conf, char *name,
    char *host, int port, char *errbuf, int errlen);

int connect_telnet_obj(obj_t *telnet);

void disconnect_telnet_obj(obj_t *telnet);

void destroy_obj(obj_t *obj);

int format_obj_string(char *buf, int buflen, obj_t *obj, const char *fmt);

int compare_objs(obj_t *obj1, obj_t *obj2);

int find_obj(obj_t *obj, obj_t *key);

void notify_console_objs(obj_t *console, char *msg);

void link_objs(obj_t *src, obj_t *dst);

void unlink_objs(obj_t *src, obj_t *dst);

void unlink_obj(obj_t *obj);

int shutdown_obj(obj_t *obj);

int read_from_obj(obj_t *obj, tpoll_t tp);

int write_obj_data(obj_t *obj, const void *src, int len, int isInfo);

int write_to_obj(obj_t *obj);


/*********************\
**  server-serial.c  **
\*********************/

int parse_serial_opts(
    seropt_t *opts, const char *str, char *errbuf, int errlen);

void set_serial_opts(struct termios *tty, obj_t *serial, seropt_t *opts);


/*******************\
**  server-sock.c  **
\*******************/

void process_client(client_arg_t *args);


#endif /* !_SERVER_H */
