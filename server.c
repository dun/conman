/******************************************************************************\
 *  $Id: server.c,v 1.22 2001/09/07 18:27:41 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
\******************************************************************************/


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

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
#include <unistd.h>
#include "common.h"
#include "errors.h"
#include "list.h"
#include "server.h"
#include "util.h"
#include "util-file.h"


static int begin_daemonize(void);
static void end_daemonize(int fd);
static void display_configuration(server_conf_t *conf);
static void exit_handler(int signum);
static void create_listen_socket(server_conf_t *conf);
static void mux_io(server_conf_t *conf);
static void accept_client(server_conf_t *conf);


static int done = 0;


int main(int argc, char *argv[])
{
    int fd;
    server_conf_t *conf;

    fd = begin_daemonize();

    posix_signal(SIGHUP, SIG_IGN);
    posix_signal(SIGPIPE, SIG_IGN);
    posix_signal(SIGINT, exit_handler);
    posix_signal(SIGTERM, exit_handler);

    conf = create_server_conf();
    process_server_cmd_line(argc, argv, conf);
    process_server_conf_file(conf);

    if (conf->logFileName)
        open_msg_log(conf->logFileName);

    if (conf->enableVerbose)
        display_configuration(conf);

    create_listen_socket(conf);

    end_daemonize(fd);

    mux_io(conf);

    destroy_server_conf(conf);

    exit(0);
}


static int begin_daemonize(void)
{
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
    log_msg(0, "Exiting on signal %d.", signum);
    done = 1;
    return;
}


static void display_configuration(server_conf_t *conf)
{
    ListIterator i;
    obj_t *obj;
    int n = 0;

    if (!(i = list_iterator_create(conf->objs)))
        err_msg(0, "Out of memory");
    while ((obj = list_next(i)))
        if (is_console_obj(obj))
            n++;
    list_iterator_destroy(i);

    printf("Starting ConMan daemon %s (pid %d).\n", VERSION, (int) getpid());
    printf("Listening on port %d.\n", conf->port);
    printf("Monitoring %d console%s.\n", n, ((n==1) ? "" : "s"));
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
        if (conf->enableZeroLogs)
            printf(" ZeroLogs");
    }
    printf("\n");

    if (fflush(stdout) < 0)
        err_msg(errno, "Unable to flush standard output");

    return;
}


static void create_listen_socket(server_conf_t *conf)
{
    int ld;
    struct sockaddr_in addr;
    const int on = 1;

    if ((ld = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        err_msg(errno, "Unable to create listening socket");

    set_descriptor_nonblocking(ld);

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
    DPRINTF("Listening for connections on port %d (fd=%d).\n",
        conf->port, conf->ld);
    return;
}


static void mux_io(server_conf_t *conf)
{
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

    if (!(i = list_iterator_create(conf->objs)))
        err_msg(0, "Out of memory");

    while (!done) {

        FD_ZERO(&rset);
        FD_ZERO(&wset);
        FD_SET(conf->ld, &rset);
        maxfd = conf->ld;

        list_iterator_reset(i);
        while ((obj = list_next(i))) {

            if (obj->fd < 0) {
                continue;
            }
            if (is_console_obj(obj) || is_client_obj(obj)) {
                FD_SET(obj->fd, &rset);
                maxfd = MAX(maxfd, obj->fd);
            }
            if (((obj->bufInPtr != obj->bufOutPtr) || (obj->gotEOF))
              && ((!is_client_obj(obj)) || (!obj->aux.client.gotSuspend))) {
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

        while ((n = select(maxfd+1, &rset, &wset, NULL, &tval)) < 0) {
            if (errno != EINTR)
                err_msg(errno, "Unable to multiplex I/O");
            else if (done)
                /* i need a */ break;
        }
        if (n <= 0)
            /* should i */ continue;

        if (FD_ISSET(conf->ld, &rset))
            accept_client(conf);

        /*  If write_to_obj() returns -1, the obj's buffer has been flushed
         *    and the obj is ready to be removed from the master objs list.
         */
        list_iterator_reset(i);
        while ((obj = list_next(i))) {

            if (obj->fd < 0) {
                continue;
            }
            if (FD_ISSET(obj->fd, &rset)) {
                if (read_from_obj(obj, &wset) < 0) {
                    list_delete(i);
                    continue;
                }
            }
            if (FD_ISSET(obj->fd, &wset)) {
                if (write_to_obj(obj) < 0)
                    list_delete(i);
                    continue;
            }
        }
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
        err_msg(0, "Out of memory");
    args->sd = sd;
    args->conf = conf;

    if ((rc = pthread_create(&tid, NULL,
      (PthreadFunc) process_client, args)) != 0)
        err_msg(rc, "Unable to create new thread");

    return;
}
