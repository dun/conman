/*****************************************************************************\
 *  $Id$
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
#include "common.h"
#include "log.h"
#include "server.h"
#include "util-file.h"
#include "util-str.h"


int parse_logfile_opts(logopt_t *opts, const char *str,
    const char *conf, int line, char *errbuf, int errlen)
{
/*  Parses 'str' for logfile device options 'opts'.
 *    The 'opts' struct should be initialized to a default value.
 *    The 'str' string is of the form "(sanitize|nosanitize)".
 *  Returns 0 and updates the 'opts' struct on success; o/w, returns -1
 *    (writing an error message into 'errbuf' if defined).
 */
    logopt_t optsTmp;
    char buf[MAX_LINE];
    const char * const separators = " \t\n.,;";
    char *tok;

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
    /*  Since strtok() modifies its string, we need to create a working copy.
     */
    if (strlcpy(buf, str, sizeof(buf)) >= sizeof(buf)) {
        if ((errbuf != NULL) && (errlen > 0))
            snprintf(errbuf, errlen, "logopt string exceeded buffer size");
        return(-1);
    }
    /*  Parse the string.
     */
    tok = strtok(buf, separators);
    while (tok != NULL) {
        if (!strcasecmp(tok, "sanitize"))
            optsTmp.enableSanitize = 1;
        else if (!strcasecmp(tok, "nosanitize"))
            optsTmp.enableSanitize = 0;
        else if (!strcasecmp(tok, "timestamp"))
            optsTmp.enableTimestamp = 1;
        else if (!strcasecmp(tok, "notimestamp"))
            optsTmp.enableTimestamp = 0;
        else {
            log_msg(LOG_ERR,
                "CONFIG[%s:%d]: ignoring unrecognized token '%s'",
                conf, line, tok);
        }
        tok = strtok(NULL, separators);
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
    assert(logfile->aux.logfile.console != NULL);
    assert(logfile->aux.logfile.console->name != NULL);

    if (logfile->fd >= 0) {
        if (close(logfile->fd) < 0)     /* log err and continue */
            log_msg(LOG_WARNING, "Unable to close logfile \"%s\": %s",
                logfile->name, strerror(errno));
        logfile->fd = -1;
    }
    if (logfile->aux.logfile.fmtName) {

        char buf[MAX_LINE];

        if (format_obj_string(buf, sizeof(buf),
          logfile->aux.logfile.console,
          logfile->aux.logfile.fmtName) < 0) {
            log_msg(LOG_WARNING,
                "Unable to open logfile for [%s]: filename exceeded buffer",
                logfile->aux.logfile.console->name);
            logfile->fd = -1;
            return(-1);
        }
        free(logfile->name);
        logfile->name = create_string(buf);
    }
    flags = O_WRONLY | O_CREAT | O_APPEND | O_NONBLOCK;
    if (gotTrunc)
        flags |= O_TRUNC;
    if ((logfile->fd = open(logfile->name, flags, S_IRUSR | S_IWUSR)) < 0) {
        log_msg(LOG_WARNING, "Unable to open logfile \"%s\": %s",
            logfile->name, strerror(errno));
        return(-1);
    }
    if (get_write_lock(logfile->fd) < 0) {
        log_msg(LOG_WARNING, "Unable to lock \"%s\"", logfile->name);
        close(logfile->fd);             /* ignore err on close() */
        logfile->fd = -1;
        return(-1);
    }
    set_fd_nonblocking(logfile->fd);    /* redundant, just playing it safe */
    set_fd_closed_on_exec(logfile->fd);

    now = create_long_time_string(0);
    msg = create_format_string("%sConsole [%s] log opened at %s%s",
        CONMAN_MSG_PREFIX, logfile->aux.logfile.console->name, now,
        CONMAN_MSG_SUFFIX);
    write_obj_data(logfile, msg, strlen(msg), 0);
    free(now);
    free(msg);
    /*
     *  Since the above console log message is not marked "informational",
     *    the test in write_obj_data() to re-init the line state will not
     *    be triggered.  Thusly, we re-initialize the line state here.
     */
    logfile->aux.logfile.lineState = CONMAN_LOG_LINE_INIT;

    DPRINTF((10, "Opened %s%slogfile \"%s\" for console [%s].\n",
        (logfile->aux.logfile.opts.enableSanitize ? "SANITIZED " : ""),
        (logfile->aux.logfile.opts.enableTimestamp ? "TIMESTAMPED " : ""),
        logfile->name, logfile->aux.logfile.console->name));
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
    assert(is_logfile_obj(logfile));
    return(logfile);
}


int write_log_data(obj_t *log, const void *src, int len)
{
/*  Writes a potentially modified version of the buffer (src) of length (len)
 *    into the logfile obj (log); the original (src) buffer is not modified.
 *  This routine is intended for writing data that was read from a console,
 *    not informational messages.
 *  If sanitized logs are enabled, data is stripped to 7-bit ASCII and
 *    control/binary characters are displayed as two-character printable
 *    sequences.
 *  If newline timestamping is enabled, the current timestamp is appended
 *    after each newline.
 *  Returns the number of bytes written into the logfile obj's buffer.
 */
    const int minbuf = 25;              /* cr/lf + timestamp + meta/char */
    unsigned char buf[MAX_BUF_SIZE - 1];
    const unsigned char *p;
    unsigned char *q;
    const unsigned char * const qLast = buf + sizeof(buf);
    int n = 0;

    assert(is_logfile_obj(log));
    assert(sizeof(buf) >= minbuf);

    /*  If no additional processing is needed, listen to Biff Tannen:
     *    "make like a tree and get outta here".
     */
    if (!log->aux.logfile.gotProcessing) {
        return(write_obj_data(log, src, len, 0));
    }
    DPRINTF((15, "Processing %d bytes for [%s] log \"%s\".\n",
        len, log->aux.logfile.console->name, log->name));

    for (p=src, q=buf; len>0; p++, len--) {
        /*
         *  A newline state machine is used to properly sanitize CR/LF line
         *    terminations.  This is responsible for coalescing multiple CRs,
         *    swapping LF/CR to CR/LF, transcribing CR/NUL to CR/LF,
         *    prepending a CR to a lonely LF, and appending a LF to a
         *    lonely CR to prevent characters from being overwritten.
         */
        if (*p == '\r') {
            if (log->aux.logfile.lineState == CONMAN_LOG_LINE_DATA) {
                log->aux.logfile.lineState = CONMAN_LOG_LINE_CR;
            }
            else if (log->aux.logfile.lineState == CONMAN_LOG_LINE_INIT) {
                if (log->aux.logfile.opts.enableTimestamp)
                    q += write_time_string(0, (char *) q, qLast - q);
                log->aux.logfile.lineState = CONMAN_LOG_LINE_CR;
            }
            else {
                ; /* ignore */
            }
        }
        else if (*p == '\n') {
            if (  (log->aux.logfile.lineState == CONMAN_LOG_LINE_INIT)
               || (log->aux.logfile.lineState == CONMAN_LOG_LINE_LF) ) {
                if (log->aux.logfile.opts.enableTimestamp)
                    q += write_time_string(0, (char *) q, qLast - q);
            }
            *q++ = '\r';
            *q++ = '\n';
            log->aux.logfile.lineState = CONMAN_LOG_LINE_LF;
        }
        else if (  (*p == '\0')
                && (  (log->aux.logfile.lineState == CONMAN_LOG_LINE_CR)
                   || (log->aux.logfile.lineState == CONMAN_LOG_LINE_LF) ) ) {
            ; /* ignore */
        }
        else {
            if (log->aux.logfile.lineState == CONMAN_LOG_LINE_CR) {
                *q++ = '\r';
                *q++ = '\n';
            }
            if (log->aux.logfile.lineState != CONMAN_LOG_LINE_DATA) {
                if (log->aux.logfile.opts.enableTimestamp)
                    q += write_time_string(0, (char *) q, qLast - q);
            }
            log->aux.logfile.lineState = CONMAN_LOG_LINE_DATA;

            if (log->aux.logfile.opts.enableSanitize) {

                int c = *p & 0x7F;      /* strip data to 7-bit ASCII */

                if (c < 0x20) {         /* ASCII ctrl-chars */
                    *q++ = (*p & 0x80) ? '~' : '^';
                    *q++ = c + '@';
                }
                else if (c == 0x7F) {   /* ASCII DEL char */
                    *q++ = (*p & 0x80) ? '~' : '^';
                    *q++ = '?';
                }
                else {
                    if (*p & 0x80)
                        *q++ = '`';
                    *q++ = c;
                }
            }
            else {
                *q++ = *p;
            }
        }
        /*  Flush internal buffer before it overruns.
         */
        if ((qLast - q) < minbuf) {
            assert((q >= buf) && (q <= qLast));
            n += write_obj_data(log, buf, q - buf, 0);
            q = buf;
        }
    }
    assert((q >= buf) && (q <= qLast));
    n += write_obj_data(log, buf, q - buf, 0);
    return(n);
}
