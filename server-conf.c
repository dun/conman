/*****************************************************************************\
 *  $Id: server-conf.c,v 1.42 2002/05/08 00:10:54 dun Exp $
 *****************************************************************************
 *  Copyright (C) 2001-2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *  UCRL-CODE-2002-009.
 *  
 *  This file is part of ConMan, a remote console management program.
 *  For details, see <http://www.llnl.gov/linux/conman.html>.
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
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "common.h"
#include "lex.h"
#include "list.h"
#include "log.h"
#include "server.h"
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
    SERVER_CONF_TCPWRAPPERS,
    SERVER_CONF_TIMESTAMP,
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
    "TCPWRAPPERS",
    "TIMESTAMP",
    NULL
};


static void display_server_help(char *prog);
static void signal_daemon(server_conf_t *conf, int signum);
static void parse_console_directive(Lex l, server_conf_t *conf);
static void parse_global_directive(Lex l, server_conf_t *conf);
static void parse_server_directive(Lex l, server_conf_t *conf);


server_conf_t * create_server_conf(void)
{
    server_conf_t *conf;

    if (!(conf = malloc(sizeof(server_conf_t))))
        out_of_memory();
    conf->confFileName = create_string(CONMAN_CONF);
    conf->logDirName = NULL;
    conf->pidFileName = NULL;
    conf->resetCmd = NULL;
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
    conf->logopts.enableSanitize = DEFAULT_LOGOPT_SANITIZE;
    conf->seropts.bps = DEFAULT_SEROPT_BPS;
    conf->seropts.databits = DEFAULT_SEROPT_DATABITS;
    conf->seropts.parity = DEFAULT_SEROPT_PARITY;
    conf->seropts.stopbits = DEFAULT_SEROPT_STOPBITS;
    conf->enableKeepAlive = 1;
    conf->enableLoopBack = 0;
    conf->enableTCPWrap = 0;
    conf->enableVerbose = 0;
    conf->enableZeroLogs = 0;
    return(conf);
}


void destroy_server_conf(server_conf_t *conf)
{
    if (!conf)
        return;

    if (conf->logDirName)
        free(conf->logDirName);
    if (conf->pidFileName) {
        if (unlink(conf->pidFileName) < 0)
            log_err(errno, "Unable to delete \"%s\"", conf->pidFileName);
        free(conf->pidFileName);
    }
    if (conf->resetCmd)
        free(conf->resetCmd);
    if (conf->fd >= 0) {
        if (close(conf->fd) < 0)
            log_err(errno, "Unable to close \"%s\"", conf->confFileName);
        conf->fd = -1;
    }
    if (conf->ld >= 0) {
        if (close(conf->ld) < 0)
            log_err(errno, "Unable to close listening socket");
        conf->ld = -1;
    }
    if (conf->objs)
        list_destroy(conf->objs);
    if (conf->confFileName)
        free(conf->confFileName);

    free(conf);
    return;
}


void process_server_cmd_line(int argc, char *argv[], server_conf_t *conf)
{
    int c;
    int signum = 0;

    opterr = 0;
    while ((c = getopt(argc, argv, "c:hkp:rvVz")) != -1) {
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
            signum = SIGTERM;
            break;
        case 'p':
            if ((conf->port = atoi(optarg)) <= 0)
                log_err(0, "CMDLINE: invalid port \"%d\"", conf->port);
            break;
        case 'r':
            signum = SIGHUP;
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

    if (signum) {
        signal_daemon(conf, signum);
        exit(0);
    }
    return;
}


void process_server_conf_file(server_conf_t *conf)
{
    int port;
    pid_t pid;
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

    if ((conf->fd = open(conf->confFileName, O_RDONLY)) < 0)
        log_err(errno, "Unable to open \"%s\"", conf->confFileName);

    if ((pid = is_write_lock_blocked(conf->fd)) > 0)
        log_err(0, "Configuration \"%s\" in use by pid %d",
            conf->confFileName, pid);
    if (get_read_lock(conf->fd) < 0)
        log_err(0, "Unable to lock configuration \"%s\"",
            conf->confFileName);

    if (fstat(conf->fd, &fdStat) < 0)
        log_err(errno, "Unable to stat \"%s\"", conf->confFileName);
    len = fdStat.st_size;
    if (!(buf = malloc(len + 1)))
        out_of_memory();
    if ((n = read_n(conf->fd, buf, len)) < 0)
        log_err(errno, "Unable to read \"%s\"", conf->confFileName);
    assert(n == len);
    buf[len] = '\0';

    l = lex_create(buf, server_conf_strs);
    while ((tok = lex_next(l)) != LEX_EOF) {
        switch(tok) {
        case SERVER_CONF_CONSOLE:
            parse_console_directive(l, conf);
            break;
        case SERVER_CONF_GLOBAL:
            parse_global_directive(l, conf);
            break;
        case SERVER_CONF_SERVER:
            parse_server_directive(l, conf);
            break;
        case LEX_EOL:
            break;
        case LEX_ERR:
            log_msg(LOG_ERR, "CONF[%s:%d]: unmatched quote",
                conf->confFileName, lex_line(l));
            break;
        default:
            log_msg(LOG_ERR, "CONF[%s:%d]: unrecognized token '%s'",
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
    if (port > 0)                       /* restore port set via cmdline */
        conf->port = port;
    else if (conf->port <= 0)           /* port not set so use default */
        conf->port = atoi(CONMAN_PORT);

    /*  The pidfile must be created after daemonize() has finished forking.
     */
    if (conf->pidFileName) {

        FILE *fp;

        if (!(fp = fopen(conf->pidFileName, "w")))
            log_msg(LOG_ERR, "Unable to open \"%s\": %s",
                conf->pidFileName, strerror(errno));
        fprintf(fp, "%d\n", (int) getpid());
        if (fclose(fp) == EOF)
            log_msg(LOG_ERR, "Unable to close \"%s\": %s",
                conf->pidFileName, strerror(errno));
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
    printf("  -p PORT   Specify port number. [%d]\n", atoi(CONMAN_PORT));
    printf("  -r        Re-open logs on daemon.\n");
    printf("  -v        Be verbose.\n");
    printf("  -V        Display version information.\n");
    printf("  -z        Zero console log files.\n");
    printf("\n");
    return;
}


static void signal_daemon(server_conf_t *conf, int signum)
{
/*  Sends the 'signum' signal to the daemon running with the
 *    configuration specified by 'conf'.
 */
    pid_t pid;
    char *msg;

    assert(conf != NULL);
    assert(conf->confFileName != NULL);
    assert(signum > 0);

    if ((conf->fd = open(conf->confFileName, O_RDONLY)) < 0) {
        log_err(errno, "Unable to open \"%s\"", conf->confFileName);
    }
    if (!(pid = is_write_lock_blocked(conf->fd))) {
        log_err(0, "Configuration \"%s\" is not active", conf->confFileName);
    }
    if (kill(pid, signum) < 0) {
        log_err(errno, "Unable to send signal=%d to pid %d",
            signum, (int) pid);
    }
    if (conf->enableVerbose) {
        if (signum == SIGHUP)
            msg = "reconfigured on";
        else if (signum == SIGTERM)
            msg = "terminated on";
        else
            msg = "sent";
        fprintf(stderr, "Configuration \"%s\" (pid %d) %s signal=%d\n",
            conf->confFileName, (int) pid, msg, signum);
    }

    destroy_server_conf(conf);
    exit(0);
}


static void parse_console_directive(Lex l, server_conf_t *conf)
{
/*  CONSOLE NAME="<str>" DEV="<file>" [LOG="<file>"] \
 *    [LOGOPTS="<str>"] [SEROPTS="<str>"]
 */
    char *directive;                    /* name of directive being parsed */
    int line;                           /* line# where directive begins */
    int tok;
    int done = 0;
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
    logopts = conf->logopts;
    seropts = conf->seropts;

    while (!done && !*err) {
        tok = lex_next(l);
        switch(tok) {

        case SERVER_CONF_NAME:
            if (lex_next(l) != '=')
                snprintf(err, sizeof(err), "expected '=' after %s keyword",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if (lex_next(l) != LEX_STR)
                snprintf(err, sizeof(err), "expected STRING for %s value",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else
                strlcpy(name, lex_text(l), MAX_LINE);
            break;

        case SERVER_CONF_DEV:
            if (lex_next(l) != '=')
                snprintf(err, sizeof(err), "expected '=' after %s keyword",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if (lex_next(l) != LEX_STR)
                snprintf(err, sizeof(err), "expected STRING for %s value",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else
                strlcpy(dev, lex_text(l), MAX_LINE);
            break;

        case SERVER_CONF_LOG:
            if (lex_next(l) != '=')
                snprintf(err, sizeof(err), "expected '=' after %s keyword",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if (lex_next(l) != LEX_STR)
                snprintf(err, sizeof(err), "expected STRING for %s value",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if ((lex_text(l)[0] != '/') && (conf->logDirName))
                snprintf(log, sizeof(log), "%s/%s",
                    conf->logDirName, lex_text(l));
            else
                strlcpy(log, lex_text(l), MAX_LINE);
            break;

        case SERVER_CONF_LOGOPTS:
            if (lex_next(l) != '=')
                snprintf(err, sizeof(err), "expected '=' after %s keyword",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if (lex_next(l) != LEX_STR)
                snprintf(err, sizeof(err), "expected STRING for %s value",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else
                parse_logfile_opts(&logopts, lex_text(l), err, sizeof(err));
            break;

        case SERVER_CONF_SEROPTS:
            if (lex_next(l) != '=')
                snprintf(err, sizeof(err), "expected '=' after %s keyword",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if (lex_next(l) != LEX_STR)
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
        log_msg(LOG_ERR, "CONF[%s:%d]: %s",
            conf->confFileName, lex_line(l), err);
        while (lex_prev(l) != LEX_EOL && lex_prev(l) != LEX_EOF)
            lex_next(l);
        return;
    }

    if ((p = strchr(dev, ':'))) {
        *p++ = '\0';
        if (!(console = create_telnet_obj(
          conf, name, dev, atoi(p), err, sizeof(err)))) {
            log_msg(LOG_ERR, "CONF[%s:%d]: console [%s] %s",
                conf->confFileName, line, name, err);
        }
    }
    else if (!(console = create_serial_obj(
      conf, name, dev, &seropts, err, sizeof(err)))) {
        log_msg(LOG_ERR, "CONF[%s:%d]: console [%s] %s",
            conf->confFileName, line, name, err);
    }

    if (console && *log) {

        if (substitute_string(name, sizeof(name), log,
          DEFAULT_CONFIG_ESCAPE, console->name) < 0) {
            log_msg(LOG_ERR, "CONF[%s:%d]: console [%s] cannot log to "
                "\"%s\": %c-expansion failed", conf->confFileName, line,
                console->name, log, DEFAULT_CONFIG_ESCAPE);
        }
        else if (!(logfile = create_logfile_obj(
          conf, name, console, &logopts, err, sizeof(err)))) {
            log_msg(LOG_ERR, "CONF[%s:%d]: console [%s] cannot log to "
                "\"%s\": %s", conf->confFileName, line, console->name,
                name, err);
        }
        else {
            link_objs(console, logfile);
        }
    }
    return;
}


static void parse_global_directive(Lex l, server_conf_t *conf)
{
    char *directive;                    /* name of directive being parsed */
    int tok;
    int done = 0;
    char err[MAX_LINE] = "";

    directive = server_conf_strs[LEX_UNTOK(lex_prev(l))];

    while (!done && !*err) {
        tok = lex_next(l);
        switch(tok) {

        case SERVER_CONF_LOGOPTS:
            if (lex_next(l) != '=')
                snprintf(err, sizeof(err), "expected '=' after %s keyword",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if (lex_next(l) != LEX_STR)
                snprintf(err, sizeof(err), "expected STRING for %s value",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else
                parse_logfile_opts(&conf->logopts, lex_text(l),
                    err, sizeof(err));
            break;

        case SERVER_CONF_SEROPTS:
            if (lex_next(l) != '=')
                snprintf(err, sizeof(err), "expected '=' after %s keyword",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if (lex_next(l) != LEX_STR)
                snprintf(err, sizeof(err), "expected STRING for %s value",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else
                parse_serial_opts(&conf->seropts, lex_text(l),
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
        log_msg(LOG_ERR, "CONF[%s:%d]: %s",
            conf->confFileName, lex_line(l), err);
        while (lex_prev(l) != LEX_EOL && lex_prev(l) != LEX_EOF)
            lex_next(l);
    }
    return;
}


static void parse_server_directive(Lex l, server_conf_t *conf)
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
            else if (lex_next(l) != LEX_STR)
                snprintf(err, sizeof(err), "expected STRING for %s value",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else {
                if (conf->logDirName)
                    free(conf->logDirName);
                conf->logDirName = create_string(lex_text(l));
                p = conf->logDirName + strlen(conf->logDirName) - 1;
                while ((p >= conf->logDirName) && (*p == '/'))
                    *p-- = '\0';
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
            else if (lex_next(l) != LEX_STR)
                snprintf(err, sizeof(err), "expected STRING for %s value",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else {
                if (conf->pidFileName)
                    free(conf->pidFileName);
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
            else if (lex_next(l) != LEX_STR)
                snprintf(err, sizeof(err), "expected STRING for %s value",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else {
                if (conf->resetCmd)
                    free(conf->resetCmd);
                conf->resetCmd = create_string(lex_text(l));
            }
            break;

        case SERVER_CONF_TCPWRAPPERS:
#ifndef WITH_TCP_WRAPPERS
            snprintf(err, sizeof(err),
                "%s keyword requires compile-time support",
                server_conf_strs[LEX_UNTOK(tok)]);
            break;
#endif /* !WITH_TCP_WRAPPERS */
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
        log_msg(LOG_ERR, "CONF[%s:%d]: %s",
            conf->confFileName, lex_line(l), err);
        while (lex_prev(l) != LEX_EOL && lex_prev(l) != LEX_EOF)
            lex_next(l);
    }
    return;
}
