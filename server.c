/******************************************************************************\
 *  server.c
 *    by Chris Dunlap <cdunlap@llnl.gov>
 *
 *  $Id: server.c,v 1.7 2001/05/18 15:48:16 dun Exp $
\******************************************************************************/


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include "conman.h"
#include "errors.h"
#include "list.h"
#include "server.h"
#include "util.h"


static void daemonize(void);
static void exit_handler(int signum);
static void create_listen_socket(server_conf_t *conf);
static void mux_io(server_conf_t *conf);
static void accept_client(server_conf_t *conf);


static int done = 0;


int main(int argc, char *argv[])
{
    server_conf_t *conf;

    /* FIX_ME: Should check be performed here to ensure we are root?
     */
    conf = create_server_conf();
    process_server_cmd_line(argc, argv, conf);
    process_server_conf_file(conf);

    if (conf->logname)
        open_msg_log(conf->logname);

    /* FIX_ME: Exit handlers do not seem to be effective after daemonize().
     */
    daemonize();

    Signal(SIGHUP, SIG_IGN);
    Signal(SIGPIPE, SIG_IGN);
    Signal(SIGINT, exit_handler);
    Signal(SIGTERM, exit_handler);

    create_listen_socket(conf);

    mux_io(conf);

    destroy_server_conf(conf);

    exit(0);
}


static void daemonize(void)
{
    pid_t pid;
    int devnull;

#ifndef NDEBUG
    /*  Don't daemonize during DEBUG.
     */
    return;
#endif /* !NDEBUG */

    /*  Automatically background the process and
     *    ensure child is not a process group leader.
     */
    if ((pid = fork()) < 0)
        err_msg(errno, "fork() failed");
    else if (pid > 0)
        exit(0);

    /*  Become a session leader and process group leader
     *    with no controlling tty.
     */
    if (setsid() == -1)
        err_msg(errno, "setsid() failed");

    /*  Abdicate session leader position in order to guarantee
     *    daemon cannot automatically re-acquire a controlling tty.
     *  And ignore SIGHUP to keep child from terminating when
     *    the session leader (ie, the parent) terminates.
     */
    Signal(SIGHUP, SIG_IGN);
    if ((pid = fork()) < 0)
        err_msg(errno, "fork() failed");
    else if (pid > 0)
        exit(0);

    /*  Discard data to/from stdin, stdout, and stderr.
     */
    if ((devnull = open("/dev/null", O_RDWR)) < 0)
        err_msg(errno, "open(/dev/null) failed");
    if (dup2(devnull, STDIN_FILENO) < 0)
        err_msg(errno, "dup2(stdin) failed");
    if (dup2(devnull, STDOUT_FILENO) < 0)
        err_msg(errno, "dup2(stdout) failed");
    if (dup2(devnull, STDERR_FILENO) < 0)
        err_msg(errno, "dup2(stderr) failed");
    if (close(devnull) < 0)
        err_msg(errno, "close(/dev/null) failed");

    /*  Clear file mode creation mask.
     */
    umask(0);

    return;
}


static void exit_handler(int signum)
{
    log_msg(0, "Exiting on signal %d.", signum);
    done = 1;
    return;
}


static void create_listen_socket(server_conf_t *conf)
{
    int ld;
    struct sockaddr_in addr;
    const int on = 1;

    if ((ld = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        err_msg(errno, "socket() failed");

    set_descriptor_nonblocking(ld);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(DEFAULT_CONMAN_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (setsockopt(ld, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
        err_msg(errno, "setsockopt() failed");

    if (bind(ld, (struct sockaddr *) &addr, sizeof(addr)) < 0)
        err_msg(errno, "bind() failed");

    if (listen(ld, 10) < 0)
        err_msg(errno, "listen() failed");

    conf->ld = ld;
    DPRINTF("Listing for client connections on fd=%d.\n", conf->ld);
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
        log_msg(0, "No consoles have been defined in the configuration");
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
            if ((obj->type == CONSOLE) || (obj->type == SOCKET)) {
                FD_SET(obj->fd, &rset);
                maxfd = MAX(maxfd, obj->fd);
            }
            if (obj->bufInPtr != obj->bufOutPtr) {
                FD_SET(obj->fd, &wset);
                maxfd = MAX(maxfd, obj->fd);
            }
        }

        /*  Specify a timeout to select() to prevent the following scenario:
         *    Suppose select() blocks after spawning a new thread to handle
         *    a CONNECT request.  This thread adds a socket obj to the
         *    conf->objs list to be handled by mux_io().  But read-activity
         *    on this socket (or its associated console) will not unblock
         *    select() because it is not yet listed in select()'s read-set.
         *  Although select() is to treat the timeval struct as a constant,
         *    Linux systems reportedly modify it (cf. Stevens UNPv1 p151).
         *    As such, initialize it before each call to select().
         */
        tval.tv_sec = 1;
        tval.tv_usec = 0;

        while ((n = select(maxfd+1, &rset, &wset, NULL, &tval)) < 0) {
            if (errno != EINTR)
                err_msg(errno, "select() failed");
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
            if (obj->fd < 0)
                continue;
            if (FD_ISSET(obj->fd, &rset))
                read_from_obj(obj, &wset);
            if (FD_ISSET(obj->fd, &wset))
                if (write_to_obj(obj) < 0)
                    list_delete(i);
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
        err_msg(errno, "accept() failed");
    }
    DPRINTF("Accepted new client on fd=%d.\n", sd);

    /*  Create a tmp struct to hold two args to pass to the thread.
     *  Note that the thread is responsible for freeing this memory.
     */
    if (!(args = malloc(sizeof(client_arg_t))))
        err_msg(0, "Out of memory");
    args->sd = sd;
    args->conf = conf;

    if ((rc = pthread_create(&tid, NULL,
      (PthreadFunc) process_client, args)) != 0)
        err_msg(rc, "pthread_create() failed");

    return;
}
