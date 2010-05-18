/*****************************************************************************
 *  $Id$
 *****************************************************************************
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2001-2007 The Regents of the University of California.
 *  UCRL-CODE-2002-009.
 *
 *  This file is part of ConMan: The Console Manager.
 *  For details, see <http://conman.googlecode.com/>.
 *
 *  ConMan is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
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


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "list.h"
#include "log.h"
#include "server.h"
#include "tpoll.h"
#include "util-file.h"
#include "util-str.h"


extern tpoll_t tp_global;               /* defined in server.c */


obj_t * create_process_obj(server_conf_t *conf, char *name, List args,
    char *errbuf, int errlen)
{
/*  Creates a new process device object and adds it to the master objs list.
 *  Note: an external process will later be fork/exec'd based on the argv
 *    by main:open_objs:reopen_obj:open_process_obj().
 *  Returns the new object, or NULL on error.
 */
    ListIterator  i;
    obj_t        *process;
    int           num_args;
    int           n;
    char         *arg;

    assert(conf != NULL);
    assert((name != NULL) && (name[0] != '\0'));
    assert((args != NULL) && (list_count(args) > 0));

    /*  Check for duplicate console names.
     */
    i = list_iterator_create(conf->objs);
    while ((process = list_next(i))) {
        if (is_console_obj(process) && !strcmp(process->name, name)) {
            snprintf(errbuf, errlen,
                "console [%s] specifies duplicate console name", name);
            break;
        }
    }
    list_iterator_destroy(i);
    if (process != NULL) {
        return(NULL);
    }
    process = create_obj(conf, name, -1, CONMAN_OBJ_PROCESS);
    process->aux.process.count = 0;
    process->aux.process.timer = -1;
    process->aux.process.pid = -1;
    process->aux.process.tStart = 0;
    process->aux.process.logfile = NULL;
    num_args = list_count(args);
    process->aux.process.argv = calloc(num_args + 1, sizeof(char *));
    if (!process->aux.process.argv) {
        out_of_memory();
    }
    for (n = 0; (n < num_args) && (arg = list_pop(args)); n++) {
        process->aux.process.argv[n] = arg;
    }
    process->aux.process.argv[n] = (char *) NULL;
    if ((arg = strrchr(process->aux.process.argv[0], '/'))) {
        process->aux.process.prog = arg + 1;
    }
    else {
        process->aux.process.prog = process->aux.process.argv[0];
    }
    /*  Add obj to the master conf->objs list.
     */
    list_append(conf->objs, process);

    return(process);
}


int open_process_obj(obj_t *process)
{
/*  (Re)opens the specified 'process' obj.
 *  Returns 0 if the child process is successfully (re)created;
 *    o/w, returns -1.
 */
    time_t   tNow;
    int      n;
    char    *delta;
    int      fdPair[2] = {-1,-1};
    pid_t    pid;

    assert(process != NULL);
    assert(is_process_obj(process));

    if (process->aux.process.timer >= 0) {
        (void) tpoll_timeout_cancel(tp_global, process->aux.process.timer);
        process->aux.process.timer = -1;
    }
    if (process->fd >= 0) {
        (void) close(process->fd);
        process->fd = -1;
    }
    if (process->aux.process.tStart > 0) {
        if (time(&tNow) == (time_t) -1) {
            log_err(errno, "Unable to get current time");
        }
        n = tNow - process->aux.process.tStart;
        delta = create_time_delta_string(process->aux.process.tStart, tNow);
        process->aux.process.tStart = 0;

        if (process->aux.process.pid >= 0) {
            write_notify_msg(process, LOG_INFO,
                "Console [%s] disconnected from \"%s\" (pid %d) after %s",
                process->name, process->aux.process.prog,
                process->aux.process.pid, delta);
            (void) kill(process->aux.process.pid, SIGKILL);
            process->aux.process.pid = -1;
        }
        free(delta);

        if (n < PROCESS_MIN_TIMEOUT) {
            if (process->aux.process.count < PROCESS_MAX_COUNT) {
                process->aux.process.timer = tpoll_timeout_relative(
                    tp_global, (callback_f) open_process_obj, process,
                    PROCESS_MIN_TIMEOUT * process->aux.process.count * 1000);
            }
            else {
                write_notify_msg(process, LOG_WARNING,
                    "Console [%s] disabled after %d failed attempt%s",
                    process->name, process->aux.process.count,
                    (process->aux.process.count == 1 ? "" : "s"));
                process->aux.process.count = 0;
            }
            goto err;
        }
        else {
            process->aux.process.count = 0;
        }
    }
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fdPair) < 0) {
        write_notify_msg(process, LOG_WARNING,
            "Console [%s] disabled due to process socketpair failure: %s",
            process->name, strerror(errno));
        goto err;
    }
    set_fd_nonblocking(fdPair[0]);
    set_fd_nonblocking(fdPair[1]);
    set_fd_closed_on_exec(fdPair[0]);
    set_fd_closed_on_exec(fdPair[1]);

    if ((pid = fork()) < 0) {
        write_notify_msg(process, LOG_WARNING,
            "Console [%s] disabled due to process fork failure: %s",
            process->name, strerror(errno));
        goto err;
    }
    else if (pid == 0) {
        if (close(fdPair[0]) < 0) {
            log_err(errno, "close() of child fdPair failed");
        }
        if (dup2(fdPair[1], STDIN_FILENO) < 0) {
            log_err(errno, "dup2() of child stdin failed");
        }
        if (dup2(fdPair[1], STDOUT_FILENO) < 0) {
            log_err(errno, "dup2() of child stdout failed");
        }
        if (dup2(fdPair[1], STDERR_FILENO) < 0) {
            log_err(errno, "dup2() of child stderr failed");
        }
        execv(process->aux.process.argv[0], process->aux.process.argv);
        _exit(127);
    }
    if (close(fdPair[1]) < 0) {
        log_err(errno, "close() of parent fdPair failed");
    }
    if (time(&(process->aux.process.tStart)) == (time_t) -1) {
        log_err(errno, "time() failed");
    }
    process->aux.process.count++;
    process->aux.process.pid = pid;
    process->fd = fdPair[0];
    process->gotEOF = 0;

    write_notify_msg(process, LOG_INFO,
        "Console [%s] connected to \"%s\" (pid %d)",
        process->name, process->aux.process.prog, process->aux.process.pid);
    DPRINTF((9, "Opened [%s] process: fd=%d/%d prog=\"%s\" pid=%d.\n",
        process->name, fdPair[0], fdPair[1], process->aux.process.argv[0],
        process->aux.process.pid));
    return(0);

err:
    if (fdPair[0] >= 0) {
        (void) close(fdPair[0]);
    }
    if (fdPair[1] >= 0) {
        (void) close(fdPair[1]);
    }
    return(-1);
}
