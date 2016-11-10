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


#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>
#include "common.h"
#include "lex.h"
#include "list.h"
#include "log.h"
#include "server.h"
#include "tpoll.h"
#include "util-file.h"
#include "util-str.h"
#include "util.h"


enum server_conf_toks {
/*
 *  Keep enums in sync w/ server_conf_strs[].
 */
    SERVER_CONF_CONSOLE = LEX_TOK_OFFSET,
    SERVER_CONF_COREDUMP,
    SERVER_CONF_COREDUMPDIR,
    SERVER_CONF_DEV,
    SERVER_CONF_EXECPATH,
    SERVER_CONF_GLOBAL,
#if WITH_FREEIPMI
    SERVER_CONF_IPMIOPTS,
#endif /* WITH_FREEIPMI */
    SERVER_CONF_KEEPALIVE,
    SERVER_CONF_LOG,
    SERVER_CONF_LOGDIR,
    SERVER_CONF_LOGFILE,
    SERVER_CONF_LOGOPTS,
    SERVER_CONF_LOOPBACK,
    SERVER_CONF_NAME,
    SERVER_CONF_NOFILE,
    SERVER_CONF_OFF,
    SERVER_CONF_ON,
    SERVER_CONF_PIDFILE,
    SERVER_CONF_PORT,
    SERVER_CONF_RESETCMD,
    SERVER_CONF_SEROPTS,
    SERVER_CONF_SERVER,
    SERVER_CONF_SYSLOG,
    SERVER_CONF_TCPWRAPPERS,
    SERVER_CONF_TESTOPTS,
    SERVER_CONF_TIMESTAMP
};

static char *server_conf_strs[] = {
/*
 *  Keep strings in sync w/ server_conf_toks enum.
 *  These must be sorted in a case-insensitive manner.
 */
    "CONSOLE",
    "COREDUMP",
    "COREDUMPDIR",
    "DEV",
    "EXECPATH",
    "GLOBAL",
#if WITH_FREEIPMI
    "IPMIOPTS",
#endif /* WITH_FREEIPMI */
    "KEEPALIVE",
    "LOG",
    "LOGDIR",
    "LOGFILE",
    "LOGOPTS",
    "LOOPBACK",
    "NAME",
    "NOFILE",
    "OFF",
    "ON",
    "PIDFILE",
    "PORT",
    "RESETCMD",
    "SEROPTS",
    "SERVER",
    "SYSLOG",
    "TCPWRAPPERS",
    "TESTOPTS",
    "TIMESTAMP",
    NULL
};


/*  The <sys/syslog.h> prioritynames[] and facilitynames[] are not portable
 *    (solaris2.8 doesn't have 'em), so they're effectively reproduced here.
 */
typedef struct tag {
    const char *key;
    int         val;
} tag_t;

static tag_t logPriorities[] = {
    { "alert",      LOG_ALERT },
    { "crit",       LOG_CRIT },
    { "critical",   LOG_CRIT },
    { "debug",      LOG_DEBUG },
    { "emerg",      LOG_EMERG },
    { "emergency",  LOG_EMERG },
    { "err",        LOG_ERR },
    { "error",      LOG_ERR },
    { "info",       LOG_INFO },
    { "notice",     LOG_NOTICE },
    { "panic",      LOG_EMERG },
    { "warn",       LOG_WARNING },
    { "warning",    LOG_WARNING },
    { NULL,         -1 }
};

static tag_t logFacilities[] = {
    { "auth",       LOG_AUTH },
#ifdef LOG_AUTHPRIV
    { "authpriv",   LOG_AUTHPRIV },
#endif /* LOG_AUTHPRIV */
    { "cron",       LOG_CRON },
    { "daemon",     LOG_DAEMON },
    { "kern",       LOG_KERN },
    { "lpr",        LOG_LPR },
    { "mail",       LOG_MAIL },
    { "news",       LOG_NEWS },
    { "user",       LOG_USER },
    { "uucp",       LOG_UUCP },
    { "local0",     LOG_LOCAL0 },
    { "local1",     LOG_LOCAL1 },
    { "local2",     LOG_LOCAL2 },
    { "local3",     LOG_LOCAL3 },
    { "local4",     LOG_LOCAL4 },
    { "local5",     LOG_LOCAL5 },
    { "local6",     LOG_LOCAL6 },
    { "local7",     LOG_LOCAL7 },
    { NULL,         -1 }
};

typedef struct console_strs {
    char *name;
    char *dev;
    char *log;
    char *lopts;
    char *sopts;
#if WITH_FREEIPMI
    char *iopts;
#endif /* WITH_FREEIPMI */
    char *topts;
} console_strs_t;


static void display_server_help(char *prog);
static void signal_daemon(server_conf_t *conf);
static void parse_console_directive(server_conf_t *conf, Lex l);
static int process_console(server_conf_t *conf, console_strs_t *con_p,
    char *errbuf, int errbuflen);
static void parse_global_directive(server_conf_t *conf, Lex l);
static void parse_server_directive(server_conf_t *conf, Lex l);
static int read_pidfile(const char *pidfile);
static int write_pidfile(const char *pidfile);
static int lookup_syslog_priority(const char *priority);
static int lookup_syslog_facility(const char *facility);


server_conf_t * create_server_conf(void)
{
    server_conf_t *conf;
    char buf[PATH_MAX];

    if (!(conf = malloc(sizeof(server_conf_t)))) {
        out_of_memory();
    }
    conf->cwd = NULL;
    conf->confFileName = create_string(CONMAN_CONF);
    conf->coreDumpDir = NULL;
    conf->execPath = NULL;
    conf->logDirName = NULL;
    conf->logFileName = NULL;
    conf->logFmtName = NULL;
    conf->logFilePtr = NULL;
    conf->logFileLevel = LOG_INFO;
    conf->numOpenFiles = 0;
    conf->pidFileName = NULL;
    conf->resetCmd = NULL;
    conf->syslogFacility = -1;
    conf->throwSignal = -1;
    conf->tStampMinutes = 0;
    conf->tStampNext = 0;
    /*
     *  The conf file's fd must be saved and kept open in order to hold an
     *    fcntl-style lock.  This lock is used to ensure only one instance
     *    of a given configuration can be running.  It is also used to
     *    obtain the pid of an active daemon in order to support the
     *    '-k' and '-r' cmdline options.
     */
    conf->fd = -1;
    conf->port = 0;
    conf->ld = -1;
    conf->objs = list_create((ListDelF) destroy_obj);
    if (!(conf->tp = tpoll_create(0))) {
        log_err(0, "Unable to create object for multiplexing I/O");
    }
    conf->globalLogName = NULL;
    conf->globalLogOpts.enableSanitize = DEFAULT_LOGOPT_SANITIZE;
    conf->globalLogOpts.enableTimestamp = DEFAULT_LOGOPT_TIMESTAMP;
    conf->globalLogOpts.enableLock = DEFAULT_LOGOPT_LOCK;
    conf->globalSerOpts.bps = DEFAULT_SEROPT_BPS;
    conf->globalSerOpts.databits = DEFAULT_SEROPT_DATABITS;
    conf->globalSerOpts.parity = DEFAULT_SEROPT_PARITY;
    conf->globalSerOpts.stopbits = DEFAULT_SEROPT_STOPBITS;

#if WITH_FREEIPMI
    if (init_ipmi_opts(&conf->globalIpmiOpts) < 0) {
        log_err(0, "Unable to initialize default IPMI options");
    }
    conf->numIpmiObjs = 0;
#endif /* WITH_FREEIPMI */

    if (init_test_opts(&conf->globalTestOpts) < 0) {
        log_err(0, "Unable to initialize default test options");
    }
    conf->enableCoreDump = 0;
    conf->enableKeepAlive = 1;
    conf->enableLoopBack = 1;
    conf->enableTCPWrap = 0;
    conf->enableVerbose = 0;
    conf->enableZeroLogs = 0;
    conf->enableForeground = 0;
    /*
     *  Copy the current working directory before we chdir() away.
     *  Since logfiles can be re-opened after the daemon has chdir()'d,
     *    we need to prepend relative paths with the cwd.
     */
    if (!getcwd(buf, sizeof(buf))) {
        log_err(errno, "Unable to determine working directory");
    }
    conf->cwd = create_string(buf);
    conf->logDirName = create_string(buf);
    return(conf);
}


void destroy_server_conf(server_conf_t *conf)
{
    if (!conf) {
        return;
    }
    if (conf->pidFileName) {
        if (unlink(conf->pidFileName) < 0) {
            log_msg(LOG_ERR, "Unable to delete pid file \"%s\": %s",
                conf->pidFileName, strerror(errno));
        }
    }
    if (conf->fd >= 0) {
        if (close(conf->fd) < 0) {
            log_msg(LOG_ERR, "Unable to close config file \"%s\": %s",
                conf->confFileName, strerror(errno));
        }
        conf->fd = -1;
    }
    if (conf->ld >= 0) {
        if (close(conf->ld) < 0) {
            log_msg(LOG_ERR, "Unable to close listening socket: %s",
                strerror(errno));
        }
        conf->ld = -1;
    }
    if (conf->objs) {
        list_destroy(conf->objs);
    }
    if (conf->tp) {
        tpoll_destroy(conf->tp);
    }
    destroy_string(conf->confFileName);
    destroy_string(conf->coreDumpDir);
    destroy_string(conf->cwd);
    destroy_string(conf->execPath);
    destroy_string(conf->globalLogName);
    destroy_string(conf->logDirName);
    destroy_string(conf->logFileName);
    destroy_string(conf->logFmtName);
    destroy_string(conf->pidFileName);
    destroy_string(conf->resetCmd);
    free(conf);
    return;
}


void process_cmdline(server_conf_t *conf, int argc, char *argv[])
{
    int c;

    opterr = 0;
    while ((c = getopt(argc, argv, "c:FhkLp:P:qrvVz")) != -1) {
        switch(c) {
        case 'c':
            destroy_string(conf->confFileName);
            conf->confFileName = create_string(optarg);
            break;
        case 'F':
            conf->enableForeground = 1;
            break;
        case 'h':
            display_server_help(argv[0]);
            exit(0);
        case 'k':
            conf->throwSignal = SIGTERM;
            break;
        case 'L':
            printf("%s", conman_license);
            exit(0);
        case 'p':
            if ((conf->port = atoi(optarg)) <= 0) {
                log_err(0, "CMDLINE: invalid port \"%d\"", conf->port);
            }
            break;
        case 'P':
            destroy_string(conf->pidFileName);
            conf->pidFileName = (optarg[0] == '/') ?
                create_string(optarg) :
                create_format_string("%s/%s", conf->cwd, optarg);
            break;
        case 'q':
            conf->throwSignal = 0;      /* null signal for error checking */
            break;
        case 'r':
            conf->throwSignal = SIGHUP;
            break;
        case 'v':
            conf->enableVerbose = 1;
            break;
        case 'V':
            printf("%s-%s%s\n", PROJECT, VERSION, SERVER_FEATURES);
            exit(0);
        case 'z':
            conf->enableZeroLogs = 1;
            break;
        case '?':                       /* invalid option */
            log_err(0, "CMDLINE: invalid option \"%c\"", optopt);
            break;
        default:
            log_err(0, "CMDLINE: option \"%c\" not implemented", c);
            break;
        }
    }
    return;
}


void process_config(server_conf_t *conf)
{
    pid_t pid;
    struct stat fdStat;
    int len;
    char *buf;
    int n;
    Lex l;
    int tok;

    /*  Keep conf->fd open after parsing the file in order to obtain the lock.
     */
    if ((conf->fd = open(conf->confFileName, O_RDONLY)) < 0) {
        log_err(errno, "Unable to open \"%s\"", conf->confFileName);
    }
    /*  The daemon must be signalled, if needed, after opening the file but
     *    before obtaining a lock.
     */
    if (conf->throwSignal >= 0) {
        signal_daemon(conf);
        exit(0);
    }
    /*  Must use a read lock here since the file is only open for reading.
     *    Exclusive access is ensured by testing for a write lock afterwards.
     */
    if (get_read_lock(conf->fd) < 0) {
        log_err(0, "Unable to lock configuration \"%s\"",
            conf->confFileName);
    }
    if ((pid = is_write_lock_blocked(conf->fd)) > 0) {
        log_err(0, "Configuration \"%s\" in use by pid %d",
            conf->confFileName, pid);
    }
    /*  Read config into memory for parsing.
     */
    DPRINTF((9, "Opened config \"%s\": fd=%d.\n",
        conf->confFileName, conf->fd));
    set_fd_closed_on_exec(conf->fd);
    if (fstat(conf->fd, &fdStat) < 0) {
        log_err(errno, "Unable to stat \"%s\"", conf->confFileName);
    }
    len = fdStat.st_size;
    if (!(buf = malloc(len + 1))) {
        out_of_memory();
    }
    if ((n = read_n(conf->fd, buf, len)) < 0) {
        log_err(errno, "Unable to read \"%s\"", conf->confFileName);
    }
    assert(n == len);
    buf[len] = '\0';

    l = lex_create(buf, server_conf_strs);
    while ((tok = lex_next(l)) != LEX_EOF) {
        switch(tok) {
        case SERVER_CONF_CONSOLE:
            parse_console_directive(conf, l);
            break;
        case SERVER_CONF_GLOBAL:
            parse_global_directive(conf, l);
            break;
        case SERVER_CONF_SERVER:
            parse_server_directive(conf, l);
            break;
        case LEX_EOL:
            break;
        case LEX_ERR:
            log_msg(LOG_ERR, "CONFIG[%s:%d]: unmatched quote",
                conf->confFileName, lex_line(l));
            break;
        default:
            log_msg(LOG_ERR, "CONFIG[%s:%d]: unrecognized token '%s'",
                conf->confFileName, lex_line(l), lex_text(l));
            while (tok != LEX_EOL && tok != LEX_EOF) {
                tok = lex_next(l);
            }
            break;
        }
    }
    lex_destroy(l);
    free(buf);

    if (conf->port <= 0) {              /* port not set so use default */
        conf->port = atoi(CONMAN_PORT);
    }
    if (conf->logFileName) {
        if (strchr(conf->logFileName, '%')) {
            conf->logFmtName = create_string(conf->logFileName);
        }
    }
    if (conf->pidFileName) {
        if (write_pidfile(conf->pidFileName) < 0) {
            free(conf->pidFileName);
            conf->pidFileName = NULL;   /* prevent unlink() at exit */
        }
    }
    return;
}


static void display_server_help(char *prog)
{
/*  Displays a help message for the server's command-line options.
 */
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("\n");
    printf("  -c FILE   Specify configuration. [%s]\n", CONMAN_CONF);
    printf("  -F        Run daemon in foreground.\n");
    printf("  -h        Display this help.\n");
    printf("  -k        Kill daemon.\n");
    printf("  -L        Display license information.\n");
    printf("  -p PORT   Specify port number. [%d]\n", atoi(CONMAN_PORT));
    printf("  -P FILE   Specify PID file.\n");
    printf("  -q        Query daemon's pid.\n");
    printf("  -r        Re-open log files.\n");
    printf("  -v        Be verbose.\n");
    printf("  -V        Display version information.\n");
    printf("  -z        Zero log files.\n");
    printf("\n");
    return;
}


static void signal_daemon(server_conf_t *conf)
{
/*  Signals the daemon running with the configuration specified by 'conf'.
 */
    pid_t pid;
    const char *msg;

    assert(conf != NULL);
    assert(conf->confFileName != NULL);
    assert(conf->fd >= 0);
    assert(conf->throwSignal >= 0);

    /*  Attempt to get the pid from the lock held on the config file.
     *  However, that could fail if the file has been modified since the
     *  daemon was invoked; this is because some editors move the old file
     *  out of the way and write a new file in its place.  As such, fall
     *  back on reading the pid file if one is available.  Unfortunately,
     *  that could fail as well if the name of the pid file was altered in
     *  the config file.  Note that the pid file does not support conversion
     *  specifiers in order to reduce the likelihood of this happening.
     */
    if ( !(pid = is_write_lock_blocked(conf->fd))
      && !(pid = read_pidfile(conf->pidFileName)) ) {
        log_err(0, "Configuration \"%s\" does not appear to be active",
            conf->confFileName);
    }
    /*  Prevent pidfile from being unlink()d by destroy_server_conf().
     */
    if (conf->pidFileName) {
        free(conf->pidFileName);
        conf->pidFileName = NULL;
    }
    /*  "Now go do that voodoo that you do so well."
     */
    if (kill(pid, conf->throwSignal) < 0) {
        if ((conf->throwSignal == 0) && (errno == EPERM)) {
            ; /* ignore permission error for pid query */
        }
        else if (errno == ESRCH) {
            log_err(0, "Configuration \"%s\" does not appear to be active",
                conf->confFileName);
        }
        else {
            log_err(errno,
                "Configuration \"%s\" (pid %d) cannot be sent signal=%d",
                conf->confFileName, (int) pid, conf->throwSignal);
        }
    }
    /*  "We don't need no stinkin' badges."
     */
    if (conf->throwSignal == 0) {
        printf("%d\n", (int) pid);
    }
    else if (conf->enableVerbose) {
        if (conf->throwSignal == SIGHUP) {
            msg = "reconfigured on";
        }
        else if (conf->throwSignal == SIGTERM) {
            msg = "terminated on";
        }
        else {
            msg = "sent";
        }
        fprintf(stderr, "Configuration \"%s\" (pid %d) %s signal=%d\n",
            conf->confFileName, (int) pid, msg, conf->throwSignal);
    }
#if WITH_FREEIPMI
    ipmi_fini();
#endif /* WITH_FREEIPMI */

    destroy_server_conf(conf);
    exit(0);
}


static void parse_console_directive(server_conf_t *conf, Lex l)
{
/*  CONSOLE NAME="<str>" DEV="<file>" [LOG="<file>"]
 *    [LOGOPTS="<str>"] [SEROPTS="<str>"] [IPMIOPTS="<str>"] [TESTOPTS="<str>"]
 *  Note: IPMIOPTS is only available if WITH_FREEIPMI is defined.
 */
    const char *directive;              /* name of directive being parsed */
    int tok;
    const char *tokstr;
    int done = 0;
    char err[MAX_LINE] = "";
    console_strs_t con;

    memset(&con, 0, sizeof(con));

    directive = lex_tok_to_str(l, lex_prev(l));
    if (!directive) {
        log_err(0, "Unable to lookup string for console directive");
    }
    while (!done && !*err) {
        tok = lex_next(l);
        tokstr = lex_tok_to_str(l, tok);
        switch(tok) {

        case SERVER_CONF_NAME:
            if (lex_next(l) != '=') {
                snprintf(err, sizeof(err),
                    "expected '=' after %s keyword", tokstr);
            }
            else if ((lex_next(l) != LEX_STR)
                    || is_empty_string(lex_text(l))) {
                snprintf(err, sizeof(err),
                    "expected STRING for %s value", tokstr);
            }
            else {
                replace_string(&con.name, lex_text(l));
            }
            break;

        case SERVER_CONF_DEV:
            if (lex_next(l) != '=') {
                snprintf(err, sizeof(err),
                    "expected '=' after %s keyword", tokstr);
            }
            else if ((lex_next(l) != LEX_STR)
                    || is_empty_string(lex_text(l))) {
                snprintf(err, sizeof(err),
                    "expected STRING for %s value", tokstr);
            }
            else {
                replace_string(&con.dev, lex_text(l));
            }
            break;

        case SERVER_CONF_LOG:
            if (lex_next(l) != '=') {
                snprintf(err, sizeof(err),
                    "expected '=' after %s keyword", tokstr);
            }
            else if (lex_next(l) != LEX_STR) {
                snprintf(err, sizeof(err),
                    "expected STRING for %s value", tokstr);
            }
#ifdef DO_CONF_ESCAPE_ERROR
            else if (strchr(lex_text(l), DEPRECATED_CONF_ESCAPE)) {
                snprintf(err, sizeof(err),
                    "ignoring %s %s value with deprecated '%c' -- use '%%N'",
                    directive, tokstr, DEPRECATED_CONF_ESCAPE);
            }
#endif /* DO_CONF_ESCAPE_ERROR */
            else if (is_empty_string(lex_text(l))) {
                replace_string(&con.log, "");
            }
            else if ((lex_text(l)[0] != '/') && (conf->logDirName)) {
                destroy_string(con.log);
                con.log = create_format_string("%s/%s",
                    conf->logDirName, lex_text(l));
            }
            else {
                replace_string(&con.log, lex_text(l));
            }
            break;

        case SERVER_CONF_LOGOPTS:
            if (lex_next(l) != '=') {
                snprintf(err, sizeof(err),
                    "expected '=' after %s keyword", tokstr);
            }
            else if ((lex_next(l) != LEX_STR)
                    || is_empty_string(lex_text(l))) {
                snprintf(err, sizeof(err),
                    "expected STRING for %s value", tokstr);
            }
            else {
                replace_string(&con.lopts, lex_text(l));
            }
            break;

        case SERVER_CONF_SEROPTS:
            if (lex_next(l) != '=') {
                snprintf(err, sizeof(err),
                    "expected '=' after %s keyword", tokstr);
            }
            else if ((lex_next(l) != LEX_STR)
                    || is_empty_string(lex_text(l))) {
                snprintf(err, sizeof(err),
                    "expected STRING for %s value", tokstr);
            }
            else {
                replace_string(&con.sopts, lex_text(l));
            }
            break;

#if WITH_FREEIPMI
        case SERVER_CONF_IPMIOPTS:
            if (lex_next(l) != '=') {
                snprintf(err, sizeof(err),
                    "unexpected '=' after %s keyword", tokstr);
            }
            else if (lex_next(l) != LEX_STR) {
                snprintf(err, sizeof(err),
                    "expected STRING for %s value", tokstr);
            }
            else {
                replace_string(&con.iopts, lex_text(l));
            }
            break;
#endif /* WITH_FREEIPMI */

        case SERVER_CONF_TESTOPTS:
            if (lex_next(l) != '=') {
                snprintf(err, sizeof(err),
                    "unexpected '=' after %s keyword", tokstr);
            }
            else if (lex_next(l) != LEX_STR) {
                snprintf(err, sizeof(err),
                    "expected STRING for %s value", tokstr);
            }
            else {
                replace_string(&con.topts, lex_text(l));
            }
            break;

        case LEX_EOF:
        case LEX_EOL:
            done = 1;
            break;

        case LEX_ERR:
            snprintf(err, sizeof(err), "unmatched quote");
            break;

        default:
            snprintf(err, sizeof(err), "unrecognized token '%s'", lex_text(l));
            break;
        }
    }
    if (!*err) {
        if (!con.name || !con.dev) {
            snprintf(err, sizeof(err), "incomplete %s directive", directive);
        }
        else {
            process_console(conf, &con, err, sizeof(err));
        }
    }
    if (*err) {
        log_msg(LOG_ERR, "CONFIG[%s:%d]: %s",
            conf->confFileName, lex_line(l), err);
        while (lex_prev(l) != LEX_EOL && lex_prev(l) != LEX_EOF) {
            (void) lex_next(l);
        }
    }
    destroy_string(con.name);
    destroy_string(con.dev);
    destroy_string(con.log);
    destroy_string(con.lopts);
    destroy_string(con.sopts);
#if WITH_FREEIPMI
    destroy_string(con.iopts);
#endif /* WITH_FREEIPMI */
    destroy_string(con.topts);
    return;
}


static int process_console(server_conf_t *conf, console_strs_t *con_p,
    char *errbuf, int errbuflen)
{
    List         args = NULL;
    char        *p;
    char        *q;
    char         quote;
    int          rc;
    char         buf[MAX_LINE];
    char        *arg0;
    char        *host = NULL;
    int          port;
    char        *path = NULL;
    obj_t       *console;
    seropt_t     seropts;
#if WITH_FREEIPMI
    ipmiopt_t    ipmiopts;
#endif /* WITH_FREEIPMI */
    logopt_t     logopts;
    test_opt_t   testopts;
    obj_t       *logfile;

    assert(conf != NULL);
    assert(con_p != NULL);
    assert(con_p->name != NULL);
    assert(con_p->dev != NULL);
    assert(errbuf != NULL);
    assert(errbuflen > 0);

    errbuf[ 0 ] = '\0';
    if (!(args = list_create((ListDelF) destroy_string))) {
        out_of_memory();
    }
    q = NULL;
    while ((rc = parse_string(con_p->dev, &p, &q, &quote)) > 0) {
        if (quote != '\'') {
            if (substitute_string(buf, sizeof(buf), p, 'N', con_p->name) < 0) {
                snprintf(errbuf, errbuflen,
                    "console [%s] dev string substitution failed",
                    con_p->name);
                goto err;
            }
            p = buf;
        }
        list_append(args, create_string(p));
    }
    if (rc < 0) {
        snprintf(errbuf, errbuflen,
            "console [%s] dev string parse error", con_p->name);
        goto err;
    }
    if (!(arg0 = list_peek(args))) {
        snprintf(errbuf, errbuflen,
            "console [%s] dev string is empty", con_p->name);
        goto err;
    }
    if (is_unixsock_dev(arg0, conf->cwd, &path)) {
        if (list_count(args) != 1) {
            snprintf(errbuf, errbuflen,
                "console [%s] dev string has too many args", con_p->name);
            goto err;
        }
        if (!(console = create_unixsock_obj(
                conf, con_p->name, path, errbuf, errbuflen))) {
            goto err;
        }
        free(path);
        path = NULL;
    }
    else if (is_telnet_dev(arg0, &host, &port)) {
        if (list_count(args) != 1) {
            snprintf(errbuf, errbuflen,
                "console [%s] dev string has too many args", con_p->name);
            goto err;
        }
        if (!(console = create_telnet_obj(
                conf, con_p->name, host, port, errbuf, errbuflen))) {
            goto err;
        }
        free(host);
        host = NULL;
    }
    else if (is_serial_dev(arg0, conf->cwd, &path)) {
        if (list_count(args) != 1) {
            snprintf(errbuf, errbuflen,
                "console [%s] dev string has too many args", con_p->name);
            goto err;
        }
        if (access(path, R_OK | W_OK) < 0) {
            snprintf(errbuf, errbuflen,
                "console [%s] device \"%s\" is not readable/writable",
                con_p->name, path);
            goto err;
        }
        seropts = conf->globalSerOpts;
        if (con_p->sopts && parse_serial_opts(
                &seropts, con_p->sopts, errbuf, errbuflen) < 0) {
            goto err;
        }
        if (!(console = create_serial_obj(
                conf, con_p->name, path, &seropts, errbuf, errbuflen))) {
            goto err;
        }
        free(path);
        path = NULL;
    }
    else if (is_process_dev(arg0, conf->cwd, conf->execPath, &path)) {
        free(list_pop(args));
        arg0 = list_push(args, path);
        path = NULL;
        if (!(console = create_process_obj(
                conf, con_p->name, args, errbuf, errbuflen))) {
            goto err;
        }
    }
#if WITH_FREEIPMI
    else if (is_ipmi_dev(arg0, &host)) {
        if (list_count(args) != 1) {
            snprintf(errbuf, errbuflen,
                "console [%s] dev string has too many args", con_p->name);
            goto err;
        }
        ipmiopts = conf->globalIpmiOpts;
        if (con_p->iopts && parse_ipmi_opts(
                &ipmiopts, con_p->iopts, errbuf, errbuflen) < 0) {
            goto err;
        }
        if (!(console = create_ipmi_obj(
                conf, con_p->name, &ipmiopts, host, errbuf, errbuflen))) {
            goto err;
        }
        free(host);
        host = NULL;
    }
#endif /* WITH_FREEIPMI */
    else if (is_test_dev(arg0)) {
        if (list_count(args) != 1) {
            snprintf(errbuf, errbuflen,
                "console [%s] dev string has too many args", con_p->name);
            goto err;
        }
        testopts = conf->globalTestOpts;
        if (con_p->topts && parse_test_opts(
                &testopts, con_p->topts, errbuf, errbuflen) < 0) {
            goto err;
        }
        if (!(console = create_test_obj(
                conf, con_p->name, &testopts, errbuf, errbuflen))) {
            goto err;
        }
    }
    else {
        snprintf(errbuf, errbuflen,
            "console [%s] device \"%s\" type unrecognized",
            con_p->name, arg0);
        goto err;
    }
    if ((con_p->log && con_p->log[ 0 ] != '\0')
            || (!con_p->log && conf->globalLogName)) {
        if (con_p->log) {
            strlcpy(buf, con_p->log, sizeof(buf));
        }
        else if ((conf->globalLogName[ 0 ] != '/') && (conf->logDirName)) {
            snprintf(buf, sizeof(buf), "%s/%s",
                conf->logDirName, conf->globalLogName);
            buf[ sizeof(buf) - 1 ] = '\0';
        }
        else {
            strlcpy(buf, conf->globalLogName, sizeof(buf));
        }
        logopts = conf->globalLogOpts;
        if (con_p->lopts && parse_logfile_opts(
                &logopts, con_p->lopts, errbuf, errbuflen) < 0) {
            goto err;
        }
        if (!(logfile = create_logfile_obj(
                conf, buf, console, &logopts, errbuf, errbuflen))) {
            goto err;
        }
        link_objs(console, logfile);
    }
    list_destroy(args);
    return(0);

err:
    errbuf[ errbuflen - 1 ] = '\0';
    list_destroy(args);
    destroy_string(host);
    destroy_string(path);
    return(-1);
}


static void parse_global_directive(server_conf_t *conf, Lex l)
{
    const char *directive;              /* name of directive being parsed */
    int tok;
    const char *tokstr;
    int done = 0;
    char err[MAX_LINE] = "";

    directive = lex_tok_to_str(l, lex_prev(l));
    if (!directive) {
        log_err(0, "Unable to lookup string for global directive");
    }
    while (!done && !*err) {
        tok = lex_next(l);
        tokstr = lex_tok_to_str(l, tok);
        switch(tok) {

        case SERVER_CONF_LOG:
            if (lex_next(l) != '=') {
                snprintf(err, sizeof(err),
                    "expected '=' after %s keyword", tokstr);
            }
            else if (lex_next(l) != LEX_STR) {
                snprintf(err, sizeof(err),
                    "expected STRING for %s value", tokstr);
            }
            else if (is_empty_string(lex_text(l))) {
                /*
                 *  Unset global log name if string is empty.
                 */
                destroy_string(conf->globalLogName);
                conf->globalLogName = NULL;
            }
#ifdef DO_CONF_ESCAPE_ERROR
            else if (strchr(lex_text(l), DEPRECATED_CONF_ESCAPE)) {
                snprintf(err, sizeof(err),
                    "ignoring %s %s value with deprecated '%c' -- use '%%N'",
                    directive, tokstr, DEPRECATED_CONF_ESCAPE);
            }
#endif /* DO_CONF_ESCAPE_ERROR */
            else if (!strstr(lex_text(l), "%N")
                    && !strstr(lex_text(l), "%D")) {
                snprintf(err, sizeof(err),
                    "ignoring %s %s value without '%%N' or '%%D'",
                    directive, tokstr);
            }
            else {
                destroy_string(conf->globalLogName);
                conf->globalLogName = create_string(lex_text(l));
            }
            break;

        case SERVER_CONF_LOGOPTS:
            if (lex_next(l) != '=') {
                snprintf(err, sizeof(err),
                    "expected '=' after %s keyword", tokstr);
            }
            else if ((lex_next(l) != LEX_STR)
                    || is_empty_string(lex_text(l))) {
                snprintf(err, sizeof(err),
                    "expected STRING for %s value", tokstr);
            }
            else {
                parse_logfile_opts(&conf->globalLogOpts, lex_text(l),
                    err, sizeof(err));
            }
            break;

        case SERVER_CONF_SEROPTS:
            if (lex_next(l) != '=') {
                snprintf(err, sizeof(err),
                    "expected '=' after %s keyword", tokstr);
            }
            else if ((lex_next(l) != LEX_STR)
                    || is_empty_string(lex_text(l))) {
                snprintf(err, sizeof(err),
                    "expected STRING for %s value", tokstr);
            }
            else {
                parse_serial_opts(&conf->globalSerOpts, lex_text(l),
                    err, sizeof(err));
            }
            break;

#if WITH_FREEIPMI
        case SERVER_CONF_IPMIOPTS:
            if (lex_next(l) != '=') {
                snprintf(err, sizeof(err),
                    "expected '=' after %s keyword", tokstr);
            }
            else if ((lex_next(l) != LEX_STR)
                    || is_empty_string(lex_text(l))) {
                snprintf(err, sizeof(err),
                    "expected STRING for %s value", tokstr);
            }
            else {
                parse_ipmi_opts(&conf->globalIpmiOpts, lex_text(l),
                    err, sizeof(err));
            }
            break;
#endif /* WITH_FREEIPMI */

        case SERVER_CONF_TESTOPTS:
            if (lex_next(l) != '=') {
                snprintf(err, sizeof(err),
                    "expected '=' after %s keyword", tokstr);
            }
            else if ((lex_next(l) != LEX_STR)
                    || is_empty_string(lex_text(l))) {
                snprintf(err, sizeof(err),
                    "expected STRING for %s value", tokstr);
            }
            else {
                parse_test_opts(&conf->globalTestOpts, lex_text(l),
                    err, sizeof(err));
            }
            break;

        case LEX_EOF:
        case LEX_EOL:
            done = 1;
            break;

        case LEX_ERR:
            snprintf(err, sizeof(err), "unmatched quote");
            break;

        default:
            snprintf(err, sizeof(err), "unrecognized token '%s'", lex_text(l));
            break;
        }
    }

    if (*err) {
        log_msg(LOG_ERR, "CONFIG[%s:%d]: %s",
            conf->confFileName, lex_line(l), err);
        while (lex_prev(l) != LEX_EOL && lex_prev(l) != LEX_EOF) {
            (void) lex_next(l);
        }
    }
    return;
}


static void parse_server_directive(server_conf_t *conf, Lex l)
{
    const char *directive;              /* name of directive being parsed */
    int tok;
    const char *tokstr;
    int done = 0;
    char err[MAX_LINE] = "";
    char *p;
    int n;
    struct stat st;

    /* Prevent command-line options from being overridden by the config file.
     */
    const int isPidFileNameSet = (conf->pidFileName != NULL);
    const int isPortSet = (conf->port > 0);

    directive = lex_tok_to_str(l, lex_prev(l));
    if (!directive) {
        log_err(0, "Unable to lookup string for server directive");
    }
    while (!done && !*err) {
        tok = lex_next(l);
        tokstr = lex_tok_to_str(l, tok);
        switch(tok) {

        case SERVER_CONF_COREDUMP:
            if (lex_next(l) != '=') {
                snprintf(err, sizeof(err),
                    "expected '=' after %s keyword", tokstr);
            }
            else if (lex_next(l) == SERVER_CONF_ON) {
                conf->enableCoreDump = 1;
            }
            else if (lex_prev(l) == SERVER_CONF_OFF) {
                conf->enableCoreDump = 0;
            }
            else {
                snprintf(err, sizeof(err),
                    "expected ON or OFF for %s value", tokstr);
            }
            break;

        case SERVER_CONF_COREDUMPDIR:
            if (lex_next(l) != '=') {
                snprintf(err, sizeof(err),
                    "expected '=' after %s keyword", tokstr);
            }
            else if ((lex_next(l) != LEX_STR)) {
                snprintf(err, sizeof(err),
                    "expected STRING for %s value", tokstr);
            }
            else if (is_empty_string(lex_text(l))) {
                destroy_string(conf->coreDumpDir);
                conf->coreDumpDir = NULL;
            }
            else if (stat(lex_text(l), &st) < 0) {
                snprintf(err, sizeof(err),
                    "cannot stat %s \"%s\"", tokstr, lex_text(l));
            }
            else if (!S_ISDIR(st.st_mode)) {
                snprintf(err, sizeof(err),
                    "invalid %s \"%s\" not a directory", tokstr, lex_text(l));
            }
            else {
                p = (lex_text(l)[0] != '/')
                    ? create_format_string("%s/%s", conf->cwd, lex_text(l))
                    : create_string(lex_text(l));
                destroy_string(conf->coreDumpDir);
                conf->coreDumpDir = p;
                /*
                 *  Remove trailing slashes from dir name.
                 */
                p += strlen(p) - 1;
                while ((p > conf->coreDumpDir) && (*p == '/')) {
                    *p-- = '\0';
                }
            }
            break;

        case SERVER_CONF_EXECPATH:
            if (lex_next(l) != '=') {
                snprintf(err, sizeof(err),
                    "expected '=' after %s keyword", tokstr);
            }
            else if ((lex_next(l) != LEX_STR)) {
                snprintf(err, sizeof(err),
                    "expected STRING for %s value", tokstr);
            }
            else if (strlen(lex_text(l)) >= PATH_MAX) {
                snprintf(err, sizeof(err),
                    "exceeded max length for %s value", tokstr);
            }
            else if (is_empty_string(lex_text(l))) {
                destroy_string(conf->execPath);
                conf->execPath = NULL;
            }
            else {
                destroy_string(conf->execPath);
                conf->execPath = create_string(lex_text(l));
            }
            break;

        case SERVER_CONF_KEEPALIVE:
            if (lex_next(l) != '=') {
                snprintf(err, sizeof(err),
                    "expected '=' after %s keyword", tokstr);
            }
            else if (lex_next(l) == SERVER_CONF_ON) {
                conf->enableKeepAlive = 1;
            }
            else if (lex_prev(l) == SERVER_CONF_OFF) {
                conf->enableKeepAlive = 0;
            }
            else {
                snprintf(err, sizeof(err),
                    "expected ON or OFF for %s value", tokstr);
            }
            break;

        case SERVER_CONF_LOGDIR:
            if (lex_next(l) != '=') {
                snprintf(err, sizeof(err),
                    "expected '=' after %s keyword", tokstr);
            }
            else if ((lex_next(l) != LEX_STR)) {
                snprintf(err, sizeof(err),
                    "expected STRING for %s value", tokstr);
            }
            else if (is_empty_string(lex_text(l))) {
                destroy_string(conf->logDirName);
                conf->logDirName = create_string(conf->cwd);
            }
            else {
                p = (lex_text(l)[0] != '/')
                    ? create_format_string("%s/%s", conf->cwd, lex_text(l))
                    : create_string(lex_text(l));
                destroy_string(conf->logDirName);
                conf->logDirName = p;
            }
            break;

        case SERVER_CONF_LOGFILE:
            if (lex_next(l) != '=') {
                snprintf(err, sizeof(err),
                    "expected '=' after %s keyword", tokstr);
            }
            else if ((lex_next(l) != LEX_STR)
                    || is_empty_string(lex_text(l))) {
                snprintf(err, sizeof(err),
                    "expected STRING for %s value", tokstr);
            }
            else {
                destroy_string(conf->logFileName);
                if ((lex_text(l)[0] != '/') && (conf->logDirName)) {
                    conf->logFileName = create_format_string("%s/%s",
                        conf->logDirName, lex_text(l));
                }
                else {
                    conf->logFileName = create_string(lex_text(l));
                }
                if ((p = strrchr(conf->logFileName, ','))) {
                    *p++ = '\0';
                    if ((n = lookup_syslog_priority(p)) < 0) {
                        snprintf(err, sizeof(err),
                            "invalid %s priority \"%s\"", tokstr, p);
                    }
                    else {
                        conf->logFileLevel = n;
                    }
                }
            }
            break;

        case SERVER_CONF_LOOPBACK:
            if (lex_next(l) != '=') {
                snprintf(err, sizeof(err),
                    "expected '=' after %s keyword", tokstr);
            }
            else if (lex_next(l) == SERVER_CONF_ON) {
                conf->enableLoopBack = 1;
            }
            else if (lex_prev(l) == SERVER_CONF_OFF) {
                conf->enableLoopBack = 0;
            }
            else {
                snprintf(err, sizeof(err),
                    "expected ON or OFF for %s value", tokstr);
            }
            break;

        case SERVER_CONF_NOFILE:
            if (lex_next(l) != '=') {
                snprintf(err, sizeof(err),
                    "expected '=' after %s keyword", tokstr);
            }
            else if (lex_next(l) != LEX_INT) {
                snprintf(err, sizeof(err),
                    "expected INTEGER for %s value", tokstr);
            }
            else {
                n = atoi(lex_text(l));
                conf->numOpenFiles = n;
            }
            break;

        case SERVER_CONF_PIDFILE:
            if (lex_next(l) != '=') {
                snprintf(err, sizeof(err),
                    "expected '=' after %s keyword", tokstr);
            }
            else if ((lex_next(l) != LEX_STR)
                    || is_empty_string(lex_text(l))) {
                snprintf(err, sizeof(err),
                    "expected STRING for %s value", tokstr);
            }
            else if (!isPidFileNameSet) {
                destroy_string(conf->pidFileName);
                if (lex_text(l)[0] != '/') {
                    conf->pidFileName = create_format_string("%s/%s",
                        conf->cwd, lex_text(l));
                }
                else {
                    conf->pidFileName = create_string(lex_text(l));
                }
            }
            break;

        case SERVER_CONF_PORT:
            if (lex_next(l) != '=') {
                snprintf(err, sizeof(err),
                    "expected '=' after %s keyword", tokstr);
            }
            else if (lex_next(l) != LEX_INT) {
                snprintf(err, sizeof(err),
                    "expected INTEGER for %s value", tokstr);
            }
            else if ((n = atoi(lex_text(l))) <= 0) {
                snprintf(err, sizeof(err),
                    "invalid %s value %d", tokstr, n);
            }
            else if (!isPortSet) {
                conf->port = n;
            }
            break;

        case SERVER_CONF_RESETCMD:
            if (lex_next(l) != '=') {
                snprintf(err, sizeof(err),
                    "expected '=' after %s keyword", tokstr);
            }
            else if ((lex_next(l) != LEX_STR)
                    || is_empty_string(lex_text(l))) {
                snprintf(err, sizeof(err),
                    "expected STRING for %s value", tokstr);
            }
#ifdef DO_CONF_ESCAPE_ERROR
            else if (strchr(lex_text(l), DEPRECATED_CONF_ESCAPE)) {
                snprintf(err, sizeof(err),
                    "ignoring %s %s value with deprecated '%c' -- use '%%N'",
                    directive, tokstr, DEPRECATED_CONF_ESCAPE);
            }
#endif /* DO_CONF_ESCAPE_ERROR */
            else {
                destroy_string(conf->resetCmd);
                conf->resetCmd = create_string(lex_text(l));
            }
            break;

        case SERVER_CONF_SYSLOG:
            if (lex_next(l) != '=') {
                snprintf(err, sizeof(err),
                    "expected '=' after %s keyword", tokstr);
            }
            else if ((lex_next(l) != LEX_STR)
                    || is_empty_string(lex_text(l))) {
                snprintf(err, sizeof(err),
                    "expected STRING for %s value", tokstr);
            }
            else if ((n = lookup_syslog_facility(lex_text(l))) < 0) {
                snprintf(err, sizeof(err),
                    "invalid %s facility \"%s\"", tokstr, lex_text(l));
            }
            else {
                conf->syslogFacility = n;
            }
            break;

        case SERVER_CONF_TCPWRAPPERS:
#if ! WITH_TCP_WRAPPERS
            snprintf(err, sizeof(err),
                "%s keyword requires compile-time support", tokstr);
#else /* WITH_TCP_WRAPPERS */
            if (lex_next(l) != '=') {
                snprintf(err, sizeof(err),
                    "expected '=' after %s keyword", tokstr);
            }
            else if (lex_next(l) == SERVER_CONF_ON) {
                conf->enableTCPWrap = 1;
            }
            else if (lex_prev(l) == SERVER_CONF_OFF) {
                conf->enableTCPWrap = 0;
            }
            else {
                snprintf(err, sizeof(err),
                    "expected ON or OFF for %s value", tokstr);
            }
#endif /* WITH_TCP_WRAPPERS */
            break;

        case SERVER_CONF_TIMESTAMP:
            if (lex_next(l) != '=') {
                snprintf(err, sizeof(err),
                    "expected '=' after %s keyword", tokstr);
            }
            else if (lex_next(l) != LEX_INT) {
                snprintf(err, sizeof(err),
                    "expected INTEGER for %s value", tokstr);
            }
            else if ((n = atoi(lex_text(l))) < 0) {
                snprintf(err, sizeof(err),
                    "invalid %s value %d", tokstr, n);
            }
            else {
                conf->tStampMinutes = n;
                if ((lex_next(l) == LEX_EOF) || (lex_prev(l) == LEX_EOL)) {
                    ; /* no-op */
                }
                else if (lex_prev(l) == LEX_STR) {
                    if (lex_text(l)[1] != '\0') {
                        conf->tStampMinutes = -1;
                    }
                    else if (lex_text(l)[0] == 'm' || lex_text(l)[0] == 'M') {
                        ; /* no-op */
                    }
                    else if (lex_text(l)[0] == 'h' || lex_text(l)[0] == 'H') {
                        conf->tStampMinutes *= 60;
                    }
                    else if (lex_text(l)[0] == 'd' || lex_text(l)[0] == 'D') {
                        conf->tStampMinutes *= 60 * 24;
                    }
                    else {
                        conf->tStampMinutes = -1;
                    }
                }
                else {
                    conf->tStampMinutes = -1;
                }

                if (conf->tStampMinutes < 0) {
                    conf->tStampMinutes = 0;
                    snprintf(err, sizeof(err),
                        "expected (m|d|h) qualifier for %s value", tokstr);
                }
            }
            break;

        case LEX_EOF:
        case LEX_EOL:
            done = 1;
            break;

        case LEX_ERR:
            snprintf(err, sizeof(err), "unmatched quote");
            break;

        default:
            snprintf(err, sizeof(err), "unrecognized token '%s'", lex_text(l));
            break;
        }
    }

    if (*err) {
        log_msg(LOG_ERR, "CONFIG[%s:%d]: %s",
            conf->confFileName, lex_line(l), err);
        while (lex_prev(l) != LEX_EOL && lex_prev(l) != LEX_EOF) {
            (void) lex_next(l);
        }
    }
    return;
}


static int read_pidfile(const char *pidfile)
{
/*  Reads the PID from the specified pidfile.
 *  Returns the PID on success, or 0 if no pidfile is found.
 */
    FILE *fp;
    int n;
    int pid = 0;

    if (!pidfile) {
        return (0);
    }
    assert(pidfile[0] == '/');

    /*  FIXME: Ensure pathname is secure before trusting pid from pidfile.
     */
    if (!(fp = fopen(pidfile, "r"))) {
        return(0);
    }
    if ((n = fscanf(fp, "%d", &pid)) == EOF) {
        log_msg(LOG_WARNING, "Unable to read from pidfile \"%s\"", pidfile);
    }
    else if (n != 1) {
        log_msg(LOG_WARNING, "Unable to obtain pid from pidfile \"%s\"",
            pidfile);
    }
    if (fclose(fp) == EOF) {
        log_msg(LOG_WARNING, "Unable to close pidfile \"%s\"", pidfile);
    }
    return(((n == 1) && (pid > 1)) ? pid : 0);
}


static int write_pidfile(const char *pidfile)
{
/*  Creates the specified pidfile.
 *  Returns 0 on success, or -1 on error.
 *
 *  The pidfile must be created after daemonize has finished forking.
 *  The pidfile must be specified with an absolute pathname; o/w, the
 *    unlink() call at exit will fail because the daemon has chdir()'d.
 */
    mode_t  mask;
    char    dirname[PATH_MAX];
    FILE   *fp;

    if (pidfile == NULL) {
        return(0);
    }
    if (pidfile[0] != '/') {
        log_msg(0, "Unable to create relative-path pidfile \"%s\"", pidfile);
        return(-1);
    }
    (void) unlink(pidfile);
    /*
     *  Protect pidfile against unauthorized writes by removing
     *    group+other write-access from current mask.
     */
    mask = umask(0);
    umask(mask | 022);
    /*
     *  Create intermediate directories.
     */
    if (get_dir_name(pidfile, dirname, sizeof(dirname))) {
        (void) create_dirs(dirname);
    }
    /*  Open the pidfile.
     */
    fp = fopen(pidfile, "w");
    umask(mask);

    if (!fp) {
        log_msg(LOG_ERR, "Unable to open pidfile \"%s\": %s",
            pidfile, strerror(errno));
    }
    else if (fprintf(fp, "%d\n", (int) getpid()) == EOF) {
        log_msg(LOG_ERR, "Unable to write to pidfile \"%s\": %s",
            pidfile, strerror(errno));
        (void) fclose(fp);
    }
    else if (fclose(fp) == EOF) {
        log_msg(LOG_ERR, "Unable to close pidfile \"%s\": %s",
            pidfile, strerror(errno));
    }
    else {
        return(0);
    }
    (void) unlink(pidfile);
    return(-1);
}


static int lookup_syslog_priority(const char *priority)
{
/*  Returns the numeric id associated with the specified syslog priority,
 *    or -1 if no match is found.
 */
    tag_t *t;

    assert(priority != NULL);

    while (*priority && isspace((int) *priority)) {
        priority++;
    }
    for (t=logPriorities; t->key; t++) {
        if (!strcasecmp(t->key, priority)) {
            return(t->val);
        }
    }
    return(-1);
}


static int lookup_syslog_facility(const char *facility)
{
/*  Returns the numeric id associated with the specified syslog facility,
 *    or -1 if no match is found.
 */
    tag_t *t;

    assert(facility != NULL);

    while (*facility && isspace((int) *facility)) {
        facility++;
    }
    for (t=logFacilities; t->key; t++) {
        if (!strcasecmp(t->key, facility)) {
            return(t->val);
        }
    }
    return(-1);
}
