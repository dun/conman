/*****************************************************************************
 *  $Id$
 *****************************************************************************
 *  This file is part of ConMan: The Console Manager.
 *  For details, see <http://www.llnl.gov/linux/conman/>.
 *
 *  Copyright (C) 2001-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *  UCRL-CODE-2002-009.
 *
 *  This is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 *****************************************************************************/


#if HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include "log.h"


/*****************************************************************************
 *  Internal Constants
 *****************************************************************************/

#define LOG_BUFFER_MAXLEN       1024
#define LOG_TRUNCATION_SUFFIX   "+"


/*****************************************************************************
 *  Internal Data Types
 *****************************************************************************/

struct log_ctx {
    FILE *file_ptr;                     /* set if logging msgs to i/o stream */
    char *file_id;                      /* id string prepended to messages   */
    int   file_options;                 /* bitmask of LOG_OPT_* options      */
    int   file_priority;                /* min priority level from syslog(3) */
    int   syslog;                       /* true if logging msgs to syslog    */
};


/*****************************************************************************
 *  Internal Function Prototypes
 *****************************************************************************/

static void _log_aux (int priority, const char *format, va_list vargs);

static void _log_aux_add_timestamp (char **pbuf, int *pbuflen);

static void _log_aux_add_identity (char **pbuf, int *pbuflen, char *identity);

static void _log_aux_add_priority (char **pbuf, int *pbuflen, int priority);

static void _log_aux_add_message (char **pbuf, int *pbuflen, const char *fmt,
    va_list vargs);

static void _log_aux_add_truncation (char **pbuf, char *buf0, int buf0len,
    char *suffix);


/*****************************************************************************
 *  Internal Variables
 *****************************************************************************/

static struct log_ctx log_ctx;


/*****************************************************************************
 *  External Functions
 *****************************************************************************/

int
log_open_file (FILE *fp, const char *identity, int options, int priority)
{
/*  Enables the logging of messages to file.
 *  The [fp] parameter specifies an open I/O stream where messages will be
 *    logged (eg, stderr).
 *  If an [identity] string is specified, the basename (ie, filename component)
 *    of this string will be prepended to messages.
 *  The [priority] level specifies the minimum level of message importance.
 *    It is one of the eight syslog(3) message levels.  Messages with a lower
 *    importance (ie, higher priority level) will not be logged to file.
 *  The [options] parameter is a bitwise-OR of any "LOG_OPT_" defines to
 *    control the formatting of the message.
 *  Returns 0 on success, or -1 on error (with logging to file disabled).
 */
    char *p;

    log_ctx.file_ptr = NULL;
    if (log_ctx.file_id) {
        free (log_ctx.file_id);
        log_ctx.file_id = NULL;
    }
    if (!fp) {
        return (-1);
    }
    if (ferror (fp)) {
        return (-1);
    }
    if (setvbuf (fp, NULL, _IONBF, 0) != 0) {
        return (-1);
    }
    if (priority < 0) {
        return (-1);
    }
    log_ctx.file_ptr = fp;
    log_ctx.file_options = options;
    log_ctx.file_priority = priority;

    if (identity) {
        if ((p = strrchr (identity, '/'))) {
            identity = p + 1;
        }
        if (*identity != '\0') {
            log_ctx.file_id = strdup (identity);
        }
    }
    return (0);
}


int
log_open_syslog (const char *identity, int options, int facility)
{
/*  Enables the logging of messages to syslog.
 *  If an [identity] string is specified, the basename (ie, filename component)
 *    of this string will be prepended to every message.  The default value is
 *    the program name.  If NULL, the current value is retained.
 *  The [options] parameter is a bitwise-OR of syslog(3) options.
 *  The [facility] parameter is a syslog(3) facility to specify the type of
 *    program generating the message.
 *  Returns 0 on success, or -1 on error.
 */
    char *p;

    if (identity) {
        if ((p = strrchr (identity, '/'))) {
            identity = p + 1;
        }
    }
    openlog (identity, options, facility);
    log_ctx.syslog = 1;
    return (0);
}


void
log_close_file (void)
{
/*  Disables the logging of messages to file.
 *  This routine DOES NOT close the file pointer passed to log_open_file().
 */
    if (log_ctx.file_ptr) {
        (void) fflush (log_ctx.file_ptr);
        log_ctx.file_ptr = NULL;
    }
    if (log_ctx.file_id) {
        free (log_ctx.file_id);
        log_ctx.file_id = NULL;
    }
    return;
}


void
log_close_syslog (void)
{
/*  Disables the logging of messages to syslog.
 */
    if (log_ctx.syslog) {
        closelog ();
        log_ctx.syslog = 0;
    }
    return;
}


void
log_close_all (void)
{
/*  Disables the logging of messages to all destinations.
 */
    log_close_file ();
    log_close_syslog ();
    return;
}


void
log_msg (int priority, const char *format, ...)
{
/*  Logs a message at the specified [priority] level according to the
 *    printf-style [format] string.
 *  The [priority] level determines the importance of the message.
 *    It is one of the eight syslog(3) message levels.
 *  Messages can be concurrently logged to both a file stream and syslog.
 */
    va_list vargs;

    va_start (vargs, format);
    _log_aux (priority, format, vargs);
    va_end (vargs);
}


/*****************************************************************************
 *  Internal Functions
 *****************************************************************************/

static void
_log_aux (int priority, const char *format, va_list vargs)
{
    char  buf[LOG_BUFFER_MAXLEN];       /* message buffer                    */
    char *p;                            /* current position in msg buf       */
    char *sbuf;                         /* syslog portion of msg buf         */
    int   len;                          /* remaining buf len including NUL   */

    if (!log_ctx.file_ptr && !log_ctx.syslog) {
        return;
    }
    if (!format) {
        return;
    }
    p = buf;
    sbuf = NULL;
    len = sizeof (buf) - 1;             /* reserve space for trailing LF */

    if (priority < LOG_EMERG) {
        priority = LOG_EMERG;
    }
    else if (priority > LOG_DEBUG) {
        priority = LOG_DEBUG;
    }

    if ((log_ctx.file_options & LOG_OPT_TIMESTAMP) && (len > 0)) {
        _log_aux_add_timestamp (&p, &len);
    }
    if ((log_ctx.file_id) && (len > 0)) {
        _log_aux_add_identity (&p, &len, log_ctx.file_id);
    }
    if ((log_ctx.file_options & LOG_OPT_PRIORITY) && (len > 0)) {
        _log_aux_add_priority (&p, &len, priority);
    }
    if (len > 0) {
        sbuf = p;
        _log_aux_add_message (&p, &len, format, vargs);
    }
    if (len <= 0) {
        _log_aux_add_truncation (&p, buf, sizeof (buf), LOG_TRUNCATION_SUFFIX);
    }
    *p++ = '\n';
    *p = '\0';
    assert (p < buf + sizeof (buf));

    if (log_ctx.syslog && sbuf) {
        syslog (priority, "%s", sbuf);
    }
    if (log_ctx.file_ptr && (priority <= log_ctx.file_priority)) {
        if (fprintf (log_ctx.file_ptr, "%s", buf) < 0) {
            if (!log_ctx.syslog) {
                openlog (NULL, LOG_PID, LOG_USER);
            }
            syslog (LOG_CRIT, "Logging to file stopped due to error");
            if (!log_ctx.syslog) {
                closelog ();
            }
            log_ctx.file_ptr = NULL;
        }
    }
    return;
}


static void
_log_aux_add_timestamp (char **pbuf, int *pbuflen)
{
    char   *buf;
    int     len;
    time_t  t;
    int     n;

    assert (log_ctx.file_options & LOG_OPT_TIMESTAMP);
    assert ((pbuf != NULL) && (*pbuf != NULL));
    assert ((pbuflen != NULL) && (*pbuflen > 0));

    buf = *pbuf;
    len = *pbuflen;

    if (time (&t) != ((time_t) -1)) {

#if HAVE_LOCALTIME_R
        struct tm  tm;
        struct tm *tm_ptr = localtime_r (&t, &tm);
#else  /* !HAVE_LOCALTIME_R */
        struct tm *tm_ptr = localtime (&t);
#endif /* !HAVE_LOCALTIME_R */

        if (tm_ptr) {
            n = strftime (buf, len, "%Y-%m-%d %H:%M:%S ", tm_ptr);
            if ((n == 0) || (n >= len)) {
                /*
                 *  Do not update [buf] here since its contents are
                 *    undefined upon strftime() error.
                 */
                len = 0;
            }
            else {
                buf += n;
                len -= n;
            }
        }
    }
    *pbuf = buf;
    *pbuflen = len;
    return;
}


static void
_log_aux_add_identity (char **pbuf, int *pbuflen, char *identity)
{
    char *buf;
    int   len;
    int   n;

    assert ((pbuf != NULL) && (*pbuf != NULL));
    assert ((pbuflen != NULL) && (*pbuflen > 0));
    assert ((identity != NULL) && (*identity != '\0'));

    buf = *pbuf;
    len = *pbuflen;

    if (log_ctx.file_options & LOG_OPT_PID) {
        n = snprintf (buf, len, "%s[%d]: ", identity, getpid ());
    }
    else {
        n = snprintf (buf, len, "%s: ", identity);
    }
    if ((n < 0) || (n >= len)) {
        buf += len - 1;
        len = 0;
    }
    else {
        buf += n;
        len -= n;
    }
    *pbuf = buf;
    *pbuflen = len;
    return;
}


static void
_log_aux_add_priority (char **pbuf, int *pbuflen, int priority)
{
    char *buf;
    int   len;
    int   n;
    int   num_spaces;
    int   pri_maxlen = 9;               /* "emergency" is the longest string */
    static const char *pri_strs[] = {
        "emergency", "alert", "critical", "error",
        "warning", "notice", "info", "debug"
    };

    assert (log_ctx.file_options & LOG_OPT_PRIORITY);
    assert ((pbuf != NULL) && (*pbuf != NULL));
    assert ((pbuflen != NULL) && (*pbuflen > 0));
    assert ((priority >= LOG_EMERG) || (priority <= LOG_DEBUG));

    buf = *pbuf;
    len = *pbuflen;

    num_spaces = 1;
    if (log_ctx.file_options & LOG_OPT_JUSTIFY) {
        num_spaces += pri_maxlen - strlen (pri_strs[priority]);
    }
    assert (num_spaces > 0);

    n = snprintf (buf, len, "%s:%*c",
        pri_strs[priority], num_spaces, 0x20);
    if ((n < 0) || (n >= len)) {
        buf += len - 1;
        len = 0;
    }
    else {
        buf += n;
        len -= n;
    }
    *pbuf = buf;
    *pbuflen = len;
    return;
}


static void
_log_aux_add_message (char **pbuf, int *pbuflen, const char *fmt,
    va_list vargs)
{
    char *buf;
    char *buf0;
    int   len;
    int   n;
    char *p;

    assert ((pbuf != NULL) && (*pbuf != NULL));
    assert ((pbuflen != NULL) && (*pbuflen > 0));
    assert (fmt != NULL);

    buf = buf0 = *pbuf;
    len = *pbuflen;

    n = vsnprintf (buf, len, fmt, vargs);
    if ((n < 0) || (n >= len)) {
        buf += len - 1;
        len = 0;
    }
    else {
        buf += n;
        len -= n;
    }
    p = buf - 1;
    while ((p >= buf0) && (isspace (*p))) {
        *p-- = '\0';
    }
    *pbuf = p + 1;
    *pbuflen = (len > 0) ? len + (buf - (p + 1)) : 0;
    return;
}


static void
_log_aux_add_truncation (char **pbuf, char *buf0, int buf0len, char *suffix)
{
    char *buf;
    int   n;
    char *p;

    assert ((pbuf != NULL) && (*pbuf != NULL));
    assert (buf0 != NULL);
    assert (buf0len > 0);
    assert ((*pbuf >= buf0) && (*pbuf < buf0 + buf0len));
    assert (suffix != NULL);

    buf = *pbuf;
    n = strlen (suffix);
    p = buf0 + buf0len - n - 1 - 1;     /* reserve space for LF & NUL */
    buf = (p < buf) ? p : buf;
    if (buf >= buf0) {
        (void) strcpy (buf, suffix);
        buf += n;
    }
    *pbuf = buf;
    return;
}
