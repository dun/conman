/******************************************************************************\
 *  server.c
 *    by Chris Dunlap <cdunlap@llnl.gov>
 *
 *  $Id: server.c,v 1.1 2001/05/04 15:26:41 dun Exp $
\******************************************************************************/


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include "conman.h"
#include "errors.h"
#include "list.h"
#include "server.h"
#include "util.h"


void daemonize(void);
void exit_handler(int signum);
void enable_console_logging(server_conf_t *conf);
void create_listen_socket(server_conf_t *conf);
void mux_io(server_conf_t *conf);


static int done = 0;


int main(int argc, char *argv[])
{
    server_conf_t *conf;

    /* FIX_ME: Should check be performed here to ensure we are root?
     */
    conf = create_server_conf();
    process_server_cmd_line(argc, argv, conf);
    process_server_conf_file(conf);

    daemonize();

    Signal(SIGHUP, SIG_IGN);
    Signal(SIGPIPE, SIG_IGN);
    Signal(SIGINT, exit_handler);
    Signal(SIGTERM, exit_handler);

    /*  Server's log file must be opened AFTER daemonize(),
     *    otherwise its file descriptor will be closed.
     */
    if (conf->logname)
        open_msg_log(conf->logname);

    enable_console_logging(conf);
    create_listen_socket(conf);

    mux_io(conf);

    destroy_server_conf(conf);

    exit(0);
}


void daemonize(void)
{
    int i;
    pid_t pid;

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

    /*  Close "all" file descriptors and ignore the return code since
     *    most of them will return EBADF.  There is no easy way to
     *    determine "all" (cf, APUE), so we just close the first 20.
     */
    for (i=0; i<20; i++)
        close(i);
    errno = 0;

    /*  Clear file mode creation mask.
     */
    umask(0);

    return;
}


void exit_handler(int signum)
{
    log_msg(0, "Exiting on signal %d.", signum);
    done = 1;
    return;
}


void enable_console_logging(server_conf_t *conf)
{
    int rc;
    ListIterator i;
    obj_t *obj;
    obj_t *log;

    if ((rc = pthread_mutex_lock(&conf->objsLock)) != 0)
        err_msg(rc, "pthread_mutex_lock() failed for objs");

    if (!(i = list_iterator_create(conf->objs)))
        err_msg(0, "Out of memory");
    while ((obj = list_next(i))) {
        if ((obj->type != CONSOLE) || (obj->aux.console.log == NULL))
            continue;
        if (!(log = create_logfile_obj(obj->aux.console.log)))
            continue;
        if (!list_append(conf->objs, log))
            err_msg(0, "Out of memory");
        create_obj_link(obj, log);
    }
    list_iterator_destroy(i);

    if ((rc = pthread_mutex_unlock(&conf->objsLock)) != 0)
        err_msg(rc, "pthread_mutex_unlock() failed for objs");
    return;
}


void create_listen_socket(server_conf_t *conf)
{
    int ld;
    struct sockaddr_in addr;
    const int on = 1;

    if ((ld = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        err_msg(errno, "socket() failed");

    set_descriptor_nonblocking(ld);

    /*  Restrict listening socket to loopback until connection is secure.
     */
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(DEFAULT_CONMAN_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (setsockopt(ld, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
        err_msg(errno, "setsockopt() failed");

    if (bind(ld, (struct sockaddr *) &addr, sizeof(addr)) < 0)
        err_msg(errno, "bind() failed");

    if (listen(ld, 10) < 0)
        err_msg(errno, "listen() failed");

    conf->ld = ld;
    return;
}


void mux_io(server_conf_t *conf)
{
    ListIterator i;
    fd_set rset, wset;
    int maxfd;
    int n;
    int rc;
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
            if (obj->fd < 0)
                continue;
            if (obj->type == CONSOLE || obj->type == SOCKET)
                FD_SET(obj->fd, &rset);
            if (obj->writer != NULL && (obj->bufInPtr != obj->bufOutPtr))
                FD_SET(obj->fd, &wset);
            maxfd = MAX(maxfd, obj->fd);
        }

        while ((n = select(maxfd+1, &rset, &wset, NULL, NULL)) < 0) {
            if (errno != EINTR)
                err_msg(errno, "select() failed");
            else if (done)
                /* i need a */ break;
        }

        if (n <= 0)
            /* should i */ continue;

        if (FD_ISSET(conf->ld, &rset)) {
            if ((rc = pthread_create(NULL, NULL,
              (PthreadFunc) process_client, (void *) conf)) != 0)
                err_msg(rc, "pthread_create() failed");
        }

        /*  FIX_ME: Something must be done to protect access to
         *    both the conf->objs list and obj->readers list.
         *    mux_io() cannot simply hold the objsLock mutex
         *    the whole time since that would prevent any other
         *    threads from accessing the list.
         *  Maybe put a mutex in the List class to protect stuff
         *    like list insertions and deletions, etc.
         */
        list_iterator_reset(i);
        while ((obj = list_next(i))) {
            if (obj->fd < 0)
                continue;
            if (FD_ISSET(obj->fd, &wset))
                write_to_obj(obj);
            if (FD_ISSET(obj->fd, &rset))
                read_from_obj(obj);
        }
    }

    list_iterator_destroy(i);
    return;
}
