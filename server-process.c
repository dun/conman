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
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "list.h"
#include "log.h"
#include "server.h"
#include "tpoll.h"
#include "util.h"
#include "util-file.h"
#include "util-str.h"


static int search_exec_path(const char *path, const char *src,
    char *dst, int dstlen);
static int  disconnect_process_obj(obj_t *process);
static int  connect_process_obj(obj_t *process);
static int  check_process_prog(obj_t *process);
static void reset_process_delay(obj_t *process);

extern tpoll_t tp_global;               /* defined in server.c */


int is_process_dev(const char *dev, const char *cwd,
    const char *exec_path, char **path_ref)
{
    char         buf[PATH_MAX];
    int          n;
    struct stat  st;

    assert(dev != NULL);

    if (!strchr(dev, '/')
            && (search_exec_path(exec_path, dev, buf, sizeof(buf)) == 0)) {
        dev = buf;
    }
    else if ((dev[0] != '/') && (cwd != NULL)) {
        n = snprintf(buf, sizeof(buf), "%s/%s", cwd, dev);
        if ((n < 0) || ((size_t) n >= sizeof(buf))) {
            return(0);
        }
        dev = buf;
    }
    if (stat(dev, &st) < 0) {
        return(0);
    }
    if (!S_ISREG(st.st_mode)) {
        return(0);
    }
    if (access(dev, X_OK) < 0) {
        return(0);
    }
    if (path_ref) {
        *path_ref = create_string(dev);
    }
    return(1);
}


static int search_exec_path(const char *path, const char *src,
    char *dst, int dstlen)
{
    char         path_buf[PATH_MAX];
    char         file_buf[PATH_MAX];
    char        *p;
    char        *q;
    struct stat  st;
    int          n;

    assert((path == NULL) || (strlen(path) < PATH_MAX));
    assert(src != NULL);
    assert(strchr(src, '/') == NULL);
    assert(dst != NULL);
    assert(dstlen >= 0);

    if (!path) {
        return(-1);
    }
    if (strlcpy(path_buf, path, sizeof(path_buf)) >= sizeof(path_buf)) {
        return(-1);
    }
    for (p = path_buf; p && *p; p = q) {
        if ((q = strchr(p, ':'))) {
            *q++ = '\0';
        }
        else {
            q = strchr(p, '\0');
        }
        if (stat(p, &st) < 0) {
            continue;
        }
        if (!S_ISDIR(st.st_mode)) {
            continue;
        }
        n = snprintf(file_buf, sizeof(file_buf), "%s/%s", p, src);
        if ((n < 0) || ((size_t) n >= sizeof(file_buf))) {
            continue;
        }
        if (access(file_buf, X_OK) < 0) {
            continue;
        }
        if ((dst != NULL) && (dstlen > 0)) {
            if (strlcpy(dst, file_buf, dstlen) >= (size_t) dstlen) {
                return(1);
            }
        }
        return(0);
    }
    return(-1);
}


obj_t * create_process_obj(server_conf_t *conf, char *name, List args,
    char *errbuf, int errlen)
{
/*  Creates a new process device object and adds it to the master objs list.
 *  Note: an external process will later be fork/exec'd based on the argv
 *    by main:open_objs:reopen_obj:open_process_obj().
 *  Returns the new object, or NULL on error.
 */
    ListIterator   i;
    obj_t         *process;
    process_obj_t *auxp;
    int            num_args;
    int            n;
    char          *arg;

    assert(conf != NULL);
    assert((name != NULL) && (name[0] != '\0'));
    assert((args != NULL) && (list_count(args) > 0));

    /*  Check for duplicate console names.
     */
    i = list_iterator_create(conf->objs);
    while ((process = list_next(i))) {
        if (is_console_obj(process) && !strcmp(process->name, name)) {
            if ((errbuf != NULL) && (errlen > 0)) {
                snprintf(errbuf, errlen,
                    "console [%s] specifies duplicate console name", name);
            }
            break;
        }
    }
    list_iterator_destroy(i);
    if (process != NULL) {
        return(NULL);
    }
    process = create_obj(conf, name, -1, CONMAN_OBJ_PROCESS);
    auxp = &(process->aux.process);

    auxp->timer = -1;
    auxp->delay = PROCESS_MIN_TIMEOUT;
    auxp->pid = -1;
    auxp->tStart = 0;
    auxp->logfile = NULL;
    auxp->state = CONMAN_PROCESS_DOWN;
    num_args = list_count(args);
    auxp->argv = calloc(num_args + 1, sizeof(char *));
    if (!auxp->argv) {
        out_of_memory();
    }
    for (n = 0; (n < num_args) && (arg = list_pop(args)); n++) {
        auxp->argv[n] = arg;
    }
    auxp->argv[n] = (char *) NULL;
    if ((arg = strrchr(auxp->argv[0], '/'))) {
        auxp->prog = arg + 1;
    }
    else {
        auxp->prog = auxp->argv[0];
    }
    /*  Add obj to the master conf->objs list.
     */
    list_append(conf->objs, process);

    return(process);
}


int open_process_obj(obj_t *process)
{
/*  (Re)opens the specified 'process' obj.
 *  Returns 0 if the child process is successfully opened;
 *    o/w, sets a reconnect timer and returns -1.
 */
    process_obj_t *auxp;
    int            rc = 0;

    assert(process != NULL);
    assert(is_process_obj(process));

    auxp = &(process->aux.process);

    if (auxp->timer >= 0) {
        (void) tpoll_timeout_cancel(tp_global, auxp->timer);
        auxp->timer = -1;
    }

    if (auxp->state == CONMAN_PROCESS_UP) {
        rc = disconnect_process_obj(process);
    }
    else if (auxp->state == CONMAN_PROCESS_DOWN) {
        rc = connect_process_obj(process);
    }

    if (rc < 0) {
        DPRINTF((15, "Retrying [%s] connection to prog=\"%s\" in %ds\n",
            process->name, auxp->argv[0], auxp->delay));

        auxp->timer = tpoll_timeout_relative(tp_global,
            (callback_f) open_process_obj, process, auxp->delay * 1000);

        auxp->delay = (auxp->delay == 0)
            ? PROCESS_MIN_TIMEOUT
            : MIN(auxp->delay * 2, PROCESS_MAX_TIMEOUT);
    }
    return(rc);
}


static int disconnect_process_obj(obj_t *process)
{
/*  Closes the existing connection with the specified 'process' obj.
 *  Always returns -1 to signal a reconnect.
 */
    process_obj_t *auxp;
    time_t         tNow;
    char          *delta_str;

    assert(process != NULL);
    assert(process->aux.process.pid > 0);
    assert(process->aux.process.tStart > 0);
    assert(process->aux.process.state != CONMAN_PROCESS_DOWN);

    auxp = &(process->aux.process);

    if (process->fd >= 0) {
        tpoll_clear(tp_global, process->fd, POLLIN | POLLOUT);
        (void) close(process->fd);
        process->fd = -1;
    }
    if (time(&tNow) == (time_t) -1) {
        log_err(errno, "time() failed");
    }
    delta_str = create_time_delta_string(auxp->tStart, tNow);

    /*  Notify linked objs when transitioning from an UP state.
     */
    assert(auxp->state == CONMAN_PROCESS_UP);
    write_notify_msg(process, LOG_INFO,
        "Console [%s] disconnected from \"%s\" (pid %d) after %s",
        process->name, auxp->prog, auxp->pid, delta_str);
    free(delta_str);

    (void) kill(auxp->pid, SIGKILL);
    auxp->pid = -1;
    auxp->tStart = 0;
    auxp->state = CONMAN_PROCESS_DOWN;
    return (-1);
}


static int connect_process_obj(obj_t *process)
{
/*  Opens a connection to the specified 'process' obj.
 *  Returns 0 if the connection is successfully completed; o/w, returns -1.
 */
    process_obj_t *auxp;
    int            fd_pair[2] = {-1,-1};
    pid_t          pid;

    assert(process != NULL);
    assert(process->fd == -1);
    assert(process->aux.process.pid == -1);
    assert(process->aux.process.state != CONMAN_PROCESS_UP);

    auxp = &(process->aux.process);

    if (check_process_prog(process) < 0) {
        goto err;
    }
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd_pair) < 0) {
        write_notify_msg(process, LOG_WARNING,
            "Console [%s] connection failed: socketpair error: %s",
            process->name, strerror(errno));
        goto err;
    }
    set_fd_nonblocking(fd_pair[0]);
    set_fd_nonblocking(fd_pair[1]);
    set_fd_closed_on_exec(fd_pair[0]);
    set_fd_closed_on_exec(fd_pair[1]);

    if ((pid = fork()) < 0) {
        write_notify_msg(process, LOG_WARNING,
            "Console [%s] connection failed: fork error: %s",
            process->name, strerror(errno));
        goto err;
    }
    else if (pid == 0) {
        if (close(fd_pair[0]) < 0) {
            log_err(errno, "close() of child fd_pair failed");
        }
        if (dup2(fd_pair[1], STDIN_FILENO) < 0) {
            log_err(errno, "dup2() of child stdin failed");
        }
        if (dup2(fd_pair[1], STDOUT_FILENO) < 0) {
            log_err(errno, "dup2() of child stdout failed");
        }
        if (dup2(fd_pair[1], STDERR_FILENO) < 0) {
            log_err(errno, "dup2() of child stderr failed");
        }
        execv(auxp->argv[0], auxp->argv);
        _exit(127);
    }
    if (close(fd_pair[1]) < 0) {
        log_err(errno, "close() of parent fd_pair failed");
    }
    if (time(&(auxp->tStart)) == (time_t) -1) {
        log_err(errno, "time() failed");
    }
    process->fd = fd_pair[0];
    auxp->pid = pid;
    process->gotEOF = 0;
    auxp->state = CONMAN_PROCESS_UP;
    tpoll_set(tp_global, process->fd, POLLIN);

    /*  Require the connection to be up for a minimum length of time before
     *    resetting the reconnect-delay back to zero.
     */
    auxp->timer = tpoll_timeout_relative(tp_global,
        (callback_f) reset_process_delay, process, PROCESS_MIN_TIMEOUT * 1000);

    /*  Notify linked objs when transitioning into an UP state.
     */
    write_notify_msg(process, LOG_INFO,
        "Console [%s] connected to \"%s\" (pid %d)",
        process->name, auxp->prog, auxp->pid);
    DPRINTF((9, "Opened [%s] process: fd=%d/%d prog=\"%s\" pid=%d.\n",
        process->name, fd_pair[0], fd_pair[1], auxp->argv[0], auxp->pid));

    return(0);

err:
    if (fd_pair[0] >= 0) {
        (void) close(fd_pair[0]);
    }
    if (fd_pair[1] >= 0) {
        (void) close(fd_pair[1]);
    }
    return(-1);
}


static int check_process_prog(obj_t *process)
{
/*  Checks whether the 'process' executable will likely exec.
 *  Returns 0 if all checks pass; o/w, returns -1.
 */
    process_obj_t *auxp;
    struct stat    st;

    assert(process != NULL);

    auxp = &(process->aux.process);

    if (stat(auxp->argv[0], &st) < 0) {
        write_notify_msg(process, LOG_WARNING,
            "Console [%s] connection failed: \"%s\" stat error: %s",
            process->name, auxp->prog, strerror(errno));
        return(-1);
    }
    if (!S_ISREG(st.st_mode)) {
        write_notify_msg(process, LOG_WARNING,
            "Console [%s] connection failed: \"%s\" not a regular file",
            process->name, auxp->prog);
        return(-1);
    }
    if (access(auxp->argv[0], X_OK) < 0) {
        write_notify_msg(process, LOG_WARNING,
            "Console [%s] connection failed: \"%s\" not executable",
            process->name, auxp->prog);
        return(-1);
    }
    return(0);
}


static void reset_process_delay(obj_t *process)
{
/*  Resets the process obj's reconnect-delay after the connection has been up
 *    for the minimum length of time.  This protects against spinning on
 *    reconnects where the process immediately terminates.
 */
    process_obj_t *auxp;

    assert(process != NULL);
    assert(is_process_obj(process));

    auxp = &(process->aux.process);

    /*  Reset the timer ID since this routine is only invoked by a timer.
     */
    auxp->timer = -1;

    DPRINTF((15, "Reset [%s] reconnect delay\n", process->name));
    auxp->delay = 0;
    return;
}
