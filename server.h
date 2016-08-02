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


#ifndef _SERVER_H
#define _SERVER_H

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#if HAVE_IPMICONSOLE_H
#  include <ipmiconsole.h>
#endif /* HAVE_IPMICONSOLE_H */

#include <sys/types.h>                  /* include before in.h for bsd */
#include <netinet/in.h>                 /* for struct sockaddr_in            */
#include <pthread.h>                    /* for pthread_mutex_t               */
#include <stdio.h>                      /* for FILE                          */
#include <termios.h>                    /* for struct termios, speed_t       */
#include <time.h>                       /* for time_t                        */
#include <unistd.h>                     /* for pid_t                         */
#include "common.h"
#include "list.h"
#include "tpoll.h"


#define DEFAULT_LOGOPT_LOCK             1
#define DEFAULT_LOGOPT_SANITIZE         0
#define DEFAULT_LOGOPT_TIMESTAMP        0

#define DEFAULT_SEROPT_BPS              B9600
#define DEFAULT_SEROPT_DATABITS         8
#define DEFAULT_SEROPT_PARITY           0
#define DEFAULT_SEROPT_STOPBITS         1

#define MIN_CONNECT_SECS                60

#if WITH_FREEIPMI
#define IPMI_ENGINE_CONSOLES_PER_THREAD 128
#define IPMI_MAX_USER_LEN               IPMI_MAX_USER_NAME_LENGTH
#define IPMI_MAX_PSWD_LEN               IPMI_2_0_MAX_PASSWORD_LENGTH
#define IPMI_MAX_KG_LEN                 IPMI_MAX_K_G_LENGTH
#define IPMI_CONNECT_TIMEOUT            300
#define IPMI_MAX_TIMEOUT                1800
#define IPMI_MIN_TIMEOUT                60
#endif /* WITH_FREEIPMI */

#define PROCESS_MAX_TIMEOUT             1800
#define PROCESS_MIN_TIMEOUT             60

#define RESET_CMD_TIMEOUT               60

#define RESOLVE_RETRY_TIMEOUT           1800

#define TELNET_MAX_TIMEOUT              1800
#define TELNET_MIN_TIMEOUT              15

#define UNIXSOCK_MAX_TIMEOUT            60
#define UNIXSOCK_MIN_TIMEOUT            1


enum obj_type {                         /* type of auxiliary obj             */
    CONMAN_OBJ_CLIENT   = 0x01,
    CONMAN_OBJ_LOGFILE  = 0x02,
    CONMAN_OBJ_PROCESS  = 0x04,
    CONMAN_OBJ_SERIAL   = 0x08,
    CONMAN_OBJ_TELNET   = 0x10,
    CONMAN_OBJ_UNIXSOCK = 0x20,
    CONMAN_OBJ_IPMI     = 0x40,
    CONMAN_OBJ_TEST     = 0x80,
    CONMAN_OBJ_LAST_ENTRY
};

typedef struct client_obj {             /* CLIENT AUX OBJ DATA:              */
    req_t           *req;               /*  client request info              */
    time_t           timeLastRead;      /*  time last data was read from fd  */
    unsigned         gotEscape:1;       /*  true if last char rcvd was esc   */
    unsigned         gotSuspend:1;      /*  true if suspending client output */
} client_obj_t;

typedef struct logfile_opt {            /* LOGFILE OBJ OPTIONS:              */
    unsigned         enableLock:1;      /*  true if logfile being locked     */
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
    unsigned         gotTruncate:1;     /*  true if ZeroLogs is enabled      */
    unsigned         lineState:2;       /*  log_line_state_t CR/LF state     */
} logfile_obj_t;

typedef enum process_connect_state {    /* process connection state (1 bit)  */
    CONMAN_PROCESS_DOWN,
    CONMAN_PROCESS_UP
} process_state_t;

typedef struct process_obj {            /* PROCESS AUX OBJ DATA              */
    char           **argv;              /*  NULL-term'd ary of ptrs to strs  */
    char            *prog;              /*  reference to basename of argv[0] */
    int              timer;             /*  timer id for repeated attempts   */
    int              delay;             /*  secs 'til next reconnect attempt */
    pid_t            pid;               /*  pid of forked process            */
    time_t           tStart;            /*  time at which process was exec'd */
    struct base_obj *logfile;           /*  log obj ref for console replay   */
    unsigned         state:1;           /*  process_state_t conn state       */
} process_obj_t;

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

typedef enum telnet_connect_state {     /* state of n/w connection (2 bits)  */
    CONMAN_TELNET_NONE,
    CONMAN_TELNET_DOWN,
    CONMAN_TELNET_PENDING,
    CONMAN_TELNET_UP
} telnet_state_t;

typedef struct telnet_obj {             /* TELNET AUX OBJ DATA:              */
    char            *host;              /*  remote telnetd host name (or ip) */
    int              port;              /*  remote telnetd port number       */
    struct base_obj *logfile;           /*  log obj ref for console replay   */
    int              timer;             /*  timer id for reconnects          */
    int              delay;             /*  secs 'til next reconnect attempt */
    int              iac;               /*  -1, or last char if in IAC seq   */
    unsigned         state:2;           /*  telnet_state_t of n/w connection */
    unsigned         enableKeepAlive:1; /*  true if using TCP keep-alive     */
} telnet_obj_t;

typedef enum unixsock_connect_state {   /* socket connection state (1 bit)   */
    CONMAN_UNIXSOCK_DOWN,
    CONMAN_UNIXSOCK_UP
} unixsock_state_t;

typedef struct unixsock_obj {           /* UNIXSOCK AUX OBJ DATA:            */
    char            *dev;               /*  unix domain socket device name   */
    struct base_obj *logfile;           /*  log obj ref for console replay   */
    int              timer;             /*  timer id for reconnects          */
    int              delay;             /*  secs 'til next reconnect attempt */
    unsigned         state:1;           /*  unixsock_state_t conn state      */
} unixsock_obj_t;

/*  Refer to struct ipmiconsole_ipmi_config in <ipmiconsole.h>.
 */
#if WITH_FREEIPMI
typedef struct ipmi_opt {                               /* IPMI OBJ OPTIONS: */
    char             username[ IPMI_MAX_USER_LEN + 1 ]; /*  BMC username     */
    char             password[ IPMI_MAX_PSWD_LEN + 1 ]; /*  BMC password     */
    unsigned char    kg[ IPMI_MAX_KG_LEN + 1 ];         /*  BMC K_g key      */
    unsigned int     kgLen;                             /*  BMC K_g key len  */
    int              privilegeLevel;                    /*  auth priv level  */
    int              cipherSuite;                       /*  cipher suite id  */
    unsigned int     workaroundFlags;                   /*  workaround flags */
} ipmiopt_t;

typedef struct ipmiconsole_ctx ipmictx_t;

typedef enum ipmi_connect_state {
    CONMAN_IPMI_DOWN,
    CONMAN_IPMI_PENDING,
    CONMAN_IPMI_UP
} ipmi_state_t;

typedef struct ipmi_obj {               /* IPMI AUX OBJ DATA:                */
    char            *host;              /*  remote bmc host name (or ip)     */
    ipmiopt_t        iconf;             /*  conf to connect to bmc           */
    ipmictx_t       *ctx;               /*  ipmi session ctx ptr             */
    struct base_obj *logfile;           /*  log obj ref for console replay   */
    ipmi_state_t     state;             /*  connection state                 */
    int              timer;             /*  timer id                         */
    int              delay;             /*  secs 'til next reconnect attempt */
    pthread_mutex_t  mutex;             /*  lock for ctx/state/timer/delay   */
} ipmi_obj_t;
#endif /* WITH_FREEIPMI */

typedef struct test_opt {               /* TEST OBJ OPTIONS:                 */
    int              numBytes;          /*  num bytes to output per burst    */
    int              msecMax;           /*  max msecs between bursts, or -1  */
    int              msecMin;           /*  min msecs between bursts, or -1  */
    int              probability;       /*  %-probability of burst, [0-100]  */
} test_opt_t;

typedef struct test_obj {               /* TEST AUX OBJ DATA:                */
    test_opt_t       opts;              /*  test obj options                 */
    struct base_obj *logfile;           /*  log obj ref for console replay   */
    int              timer;             /*  timer id for next burst          */
    int              numLeft;           /*  num bytes remaining in burst     */
    char             lastChar;          /*  last char output by test console */
} test_obj_t;

typedef union aux_obj {
    client_obj_t     client;
    logfile_obj_t    logfile;
    process_obj_t    process;
    serial_obj_t     serial;
    telnet_obj_t     telnet;
    unixsock_obj_t   unixsock;
#if WITH_FREEIPMI
    ipmi_obj_t       ipmi;
#endif /* WITH_FREEIPMI */
    test_obj_t       test;
} aux_obj_t;

typedef struct base_obj {               /* BASE OBJ:                         */
    char            *name;              /*  obj name                         */
    int              fd;                /*  file descriptor                  */
    unsigned char    buf[OBJ_BUF_SIZE]; /*  circular-buf to be written to fd */
    unsigned char   *bufInPtr;          /*  ptr for data written in to buf   */
    unsigned char   *bufOutPtr;         /*  ptr for data written out to fd   */
    pthread_mutex_t  bufLock;           /*  lock protecting access to buf    */
    List             readers;           /*  list of objs that read from me   */
    List             writers;           /*  list of objs that write to me    */
    char            *resetCmdRef;       /*  console reset cmd string ref     */
    pid_t            resetCmdPid;       /*  console reset cmd active pid     */
    int              resetCmdTimer;     /*  console reset cmd timer id       */
    unsigned         type;              /*  enum obj_type of auxiliary obj   */
    unsigned         gotBufWrap:1;      /*  true if circular-buf has wrapped */
    unsigned         gotEOF:1;          /*  true if obj got EOF on last read */
    aux_obj_t        aux;               /*  auxiliary obj data union         */
} obj_t;

typedef struct server_conf {
    char            *confFileName;      /* configuration file name           */
    char            *coreDumpDir;       /* dir where core dumps are written  */
    char            *cwd;               /* cwd when daemon was started       */
    char            *execPath;          /* process exec path                 */
    char            *logDirName;        /* dir prefix for relative logfiles  */
    char            *logFileName;       /* file to which logmsgs are written */
    char            *logFmtName;        /* name with conversion specifiers   */
    FILE            *logFilePtr;        /* msg log file ptr, !closed at exit */
    int              logFileLevel;      /* level at which to log msg to file */
    int              numOpenFiles;      /* rlimit for number of open files   */
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
    tpoll_t          tp;                /* tpoll obj for muxing i/o & timers */
    char            *globalLogName;     /* global log name (must contain &)  */
    logopt_t         globalLogOpts;     /* global opts for logfile objects   */
    seropt_t         globalSerOpts;     /* global opts for serial objects    */
#if WITH_FREEIPMI
    ipmiopt_t        globalIpmiOpts;    /* global opts for ipmi objects      */
    int              numIpmiObjs;       /* number of ipmi consoles in config */
#endif /* WITH_FREEIPMI */
    test_opt_t       globalTestOpts;    /* global opts for test objs         */
    unsigned         enableCoreDump:1;  /* true if core dumps are enabled    */
    unsigned         enableKeepAlive:1; /* true if using TCP keep-alive      */
    unsigned         enableLoopBack:1;  /* true if only listening on loopback*/
    unsigned         enableTCPWrap:1;   /* true if TCP-Wrappers is enabled   */
    unsigned         enableVerbose:1;   /* true if verbose output requested  */
    unsigned         enableZeroLogs:1;  /* true if console logs are zero'd   */
    unsigned         enableForeground:1;/* true if daemon should not fork    */
} server_conf_t;

typedef struct client_args {
    int              sd;                /* socket descriptor of new client   */
    server_conf_t   *conf;              /* server's configuration            */
} client_arg_t;


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
 *  CONSOLE objects: (aka PROCESS/SERIAL/TELNET objects)
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


/*  Macros
 */
#define CONMAN_OBJ_IS_CONSOLE \
  ( CONMAN_OBJ_PROCESS  |     \
    CONMAN_OBJ_SERIAL   |     \
    CONMAN_OBJ_TELNET   |     \
    CONMAN_OBJ_UNIXSOCK |     \
    CONMAN_OBJ_IPMI     |     \
    CONMAN_OBJ_TEST           \
  )
#define is_client_obj(OBJ)   (OBJ->type == CONMAN_OBJ_CLIENT)
#define is_ipmi_obj(OBJ)     (OBJ->type == CONMAN_OBJ_IPMI)
#define is_logfile_obj(OBJ)  (OBJ->type == CONMAN_OBJ_LOGFILE)
#define is_process_obj(OBJ)  (OBJ->type == CONMAN_OBJ_PROCESS)
#define is_serial_obj(OBJ)   (OBJ->type == CONMAN_OBJ_SERIAL)
#define is_telnet_obj(OBJ)   (OBJ->type == CONMAN_OBJ_TELNET)
#define is_test_obj(OBJ)     (OBJ->type == CONMAN_OBJ_TEST)
#define is_unixsock_obj(OBJ) (OBJ->type == CONMAN_OBJ_UNIXSOCK)
#define is_console_obj(OBJ)  (OBJ->type &  CONMAN_OBJ_IS_CONSOLE)


/*  server-conf.c
 */
server_conf_t * create_server_conf(void);

void destroy_server_conf(server_conf_t *conf);

void process_cmdline(server_conf_t *conf, int argc, char *argv[]);

void process_config(server_conf_t *conf);


/*  server-esc.c
 */
int process_client_escapes(obj_t *client, void *src, int len);


/* server-ipmi.c
 */
#if WITH_FREEIPMI

void ipmi_init(int num_consoles);

void ipmi_fini(void);

int is_ipmi_dev(const char *dev, char **host_ref);

int init_ipmi_opts(ipmiopt_t *iopts);

int parse_ipmi_opts(
    ipmiopt_t *iopts, const char *str, char *errbuf, int errlen);

obj_t * create_ipmi_obj(server_conf_t *conf, char *name,
    ipmiopt_t *iconf, char *host, char *errbuf, int errlen);

int open_ipmi_obj(obj_t *ipmi);

int send_ipmi_break(obj_t *ipmi);

#endif /* WITH_FREEIPMI */


/*  server-logfile.c
 */
int parse_logfile_opts(logopt_t *opts, const char *str,
    char *errbuf, int errlen);

obj_t * create_logfile_obj(server_conf_t *conf, char *name,
    obj_t *console, logopt_t *opts, char *errbuf, int errlen);

int open_logfile_obj(obj_t *logfile);

obj_t * get_console_logfile_obj(obj_t *console);

int write_log_data(obj_t *log, const void *src, int len);


/*  server-obj.c
 */
obj_t * create_obj(server_conf_t *conf, char *name,
    int fd, enum obj_type type);

obj_t * create_client_obj(server_conf_t *conf, req_t *req);

void destroy_obj(obj_t *obj);

void reopen_obj(obj_t *obj);

int format_obj_string(char *buf, int buflen, obj_t *obj, const char *fmt);

int compare_objs(obj_t *obj1, obj_t *obj2);

int find_obj(obj_t *obj, obj_t *key);

int write_notify_msg(obj_t *console, int priority, char *fmt, ...);

void notify_console_objs(obj_t *console, char *msg);

void link_objs(obj_t *src, obj_t *dst);

void unlink_objs(obj_t *src, obj_t *dst);

void unlink_obj(obj_t *obj);

int shutdown_obj(obj_t *obj);

int read_from_obj(obj_t *obj);

int write_obj_data(obj_t *obj, const void *src, int len, int isInfo);

int write_to_obj(obj_t *obj);


/*  server-process.c
 */
int is_process_dev(const char *dev, const char *cwd,
    const char *exec_path, char **path_ref);

obj_t * create_process_obj(server_conf_t *conf, char *name, List args,
    char *errbuf, int errlen);

int open_process_obj(obj_t *process);


/*  server-serial.c
 */
int is_serial_dev(const char *dev, const char *cwd, char **path_ref);

int parse_serial_opts(
    seropt_t *opts, const char *str, char *errbuf, int errlen);

void set_serial_opts(struct termios *tty, obj_t *serial, seropt_t *opts);

obj_t * create_serial_obj(server_conf_t *conf, char *name,
    char *dev, seropt_t *opts, char *errbuf, int errlen);

int open_serial_obj(obj_t *serial);


/*  server-sock.c
 */
void process_client(client_arg_t *args);


/*  server-telnet.c
 */
int is_telnet_dev(const char *dev, char **host_ref, int *port_ref);

obj_t * create_telnet_obj(server_conf_t *conf, char *name,
    char *host, int port, char *errbuf, int errlen);

int open_telnet_obj(obj_t *telnet);

int process_telnet_escapes(obj_t *telnet, void *src, int len);

int send_telnet_cmd(obj_t *telnet, int cmd, int opt);


/*  server-test.c
 */
int is_test_dev(const char *dev);

int init_test_opts(test_opt_t *opts);

int parse_test_opts(test_opt_t *opts, const char *str,
    char *errbuf, int errlen);

obj_t * create_test_obj(server_conf_t *conf, char *name,
    test_opt_t *opts, char *errbuf, int errlen);

int open_test_obj(obj_t *test);

int read_test_obj(obj_t *test);


/*  server-unixsock.c
 */
int is_unixsock_dev(const char *dev, const char *cwd, char **path_ref);

obj_t * create_unixsock_obj(server_conf_t *conf, char *name, char *dev,
    char *errbuf, int errlen);

int open_unixsock_obj(obj_t *unixsock);


#endif /* !_SERVER_H */
