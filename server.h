/******************************************************************************\
 *  $Id: server.h,v 1.29 2001/09/17 16:20:17 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
\******************************************************************************/


#ifndef _SERVER_H
#define _SERVER_H


#include <arpa/telnet.h>
#include <netinet/in.h>			/* for struct sockaddr_in             */
#include <pthread.h>
#include <sys/types.h>
#include <termios.h>			/* for struct termios                 */
#include <time.h>			/* for time_t                         */
#include "common.h"
#include "list.h"


enum obj_type {				/* bit-field limited to 4 values      */
    CLIENT,
    LOGFILE,
    SERIAL,
    TELNET,
};

typedef struct sockaddr_in sockaddr_t;

typedef enum telnet_connect_state {	/* bit-field limited to 4 values      */
    TELCON_NONE,
    TELCON_DOWN,
    TELCON_PENDING,
    TELCON_UP,
} telcon_state_t;

typedef enum telnet_option_state {	/* rfc1143 Telnet Q-Method opt state  */
    TELOPT_NO,
    TELOPT_YES,
    TELOPT_WANT_NO_EMP,
    TELOPT_WANT_NO_OPP,
    TELOPT_WANT_YES_EMP,
    TELOPT_WANT_YES_OPP,
} telopt_state_t;

typedef struct client_obj {		/* CLIENT AUX OBJ DATA:               */
    req_t           *req;		/*  client request info               */
    time_t           timeLastRead;	/*  time last data was read from fd   */
    unsigned         gotEscape:1;	/*  true if last char rcvd was an esc */
    unsigned         gotSuspend:1;	/*  true if suspending client output  */
} client_obj_t;

typedef struct logfile_obj {		/* LOGFILE AUX OBJ DATA:              */
    char            *consoleName;	/*  name of console being logged      */
} logfile_obj_t;

typedef struct serial_obj {		/* SERIAL AUX OBJ DATA:               */
    char            *dev;		/*  local serial device name          */
    struct base_obj *logfile;		/*  log obj ref for console output    */
    struct termios   tty;		/*  saved cooked tty mode             */
} serial_obj_t;

typedef struct telnet_obj {		/* TELNET AUX OBJ DATA:               */
    sockaddr_t       saddr;		/*  n/w address of terminal server    */
    struct base_obj *logfile;		/*  log obj ref for console output    */
    int              iac;		/*  -1, or last char if in IAC seq    */
    telcon_state_t   conState:2;	/*  state of network connection       */
    unsigned char    optState[NTELOPTS];/*  rfc1143 Q-Method option state     */
} telnet_obj_t;

typedef union aux_obj {
    client_obj_t     client;
    logfile_obj_t    logfile;
    serial_obj_t     serial;
    telnet_obj_t     telnet;
} aux_obj_t;

typedef struct base_obj {		/* BASE OBJ:                          */
    char            *name;		/*  obj name                          */
    int              fd;		/*  file descriptor                   */
    unsigned char    buf[MAX_BUF_SIZE];	/*  circular-buf to be written to fd  */
    unsigned char   *bufInPtr;		/*  ptr for data written in to buf    */
    unsigned char   *bufOutPtr;		/*  ptr for data written out to fd    */
    pthread_mutex_t  bufLock;		/*  lock protecting access to buf     */
    List             readers;		/*  list of objs that i write to      */
    List             writers;		/*  list of objs that write to me     */
    enum obj_type    type:2;		/*  type of auxiliary obj             */
    unsigned         gotBufWrap:1;	/*  true if circular-buf has wrapped  */
    unsigned         gotEOF:1;		/*  true if obj rcvd EOF on last read */
    aux_obj_t        aux;		/*  auxiliary obj data union          */
} obj_t;

typedef struct server_conf {
    char            *confFileName;	/* configuration file name            */
    char            *logDirName;	/* dir prefix for relative logfiles   */
    char            *logFileName;	/* file to which events are logged    */
    char            *pidFileName;	/* file to which pid is written       */
    int              fd;		/* configuration file descriptor      */
    int              port;		/* port number on which to listen     */
    int              ld;		/* listening socket descriptor        */
    List             objs;		/* list of all server obj_t's         */
    unsigned         enableKeepAlive:1;	/* true if using TCP keep-alive       */
    unsigned         enableLoopBack:1;	/* true if only listening on loopback */
    unsigned         enableVerbose:1;	/* true if verbose output requested   */
    unsigned         enableZeroLogs:1;	/* true if console logs are zero'd    */
} server_conf_t;

typedef struct client_args {
    int              sd;		/* socket descriptor of new client    */
    server_conf_t   *conf;		/* server's configuration             */
} client_arg_t;


#define is_client_obj(OBJ)   (OBJ->type == CLIENT)
#define is_logfile_obj(OBJ)  (OBJ->type == LOGFILE)
#define is_serial_obj(OBJ)   (OBJ->type == SERIAL)
#define is_telnet_obj(OBJ)   (OBJ->type == TELNET)
#define is_console_obj(OBJ) ((OBJ->type == SERIAL) || (OBJ->type == TELNET))


/*******************\
**  server-conf.c  **
\*******************/

server_conf_t * create_server_conf(void);

void destroy_server_conf(server_conf_t *conf);

void process_server_cmd_line(int argc, char *argv[], server_conf_t *conf);

void process_server_conf_file(server_conf_t *conf);


/******************\
**  server-esc.c  **
\******************/

int process_client_escapes(obj_t *client, void *src, int len);

int process_telnet_escapes(obj_t *telnet, void *src, int len);


/******************\
**  server-obj.c  **
\******************/

obj_t * create_client_obj(server_conf_t *conf, req_t *req);

obj_t * create_logfile_obj(server_conf_t *conf, char *name, obj_t *console);

obj_t * create_serial_obj(
    server_conf_t *conf, char *name, char *dev, char *opts);

obj_t * create_telnet_obj(
    server_conf_t *conf, char *name, char *host, int port);

void destroy_obj(obj_t *obj);

int compare_objs(obj_t *obj1, obj_t *obj2);

int find_obj(obj_t *obj, obj_t *key);

void link_objs(obj_t *src, obj_t *dst);

void unlink_objs(obj_t *obj1, obj_t *obj2);

void shutdown_obj(obj_t *obj);

int read_from_obj(obj_t *obj, fd_set *pWriteSet);

int write_obj_data(obj_t *obj, void *src, int len, int isInfo);

int write_to_obj(obj_t *obj);


/*******************\
**  server-sock.c  **
\*******************/

void process_client(client_arg_t *args);


/*******************\
**  server-tty.c  **
\*******************/

void set_serial_opts(struct termios *tty, obj_t *serial, char *str);


#endif /* !_SERVER_H */
