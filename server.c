/******************************************************************************\
 *  $Id: server.c,v 1.42 2001/12/29 04:38:23 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
\******************************************************************************/


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <arpa/telnet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
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
#include "errors.h"
#include "list.h"
#include "server.h"
#include "tselect.h"
#include "util.h"
#include "util-file.h"
#include "util-str.h"
#include "wrapper.h"


static int begin_daemonize(void);
static void end_daemonize(int fd);
static void display_configuration(server_conf_t *conf);
static void exit_handler(int signum);
static void sig_chld_handler(int signum);
static void sig_hup_handler(int signum);
static void schedule_timestamp(server_conf_t *conf);
static void timestamp_logfiles(server_conf_t *conf);
static void create_listen_socket(server_conf_t *conf);
static void mux_io(server_conf_t *conf);
static void reopen_logfiles(List objs);
static void accept_client(server_conf_t *conf);
static void reset_console(obj_t *console, const char *cmd);
static void kill_console_reset(pid_t pid);


static int done = 0;
static int reconfig = 0;


int main(int argc, char *argv[])
{
    int fd;
    server_conf_t *conf;

    fd = begin_daemonize();

    posix_signal(SIGCHLD, sig_chld_handler);
    posix_signal(SIGHUP, sig_hup_handler);
    posix_signal(SIGINT, exit_handler);
    posix_signal(SIGPIPE, SIG_IGN);
    posix_signal(SIGTERM, exit_handler);

    conf = create_server_conf();
    process_server_cmd_line(argc, argv, conf);
    process_server_conf_file(conf);

    if (conf->logFileName)
        open_msg_log(conf->logFileName);
    if (conf->enableVerbose)
        display_configuration(conf);
    if (conf->tStampMinutes > 0)
        schedule_timestamp(conf);

    create_listen_socket(conf);
    end_daemonize(fd);

    mux_io(conf);
    destroy_server_conf(conf);

    exit(0);
}


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

#ifndef NDEBUG
    /*  Do not execute routine during DEBUG.
     */
    return(-1);
#endif /* !NDEBUG */

    /*  Clear file mode creation mask.
     */
    umask(0);

    /*  Disable creation of core files.
     */
    limit.rlim_cur = 0;
    limit.rlim_max = 0;
    if (setrlimit(RLIMIT_CORE, &limit) < 0)
        err_msg(errno, "Unable to prevent creation of core file");

    /*  Create pipe for IPC so parent process will wait to terminate until
     *    signaled by grandchild process.  This allows messages written to
     *    stdout/stderr by the grandchild to be properly displayed before
     *    the parent process returns control to the shell.
     */
    if (pipe(fdPair) < 0)
        err_msg(errno, "Unable to create pipe");

    /*  Automatically background the process and
     *    ensure child is not a process group leader.
     */
    if ((pid = fork()) < 0) {
        err_msg(errno, "Unable to create child process");
    }
    else if (pid > 0) {
        if (close(fdPair[1]) < 0)
            err_msg(errno, "Unable to close write-pipe in parent");
        if (read(fdPair[0], &c, 1) < 0)
            err_msg(errno, "Read failed while awaiting EOF from grandchild");
        exit(0);
    }
    if (close(fdPair[0]) < 0)
        err_msg(errno, "Unable to close read-pipe in child");

    /*  Become a session leader and process group leader
     *    with no controlling tty.
     */
    if (setsid() < 0)
        err_msg(errno, "Unable to disassociate controlling tty");

    /*  Ignore SIGHUP to keep child from terminating when
     *    the session leader (ie, the parent) terminates.
     */
    posix_signal(SIGHUP, SIG_IGN);

    /*  Abdicate session leader position in order to guarantee
     *    daemon cannot automatically re-acquire a controlling tty.
     */
    if ((pid = fork()) < 0)
        err_msg(errno, "Unable to create grandchild process");
    else if (pid > 0)
        exit(0);

    return(fdPair[1]);
}


static void end_daemonize(int fd)
{
/*  Completes the daemonization of the process,
 *    where 'fd' is the value returned by begin_daemonize().
 */
    int devNull;

#ifndef NDEBUG
    /*  Do not execute routine during DEBUG.
     */
    return;
#endif /* !NDEBUG */

    /*  Ensure process does not keep a directory in use.
     */
    if (chdir("/") < 0)
        err_msg(errno, "Unable to change to root directory");

    /*  Discard data to/from stdin, stdout, and stderr.
     */
    if ((devNull = open("/dev/null", O_RDWR)) < 0)
        err_msg(errno, "Unable to open \"/dev/null\"");
    if (dup2(devNull, STDIN_FILENO) < 0)
        err_msg(errno, "Unable to dup \"/dev/null\" onto stdin");
    if (dup2(devNull, STDOUT_FILENO) < 0)
        err_msg(errno, "Unable to dup \"/dev/null\" onto stdout");
    if (dup2(devNull, STDERR_FILENO) < 0)
        err_msg(errno, "Unable to dup \"/dev/null\" onto stderr");
    if (close(devNull) < 0)
        err_msg(errno, "Unable to close \"/dev/null\"");

    /*  Signal grandparent process to terminate.
     */
    if ((fd >= 0) && (close(fd) < 0))
        err_msg(errno, "Unable to close write-pipe in grandchild");

    return;
}


static void exit_handler(int signum)
{
    log_msg(0, "Exiting on signal=%d.", signum);
    done = 1;
    return;
}


static void sig_hup_handler(int signum)
{
    log_msg(0, "Performing reconfig on signal=%d.", signum);
    reconfig = 1;
    return;
}


static void sig_chld_handler(int signum)
{
    pid_t pid;

    while ((pid = waitpid(-1, NULL, WNOHANG)) > 0)
        DPRINTF("Process %d terminated.\n", (int) pid);
    return;
}


static void display_configuration(server_conf_t *conf)
{
/*  Displays a summary of the server's configuration.
 */
    ListIterator i;
    obj_t *obj;
    int n = 0;

    i = list_iterator_create(conf->objs);
    while ((obj = list_next(i)))
        if (is_console_obj(obj))
            n++;
    list_iterator_destroy(i);

    printf("Starting ConMan daemon %s (pid %d).\n", VERSION, (int) getpid());
    printf("Configuration: %s\n", conf->confFileName);
    printf("Options:");
    if (!conf->enableKeepAlive
      && !conf->enableZeroLogs
      && !conf->enableLoopBack) {
        printf(" None");
    }
    else {
        if (conf->enableKeepAlive)
            printf(" KeepAlive");
        if (conf->enableLoopBack)
            printf(" LoopBack");
        if (conf->resetCmd)
            printf(" ResetCmd");
        if (conf->enableTCPWrap)
            printf(" TCP-Wrappers");
        if (conf->tStampMinutes > 0)
            printf(" TimeStamp=%dm", conf->tStampMinutes);
        if (conf->enableZeroLogs)
            printf(" ZeroLogs");
    }
    printf("\n");
    printf("Listening on port %d.\n", conf->port);
    printf("Monitoring %d console%s.\n", n, ((n == 1) ? "" : "s"));

    if (fflush(stdout) < 0)
        err_msg(errno, "Unable to flush standard output");

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

    if ((t = mktime(&tm)) == ((time_t) -1))
        err_msg(errno, "Unable to determine time of next logfile timestamp");
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

    now = create_long_time_string(0);
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
    free(now);

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

    if ((ld = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        err_msg(errno, "Unable to create listening socket");

    set_fd_nonblocking(ld);
    set_fd_closed_on_exec(ld);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(conf->port);

    if (conf->enableLoopBack)
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    else
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (setsockopt(ld, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
        err_msg(errno, "Unable to set REUSEADDR socket option");

    if (bind(ld, (struct sockaddr *) &addr, sizeof(addr)) < 0)
        err_msg(errno, "Unable to bind to port %d", conf->port);

    if (listen(ld, 10) < 0)
        err_msg(errno, "Unable to listen on port %d", conf->port);

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
        log_msg(0, "No consoles are defined in this configuration.");
        return;
    }

    i = list_iterator_create(conf->objs);

    while (!done) {

        if (reconfig) {
            reopen_logfiles(conf->objs);
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
            if ((is_telnet_obj(obj) && obj->aux.telnet.conState == TELCON_UP)
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
              && obj->aux.telnet.conState == TELCON_PENDING) {
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
                err_msg(errno, "Unable to multiplex I/O");
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
              && (obj->aux.telnet.conState == TELCON_PENDING)
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


static void reopen_logfiles(List objs)
{
/*  Reopens all of the logfiles in the 'objs' list.
 */
    ListIterator i;
    obj_t *logfile;

    /*  FIX_ME: Re-Open server's logfile here as well.
     */
    i = list_iterator_create(objs);
    while ((logfile = list_next(i))) {
        if (!is_logfile_obj(logfile))
            continue;
        open_logfile_obj(logfile, 0);	/* do not truncate the logfile */
    }
    list_iterator_destroy(i);
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
        err_msg(errno, "Unable to accept new connection");
    }
    DPRINTF("Accepted new client on fd=%d.\n", sd);

    if (conf->enableKeepAlive) {
        if (setsockopt(sd, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on)) < 0)
            err_msg(errno, "Unable to set KEEPALIVE socket option");
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
        err_msg(rc, "Unable to create new thread");

    return;
}


static void reset_console(obj_t *console, const char *cmd)
{
/*  Resets the 'console' obj by performing the reset 'cmd' in a subshell.
 */
    char cmdbuf[MAX_LINE];
    pid_t pid;

    assert(is_console_obj(console));
    assert(console->gotReset);
    assert(cmd != NULL);

    DPRINTF("Resetting console [%s].\n", console->name);
    console->gotReset = 0;

    if (substitute_string(cmdbuf, sizeof(cmdbuf), cmd,
      DEFAULT_CONFIG_ESCAPE, console->name) < 0) {
        log_msg(0, "Unable to reset console [%s]: command too long.",
            console->name);
        return;
    }
    if ((pid = fork()) < 0) {
        log_msg(0, "Unable to reset console [%s]: %s.",
            console->name, strerror(errno));
        return;
    }
    else if (pid == 0) {
        setpgid(pid, 0);
        close(STDIN_FILENO);    	/* ignore errors on close() */
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        execl("/bin/sh", "sh", "-c", cmdbuf, NULL);
        _exit(127);			/* execl() error */
    }
    /*  Both parent and child call setpgid() to make the child a process
     *    group leader.  One of these calls is redundant, but by doing
     *    both we avoid a race condition.  (cf. APUE 9.4 p244)
     */
    setpgid(pid, 0);
    /*
     *  Set a timer to ensure the reset cmd does not exceed its time limit.
     */
    timeout((CallBackF) kill_console_reset, (void *) pid,
        RESET_CMD_TIMEOUT * 1000);
    return;
}


static void kill_console_reset(pid_t pid)
{
/*  Terminates the "ResetCmd" process 'pid' if it has exceeded its time limit.
 */
    assert(pid > 0);

    if (kill(pid, 0) < 0)		/* process is no longer running */
        return;
    if (kill(-pid, SIGKILL) == 0)	/* kill entire process group */
        log_msg(0, "ResetCmd process pid=%d exceeded %ds time limit.",
            (int) pid, RESET_CMD_TIMEOUT);
    return;
}
