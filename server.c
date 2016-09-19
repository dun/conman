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
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "common.h"
#include "inevent.h"
#include "list.h"
#include "log.h"
#include "server.h"
#include "tpoll.h"
#include "util-file.h"
#include "util-str.h"
#include "util.h"


static void begin_daemonize(int *fd_ptr, pid_t *pgid_ptr);
static void end_daemonize(int fd);
static void setup_coredump(server_conf_t *conf);
static void setup_signals(server_conf_t *conf);
static void sig_chld_handler(int signum);
static void sig_hup_handler(int signum);
static void exit_handler(int signum);
static void coredump_handler(int signum);
static char ** get_sane_env(void);
static void display_configuration(server_conf_t *conf);
static void schedule_timestamp(server_conf_t *conf);
static void timestamp_logfiles(server_conf_t *conf);
static void create_listen_socket(server_conf_t *conf);
static void setup_nofile_limit(server_conf_t *conf);
static void open_objs(server_conf_t *conf);
static void mux_io(server_conf_t *conf);
static void open_daemon_logfile(server_conf_t *conf);
static void reopen_logfiles(server_conf_t *conf);
static void accept_client(server_conf_t *conf);

/*  Signal handler flags and whatnot.
 */
static volatile sig_atomic_t done = 0;
static volatile sig_atomic_t reconfig = 0;
static int coredump = 0;
static char coredumpdir[PATH_MAX];

/*  The 'tp_global' var is to allow timers to be set or canceled
 *    without having to pass the conf's tp var through the call stack.
 */
tpoll_t tp_global = NULL;

extern char ** environ;


int main(int argc, char *argv[])
{
    int fd = -1;
    pid_t pgid = -1;
    server_conf_t *conf;
    int log_priority = LOG_INFO;
    char ** const environ_bak = environ;

#ifndef NDEBUG
    log_priority = LOG_DEBUG;
#endif /* NDEBUG */
    log_set_file(stderr, log_priority, 0);

    conf = create_server_conf();
    tp_global = conf->tp;

    process_cmdline(conf, argc, argv);
    if (!conf->enableForeground) {
        begin_daemonize(&fd, &pgid);
    }
    process_config(conf);
    setup_coredump(conf);
    setup_signals(conf);

    if (!(environ = get_sane_env())) {
        log_err(ENOMEM, "Unable to create sanitized environment");
    }
    if (conf->enableVerbose) {
        display_configuration(conf);
    }
    if (list_is_empty(conf->objs)) {
        log_err(0, "Configuration \"%s\" has no consoles defined",
            conf->confFileName);
    }
    if (conf->tStampMinutes > 0) {
        schedule_timestamp(conf);
    }
    create_listen_socket(conf);

    if (!conf->enableForeground) {
        if (conf->syslogFacility > 0) {
            log_set_syslog(argv[0], conf->syslogFacility);
        }
        if (conf->logFileName) {
            open_daemon_logfile(conf);
        }
        else {
            log_set_file(NULL, 0, 0);
        }
        end_daemonize(fd);
    }

    log_msg(LOG_NOTICE, "Starting ConMan daemon %s (pid %d)",
        VERSION, (int) getpid());

#if WITH_FREEIPMI
    ipmi_init(conf->numIpmiObjs);
#endif /* WITH_FREEIPMI */

    setup_nofile_limit(conf);
    open_objs(conf);
    mux_io(conf);

#if WITH_FREEIPMI
    ipmi_fini();
#endif /* WITH_FREEIPMI */

    destroy_server_conf(conf);

    if (pgid > 0) {
        if (kill(-pgid, SIGTERM) < 0) {
            log_msg(LOG_WARNING, "Unable to terminate process group ID %d: %s",
                pgid, strerror(errno));
        }
    }
    log_msg(LOG_NOTICE, "Stopping ConMan daemon %s (pid %d)",
        VERSION, (int) getpid());

    free(environ);
    environ = environ_bak;
    exit(0);
}


static void begin_daemonize(int *fd_ptr, pid_t *pgid_ptr)
{
/*  Begins the daemonization of the process.
 *  Despite the fact that this routine backgrounds the process, control
 *    will not be returned to the shell until end_daemonize() is called.
 *  If 'fd_ptr' is non-null, it will be set to the fd to pass to
 *    end_daemonize() in order to complete the daemonization.
 *  If 'pgid_ptr' is non-null, it will be set to the daemonized
 *    process group ID.
 */
    int fds[2];
    pid_t pid;
    int n;
    signed char priority;
    char ebuf[1024];
    pid_t pgid;

    /*  Clear file mode creation mask.
     */
    umask(0);

    /*  Create pipe for IPC so parent process will wait to terminate until
     *    signaled by grandchild process.  This allows messages written to
     *    stdout/stderr by the grandchild to be properly displayed before
     *    the parent process returns control to the shell.
     */
    if (pipe(fds) < 0) {
        log_err(errno, "Unable to create daemon pipe");
    }
    /*  Set the fd used by log_err() to return status back to the parent.
     */
    log_set_err_pipe(fds[1]);

    /*  Automatically background the process and
     *    ensure child is not a process group leader.
     */
    if ((pid = fork()) < 0) {
        log_err(errno, "Unable to create child process");
    }
    else if (pid > 0) {
        log_set_err_pipe(-1);
        /*
         *  FIXME: The following log_err()s don't necessarily indicate the
         *    daemon has failed since the grandchild process may still be
         *    running.  This could confuse the user.  Should they be warnings
         *    instead?
         */
        if (close(fds[1]) < 0) {
            log_err(errno, "Unable to close write-pipe in parent process");
        }
        if ((n = read(fds[0], &priority, sizeof(priority))) < 0) {
            log_err(errno, "Unable to read status from grandchild process");
        }
        if ((n > 0) && (priority >= 0)) {
            if ((n = read(fds[0], ebuf, sizeof(ebuf))) < 0) {
                log_err(errno,
                    "Unable to read error message from grandchild process");
            }
            if ((n > 0) && (ebuf[0] != '\0')) {
                log_set_file(stderr, priority, 0);
                log_msg(priority, "%s", ebuf);
            }
            exit(1);
        }
        exit(0);
    }
    if (close(fds[0]) < 0) {
        log_err(errno, "Unable to close read-pipe in child process");
    }
    /*  Become a session leader and process group leader
     *    with no controlling tty.
     */
    if ((pgid = setsid()) < 0) {
        log_err(errno, "Unable to disassociate controlling tty");
    }
    /*  Ignore SIGHUP to keep child from terminating when
     *    the session leader (ie, the parent) terminates.
     */
    posix_signal(SIGHUP, SIG_IGN);

    /*  Abdicate session leader position in order to guarantee
     *    daemon cannot automatically re-acquire a controlling tty.
     */
    if ((pid = fork()) < 0) {
        log_err(errno, "Unable to create grandchild process");
    }
    else if (pid > 0) {
        exit(0);
    }
    /*  Return.
     */
    if (fd_ptr) {
        *fd_ptr = fds[1];
    }
    if (pgid_ptr) {
        *pgid_ptr = pgid;
    }
    return;
}


static void end_daemonize(int fd)
{
/*  Completes the daemonization of the process,
 *    where 'fd' is the value returned by begin_daemonize().
 */
    int dev_null;

    /*  Ensure process does not keep a directory in use.
     *  Avoid relative pathname from this point on!
     */
    if (chdir("/") < 0) {
        log_err(errno, "Unable to change to root directory");
    }
    /*  Discard data to/from stdin, stdout, and stderr.
     */
    if ((dev_null = open("/dev/null", O_RDWR)) < 0) {
        log_err(errno, "Unable to open \"/dev/null\"");
    }
    if (dup2(dev_null, STDIN_FILENO) < 0) {
        log_err(errno, "Unable to dup \"/dev/null\" onto stdin");
    }
    if (dup2(dev_null, STDOUT_FILENO) < 0) {
        log_err(errno, "Unable to dup \"/dev/null\" onto stdout");
    }
    if (dup2(dev_null, STDERR_FILENO) < 0) {
        log_err(errno, "Unable to dup \"/dev/null\" onto stderr");
    }
    if (close(dev_null) < 0) {
        log_err(errno, "Unable to close \"/dev/null\"");
    }
    /*  Clear the fd used by log_err() to return status back to the parent.
     */
    log_set_err_pipe(-1);

    /*  Signal grandparent process to terminate.
     */
    if ((fd >= 0) && (close(fd) < 0)) {
        log_err(errno, "Unable to close write-pipe in grandchild process");
    }
    return;
}


static void setup_coredump(server_conf_t *conf)
{
    struct rlimit limit;

    if (conf->enableCoreDump) {
        limit.rlim_cur = RLIM_INFINITY;
        limit.rlim_max = RLIM_INFINITY;
        coredump = 1;
        if (conf->coreDumpDir) {
            strlcpy(coredumpdir, conf->coreDumpDir, sizeof(coredumpdir));
        }
        else if (!getcwd(coredumpdir, sizeof(coredumpdir))) {
            log_err(errno, "Unable to get current working directory");
        }
    }
    else {
        limit.rlim_cur = 0;
        limit.rlim_max = 0;
        coredump = 0;
        coredumpdir[0] = '\0';
    }

    if (setrlimit(RLIMIT_CORE, &limit) < 0) {
        log_err(errno, "Unable to set core dump file limit");
    }
    return;
}


static void setup_signals(server_conf_t *conf)
{
    posix_signal(SIGCHLD, sig_chld_handler);
    posix_signal(SIGHUP, sig_hup_handler);
    posix_signal(SIGINT, exit_handler);
    posix_signal(SIGPIPE, SIG_IGN);
    posix_signal(SIGTERM, exit_handler);

    /*  These signals have a default action of terminate+core according to SUS.
     */
    posix_signal(SIGABRT, coredump_handler);
    posix_signal(SIGBUS, coredump_handler);
    posix_signal(SIGFPE, coredump_handler);
    posix_signal(SIGILL, coredump_handler);
    posix_signal(SIGQUIT, coredump_handler);
    posix_signal(SIGSEGV, coredump_handler);
    return;
}


static void sig_chld_handler(int signum)
{
    pid_t pid;

    while ((pid = waitpid(-1, NULL, WNOHANG)) > 0) {;}
    return;
}


static void sig_hup_handler(int signum)
{
    reconfig = signum;
    return;
}


static void exit_handler(int signum)
{
    done = signum;
    return;
}


static void coredump_handler(int signum)
{
    if (coredump && *coredumpdir) {
        log_msg(LOG_ERR, "Terminating on signal=%d (check \"%s\" for core)",
            signum, coredumpdir);
    }
    else {
        log_msg(LOG_ERR, "Terminating on signal=%d", signum);
    }

    umask(077);
    if (coredump && *coredumpdir && (chdir(coredumpdir) < 0)) {
        log_msg(LOG_ERR,
            "Unable to change directory to coredumpdir \"%s\": %s",
            coredumpdir, strerror(errno));
    }
    posix_signal(signum, SIG_DFL);
    (void) kill(getpid(), signum);

    return;
}


static char ** get_sane_env(void)
{
/*  Creates a sanitized environment.
 *  Returns a NULL-terminated array of NUL-terminated "key=value" strings,
 *    or NULL on error.
 *  Based on the example in "Safe Initialization: Sanitizing the Environment",
 *    Chapter 1.1 of Secure Programming Cookbook by John Viega & Matt Messier.
 */
    int    env_num;
    int    env_len;
    char **pp;
    char  *p;
    char **env;
    char  *ptr;
    int    num;
    int    len;

    char *env_restrict[] = {
        "IFS= \t\n",
        "PATH=" _PATH_STDPATH,
        "TERM=dumb",
        NULL
    };
    char *env_preserve[] = {
        "DEBUG",
        "TZ",
        NULL
    };

    env_num = 1;                                /* for terminating NULL */
    env_len = 0;
    for (pp = env_restrict; *pp; pp++) {
        env_len += strlen(*pp) + 1;             /* str + NUL */
        env_num++;
    }
    for (pp = env_preserve; *pp; pp++) {
        if (!(p = getenv(*pp)))
            continue;
        env_len += strlen(*pp) + strlen(p) + 2; /* key + '=' + val + NUL */
        env_num++;
    }
    env_len += (env_num * sizeof(char *));
    if (!(env = malloc(env_len))) {
        return(NULL);
    }
    ptr = (char *) env + (env_num * sizeof(char *));
    num = 0;
    for (pp = env_restrict; *pp; pp++) {
        env[num++] = ptr;
        len = strlen(*pp) + 1;
        memcpy(ptr, *pp, len);
        ptr += len;
    }
    for (pp = env_preserve; *pp; pp++) {
        if (!(p = getenv(*pp)))
            continue;
        env[num++] = ptr;
        len = strlen(*pp);
        memcpy(ptr, *pp, len);
        ptr += len;
        *ptr++ = '=';
        len = strlen(p) + 1;
        memcpy(ptr, p, len);
        ptr += len;
    }
    env[num++] = NULL;
    assert(num == env_num);
    assert(ptr == (char *) env + env_len);
    return(env);
}


static void display_configuration(server_conf_t *conf)
{
/*  Displays a summary of the server's configuration.
 */
    ListIterator i;
    obj_t *obj;
    int n = 0;
    int gotOptions = 0;

    i = list_iterator_create(conf->objs);
    while ((obj = list_next(i))) {
        if (is_console_obj(obj)) {
            n++;
        }
    }
    list_iterator_destroy(i);

    fprintf(stderr, "\nStarting ConMan daemon %s (pid %d)\n",
        VERSION, (int) getpid());
    fprintf(stderr, "Configuration: %s\n", conf->confFileName);
    fprintf(stderr, "Options:");

    if (conf->enableCoreDump) {
        fprintf(stderr, " CoreDump");
        gotOptions++;
    }
    if (conf->enableKeepAlive) {
        fprintf(stderr, " KeepAlive");
        gotOptions++;
    }
    if (conf->logFileName) {
        fprintf(stderr, " LogFile");
        gotOptions++;
    }
    if (conf->enableLoopBack) {
        fprintf(stderr, " LoopBack");
        gotOptions++;
    }
    if (conf->resetCmd) {
        fprintf(stderr, " ResetCmd");
        gotOptions++;
    }
    if (conf->syslogFacility >= 0) {
        fprintf(stderr, " SysLog");
        gotOptions++;
    }
    if (conf->enableTCPWrap) {
        fprintf(stderr, " TCP-Wrappers");
        gotOptions++;
    }
    if (conf->tStampMinutes > 0) {
        fprintf(stderr, " TimeStamp=%dm", conf->tStampMinutes);
        gotOptions++;
    }
    if (conf->enableZeroLogs) {
        fprintf(stderr, " ZeroLogs");
        gotOptions++;
    }
    if (!gotOptions) {
        fprintf(stderr, " None");
    }
    fprintf(stderr, "\n");
    fprintf(stderr, "Listening on port %d\n", conf->port);
    fprintf(stderr, "Monitoring %d console%s\n", n, ((n == 1) ? "" : "s"));
    fprintf(stderr, "\n");
    return;
}


static void schedule_timestamp(server_conf_t *conf)
{
/*  Schedules a timer for writing timestamps to the console logfiles.
 */
    time_t t;
    struct tm tm;
    struct timeval tv;
    int numCompleted;

    assert(conf->tStampMinutes > 0);

    t = conf->tStampNext;
    get_localtime(&t, &tm);
    /*
     *  If this is the first scheduled timestamp, compute the expiration time
     *    assuming timestamps have been scheduled regularly since midnight.
     *  Otherwise, base it off of the previous timestamp.
     */
    if (!conf->tStampNext) {
        numCompleted = ((tm.tm_hour * 60) + tm.tm_min) / conf->tStampMinutes;
        tm.tm_min = (numCompleted + 1) * conf->tStampMinutes;
        tm.tm_hour = 0;
    }
    else {
        tm.tm_min += conf->tStampMinutes;
    }
    tm.tm_sec = 0;

    if ((t = mktime(&tm)) == ((time_t) -1)) {
        log_err(errno, "Unable to determine time of next logfile timestamp");
    }
    tv.tv_sec = t;
    tv.tv_usec = 0;
    conf->tStampNext = t;

    /*  The timer id is not saved because this timer will never be canceled.
     */
    if (tpoll_timeout_absolute (tp_global,
            (callback_f) timestamp_logfiles, conf, &tv) < 0) {
        log_err(0, "Unable to create timer for timestamping logfiles");
    }
    return;
}


static void timestamp_logfiles(server_conf_t *conf)
{
/*  Writes a timestamp message into all of the console logfiles.
 */
    char *now;
    ListIterator i;
    obj_t *logfile;
    char buf[MAX_LINE];
    int gotLogs = 0;

    now = create_long_time_string(0);
    i = list_iterator_create(conf->objs);
    while ((logfile = list_next(i))) {
        if (!is_logfile_obj(logfile)) {
            continue;
        }
        snprintf(buf, sizeof(buf), "%sConsole [%s] log at %s%s",
            CONMAN_MSG_PREFIX, logfile->aux.logfile.console->name,
            now, CONMAN_MSG_SUFFIX);
        strcpy(&buf[sizeof(buf) - 3], "\r\n");
        write_obj_data(logfile, buf, strlen(buf), 1);
        gotLogs = 1;
    }
    list_iterator_destroy(i);
    free(now);

    /*  If any logfile objs exist, schedule a timer for the next timestamp.
     */
    if (gotLogs) {
        schedule_timestamp(conf);
    }
    return;
}


static void create_listen_socket(server_conf_t *conf)
{
/*  Creates the socket on which to listen for client connections.
 */
    int ld;
    struct sockaddr_in addr;
    const int on = 1;

    if ((ld = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        log_err(errno, "Unable to create listening socket");
    }
    DPRINTF((9, "Opened listen socket: fd=%d.\n", ld));
    set_fd_nonblocking(ld);
    set_fd_closed_on_exec(ld);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(conf->port);

    if (conf->enableLoopBack) {
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    }
    else {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    if (setsockopt(ld, SOL_SOCKET, SO_REUSEADDR,
      (const void *) &on, sizeof(on)) < 0) {
        log_err(errno, "Unable to set REUSEADDR socket option");
    }
    if (bind(ld, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        log_err(errno, "Unable to bind to port %d", conf->port);
    }
    if (listen(ld, 10) < 0) {
        log_err(errno, "Unable to listen on port %d", conf->port);
    }
    conf->ld = ld;
    tpoll_set(conf->tp, conf->ld, POLLIN);
    return;
}


static void setup_nofile_limit(server_conf_t *conf)
{
/*  Sets the NOFILE limit as specified in the configuration file.
 *  If set to  0, use the current (soft) limit. (default)
 *  If set to -1, use the maximum (hard) limit.
 */
    struct rlimit limit;

    if (getrlimit(RLIMIT_NOFILE, &limit) < 0) {
        log_err(errno, "Unable to get open file limit");
    }

    if (conf->numOpenFiles > 0) {
        limit.rlim_cur = conf->numOpenFiles;
        if (limit.rlim_cur > limit.rlim_max) {
            limit.rlim_max = limit.rlim_cur;
        }
    }
    else if (conf->numOpenFiles < 0) {
        limit.rlim_cur = limit.rlim_max;
    }

    if (conf->numOpenFiles) {
        if (setrlimit(RLIMIT_NOFILE, &limit) < 0) {
            log_err(errno, "Unable to set open file limit to %d",
                    limit.rlim_cur);
        }
    }
    log_msg(LOG_INFO, "Open file limit set to %d", limit.rlim_cur);
    return;
}


static void open_objs(server_conf_t *conf)
{
/*  Initially opens everything in the 'objs' list.
 *  A ptr to conf->resetCmd is copied into all console objs to avoid passing
 *    the resetCmd string as a global.  When the reset escape sequence is
 *    processed by process_client_escapes(), perform_reset() has a ptr to the
 *    client obj but does not have access to the conf struct in which the
 *    resetCmd string resides.
 *  Setting resetCmdRef must occur after the entire config file has been parsed
 *    (in process_config()); the ResetCmd string might not yet have been
 *    specified when create_obj() initializes the obj members.
 *  This function is called once, performs a full traversal of the obj list,
 *    and allows resetCmdRef to be set before entering mux_io().
 */
    ListIterator i;
    obj_t *obj;

    i = list_iterator_create(conf->objs);
    while ((obj = list_next(i))) {
        if (is_console_obj(obj)) {
            obj->resetCmdRef = conf->resetCmd;
        }
        reopen_obj(obj);
    }
    list_iterator_destroy(i);
    return;
}


static void mux_io(server_conf_t *conf)
{
/*  Multiplexes I/O between all of the objs in the configuration.
 *  This routine is the heart of ConMan.
 */
    ListIterator i;
    int n;
    obj_t *obj;
    int inevent_fd;
    int rvr, rvw;

    assert(conf->tp != NULL);
    assert(!list_is_empty(conf->objs));

    inevent_fd = inevent_get_fd();
    if (inevent_fd >= 0) {
        tpoll_set(conf->tp, inevent_get_fd(), POLLIN);
    }
    i = list_iterator_create(conf->objs);

    while (!done) {

        if (reconfig) {
            /*
             *  FIXME: A reconfig should pro'ly resurrect "downed" serial objs
             *    and reset reconnect timers of "downed" telnet objs.
             */
            log_msg(LOG_NOTICE, "Performing reconfig on signal=%d", reconfig);
            reopen_logfiles(conf);
            reconfig = 0;
        }
        while ((n = tpoll(conf->tp, -1)) < 0) {
            if (errno != EINTR) {
                log_err(errno, "Unable to multiplex I/O");
            }
            else if (done || reconfig) {
                break;
            }
        }
        if ((n > 0) &&
                (tpoll_is_set(conf->tp, conf->ld, POLLIN) > 0)) {
            n--;
            accept_client(conf);
        }
        if ((inevent_fd >= 0) &&
                (n > 0) &&
                (tpoll_is_set(conf->tp, inevent_fd, POLLIN) > 0)) {
            n--;
            inevent_process();
        }
        /*  If read_from_obj() or write_to_obj() returns -1,
         *    the obj's buffer has been flushed.  If it is a console obj,
         *    retain it and attempt to re-establish the connection;
         *    o/w, give up and remove it from the master objs list.
         */
        list_iterator_reset(i);
        while ((n > 0) &&
                ((obj = list_next(i)) != NULL)) {

            rvr = tpoll_is_set(conf->tp, obj->fd, POLLIN | POLLHUP | POLLERR);
            rvw = tpoll_is_set(conf->tp, obj->fd, POLLOUT);
            if ((rvr > 0) || (rvw > 0)) {
                n--;
            }
            if ((rvr > 0) && (read_from_obj(obj) < 0)) {
                list_delete(i);
                continue;
            }
            if ((rvw > 0) && (write_to_obj(obj) < 0)) {
                list_delete(i);
                continue;
            }
        }
    }
    log_msg(LOG_NOTICE, "Exiting on signal=%d", done);
    list_iterator_destroy(i);
    return;
}


static void open_daemon_logfile(server_conf_t *conf)
{
/*  (Re)opens the daemon logfile.
 *  Since this logfile can be re-opened after the daemon has chdir()'d,
 *    it must be specified with an absolute pathname.
 */
    static int  once = 1;
    const char *mode = "a";
    mode_t      mask;
    char        dirname[PATH_MAX];
    FILE       *fp = NULL;
    int         fd;

    assert(conf->logFileName != NULL);
    assert(conf->logFileName[0] == '/');
    assert(!conf->enableForeground);

    /*  Only truncate logfile at startup if needed.
     */
    if (once) {
        if (conf->enableZeroLogs) {
            mode = "w";
        }
        once = 0;
    }
    /*  Perform conversion specifier expansion.
     */
    if (conf->logFmtName) {

        char buf[MAX_LINE];

        if (format_obj_string(buf, sizeof(buf), NULL, conf->logFmtName) < 0) {
            log_msg(LOG_WARNING,
                "Unable to open daemon logfile: filename too long");
            goto err;
        }
        free(conf->logFileName);
        conf->logFileName = create_string(buf);
    }
    /*  Protect logfile against unauthorized writes by removing
     *    group+other write-access from current mask.
     */
    mask = umask(0);
    umask(mask | 022);
    /*
     *  Create intermediate directories.
     */
    if (get_dir_name(conf->logFileName, dirname, sizeof(dirname))) {
        (void) create_dirs(dirname);
    }
    /*  Open the logfile.
     */
    fp = fopen(conf->logFileName, mode);
    umask(mask);

    if (!fp) {
        log_msg(LOG_WARNING, "Unable to open daemon logfile \"%s\": %s",
            conf->logFileName, strerror(errno));
        goto err;
    }
    if ((fd = fileno(fp)) < 0) {
        log_msg(LOG_WARNING,
            "Unable to obtain descriptor for daemon logfile \"%s\": %s",
            conf->logFileName, strerror(errno));
        goto err;
    }
    if (get_write_lock(fd) < 0) {
        log_msg(LOG_WARNING, "Unable to lock daemon logfile \"%s\"",
            conf->logFileName);
        goto err;
    }
    set_fd_closed_on_exec(fd);
    /*
     *  Transition to new log file.
     */
    log_set_file(fp, conf->logFileLevel, 1);
    if (conf->logFilePtr) {
        if (fclose(conf->logFilePtr) == EOF) {
            log_msg(LOG_WARNING, "Unable to close daemon logfile \"%s\"",
                conf->logFileName);
        }
    }
    conf->logFilePtr = fp;
    DPRINTF((9, "Opened logfile \"%s\": fd=%d.\n", conf->logFileName, fd));
    return;

err:
    if (fp) {
        (void) fclose(fp);
    }
    /*  Abandon old log file and go logless.
     */
    log_set_file(NULL, 0, 0);
    if (conf->logFilePtr) {
        if (fclose(conf->logFilePtr) == EOF) {
            log_msg(LOG_WARNING, "Unable to close daemon logfile \"%s\"",
                conf->logFileName);
        }
    }
    conf->logFilePtr = NULL;
    return;
}


static void reopen_logfiles(server_conf_t *conf)
{
/*  Reopens the daemon logfile and all of the logfiles in the 'objs' list.
 */
    ListIterator i;
    obj_t *logfile;

    i = list_iterator_create(conf->objs);
    while ((logfile = list_next(i))) {
        if (!is_logfile_obj(logfile)) {
            continue;
        }
        open_logfile_obj(logfile);
    }
    list_iterator_destroy(i);

    if (conf->logFileName && !conf->enableForeground) {
        open_daemon_logfile(conf);
    }
    return;
}


static void accept_client(server_conf_t *conf)
{
/*  Accepts a new client connection on the listening socket.
 *  The new socket connection must be accept()'d within the poll() loop.
 *    O/w, the following scenario could occur:  Read activity would be
 *    poll()'d on the listen socket.  A new thread would be created to
 *    process this request.  Before this new thread is scheduled and the
 *    socket connection is accept()'d, the poll() loop begins its next
 *    iteration.  It notices read activity on the listen socket from the
 *    client that has not yet been accepted, so a new thread is created.
 *    Since the listen socket is set non-blocking, this new thread would
 *    receive an EAGAIN/EWOULDBLOCK on the accept() and terminate, but still...
 */
    int sd;
    const int on = 1;
    client_arg_t *args;
    int rc;
    pthread_t tid;

    while ((sd = accept(conf->ld, NULL, NULL)) < 0) {
        if (errno == EINTR) {
            continue;
        }
        if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
            return;
        }
        if (errno == ECONNABORTED) {
            return;
        }
        log_err(errno, "Unable to accept new connection");
    }
    DPRINTF((5, "Accepted new client on fd=%d.\n", sd));

    /*  While the listen fd is non-blocking, new fds that are accept()d from
     *    it can be either blocking or non-blocking depending on the platform.
     *  The current model spawns a thread to handle a new client with
     *    blocking I/O.  Once the client request has been processed,
     *    this fd is set non-blocking and moved to the main fd set.
     *  Consequently, we force the new fd to be blocking here for portability.
     */
    set_fd_blocking(sd);

    if (conf->enableKeepAlive) {
        if (setsockopt(sd, SOL_SOCKET, SO_KEEPALIVE,
          (const void *) &on, sizeof(on)) < 0) {
            log_err(errno, "Unable to set KEEPALIVE socket option");
        }
    }
    /*  Create a tmp struct to hold two args to pass to the thread.
     *  Note that the thread is responsible for freeing this memory.
     */
    if (!(args = malloc(sizeof(client_arg_t)))) {
        out_of_memory();
    }
    args->sd = sd;
    args->conf = conf;

    if ((rc = pthread_create(&tid, NULL,
      (PthreadFunc) process_client, args)) != 0) {
        log_err(rc, "Unable to create new thread");
    }
    return;
}
