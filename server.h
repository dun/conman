/******************************************************************************\
 *  $Id: server.h,v 1.25 2001/08/28 22:16:17 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
\******************************************************************************/


#ifndef _SERVER_H
#define _SERVER_H


#include <pthread.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include "common.h"
#include "list.h"


enum obj_type {				/* bit-field limited to 4 values      */
    CLIENT,
    CONSOLE,
    LOGFILE,
};

typedef struct client_obj {		/* CLIENT AUX OBJ DATA:               */
    req_t           *req;		/*  client request info               */
    time_t           timeLastRead;	/*  time last data was read from fd   */
    unsigned         gotEscape:1;	/*  true if last char rcvd was an esc */
    unsigned         gotSuspend:1;	/*  true if suspending client output  */
} client_obj_t;

typedef struct console_obj {		/* CONSOLE AUX OBJ DATA:              */
    char            *dev;		/*  console device name               */
    struct base_obj *logfile;		/*  log obj ref for console output    */
    struct termios   tty;		/*  saved cooked tty mode             */
} console_obj_t;

typedef struct logfile_obj {		/* LOGFILE AUX OBJ DATA:              */
    char            *console;		/*  name of console being logged      */
} logfile_obj_t;

typedef union aux_obj {
    client_obj_t     client;
    console_obj_t    console;
    logfile_obj_t    logfile;
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

int process_escape_chars(obj_t *client, void *src, int len);


/******************\
**  server-obj.c  **
\******************/

obj_t * create_console_obj(
    server_conf_t *conf, char *name, char *dev, char *opts);

obj_t * create_logfile_obj(server_conf_t *conf, char *name, obj_t *console);

obj_t * create_client_obj(server_conf_t *conf, req_t *req);

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

void set_serial_opts(struct termios *tty, obj_t *console, char *str);


#endif /* !_SERVER_H */
