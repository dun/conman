/*****************************************************************************\
 *  $Id: server-logfile.c,v 1.13 2002/09/18 20:32:17 dun Exp $
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

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "fd.h"
#include "log.h"
#include "server.h"
#include "str.h"


int parse_logfile_opts(
    logopt_t *opts, const char *str, char *errbuf, int errlen)
{
/*  Parses 'str' for logfile device options 'opts'.
 *    The 'opts' struct should be initialized to a default value.
 *    The 'str' string is of the form "(sanitize|nosanitize)".
 *  Returns 0 and updates the 'opts' struct on success; o/w, returns -1
 *    (writing an error message into 'errbuf' if defined).
 */
    logopt_t optsTmp;

    assert(opts != NULL);

    /*  By setting the tmp opts to the 'opts' that are passed in,
     *    we establish defaults for any values that are not changed by 'str'.
     */
    optsTmp = *opts;

    if ((str == NULL) || str[0] == '\0') {
        if ((errbuf != NULL) && (errlen > 0))
            snprintf(errbuf, errlen, "encountered empty options string");
        return(-1);
    }

    if (!strcasecmp(str, "sanitize"))
        optsTmp.enableSanitize = 1;
    else if (!strcasecmp(str, "nosanitize"))
        optsTmp.enableSanitize = 0;
    else {
        if ((errbuf != NULL) && (errlen > 0))
            snprintf(errbuf, errlen, "expected 'SANITIZE' or 'NOSANITIZE'");
        return(-1);
    }

    *opts = optsTmp;
    return(0);
}


int open_logfile_obj(obj_t *logfile, int gotTrunc)
{
/*  (Re)opens the specified 'logfile' obj; the logfile will be truncated
 *    if 'gotTrunc' is true (ie, non-zero).
 *  Since this logfile can be re-opened after the daemon has chdir()'d,
 *    it must be specified with an absolute pathname.
 *  Returns 0 if the logfile is successfully opened; o/w, returns -1.
 */
    int flags;
    char *now, *msg;

    assert(logfile != NULL);
    assert(is_logfile_obj(logfile));
    assert(logfile->name != NULL);
    assert(logfile->name[0] == '/');
    assert(logfile->aux.logfile.consoleName != NULL);

    if (logfile->fd >= 0) {
        if (close(logfile->fd) < 0)     /* log err and continue */
            log_msg(LOG_WARNING, "Unable to close logfile \"%s\": %s",
                logfile->name, strerror(errno));
        logfile->fd = -1;
    }

    flags = O_WRONLY | O_CREAT | O_APPEND | O_NONBLOCK;
    if (gotTrunc)
        flags |= O_TRUNC;
    if ((logfile->fd = open(logfile->name, flags, S_IRUSR | S_IWUSR)) < 0) {
        log_msg(LOG_WARNING, "Unable to open logfile \"%s\": %s",
            logfile->name, strerror(errno));
        return(-1);
    }
    if (fd_get_write_lock(logfile->fd) < 0) {
        log_msg(LOG_WARNING, "Unable to lock \"%s\"", logfile->name);
        close(logfile->fd);             /* ignore err on close() */
        logfile->fd = -1;
        return(-1);
    }
    fd_set_nonblocking(logfile->fd);    /* redundant, just playing it safe */
    fd_set_close_on_exec(logfile->fd);

    now = str_get_time_long(0);
    msg = str_create_fmt("%sConsole [%s] log opened at %s%s",
        CONMAN_MSG_PREFIX, logfile->aux.logfile.consoleName, now,
        CONMAN_MSG_SUFFIX);
    write_obj_data(logfile, msg, strlen(msg), 0);
    free(msg);

    DPRINTF((10, "Opened %slogfile \"%s\" for console [%s].\n",
        (logfile->aux.logfile.opts.enableSanitize ? "SANE " : ""),
        logfile->name, logfile->aux.logfile.consoleName));
    return(0);
}


obj_t * get_console_logfile_obj(obj_t *console)
{
/*  Returns a ptr to the logfile obj associated with 'console'
 *    if one exists and is currently active; o/w, returns NULL.
 */
    obj_t *logfile = NULL;

    assert(console != NULL);
    assert(is_console_obj(console));

    if (is_serial_obj(console))
        logfile = console->aux.serial.logfile;
    else if (is_telnet_obj(console))
        logfile = console->aux.telnet.logfile;
    else
        log_err(0, "INTERNAL: Unrecognized console [%s] type=%d",
            console->name, console->type);

    if (!logfile || (logfile->fd < 0))
        return(NULL);
    return(logfile);
}


int write_sanitized_log_data(obj_t *log, const void *src, int len)
{
/*  Writes a sanitized version of the buffer (src) of length (len) into
 *    the logfile obj (log); the original (src) buffer is not modified.
 *    This strips the data to 7-bit ASCII and causes control characters
 *    to be displayed as two-character printable sequences.
 *  Returns the number of bytes written into the logfile obj's buffer.
 *
 *  Note that this routine can sanitize at most ((MAX_BUF_SIZE - 1) / 2)
 *    bytes of data since the worst-case scenario causes results in a
 *    twofold expansion.
 */
    unsigned char buf[MAX_BUF_SIZE - 1];
    const unsigned char *p;
    unsigned char *q;
    int c;

    assert(is_logfile_obj(log));
    assert(log->aux.logfile.opts.enableSanitize);

    /*  An obj's circular-buffer is empty when (bufInPtr == bufOutPtr).
     *    Thus, it can hold at most (MAX_BUF_SIZE - 1) bytes of data.
     */
    if (len > sizeof(buf) / 2)
        len = sizeof(buf) / 2;

    DPRINTF((15, "Sanitizing %d bytes for [%s] log.\n",
        len, log->aux.logfile.consoleName));

    for (p=src, q=buf; len>0; p++, len--) {

        /*  A "sanitize" state machine is used to properly sanitize CR/LF line
         *    terminations.  This is responsible for coalescing multiple CRs,
         *    swapping LF/CR to CR/LF, prepending a CR to a lonely LF, and
         *    appending a LF to a lonely CR to prevent characters from being
         *    overwritten.
         */
        if (*p == '\r') {
            if (log->aux.logfile.sanitizeState == CONMAN_LOG_SANE_INIT)
                log->aux.logfile.sanitizeState = CONMAN_LOG_SANE_CR;
        }
        else if (*p == '\n') {
            log->aux.logfile.sanitizeState = CONMAN_LOG_SANE_LF;
            *q++ = '\r';
            *q++ = '\n';
        }
        else {
            if (log->aux.logfile.sanitizeState == CONMAN_LOG_SANE_CR) {
                *q++ = '\r';
                *q++ = '\n';
            }
            log->aux.logfile.sanitizeState = CONMAN_LOG_SANE_INIT;

            c = *p & 0x7F;              /* strip data to 7-bit ASCII */

            if (c < 0x20) {             /* ASCII ctrl-chars */
                *q++ = '^';
                *q++ = c + '@';
            }
            else if (c == 0x7F) {       /* ASCII DEL char */
                *q++ = '^';
                *q++ = c + '?';
            }
            else {
                *q++ = c;
            }
        }
    }

    assert((q >= buf) && (q <= buf + sizeof(buf)));
    return(write_obj_data(log, buf, q - buf, 0));
}
