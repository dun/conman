/******************************************************************************\
 *  server.h
 *    by Chris Dunlap <cdunlap@llnl.gov>
 *
 *  $Id: server.h,v 1.4 2001/05/14 16:22:09 dun Exp $
\******************************************************************************/


#ifndef _SERVER_H
#define _SERVER_H


#include <pthread.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include "conman.h"
#include "list.h"


enum obj_type {
    CONSOLE,
    LOGFILE,
    SOCKET,
};

typedef struct console_obj {		/* CONSOLE AUX OBJ DATA:              */
    char            *dev;		/*  console device name               */
    char            *log;		/*  filename where output is logged   */
    char            *rst;		/*  filename of "console-reset" prog  */
    int              bps;		/*  console baud rate                 */
    struct termios   term;		/*  saved cooked tty mode             */
} console_obj_t;

typedef struct logfile_obj {		/* LOGFILE AUX OBJ DATA:              */
    ;					/*  empty (is this legal C?)          */
} logfile_obj_t;

typedef struct socket_obj {		/* SOCKET AUX OBJ DATA:               */
    int              gotIAC;		/*  true if rcvd IAC escape on fd     */
    time_t           timeLastRead;	/*  time last data was read from fd   */
} socket_obj_t;

typedef union aux_obj {
    console_obj_t    console;
    logfile_obj_t    logfile;
    socket_obj_t     socket;
} aux_obj_t;

typedef struct base_obj {		/* BASE OBJ:                          */
    char            *name;		/*  obj name                          */
    int              fd;		/*  file descriptor, -1 if inactive   */
    int              gotEOF;		/*  true if obj rcvd EOF on last read */
    unsigned char    buf[MAX_BUF_SIZE];	/*  circ-buffer to be written to fd   */
    unsigned char   *bufInPtr;		/*  ptr for data written in to buf    */
    unsigned char   *bufOutPtr;		/*  ptr for data written out to fd    */
    pthread_mutex_t  bufLock;		/*  lock protecting access to buf     */
    struct base_obj *writer;		/*  obj that writes to me             */
    List             readers;		/*  list of objs that i write to      */
    List             objs;		/*  list of all objs from conf        */
    enum obj_type    type;		/*  type of auxxiliary obj            */
    aux_obj_t        aux;		/*  auxiliary obj data                */
} obj_t;

typedef struct server_conf {
    char            *filename;		/* configuration filename             */
    char            *logname;		/* file to which events are logged    */
    int              ld;		/* listening socket descriptor        */
    List             objs;		/* list of all server objects         */
} server_conf_t;


/*******************\
**  server-conf.c  **
\*******************/

server_conf_t * create_server_conf(void);

void destroy_server_conf(server_conf_t *conf);

void process_server_cmd_line(int argc, char *argv[], server_conf_t *conf);

void process_server_conf_file(server_conf_t *conf);


/******************\
**  server-obj.c  **
\******************/

obj_t * create_console_obj(List objs, char *name, char *dev,
    char *log, char *rst, int bps);

obj_t * create_logfile_obj(List objs, char *name);

obj_t * create_socket_obj(List objs, char *user, char *host, int sd);

void destroy_obj(obj_t *obj);

void dealloc_obj(obj_t *obj);

int open_obj(obj_t *obj);

void close_obj(obj_t *obj);

int compare_objs(obj_t *obj1, obj_t *obj2);

int find_obj(obj_t *obj, obj_t *key);

int link_objs(obj_t *src, obj_t *dst);

int unlink_obj(obj_t *obj);

void write_to_obj(obj_t *obj);

void read_from_obj(obj_t *obj);

int write_obj_data(obj_t *obj, void *src, int len);


/*******************\
**  server-sock.c  **
\*******************/

void process_client(server_conf_t *conf);


#endif /* !_SERVER_H */
