/*****************************************************************************\
 *  $Id: server.c,v 1.65 2002/09/18 20:32:17 dun Exp $
 *****************************************************************************
 *  Copyright (C) 2001-2002 The Regents of the University of California.
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

#include <arpa/telnet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
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
#include "fd.h"
#include "list.h"
#include "log.h"
#include "server.h"
#include "str.h"
#include "tselect.h"
#include "util.h"
#include "wrapper.h"


#ifdef NDEBUG
static int begin_daemonize(void);
static void end_daemonize(int fd);
#endif /* NDEBUG */
static void display_configuration(server_conf_t *conf);
static void exit_handler(int signum);
static void sig_chld_handler(int signum);
static void sig_hup_handler(int signum);
static void schedule_timestamp(server_conf_t *conf);
static void timestamp_logfiles(server_conf_t *conf);
static void create_listen_socket(server_conf_t *conf);
static void mux_io(server_conf_t *conf);
static void open_daemon_logfile(server_conf_t *conf);
static void reopen_logfiles(server_conf_t *conf);
static void accept_client(server_conf_t *conf);
static void reset_console(obj_t *console, const char *cmd);
static void kill_console_reset(pid_t *arg);


static int done = 0;
static int reconfig = 0;


int main(int argc, char *argv[])
{
    int fd;
    server_conf_t *conf;

#ifdef NDEBUG
    log_set_file(stderr, LOG_WARNING, 0);
    fd = begin_daemonize();
#else /* !NDEBUG */
    log_set_file(stderr, LOG_DEBUG, 0);
    fd = -1;                            /* suppress unused variable warning */
#endif /* !NDEBUG */

    posix_signal(SIGCHLD, sig_chld_handler);
    posix_signal(SIGHUP, sig_hup_handler);
    posix_signal(SIGINT, exit_handler);
    posix_signal(SIGPIPE, SIG_IGN);
    posix_signal(SIGTERM, exit_handler);

    conf = create_server_conf();
    process_server_cmd_line(argc, argv, conf);
    process_server_conf_file(conf);

    if (conf->enableVerbose)
        display_configuration(conf);
    if (conf->tStampMinutes > 0)
        schedule_timestamp(conf);

    create_listen_socket(conf);

    if (conf->syslogFacility > 0)
        log_set_syslog(argv[0], conf->syslogFacility);
    if (conf->logFileName)
        open_daemon_logfile(conf);

#ifdef NDEBUG
    end_daemonize(fd);
    if (!conf->logFileName)
        log_set_file(NULL, 0, 0);
#endif /* NDEBUG */

    log_msg(LOG_NOTICE, "Starting ConMan daemon %s (pid %d)",
        VERSION, (int) getpid());

    mux_io(conf);
    destroy_server_conf(conf);

    log_msg(LOG_NOTICE, "Stopping ConMan daemon %s (pid %d)",
        VERSION, (int) getpid());
    exit(0);
}


#ifdef NDEBUG
static int begin_daemonize(void)
{
/*  Begins the daemonization of the process.
 *  Despite the fact that this routine backgrounds the process, control
 *    will not be returned to the shell until end_daemonize() is called.
 *  Returns an 'fd' to pass to end_daemonize() to complete the daemonization.
 */
    struct rlimit limit;
    int fdPair[2];
    pid_t pid;
    char c;

    /*  Clear file mode creation mask.
     */
    umask(0);

    /*  Disable creation of core files.
     */
    limit.rlim_cur = 0;
    limit.rlim_max = 0;
    if (setrlimit(RLIMIT_CORE, &limit) < 0)
        log_err(errno, "Unable to prevent creation of core file");

    /*  Create pipe for IPC so parent process will wait to terminate until
     *    signaled by grandchild process.  This allows messages written to
     *    stdout/stderr by the grandchild to be properly displayed before
     *    the parent process returns control to the shell.
     */
    if (pipe(fdPair) < 0)
        log_err(errno, "Unable to create pipe");

    /*  Automatically background the process and
     *    ensure child is not a process group leader.
     */
    if ((pid = fork()) < 0) {
        log_err(errno, "Unable to create child process");
    }
    else if (pid > 0) {
        if (close(fdPair[1]) < 0)
            log_err(errno, "Unable to close write-pipe in parent");
        if (read(fdPair[0], &c, 1) < 0)
            log_err(errno, "Read failed while awaiting EOF from grandchild");
        exit(0);
    }
    if (close(fdPair[0]) < 0)
        log_err(errno, "Unable to close read-pipe in child");

    /*  Become a session leader and process group leader
     *    with no controlling tty.
     */
    if (setsid() < 0)
        log_err(errno, "Unable to disassociate controlling tty");

    /*  Ignore SIGHUP to keep child from terminating when
     *    the session leader (ie, the parent) terminates.
     */
    posix_signal(SIGHUP, SIG_IGN);

    /*  Abdicate session leader position in order to guarantee
     *    daemon cannot automatically re-acquire a controlling tty.
     */
    if ((pid = fork()) < 0)
        log_err(errno, "Unable to create grandchild process");
    else if (pid > 0)
        exit(0);

    return(fdPair[1]);
}
#endif /* NDEBUG */


#ifdef NDEBUG
static void end_daemonize(int fd)
{
/*  Completes the daemonization of the process,
 *    where 'fd' is the value returned by begin_daemonize().
 */
    int devNull;

    /*  Ensure process does not keep a directory in use.
     *
     *  XXX: Avoid relative pathname from this point on!
     */
    if (chdir("/") < 0)
        log_err(errno, "Unable to change to root directory");

    /*  Discard data to/from stdin, stdout, and stderr.
     */
    if ((devNull = open("/dev/null", O_RDWR)) < 0)
        log_err(errno, "Unable to open \"/dev/null\"");
    if (dup2(devNull, STDIN_FILENO) < 0)
        log_err(errno, "Unable to dup \"/dev/null\" onto stdin");
    if (dup2(devNull, STDOUT_FILENO) < 0)
        log_err(errno, "Unable to dup \"/dev/null\" onto stdout");
    if (dup2(devNull, STDERR_FILENO) < 0)
        log_err(errno, "Unable to dup \"/dev/null\" onto stderr");
    if (close(devNull) < 0)
        log_err(errno, "Unable to close \"/dev/null\"");

    /*  Signal grandparent process to terminate.
     */
    if ((fd >= 0) && (close(fd) < 0))
        log_err(errno, "Unable to close write-pipe in grandchild");

    return;
}
#endif /* NDEBUG */


static void exit_handler(int signum)
{
    log_msg(LOG_NOTICE, "Exiting on signal=%d", signum);
    done = 1;
    return;
}


static void sig_hup_handler(int signum)
{
    log_msg(LOG_NOTICE, "Performing reconfig on signal=%d", signum);
    reconfig = 1;
    return;
}


static void sig_chld_handler(int signum)
{
    pid_t pid;

    while ((pid = waitpid(-1, NULL, WNOHANG)) > 0)
        DPRINTF((5, "Process %d terminated.\n", (int) pid));
    return;
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
    while ((obj = list_next(i)))
        if (is_console_obj(obj))
            n++;
    list_iterator_destroy(i);

    fprintf(stderr, "\nStarting ConMan daemon %s (pid %d)\n",
        VERSION, (int) getpid());
    fprintf(stderr, "Configuration: %s\n", conf->confFileName);
    fprintf(stderr, "Options:");

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
    struct tm *tmptr;
    struct timeval tv;
    int numCompleted;

    assert(conf->tStampMinutes > 0);

    t = conf->tStampNext;
    if (t == 0) {
        if (time(&t) == (time_t) -1)
            log_err(errno, "time() failed");
    }
    if (!(tmptr = localtime(&t)))
        log_err(errno, "localtime() failed");
    /*
     *  If this is the first scheduled timestamp, compute the expiration time
     *    assuming timestamps have been scheduled regularly since midnight.
     *  Otherwise, base it off of the previous timestamp.
     */
    if (!conf->tStampNext) {
        numCompleted = ((tmptr->tm_hour * 60) + tmptr->tm_min)
            / conf->tStampMinutes;
        tmptr->tm_min = (numCompleted + 1) * conf->tStampMinutes;
        tmptr->tm_hour = 0;
    }
    else {
        tmptr->tm_min += conf->tStampMinutes;
    }
    tmptr->tm_sec = 0;

    if ((t = mktime(tmptr)) == ((time_t) -1))
        log_err(errno, "Unable to determine time of next logfile timestamp");
    tv.tv_sec = t;
    tv.tv_usec = 0;
    conf->tStampNext = t;

    /*  The timer id is not saved because this timer will never be canceled.
     */
    abtimeout((CallBackF) timestamp_logfiles, conf, &tv);

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

    now = str_get_time_long(0);
    i = list_iterator_create(conf->objs);
    while ((logfile = list_next(i))) {
        if (!is_logfile_obj(logfile))
            continue;
        snprintf(buf, sizeof(buf), "%sConsole [%s] log at %s%s",
            CONMAN_MSG_PREFIX, logfile->aux.logfile.consoleName,
            now, CONMAN_MSG_SUFFIX);
        strcpy(&buf[sizeof(buf) - 3], "\r\n");
        write_obj_data(logfile, buf, strlen(buf), 1);
        gotLogs = 1;
    }
    list_iterator_destroy(i);

    /*  If any logfile objs exist, schedule a timer for the next timestamp.
     */
    if (gotLogs)
        schedule_timestamp(conf);

    return;
}


static void create_listen_socket(server_conf_t *conf)
{
/*  Creates the socket on which to listen for client connections.
 */
    int ld;
    struct sockaddr_in addr;
    const int on = 1;

    if ((ld = socket(PF_INET, SOCK_STREAM, 0)) < 0)
        log_err(errno, "Unable to create listening socket");

    fd_set_nonblocking(ld);
    fd_set_close_on_exec(ld);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(conf->port);

    if (conf->enableLoopBack)
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    else
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (setsockopt(ld, SOL_SOCKET, SO_REUSEADDR,
      (const void *) &on, sizeof(on)) < 0)
        log_err(errno, "Unable to set REUSEADDR socket option");

    if (bind(ld, (struct sockaddr *) &addr, sizeof(addr)) < 0)
        log_err(errno, "Unable to bind to port %d", conf->port);

    if (listen(ld, 10) < 0)
        log_err(errno, "Unable to listen on port %d", conf->port);

    conf->ld = ld;
    return;
}


static void mux_io(server_conf_t *conf)
{
/*  Multiplexes I/O between all of the objs in the configuration.
 *  This routine is the heart of ConMan.
 */
    ListIterator i;
    fd_set rset, wset;
    struct timeval tval;
    int maxfd;
    int n;
    obj_t *obj;

    if (list_is_empty(conf->objs)) {
        log_msg(LOG_NOTICE, "No consoles are defined in this configuration");
        return;
    }

    i = list_iterator_create(conf->objs);

    while (!done) {

        if (reconfig) {
            reopen_logfiles(conf);
            reconfig = 0;
        }

        FD_ZERO(&rset);
        FD_ZERO(&wset);
        FD_SET(conf->ld, &rset);
        maxfd = conf->ld;

        list_iterator_reset(i);
        while ((obj = list_next(i))) {

            if (obj->gotReset) {
                reset_console(obj, conf->resetCmd);
            }
            if (obj->fd < 0) {
                continue;
            }
            if ((is_telnet_obj(obj)
                && obj->aux.telnet.conState == CONMAN_TELCON_UP)
              || is_serial_obj(obj)
              || is_client_obj(obj)) {
                FD_SET(obj->fd, &rset);
                maxfd = MAX(maxfd, obj->fd);
            }
            if (((obj->bufInPtr != obj->bufOutPtr) || (obj->gotEOF))
              && (!(is_client_obj(obj) && obj->aux.client.gotSuspend))) {
                FD_SET(obj->fd, &wset);
                maxfd = MAX(maxfd, obj->fd);
            }
            if (is_telnet_obj(obj)
              && obj->aux.telnet.conState == CONMAN_TELCON_PENDING) {
                FD_SET(obj->fd, &rset);
                FD_SET(obj->fd, &wset);
                maxfd = MAX(maxfd, obj->fd);
            }
        }

        /*  Specify a timeout to select() to prevent the following scenario:
         *    Suppose select() blocks after spawning a new thread to handle
         *    a CONNECT request.  This thread adds a client obj to the
         *    conf->objs list to be handled by mux_io().  But read-activity
         *    on this client's socket will not unblock select() because it
         *    does not yet exist in select()'s read-set.
         *  Although select() is to treat the timeval struct as a constant,
         *    Linux systems reportedly modify it (cf. Stevens UNPv1 p151).
         *    As such, initialize it before each call to select().
         */
        tval.tv_sec = 1;
        tval.tv_usec = 0;

        while ((n = tselect(maxfd+1, &rset, &wset, NULL)) < 0) {
            if (errno != EINTR)
                log_err(errno, "Unable to multiplex I/O");
            else if (done || reconfig)
                /* i need a */ break;
        }
        if (n <= 0)
            /* should i */ continue;

        if (FD_ISSET(conf->ld, &rset))
            accept_client(conf);

        /*  If read_from_obj() or write_to_obj() returns -1,
         *    the obj's buffer has been flushed.  If it is a telnet obj,
         *    retain it and attempt to re-establish the connection;
         *    o/w, give up and remove it from the master objs list.
         */
        list_iterator_reset(i);
        while ((obj = list_next(i))) {

            if (obj->fd < 0) {
                continue;
            }
            if (is_telnet_obj(obj)
              && (obj->aux.telnet.conState == CONMAN_TELCON_PENDING)
              && (FD_ISSET(obj->fd, &rset) || FD_ISSET(obj->fd, &wset))) {
                connect_telnet_obj(obj);
                continue;
            }
            if (FD_ISSET(obj->fd, &rset)) {
                if (read_from_obj(obj, &wset) < 0) {
                    list_delete(i);
                    continue;
                }
                if (obj->fd < 0)
                    continue;
            }
            if (FD_ISSET(obj->fd, &wset)) {
                if (write_to_obj(obj) < 0) {
                    list_delete(i);
                    continue;
                }
                if (obj->fd < 0)
                    continue;
            }
        }
    }

    list_iterator_destroy(i);
    return;
}


static void open_daemon_logfile(server_conf_t *conf)
{
/*  (Re)opens the daemon logfile.
 *  Since this logfile can be re-opened after the daemon has chdir()'d,
 *    it must be specified with an absolute pathname.
 */
    static int once = 1;
    const char *mode = "a";
    FILE *fp;
    int fd;

    assert(conf->logFileName != NULL);
    assert(conf->logFileName[0] == '/');

    /*  Only truncate logfile at startup if needed.
     */
    if (once) {
        if (conf->enableZeroLogs)
            mode = "w";
        once = 0;
    }
    if (!(fp = fopen(conf->logFileName, mode))) {
        log_msg(LOG_WARNING, "Unable to open logfile \"%s\": %s",
            conf->logFileName, strerror(errno));
        goto err;
    }
    if ((fd = fileno(fp)) < 0) {
        log_msg(LOG_WARNING,
            "Unable to obtain descriptor for logfile \"%s\": %s",
            conf->logFileName, strerror(errno));
        goto err;
    }
    if (fd_get_write_lock(fd) < 0) {
        log_msg(LOG_WARNING, "Unable to lock logfile \"%s\"",
            conf->logFileName);
        if (fclose(fp) == EOF)
            log_msg(LOG_WARNING, "Unable to close logfile \"%s\"",
                conf->logFileName);
        goto err;
    }
    fd_set_close_on_exec(fd);

    /*  Transition to new log file.
     */
    log_set_file(fp, conf->logFileLevel, 1);
    if (conf->logFilePtr)
        if (fclose(conf->logFilePtr) == EOF)
            log_msg(LOG_WARNING, "Unable to close logfile \"%s\"",
                conf->logFileName);
    conf->logFilePtr = fp;
    return;

err:
    /*  Abandon old log file and go logless.
     */
    log_set_file(NULL, 0, 0);
    if (conf->logFilePtr)
        if (fclose(conf->logFilePtr) == EOF)
            log_msg(LOG_WARNING, "Unable to close logfile \"%s\"",
                conf->logFileName);
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
        if (!is_logfile_obj(logfile))
            continue;
        open_logfile_obj(logfile, 0);   /* do not truncate the logfile */
    }
    list_iterator_destroy(i);

    open_daemon_logfile(conf);

    return;
}


static void accept_client(server_conf_t *conf)
{
/*  Accepts a new client connection on the listening socket.
 *  The new socket connection must be accept()'d within the select() loop.
 *    O/w, the following scenario could occur:  Read activity would be
 *    select()'d on the listen socket.  A new thread would be created to
 *    process this request.  Before this new thread is scheduled and the
 *    socket connection is accept()'d, the select() loop begins its next
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
        if (errno == EINTR)
            continue;
        if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
            return;
        if (errno == ECONNABORTED)
            return;
        log_err(errno, "Unable to accept new connection");
    }
    DPRINTF((5, "Accepted new client on fd=%d.\n", sd));

    if (conf->enableKeepAlive) {
        if (setsockopt(sd, SOL_SOCKET, SO_KEEPALIVE,
          (const void *) &on, sizeof(on)) < 0)
            log_err(errno, "Unable to set KEEPALIVE socket option");
    }

    /*  Create a tmp struct to hold two args to pass to the thread.
     *  Note that the thread is responsible for freeing this memory.
     */
    if (!(args = malloc(sizeof(client_arg_t))))
        out_of_memory();
    args->sd = sd;
    args->conf = conf;

    if ((rc = pthread_create(&tid, NULL,
      (PthreadFunc) process_client, args)) != 0)
        log_err(rc, "Unable to create new thread");

    return;
}


static void reset_console(obj_t *console, const char *cmd)
{
/*  Resets the 'console' obj by performing the reset 'cmd' in a subshell.
 */
    char cmdbuf[MAX_LINE];
    pid_t pid;
    pid_t *arg;

    assert(is_console_obj(console));
    assert(console->gotReset);
    assert(cmd != NULL);

    DPRINTF((5, "Resetting console [%s].\n", console->name));
    console->gotReset = 0;

    if (str_sub(cmdbuf, sizeof(cmdbuf), cmd,
      DEFAULT_CONFIG_ESCAPE, console->name) < 0) {
        log_msg(LOG_NOTICE, "Unable to reset console [%s]: command too long",
            console->name);
        return;
    }
    if ((pid = fork()) < 0) {
        log_msg(LOG_NOTICE, "Unable to reset console [%s]: %s",
            console->name, strerror(errno));
        return;
    }
    else if (pid == 0) {
        setpgid(pid, 0);
        close(STDIN_FILENO);            /* ignore errors on close() */
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        execl("/bin/sh", "sh", "-c", cmdbuf, NULL);
        _exit(127);                     /* execl() error */
    }
    /*  Both parent and child call setpgid() to make the child a process
     *    group leader.  One of these calls is redundant, but by doing
     *    both we avoid a race condition.  (cf. APUE 9.4 p244)
     */
    setpgid(pid, 0);

    /*  Set a timer to ensure the reset cmd does not exceed its time limit.
     *  The callback function's arg must be allocated on the heap since
     *    local vars on the stack will be lost once this routine returns.
     */
    if (!(arg = malloc(sizeof *arg)))
        out_of_memory();
    *arg = pid;
    timeout((CallBackF) kill_console_reset, arg, RESET_CMD_TIMEOUT * 1000);

    return;
}


static void kill_console_reset(pid_t *arg)
{
/*  Terminates the "ResetCmd" process associated with 'arg' if it has
 *    exceeded its time limit.
 *  Memory allocated to 'arg' will be free()'d by this routine.
 */
    pid_t pid;

    assert(arg != NULL);
    pid = *arg;
    assert(pid > 0);
    free(arg);

    if (kill(pid, 0) < 0)               /* process is no longer running */
        return;
    if (kill(-pid, SIGKILL) == 0)       /* kill entire process group */
        log_msg(LOG_WARNING, "ResetCmd process pid=%d exceeded %ds time limit",
            (int) pid, RESET_CMD_TIMEOUT);
    return;
}
