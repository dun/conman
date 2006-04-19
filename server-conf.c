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


#ifdef HAVE_CONFIG_H
#  include "config.h"
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


enum server_conf_toks {
/*
 *  Keep enums in sync w/ server_conf_strs[].
 */
    SERVER_CONF_CONSOLE = LEX_TOK_OFFSET,
    SERVER_CONF_DEV,
    SERVER_CONF_GLOBAL,
    SERVER_CONF_KEEPALIVE,
    SERVER_CONF_LOG,
    SERVER_CONF_LOGDIR,
    SERVER_CONF_LOGFILE,
    SERVER_CONF_LOGOPTS,
    SERVER_CONF_LOOPBACK,
    SERVER_CONF_NAME,
    SERVER_CONF_OFF,
    SERVER_CONF_ON,
    SERVER_CONF_PIDFILE,
    SERVER_CONF_PORT,
    SERVER_CONF_RESETCMD,
    SERVER_CONF_SEROPTS,
    SERVER_CONF_SERVER,
    SERVER_CONF_SYSLOG,
    SERVER_CONF_TCPWRAPPERS,
    SERVER_CONF_TIMESTAMP
};

static char *server_conf_strs[] = {
/*
 *  Keep strings in sync w/ server_conf_toks enum.
 *  These must be sorted in a case-insensitive manner.
 */
    "CONSOLE",
    "DEV",
    "GLOBAL",
    "KEEPALIVE",
    "LOG",
    "LOGDIR",
    "LOGFILE",
    "LOGOPTS",
    "LOOPBACK",
    "NAME",
    "OFF",
    "ON",
    "PIDFILE",
    "PORT",
    "RESETCMD",
    "SEROPTS",
    "SERVER",
    "SYSLOG",
    "TCPWRAPPERS",
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


static void process_server_conf(server_conf_t *conf, int argc, char *argv[]);
static void parse_server_cmd_line(server_conf_t *conf, int argc, char *argv[]);
static void parse_server_conf_file(server_conf_t *conf);
static void display_server_help(char *prog);
static void signal_daemon(server_conf_t *conf);
static void parse_console_directive(server_conf_t *conf, Lex l);
static void parse_global_directive(server_conf_t *conf, Lex l);
static void parse_server_directive(server_conf_t *conf, Lex l);
static int read_pidfile(const char *pidfile);
static int write_pidfile(const char *pidfile);
static int is_empty_string(const char *s);
static int lookup_syslog_priority(const char *priority);
static int lookup_syslog_facility(const char *facility);


server_conf_t * create_server_conf(int argc, char *argv[])
{
    server_conf_t *conf;
    char buf[PATH_MAX];

    if (!(conf = malloc(sizeof(server_conf_t))))
        out_of_memory();

    conf->cwd = NULL;
    conf->confFileName = create_string(CONMAN_CONF);
    conf->logDirName = NULL;
    conf->logFileName = NULL;
    conf->logFmtName = NULL;
    conf->logFilePtr = NULL;
    conf->logFileLevel = LOG_INFO;
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
    /*
     *  The port is initialized to zero here because it can be set
     *    (in order of precedence, from highest to lowest) via:
     *    1. command-line option (-p)
     *    2. configuration file (SERVER PORT=<int>)
     *    3. macro def (CONMAN_PORT in "config.h")
     *  The problem is that the command-line options need to be processed
     *    before the configuration file, because an alternative configuration
     *    can be specified via the command-line.  If the port is set to its
     *    default value here, the configuration parsing routine cannot tell
     *    whether it should overwrite the value because it cannot tell whether
     *    the current value is from the command-line or the macro def.
     *  Therefore, we add a kludge at the end of process_server_conf_file()
     *    to set the default value if one has not already been specified.
     */
    conf->port = 0;
    conf->ld = -1;
    conf->objs = list_create((ListDelF) destroy_obj);
    if (!(conf->tp = tpoll_create(0)))
        log_err(0, "Unable to create object for multiplexing I/O");
    conf->globalLogName = NULL;
    conf->globalLogopts.enableSanitize = DEFAULT_LOGOPT_SANITIZE;
    conf->globalLogopts.enableTimestamp = DEFAULT_LOGOPT_TIMESTAMP;
    conf->globalSeropts.bps = DEFAULT_SEROPT_BPS;
    conf->globalSeropts.databits = DEFAULT_SEROPT_DATABITS;
    conf->globalSeropts.parity = DEFAULT_SEROPT_PARITY;
    conf->globalSeropts.stopbits = DEFAULT_SEROPT_STOPBITS;
    conf->enableKeepAlive = 1;
    conf->enableLoopBack = 0;
    conf->enableTCPWrap = 0;
    conf->enableVerbose = 0;
    conf->enableZeroLogs = 0;
    /*
     *  Copy the current working directory before we chdir() away.
     *  Since logfiles can be re-opened after the daemon has chdir()'d,
     *    we need to prepend relative paths with the cwd.
     */
    if (!getcwd(buf, sizeof(buf))) {
        log_msg(LOG_WARNING, "Unable to determine working directory");
    }
    else {
        conf->cwd = create_string(buf);
        conf->logDirName = create_string(buf);
    }
    process_server_conf(conf, argc, argv);
    return(conf);
}


void destroy_server_conf(server_conf_t *conf)
{
    if (!conf)
        return;

    if (conf->cwd)
        free(conf->cwd);
    if (conf->logDirName)
        free(conf->logDirName);
    if (conf->logFileName)
        free(conf->logFileName);
    if (conf->logFmtName)
        free(conf->logFmtName);
    if (conf->pidFileName) {
        if (unlink(conf->pidFileName) < 0)
            log_msg(LOG_ERR, "Unable to delete pidfile \"%s\": %s",
                conf->pidFileName, strerror(errno));
        free(conf->pidFileName);
    }
    if (conf->resetCmd)
        free(conf->resetCmd);
    if (conf->fd >= 0) {
        if (close(conf->fd) < 0)
            log_msg(LOG_ERR, "Unable to close \"%s\": %s",
                conf->confFileName, strerror(errno));
        conf->fd = -1;
    }
    if (conf->ld >= 0) {
        if (close(conf->ld) < 0)
            log_msg(LOG_ERR, "Unable to close listening socket: %s",
                strerror(errno));
        conf->ld = -1;
    }
    if (conf->objs)
        list_destroy(conf->objs);
    if (conf->tp)
        tpoll_destroy(conf->tp);
    if (conf->globalLogName)
        free(conf->globalLogName);
    if (conf->confFileName)
        free(conf->confFileName);

    free(conf);
    return;
}


static void process_server_conf(server_conf_t *conf, int argc, char *argv[])
{
    pid_t pid;

    parse_server_cmd_line(conf, argc, argv);
    parse_server_conf_file(conf);

    if (conf->throwSignal >= 0) {
        signal_daemon(conf);
        exit(0);
    }
    if ((pid = is_write_lock_blocked(conf->fd)) > 0) {
        log_err(0, "Configuration \"%s\" in use by pid %d",
            conf->confFileName, pid);
    }
    /*  Must use a read lock here since the file is only open for reading.
     *    But exclusive access is ensured by testing for a write lock above.
     *  Truth be told, use of a read lock here provides a small window for
     *    a race condition where multiple processes could obtain locks.
     *    But since the config file could be read-only, this seems an
     *    acceptable risk.  You've got to ask yourself one question:
     *    Do I feel lucky?  Well, do ya, punk?
     */
    if (get_read_lock(conf->fd) < 0) {
        log_err(0, "Unable to lock configuration \"%s\"",
            conf->confFileName);
    }
    if (conf->pidFileName) {
        if (write_pidfile(conf->pidFileName) < 0) {
            free(conf->pidFileName);
            conf->pidFileName = NULL;   /* prevent unlink() at exit */
        }
    }
    return;
}


static void parse_server_cmd_line(server_conf_t *conf, int argc, char *argv[])
{
    int c;

    opterr = 0;
    while ((c = getopt(argc, argv, "c:hkLp:qrvVz")) != -1) {
        switch(c) {
        case 'c':
            if (conf->confFileName)
                free(conf->confFileName);
            conf->confFileName = create_string(optarg);
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
            if ((conf->port = atoi(optarg)) <= 0)
                log_err(0, "CMDLINE: invalid port \"%d\"", conf->port);
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


static void parse_server_conf_file(server_conf_t *conf)
{
    int port;
    struct stat fdStat;
    int len;
    char *buf;
    int n;
    Lex l;
    int tok;

    /*  Save conf->port 'cause it may be redefined by parse_server_directive().
     *  If (port > 0), port was specified via the command-line.
     */
    port = conf->port;
    /*
     *  Keep conf->fd open after parsing the file in order to obtain the lock.
     */
    if ((conf->fd = open(conf->confFileName, O_RDONLY)) < 0) {
        log_err(errno, "Unable to open \"%s\"", conf->confFileName);
    }
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
            while (tok != LEX_EOL && tok != LEX_EOF)
                tok = lex_next(l);
            break;
        }
    }
    lex_destroy(l);
    free(buf);

    /*  Kludge to ensure port is properly set (cf, create_server_conf()).
     */
    if (port > 0) {                     /* restore port set via cmdline */
        conf->port = port;
    }
    else if (conf->port <= 0) {         /* port not set so use default */
        conf->port = atoi(CONMAN_PORT);
    }
    if (conf->logFileName) {
        if (strchr(conf->logFileName, '%'))
            conf->logFmtName = create_string(conf->logFileName);
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
    printf("  -h        Display this help.\n");
    printf("  -k        Kill daemon.\n");
    printf("  -L        Display license information.\n");
    printf("  -p PORT   Specify port number. [%d]\n", atoi(CONMAN_PORT));
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
        if ((conf->throwSignal == 0) && (errno == EPERM))
            ; /* ignore permission error for pid query */
        else if (errno == ESRCH)
            log_err(0, "Configuration \"%s\" does not appear to be active",
                conf->confFileName);
        else
            log_err(errno,
                "Configuration \"%s\" (pid %d) cannot be sent signal=%d",
                conf->confFileName, (int) pid, conf->throwSignal);
    }
    /*  "We don't need no stinkin' badges."
     */
    if (conf->throwSignal == 0) {
        printf("%d\n", (int) pid);
    }
    else if (conf->enableVerbose) {
        if (conf->throwSignal == SIGHUP)
            msg = "reconfigured on";
        else if (conf->throwSignal == SIGTERM)
            msg = "terminated on";
        else
            msg = "sent";
        fprintf(stderr, "Configuration \"%s\" (pid %d) %s signal=%d\n",
            conf->confFileName, (int) pid, msg, conf->throwSignal);
    }
    destroy_server_conf(conf);
    exit(0);
}


static void parse_console_directive(server_conf_t *conf, Lex l)
{
/*  CONSOLE NAME="<str>" DEV="<file>" \
 *    [LOG="<file>"] [LOGOPTS="<str>"] [SEROPTS="<str>"]
 */
    char *directive;                    /* name of directive being parsed */
    int line;                           /* line# where directive begins */
    int tok;
    int done = 0;
    int gotEmptyLogName = 0;
    char err[MAX_LINE] = "";
    char name[MAX_LINE] = "";
    char dev[MAX_LINE] = "";
    char log[MAX_LINE] = "";
    char *p;
    obj_t *console;
    obj_t *logfile;
    logopt_t logopts;
    seropt_t seropts;

    directive = server_conf_strs[LEX_UNTOK(lex_prev(l))];
    line = lex_line(l);

    /*  Set options to global values which may be overridden.
     */
    logopts = conf->globalLogopts;
    seropts = conf->globalSeropts;

    while (!done && !*err) {
        tok = lex_next(l);
        switch(tok) {

        case SERVER_CONF_NAME:
            if (lex_next(l) != '=')
                snprintf(err, sizeof(err), "expected '=' after %s keyword",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if ((lex_next(l) != LEX_STR) || is_empty_string(lex_text(l)))
                snprintf(err, sizeof(err), "expected STRING for %s value",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else
                strlcpy(name, lex_text(l), sizeof(name));
            break;

        case SERVER_CONF_DEV:
            if (lex_next(l) != '=')
                snprintf(err, sizeof(err), "expected '=' after %s keyword",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if ((lex_next(l) != LEX_STR) || is_empty_string(lex_text(l)))
                snprintf(err, sizeof(err), "expected STRING for %s value",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else
                strlcpy(dev, lex_text(l), sizeof(dev));
            break;

        case SERVER_CONF_LOG:
            if (lex_next(l) != '=')
                snprintf(err, sizeof(err), "expected '=' after %s keyword",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if (lex_next(l) != LEX_STR)
                snprintf(err, sizeof(err), "expected STRING for %s value",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if (is_empty_string(lex_text(l))) {
                gotEmptyLogName = 1;
                *log = '\0';
            }
#ifdef DO_CONF_ESCAPE_ERROR
            else if (strchr(lex_text(l), DEPRECATED_CONF_ESCAPE))
                snprintf(err, sizeof(err),
                    "ignoring %s %s value with deprecated '%c' -- use '%%N'",
                    directive, server_conf_strs[LEX_UNTOK(tok)],
                    DEPRECATED_CONF_ESCAPE);
#endif /* DO_CONF_ESCAPE_ERROR */
            else {
                if ((lex_text(l)[0] != '/') && (conf->logDirName))
                    snprintf(log, sizeof(log), "%s/%s",
                        conf->logDirName, lex_text(l));
                else
                    strlcpy(log, lex_text(l), sizeof(log));
            }
            break;

        case SERVER_CONF_LOGOPTS:
            if (lex_next(l) != '=')
                snprintf(err, sizeof(err), "expected '=' after %s keyword",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if ((lex_next(l) != LEX_STR) || is_empty_string(lex_text(l)))
                snprintf(err, sizeof(err), "expected STRING for %s value",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else
                parse_logfile_opts(&logopts, lex_text(l),
                    conf->confFileName, lex_line(l), err, sizeof(err));
            break;

        case SERVER_CONF_SEROPTS:
            if (lex_next(l) != '=')
                snprintf(err, sizeof(err), "expected '=' after %s keyword",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if ((lex_next(l) != LEX_STR) || is_empty_string(lex_text(l)))
                snprintf(err, sizeof(err), "expected STRING for %s value",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else
                parse_serial_opts(&seropts, lex_text(l), err, sizeof(err));
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

    if ((!*name || !*dev) && !*err) {
        snprintf(err, sizeof(err), "incomplete %s directive", directive);
    }
    if (*err) {
        log_msg(LOG_ERR, "CONFIG[%s:%d]: %s",
            conf->confFileName, lex_line(l), err);
        while (lex_prev(l) != LEX_EOL && lex_prev(l) != LEX_EOF)
            (void) lex_next(l);
        return;
    }

    if ((p = strchr(dev, ':'))) {
        *p++ = '\0';
        if (!(console = create_telnet_obj(
          conf, name, dev, atoi(p), err, sizeof(err)))) {
            log_msg(LOG_ERR, "CONFIG[%s:%d]: console [%s] %s",
                conf->confFileName, line, name, err);
        }
    }
    else if (!(console = create_serial_obj(
      conf, name, dev, &seropts, err, sizeof(err)))) {
        log_msg(LOG_ERR, "CONFIG[%s:%d]: console [%s] %s",
            conf->confFileName, line, name, err);
    }

    if (!*log && !gotEmptyLogName && conf->globalLogName) {
        if ((conf->globalLogName[0] != '/') && (conf->logDirName))
            snprintf(log, sizeof(log), "%s/%s",
                conf->logDirName, conf->globalLogName);
        else
            strlcpy(log, conf->globalLogName, sizeof(log));
    }

    if (console && *log) {
        if (!(logfile = create_logfile_obj(
          conf, log, console, &logopts, err, sizeof(err)))) {
            log_msg(LOG_ERR, "CONFIG[%s:%d]: console [%s] cannot log to "
                "specified file: %s", conf->confFileName, line,
                console->name, err);
        }
        else {
            link_objs(console, logfile);
        }
    }
    return;
}


static void parse_global_directive(server_conf_t *conf, Lex l)
{
    char *directive;                    /* name of directive being parsed */
    int tok;
    int done = 0;
    char err[MAX_LINE] = "";

    directive = server_conf_strs[LEX_UNTOK(lex_prev(l))];

    while (!done && !*err) {
        tok = lex_next(l);
        switch(tok) {

        case SERVER_CONF_LOG:
            if (lex_next(l) != '=')
                snprintf(err, sizeof(err), "expected '=' after %s keyword",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if (lex_next(l) != LEX_STR)
                snprintf(err, sizeof(err), "expected STRING for %s value",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if (is_empty_string(lex_text(l))) {
                /*
                 *  Unset global log name if string is empty.
                 */
                if (conf->globalLogName) {
                    free(conf->globalLogName);
                    conf->globalLogName = NULL;
                }
            }
#ifdef DO_CONF_ESCAPE_ERROR
            else if (strchr(lex_text(l), DEPRECATED_CONF_ESCAPE))
                snprintf(err, sizeof(err),
                    "ignoring %s %s value with deprecated '%c' -- use '%%N'",
                    directive, server_conf_strs[LEX_UNTOK(tok)],
                    DEPRECATED_CONF_ESCAPE);
#endif /* DO_CONF_ESCAPE_ERROR */
            else if (!strstr(lex_text(l), "%N") && !strstr(lex_text(l), "%D"))
                snprintf(err, sizeof(err),
                    "ignoring %s %s value without '%%N' or '%%D'",
                    directive, server_conf_strs[LEX_UNTOK(tok)]);
            else {
                if (conf->globalLogName)
                    free(conf->globalLogName);
                conf->globalLogName = create_string(lex_text(l));
            }
            break;

        case SERVER_CONF_LOGOPTS:
            if (lex_next(l) != '=')
                snprintf(err, sizeof(err), "expected '=' after %s keyword",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if ((lex_next(l) != LEX_STR) || is_empty_string(lex_text(l)))
                snprintf(err, sizeof(err), "expected STRING for %s value",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else
                parse_logfile_opts(&conf->globalLogopts, lex_text(l),
                    conf->confFileName, lex_line(l), err, sizeof(err));
            break;

        case SERVER_CONF_SEROPTS:
            if (lex_next(l) != '=')
                snprintf(err, sizeof(err), "expected '=' after %s keyword",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if ((lex_next(l) != LEX_STR) || is_empty_string(lex_text(l)))
                snprintf(err, sizeof(err), "expected STRING for %s value",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else
                parse_serial_opts(&conf->globalSeropts, lex_text(l),
                    err, sizeof(err));
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
        while (lex_prev(l) != LEX_EOL && lex_prev(l) != LEX_EOF)
            (void) lex_next(l);
    }
    return;
}


static void parse_server_directive(server_conf_t *conf, Lex l)
{
    char *directive;
    int tok;
    int done = 0;
    char err[MAX_LINE] = "";
    char *p;
    int n;

    directive = server_conf_strs[LEX_UNTOK(lex_prev(l))];

    while (!done && !*err) {
        tok = lex_next(l);
        switch(tok) {

        case SERVER_CONF_KEEPALIVE:
            if (lex_next(l) != '=')
                snprintf(err, sizeof(err), "expected '=' after %s keyword",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if (lex_next(l) == SERVER_CONF_ON)
                conf->enableKeepAlive = 1;
            else if (lex_prev(l) == SERVER_CONF_OFF)
                conf->enableKeepAlive = 0;
            else
                snprintf(err, sizeof(err), "expected ON or OFF for %s value",
                    server_conf_strs[LEX_UNTOK(tok)]);
            break;

        case SERVER_CONF_LOGDIR:
            if (lex_next(l) != '=')
                snprintf(err, sizeof(err), "expected '=' after %s keyword",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if ((lex_next(l) != LEX_STR))
                snprintf(err, sizeof(err), "expected STRING for %s value",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if (is_empty_string(lex_text(l))) {
                if (conf->logDirName)
                    free(conf->logDirName);
                conf->logDirName = create_string(conf->cwd);
            }
            else {
                if (conf->logDirName)
                    free(conf->logDirName);
                if ((lex_text(l)[0] != '/') && (conf->cwd))
                    conf->logDirName = create_format_string("%s/%s",
                        conf->cwd, lex_text(l));
                else
                    conf->logDirName = create_string(lex_text(l));
                p = conf->logDirName + strlen(conf->logDirName) - 1;
                while ((p >= conf->logDirName) && (*p == '/'))
                    *p-- = '\0';
            }
            break;

        case SERVER_CONF_LOGFILE:
            if (lex_next(l) != '=')
                snprintf(err, sizeof(err), "expected '=' after %s keyword",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if ((lex_next(l) != LEX_STR) || is_empty_string(lex_text(l)))
                snprintf(err, sizeof(err), "expected STRING for %s value",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else {
                if (conf->logFileName)
                    free(conf->logFileName);
                if ((lex_text(l)[0] != '/') && (conf->logDirName))
                    conf->logFileName = create_format_string("%s/%s",
                        conf->logDirName, lex_text(l));
                else
                    conf->logFileName = create_string(lex_text(l));
                if ((p = strrchr(conf->logFileName, ','))) {
                    *p++ = '\0';
                    if ((n = lookup_syslog_priority(p)) < 0)
                        snprintf(err, sizeof(err),
                            "invalid %s priority \"%s\"",
                            server_conf_strs[LEX_UNTOK(tok)], p);
                    else
                        conf->logFileLevel = n;
                }
            }
            break;

        case SERVER_CONF_LOOPBACK:
            if (lex_next(l) != '=')
                snprintf(err, sizeof(err), "expected '=' after %s keyword",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if (lex_next(l) == SERVER_CONF_ON)
                conf->enableLoopBack = 1;
            else if (lex_prev(l) == SERVER_CONF_OFF)
                conf->enableLoopBack = 0;
            else
                snprintf(err, sizeof(err), "expected ON or OFF for %s value",
                    server_conf_strs[LEX_UNTOK(tok)]);
            break;

        case SERVER_CONF_PIDFILE:
            if (lex_next(l) != '=')
                snprintf(err, sizeof(err), "expected '=' after %s keyword",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if ((lex_next(l) != LEX_STR) || is_empty_string(lex_text(l)))
                snprintf(err, sizeof(err), "expected STRING for %s value",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else {
                if (conf->pidFileName)
                    free(conf->pidFileName);
                if ((lex_text(l)[0] != '/') && (conf->cwd))
                    conf->pidFileName = create_format_string("%s/%s",
                        conf->cwd, lex_text(l));
                else
                    conf->pidFileName = create_string(lex_text(l));
            }
            break;

        case SERVER_CONF_PORT:
            if (lex_next(l) != '=')
                snprintf(err, sizeof(err), "expected '=' after %s keyword",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if (lex_next(l) != LEX_INT)
                snprintf(err, sizeof(err), "expected INTEGER for %s value",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if ((n = atoi(lex_text(l))) <= 0)
                snprintf(err, sizeof(err), "invalid %s value %d",
                    server_conf_strs[LEX_UNTOK(tok)], n);
            else
                conf->port = n;
            break;

        case SERVER_CONF_RESETCMD:
            if (lex_next(l) != '=')
                snprintf(err, sizeof(err), "expected '=' after %s keyword",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if ((lex_next(l) != LEX_STR) || is_empty_string(lex_text(l)))
                snprintf(err, sizeof(err), "expected STRING for %s value",
                    server_conf_strs[LEX_UNTOK(tok)]);
#ifdef DO_CONF_ESCAPE_ERROR
            else if (strchr(lex_text(l), DEPRECATED_CONF_ESCAPE))
                snprintf(err, sizeof(err),
                    "ignoring %s %s value with deprecated '%c' -- use '%%N'",
                    directive, server_conf_strs[LEX_UNTOK(tok)],
                    DEPRECATED_CONF_ESCAPE);
#endif /* DO_CONF_ESCAPE_ERROR */
            else {
                if (conf->resetCmd)
                    free(conf->resetCmd);
                conf->resetCmd = create_string(lex_text(l));
            }
            break;

        case SERVER_CONF_SYSLOG:
            if (lex_next(l) != '=')
                snprintf(err, sizeof(err), "expected '=' after %s keyword",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if ((lex_next(l) != LEX_STR) || is_empty_string(lex_text(l)))
                snprintf(err, sizeof(err), "expected STRING for %s value",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if ((n = lookup_syslog_facility(lex_text(l))) < 0)
                snprintf(err, sizeof(err), "invalid %s facility \"%s\"",
                    server_conf_strs[LEX_UNTOK(tok)], lex_text(l));
            else
                conf->syslogFacility = n;
            break;

        case SERVER_CONF_TCPWRAPPERS:
#ifndef WITH_TCP_WRAPPERS
            snprintf(err, sizeof(err),
                "%s keyword requires compile-time support",
                server_conf_strs[LEX_UNTOK(tok)]);
#else /* WITH_TCP_WRAPPERS */
            if (lex_next(l) != '=')
                snprintf(err, sizeof(err), "expected '=' after %s keyword",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if (lex_next(l) == SERVER_CONF_ON)
                conf->enableTCPWrap = 1;
            else if (lex_prev(l) == SERVER_CONF_OFF)
                conf->enableTCPWrap = 0;
            else
                snprintf(err, sizeof(err), "expected ON or OFF for %s value",
                    server_conf_strs[LEX_UNTOK(tok)]);
#endif /* WITH_TCP_WRAPPERS */
            break;

        case SERVER_CONF_TIMESTAMP:
            if (lex_next(l) != '=')
                snprintf(err, sizeof(err), "expected '=' after %s keyword",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if (lex_next(l) != LEX_INT)
                snprintf(err, sizeof(err), "expected INTEGER for %s value",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if ((n = atoi(lex_text(l))) < 0)
                snprintf(err, sizeof(err), "invalid %s value %d",
                    server_conf_strs[LEX_UNTOK(tok)], n);
            else {
                conf->tStampMinutes = n;
                if ((lex_next(l) == LEX_EOF) || (lex_prev(l) == LEX_EOL))
                    ; /* no-op */
                else if (lex_prev(l) == LEX_STR) {
                    if (lex_text(l)[1] != '\0')
                        conf->tStampMinutes = -1;
                    else if (lex_text(l)[0] == 'm' || lex_text(l)[0] == 'M')
                        ; /* no-op */
                    else if (lex_text(l)[0] == 'h' || lex_text(l)[0] == 'H')
                        conf->tStampMinutes *= 60;
                    else if (lex_text(l)[0] == 'd' || lex_text(l)[0] == 'D')
                        conf->tStampMinutes *= 60 * 24;
                    else
                        conf->tStampMinutes = -1;
                }
                else
                    conf->tStampMinutes = -1;

                if (conf->tStampMinutes < 0) {
                    conf->tStampMinutes = 0;
                    snprintf(err, sizeof(err),
                        "expected (m|d|h) qualifier for %s value",
                        server_conf_strs[LEX_UNTOK(tok)]);
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
        while (lex_prev(l) != LEX_EOL && lex_prev(l) != LEX_EOF)
            (void) lex_next(l);
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
 *  The pidfile must be created after daemonize() has finished forking.
 *  The pidfile must be specified with an absolute pathname; o/w, the
 *    unlink() call at exit will fail because the daemon has chdir()'d.
 */
    mode_t mask;
    FILE *fp;

    assert(pidfile != NULL);
    assert(pidfile[0] == '/');

    /*  Protect pidfile against unauthorized writes by removing
     *    group+other write-access from current mask.
     */
    mask = umask(0);
    umask(mask | 022);

    unlink(pidfile);                    /* ignore errors */
    fp = fopen(pidfile, "w");
    umask(mask);

    if (!fp) {
        log_msg(LOG_ERR, "Unable to open pidfile \"%s\": %s",
            pidfile, strerror(errno));
    }
    else if (fprintf(fp, "%d\n", (int) getpid()) == EOF) {
        log_msg(LOG_ERR, "Unable to write to pidfile \"%s\": %s",
            pidfile, strerror(errno));
    }
    else {
        return(0);
    }
    if (fp) {
        fclose(fp);
        unlink(pidfile);                /* ignore errors */
    }
    return(-1);
}


static int is_empty_string(const char *s)
{
/*  Returns non-zero if the string is empty (ie, contains only white-space).
 */
    const char *p;

    assert(s != NULL);

    for (p=s; *p; p++)
        if (!isspace((int) *p))
            return(0);
    return(1);
}


static int lookup_syslog_priority(const char *priority)
{
/*  Returns the numeric id associated with the specified syslog priority,
 *    or -1 if no match is found.
 */
    tag_t *t;

    assert(priority != NULL);

    while (*priority && isspace((int) *priority))
        priority++;

    for (t=logPriorities; t->key; t++)
        if (!strcasecmp(t->key, priority))
            return(t->val);
    return(-1);
}


static int lookup_syslog_facility(const char *facility)
{
/*  Returns the numeric id associated with the specified syslog facility,
 *    or -1 if no match is found.
 */
    tag_t *t;

    assert(facility != NULL);

    while (*facility && isspace((int) *facility))
        facility++;

    for (t=logFacilities; t->key; t++)
        if (!strcasecmp(t->key, facility))
            return(t->val);
    return(-1);
}
