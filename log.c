/*****************************************************************************\
 *  $Id: log.c,v 1.1 2002/05/08 00:10:54 dun Exp $
 *****************************************************************************
 *  Copyright (C) 2001-2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *  UCRL-CODE-2002-009.
 *  
 *  This file is part of ConMan, a remote console management program.
 *  For details, see <http://www.llnl.gov/linux/conman.html>.
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
 *****************************************************************************
 *  Refer to "log.h" for documentation on public functions.
\*****************************************************************************/


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include "log.h"


#ifndef MAX_LINE
#  define MAX_LINE 1024
#endif /* !MAX_LINE */


static FILE * log_file = NULL;
static int    log_file_priority = 0;
static int    log_syslog = 0;


static void log_aux(int priority, int errnum,
    const char *format, va_list vargs);

static const char * log_prefix(int priority);


void dprintf(int level, const char *format, ...)
{
    static int debug_level = -1;
    va_list vargs;
    char *p;
    int i = 0;

    if (debug_level < 0) {
        if ((p = getenv("DEBUG")))
            i = atoi(p);
        debug_level = (i > 0) ? i : 0;
    }
    if ((level > 0) && (level <= debug_level)) {
        va_start(vargs, format);
        vfprintf(stderr, format, vargs);
        va_end(vargs);
    }
    return;
}


void log_set_file(FILE *fp, int priority)
{
    if (fp && !ferror(fp)) {
        log_file_priority = priority;
        log_file = fp;
        setvbuf(fp, NULL, _IONBF, 0);   /* set stream unbuffered */
    }
    else {
        log_file = NULL;
    }
    return;
}


void log_set_syslog(char *ident)
{
    char *p;

    if ((p = strrchr(ident, '/')))
        ident = p + 1;

    if (ident) {
        openlog(ident, LOG_NDELAY | LOG_PID, LOG_DAEMON);
        log_syslog = 1;
    }
    else {
        closelog();
        log_syslog = 0;
    }
    return;
}


void log_err(int errnum, const char *format, ...)
{
    va_list vargs;

    va_start(vargs, format);
    log_aux(LOG_ERR, errnum, format, vargs);
    va_end(vargs);

#ifndef NDEBUG
    abort();                            /* generate core for debugging */
#endif /* !NDEBUG */
    exit(1);
}


void log_msg(int priority, const char *format, ...)
{
    va_list vargs;

    va_start(vargs, format);
    log_aux(priority, 0, format, vargs);
    va_end(vargs);

    return;
}


static void log_aux(int priority, int errnum,
    const char *format, va_list vargs)
{
    const char *prefix;
    char buf[MAX_LINE];
    char *sbuf;
    char *p;
    int len;
    int n;

    p = sbuf = buf;
    len = sizeof(buf) - 1;              /* reserve char for terminating '\n' */

    if ((prefix = log_prefix(priority))) {
        n = snprintf(p, len, "%s: ", prefix);
        if ((n < 0) || (n >= len))
            n = len - 1;
        sbuf = p += n;
        len -= n;
    }

    n = vsnprintf(p, len, format, vargs);
    if ((n < 0) || (n >= len))
        n = len - 1;
    p += n;
    len -= n;

    if (format[strlen(format) - 1] != '\n') {
        if ((errnum > 0) && (len > 0)) {
            n = snprintf(p, len, ": %s", strerror(errnum));
            if ((n < 0) || (n >= len))
                n = len - 1;
            p += n;
            len -= n;
        }
        strcat(p, "\n");
    }

    if (log_syslog)
        syslog(priority, "%s", sbuf);
    if (log_file && (priority <= log_file_priority)) {
        fprintf(log_file, "%s", buf);
    }
    return;
}


static const char * log_prefix(int priority)
{
    switch (priority) {
    case LOG_EMERG:
        return("EMERGENCY");
    case LOG_ALERT:
        return("ALERT");
    case LOG_CRIT:
        return("CRITICAL");
    case LOG_ERR:
        return("ERROR");
    case LOG_WARNING:
        return("WARNING");
    case LOG_NOTICE:
        return("NOTICE");
    case LOG_INFO:
        return("INFO");
    case LOG_DEBUG:
        return("DEBUG");
    default:
        return("UNKNOWN");
    }
}
