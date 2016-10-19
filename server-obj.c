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

#if HAVE_IPMICONSOLE_H
#  include <ipmiconsole.h>
#endif /* HAVE_IPMICONSOLE_H */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>
#include "common.h"
#include "inevent.h"
#include "list.h"
#include "log.h"
#include "server.h"
#include "tpoll.h"
#include "util-file.h"
#include "util-str.h"
#include "util.h"
#include "wrapper.h"

extern tpoll_t tp_global;               /* defined in server.c */


static char * sanitize_file_string(char *str);
static char * find_trailing_int_str(char *str);
#ifndef NDEBUG
static int validate_obj_links(obj_t *obj);
#endif /* !NDEBUG */
static int num_bytes_buffered(obj_t *obj);


obj_t * create_obj(
    server_conf_t *conf, char *name, int fd, enum obj_type type)
{
/*  Creates an object of the specified (type) opened on (fd).
 */
    obj_t *obj;

    assert(conf != NULL);
    assert(name != NULL);

    if (!(obj = malloc(sizeof(obj_t))))
        out_of_memory();
    obj->name = create_string(name);
    obj->fd = fd;
    obj->bufInPtr = obj->bufOutPtr = obj->buf;
    x_pthread_mutex_init(&obj->bufLock, NULL);
    obj->readers = list_create(NULL);
    obj->writers = list_create(NULL);
    if ((type == 0) || (type >= CONMAN_OBJ_LAST_ENTRY)) {
        log_err(0, "INTERNAL: Unrecognized object [%s] type=%d", name, type);
    }
    obj->type = type;
    obj->gotBufWrap = 0;
    obj->gotEOF = 0;
    /*
     *  resetCmdRef, resetCmdPid, and resetCmdTimer only apply to console objs.
     *  But the code is simplified if they are placed in the base obj.
     */
    obj->resetCmdRef = NULL;
    obj->resetCmdPid = 0;
    obj->resetCmdTimer = 0;

    DPRINTF((10, "Created object [%s].\n", obj->name));
    return(obj);
}


obj_t * create_client_obj(server_conf_t *conf, req_t *req)
{
/*  Creates a new client object and adds it to the master objs list.
 *    Note: the socket is open and set for non-blocking I/O.
 *  Returns the new object.
 */
    char name[MAX_LINE];
    obj_t *client;

    assert(conf != NULL);
    assert(req != NULL);
    assert(req->sd >= 0);
    assert((req->user != NULL) && (req->user[0] != '\0'));
    assert((req->host != NULL) && (req->host[0] != '\0'));

    set_fd_nonblocking(req->sd);
    set_fd_closed_on_exec(req->sd);
    tpoll_set(tp_global, req->sd, POLLIN);

    snprintf(name, sizeof(name), "%s@%s:%d", req->user, req->host, req->port);
    name[sizeof(name) - 1] = '\0';
    client = create_obj(conf, name, req->sd, CONMAN_OBJ_CLIENT);
    client->aux.client.req = req;
    time(&client->aux.client.timeLastRead);
    if (client->aux.client.timeLastRead == (time_t) -1)
        log_err(errno, "time() failed");
    client->aux.client.gotEscape = 0;
    client->aux.client.gotSuspend = 0;

    /*  Add obj to the master conf->objs list.
     */
    list_append(conf->objs, client);

    DPRINTF((9, "Opened client: fd=%d user=%s tty=%s host=%s port=%d.\n",
        req->sd, req->user, req->tty, req->host, req->port));
    return(client);
}


void destroy_obj(obj_t *obj)
{
/*  Destroys the object, closing the fd and freeing resources as needed.
 *  This routine should only be called via the obj's list destructor, thereby
 *    ensuring it will be removed from the master objs list before destruction.
 */
    int n;
    char **pp;

    assert(obj != NULL);
    DPRINTF((10, "Destroying object [%s].\n", obj->name));

    n = num_bytes_buffered(obj);
    if (n > 0) {
        log_msg(LOG_WARNING,
            "Destroying [%s] with %d byte%s of unwritten data",
            obj->name, n, (n == 1 ? "" : "s"));
    }

    switch(obj->type) {
    case CONMAN_OBJ_CLIENT:
        if (obj->aux.client.req) {
            req_t *req = obj->aux.client.req;
            log_msg(LOG_INFO, "Client <%s@%s:%d> disconnected",
                req->user, req->fqdn, req->port);
            req->sd = -1;       /* prevent destroy_req from also closing sd */
            destroy_req(req);
            obj->aux.client.req = NULL;
        }
        break;
    case CONMAN_OBJ_LOGFILE:
        if (obj->aux.logfile.fmtName) {
            free(obj->aux.logfile.fmtName);
        }
        break;
    case CONMAN_OBJ_PROCESS:
        for (pp = obj->aux.process.argv; *pp != NULL; pp++) {
            free(*pp);
        }
        /*  Do not destroy obj->aux.process.logfile since it is only a ref.
         */
        break;
    case CONMAN_OBJ_SERIAL:
        /*
         *  According to the UNIX Programming FAQ v1.37
         *    <http://www.faqs.org/faqs/unix-faq/programmer/faq/>
         *    (Section 3.6: How to Handle a Serial Port or Modem),
         *    if there is any pending output waiting to be written
         *    to the device (eg, if output flow is stopped by h/w
         *    or s/w handshaking), the process can hang _unkillably_
         *    in the close() call until the output drains.
         *    Play it safe and discard any pending output.
         */
        if (obj->fd >= 0) {
            set_tty_mode(&obj->aux.serial.tty, obj->fd);
            if (tcflush(obj->fd, TCIOFLUSH) < 0)
                log_msg(LOG_INFO,
                    "Unable to flush tty device for console [%s]", obj->name);
        }
        if (obj->aux.serial.dev) {
            free(obj->aux.serial.dev);
        }
        /*  Do not destroy obj->aux.serial.logfile since it is only a ref.
         */
        break;
    case CONMAN_OBJ_TELNET:
        if (obj->aux.telnet.host) {
            free(obj->aux.telnet.host);
        }
        /*  Do not destroy obj->aux.telnet.logfile since it is only a ref.
         */
        break;
    case CONMAN_OBJ_UNIXSOCK:
        if (obj->aux.unixsock.dev) {
            (void) inevent_remove(obj->aux.unixsock.dev);
            free(obj->aux.unixsock.dev);
        }
        /*  Do not destroy obj->aux.unixsock.logfile since it is only a ref.
         */
        break;
#if WITH_FREEIPMI
    case CONMAN_OBJ_IPMI:
        if (obj->aux.ipmi.host) {
            free(obj->aux.ipmi.host);
        }
        if (obj->aux.ipmi.ctx) {
            ipmiconsole_ctx_destroy(obj->aux.ipmi.ctx);
        }
        x_pthread_mutex_destroy(&obj->aux.ipmi.mutex);
        break;
#endif /* WITH_FREEIPMI */
    case CONMAN_OBJ_TEST:
        break;
    default:
        log_err(0, "INTERNAL: Unrecognized object [%s] type=%d",
            obj->name, obj->type);
        break;
    }

    x_pthread_mutex_destroy(&obj->bufLock);
    if (obj->readers) {
        list_destroy(obj->readers);
    }
    if (obj->writers) {
        list_destroy(obj->writers);
    }
    if (obj->fd >= 0) {
        tpoll_clear(tp_global, obj->fd, POLLIN | POLLOUT);
        if (close(obj->fd) < 0) {
            log_msg(LOG_WARNING, "Unable to close [%s] during destruction: %s",
                obj->name, strerror(errno));
        }
        obj->fd = -1;
    }
    if (obj->name) {
        free(obj->name);
    }
    free(obj);
    return;
}


void reopen_obj(obj_t *obj)
{
    assert(obj != NULL);

    if (is_logfile_obj(obj)) {
        open_logfile_obj(obj);
    }
    else if (is_process_obj(obj)) {
        open_process_obj(obj);
    }
    else if (is_serial_obj(obj)) {
        open_serial_obj(obj);
    }
    else if (is_telnet_obj(obj)) {
        open_telnet_obj(obj);
    }
    else if (is_unixsock_obj(obj)) {
        open_unixsock_obj(obj);
    }
#if WITH_FREEIPMI
    else if (is_ipmi_obj(obj)) {
        open_ipmi_obj(obj);
    }
#endif /* WITH_FREEIPMI */
    else if (is_client_obj(obj)) {
        ; /* no-op */
    }
    else if (is_test_obj(obj)) {
        open_test_obj(obj);
    }
    else {
        log_err(0, "INTERNAL: Cannot re-open unrecognized object [%s] type=%d",
            obj->name, obj->type);
    }
    return;
}


int format_obj_string(char *buf, int buflen, obj_t *obj, const char *fmt)
{
/*  Prints the format string (fmt) based on object (obj)
 *    into the buffer (buf) of length (buflen).
 *  Returns the number of characters written into (buf) on success,
 *    or -1 if (buf) was of insufficient length.
 */
    const char *psrc;
    char *pdst;
    int n, m;
    time_t t;
    struct tm tm;
    char *p, *q;
    char c;
    char tfmt[3] = "%%";
    char * const pspec = &tfmt[1];

    assert (buf != NULL);
    assert (fmt != NULL);

    if ((buf == NULL) || (fmt == NULL)) {
        return(-1);
    }
    psrc = fmt;
    pdst = buf;
    n = buflen;

    t = 0;
    get_localtime(&t, &tm);

    while (((c = *psrc++) != '\0') && (n > 0)) {
        if ((c != '%') || ((*pspec = *psrc++) == '\0')) {
            if (--n > 0) {
                *pdst++ = c;
            }
        }
        else {
            switch (*pspec) {
            case 'N':                   /* console name */
                if (!obj)
                    goto ignore_specifier;
                if (is_console_obj(obj)) {
                    p = obj->name;
                    m = strlen(p);
                    if ((n -= m) > 0) {
                        strcpy(pdst, p);
                        sanitize_file_string(pdst);
                        pdst += m;
                    }
                }
                break;
            case 'D':                   /* console device */
                if (!obj)
                    goto ignore_specifier;
                if (is_serial_obj(obj) || is_unixsock_obj(obj)) {
                    q = obj->aux.serial.dev;
                    p = (p = strrchr(q, '/')) ? p + 1 : q;
                    m = strlen(p);
                    if ((n -= m) > 0) {
                        strcpy(pdst, p);
                        sanitize_file_string(pdst);
                        pdst += m;
                    }
                }
                else if (is_telnet_obj(obj)) {
                    assert(n > 0);
                    m = snprintf (pdst, n, "%s:%d",
                        obj->aux.telnet.host, obj->aux.telnet.port);
                    if ((m < 0) || (m >= n))
                        n = 0;
                    else {
                        sanitize_file_string(pdst);
                        n -= m;
                        pdst += m;
                    }
                }
                break;
            case 'P':                   /* daemon's pid */
                assert(n > 0);
                m = snprintf (pdst, n, "%d", (int) getpid());
                if ((m < 0) || (m >= n))
                    n = 0;
                else {
                    n -= m;
                    pdst += m;
                }
                break;
            case 'Y':                   /* year with century (0000-9999) */
                /* fall-thru */
            case 'y':                   /* year without century (00-99) */
                /* fall-thru */
            case 'm':                   /* month (01-12) */
                /* fall-thru */
            case 'd':                   /* day of month (01-31) */
                /* fall-thru */
            case 'H':                   /* hour (00-23) */
                /* fall-thru */
            case 'M':                   /* minute (00-59) */
                /* fall-thru */
            case 'S':                   /* second (00-61) */
                /* fall-thru */
            case 's':                   /* seconds since unix epoch */
                assert(n > 0);
                if (!(m = strftime (pdst, n, tfmt, &tm)))
                    n = 0;
                else {
                    n -= m;
                    pdst += m;
                }
                break;
            case '%':                   /* literal '%' character */
                if (--n > 0) {
                    *pdst++ = *pspec;
                }
                break;
ignore_specifier:
            default:                    /* ignore conversion specifier */
                if ((n -= 2) > 0) {
                    *pdst++ = '%';
                    *pdst++ = *pspec;
                }
                break;
            }
        }
    }
    assert (pdst < (buf + buflen));
    *pdst = '\0';
    return((n <= 0) ? -1 : pdst - buf);
}


static char * sanitize_file_string(char *str)
{
/*  Replaces non-printable characters in the string (str) with underscores.
 *  Returns the original (potentially modified) string (str).
 */
    char *p;

    if (str) {
        for (p=str; *p; p++) {
            if (!isgraph((int) *p) || (*p == '/'))
                *p = '_';
        }
    }
    return(str);
}


int compare_objs(obj_t *obj1, obj_t *obj2)
{
/*  Used by list_sort() to compare the name of (obj1) to that of (obj2).
 *  Objects are sorted by their name in ascending ASCII order, with the
 *    exception that if both objects match up to the point of a trailing
 *    integer string then they will be sorted numerically according to
 *    this integer (eg, foo1 < foo2 < foo10).
 *  Returns less-than-zero if (obj1 < obj2), zero if (obj1 == obj2),
 *    and greater-than-zero if (obj1 > obj2).
 */
    char *str1, *str2;
    char *int1, *int2;

    assert(obj1 != NULL);
    assert(obj2 != NULL);
    assert(obj1->name != NULL);
    assert(obj2->name != NULL);

    str1 = obj1->name;
    str2 = obj2->name;
    int1 = find_trailing_int_str(str1);
    int2 = find_trailing_int_str(str2);

    while (*str1) {
        if ((str1 == int1) && (str2 == int2))
            return(atoi(int1) - atoi(int2));
        else if (*str1 == *str2)
            str1++, str2++;
        else
            break;
    }
    return(*str1 - *str2);
}


static char * find_trailing_int_str(char *str)
{
/*  Searches string 'str' for a trailing integer.
 *  Returns a ptr to the start of the integer; o/w, returns NULL.
 */
    char *p, *q;

    for (p=str, q=NULL; p && *p; p++) {
        if (!isdigit((int) *p))
            q = NULL;
        else if (!q)
            q = p;
    }
    return(q);
}


int find_obj(obj_t *obj, obj_t *key)
{
/*  Used by list_find_first() and list_delete_all() to locate
 *    the object specified by (key) within the list.
 *  Returns non-zero if (obj == key); o/w returns zero.
 */
    assert(obj != NULL);
    assert(key != NULL);

    return(obj == key);
}


int write_notify_msg(obj_t *console, int priority, char *fmt, ...)
{
/*  Writes a notification message to the daemon logfile and all attached
 *    readers & writers of (console).
 */
    char     buf[MAX_LINE];
    char    *p;
    int      len;
    int      n;
    va_list  vargs;

    assert(console != NULL);
    assert(is_console_obj(console));
    assert(fmt != NULL);

    if (!console || !is_console_obj(console) || !fmt) {
        errno = EINVAL;
        return(-1);
    }

    p = buf;
    len = sizeof(buf);
    n = snprintf(p, len, "%s", CONMAN_MSG_PREFIX);
    if ((n < 0) || (n >= len)) {
        len = 0;
    }
    else {
        p += n;
        len -= n;
    }

    if (len > 0) {
        va_start(vargs, fmt);
        n = vsnprintf(p, len, fmt, vargs);
        va_end(vargs);
        log_msg(priority, "%s", p);
        if ((n < 0) || (n >= len)) {
            len = 0;
        }
        else {
            p += n;
            len -= n;
        }
    }

    assert(len >= 0);
    if ((size_t) len < strlen(CONMAN_MSG_SUFFIX) + 1) {
        p = buf + sizeof(buf) - strlen(CONMAN_MSG_SUFFIX) - 1;
        if (p < buf)
            p = buf;
        len = buf + sizeof(buf) - p;
        log_msg(LOG_WARNING, "Truncated notify message");
    }
    (void) snprintf(p, len, "%s", CONMAN_MSG_SUFFIX);

    notify_console_objs(console, buf);
    return(0);
}


void notify_console_objs(obj_t *console, char *msg)
{
/*  Notifies all readers & writers of (console) with the informational (msg).
 *  If an obj is both a reader and a writer, it will only be notified once.
 */
    ListIterator i;
    obj_t *obj;

    assert(is_console_obj(console));

    if (!msg || !strlen(msg)) {
        return;
    }
    i = list_iterator_create(console->readers);
    while ((obj = list_next(i))) {
        write_obj_data(obj, msg, strlen(msg), 1);
    }
    list_iterator_destroy(i);

    i = list_iterator_create(console->writers);
    while ((obj = list_next(i))) {
        if (!list_find_first(console->readers, (ListFindF) find_obj, obj)) {
            write_obj_data(obj, msg, strlen(msg), 1);
        }
    }
    list_iterator_destroy(i);
    return;
}


void link_objs(obj_t *src, obj_t *dst)
{
/*  Creates a link so data read from (src) is written to (dst).
 */
    int gotBcast;
    int gotStolen;
    char *now;
    char *tty;
    char buf[MAX_LINE];
    ListIterator i;
    obj_t *writer;

    if (is_client_obj(src) && is_console_obj(dst)) {

        gotBcast = src->aux.client.req->enableBroadcast;
        gotStolen = src->aux.client.req->enableForce
            && !list_is_empty(dst->writers);
        now = create_short_time_string(0);

        /*  Notify existing console readers and writers
         *    regarding the "writable" client's arrival.
         */
        tty = src->aux.client.req->tty;
        snprintf(buf, sizeof(buf),
            "%sConsole [%s] %s%s by <%s@%s>%s%s at %s%s",
            CONMAN_MSG_PREFIX, dst->name,
            (gotStolen ? "stolen" : "joined"), (gotBcast ? " for B/C" : ""),
            src->aux.client.req->user, src->aux.client.req->host,
            (tty ? " on " : ""), (tty ? tty : ""), now, CONMAN_MSG_SUFFIX);
        strcpy(&buf[sizeof(buf) - 3], "\r\n");
        notify_console_objs(dst, buf);

        /*  Write msg(s) to new client regarding existing console writer(s).
         */
        i = list_iterator_create(dst->writers);
        while ((writer = list_next(i))) {
            assert(is_client_obj(writer));
            tty = writer->aux.client.req->tty;
            snprintf(buf, sizeof(buf),
                "%sConsole [%s] %s <%s@%s>%s%s at %s%s",
                CONMAN_MSG_PREFIX, dst->name,
                (gotStolen ? "stolen from" : "joined with"),
                writer->aux.client.req->user, writer->aux.client.req->host,
                (tty ? " on " : ""), (tty ? tty : ""), now, CONMAN_MSG_SUFFIX);
            strcpy(&buf[sizeof(buf) - 3], "\r\n");
            write_obj_data(src, buf, strlen(buf), 1);
        }
        list_iterator_destroy(i);

        /*  If the client is forcing the console session,
         *    disconnect existing clients with write-privileges.
         */
        if (gotStolen) {
            i = list_iterator_create(dst->writers);
            while ((writer = list_next(i))) {
                assert(is_client_obj(writer));
                unlink_obj(writer);
            }
            list_iterator_destroy(i);
        }

        free(now);
    }

    /*  Create link from src reads to dst writes.
     */
    assert(!list_find_first(src->readers, (ListFindF) find_obj, dst));
    list_append(src->readers, dst);
    assert(!list_find_first(dst->writers, (ListFindF) find_obj, src));
    list_append(dst->writers, src);

    DPRINTF((10, "Linked [%s] reads to [%s] writes.\n", src->name, dst->name));
    assert(validate_obj_links(src) >= 0);
    assert(validate_obj_links(dst) >= 0);
    return;
}


void unlink_objs(obj_t *src, obj_t *dst)
{
/*  Destroys the link allowing data read from (src) to be written to (dst)
 *    (ie, the link from src readers to dst writers).
 */
    int n;
    char *now;
    char *tty;
    char buf[MAX_LINE];

    if (list_delete_all(src->readers, (ListFindF) find_obj, dst)) {
        DPRINTF((10, "Removing [%s] from [%s] readers.\n",
            dst->name, src->name));
    }
    if ((n = list_delete_all(dst->writers, (ListFindF) find_obj, src))) {
        DPRINTF((10, "Removing [%s] from [%s] writers.\n",
            src->name, dst->name));
    }
    /*  If a "writable" client is being unlinked from a console ...
     */
    if ((n > 0) && is_client_obj(src) && is_console_obj(dst)) {

        /*  Notify existing console readers and writers
         *    regarding the "writable" client's departure.
         */
        now = create_short_time_string(0);
        tty = src->aux.client.req->tty;
        snprintf(buf, sizeof(buf),
            "%sConsole [%s] departed by <%s@%s>%s%s at %s%s",
            CONMAN_MSG_PREFIX, dst->name,
            src->aux.client.req->user, src->aux.client.req->host,
            (tty ? " on " : ""), (tty ? tty : ""), now, CONMAN_MSG_SUFFIX);
        free(now);
        strcpy(&buf[sizeof(buf) - 3], "\r\n");
        notify_console_objs(dst, buf);
    }

    /*  If a client obj has become completely unlinked, set its EOF flag.
     *    This will prevent new data from being added to the obj's buffer,
     *    and the obj will be closed once its buffer is empty.
     */
    if (is_client_obj(src)
            && list_is_empty(src->readers) && list_is_empty(src->writers)) {
        assert(is_console_obj(dst));
        src->gotEOF = 1;
    }
    else if (is_client_obj(dst)
            && list_is_empty(dst->readers) && list_is_empty(dst->writers)) {
        assert(is_console_obj(src));
        dst->gotEOF = 1;
    }

    DPRINTF((10, "Unlinked [%s] reads from [%s] writes.\n",
        src->name, dst->name));
    assert(validate_obj_links(src) >= 0);
    assert(validate_obj_links(dst) >= 0);
    return;
}


void unlink_obj(obj_t *obj)
{
/*  Destroys all links between (obj) and its readers & writers.
 */
    obj_t *x;

    while ((x = list_peek(obj->writers))) {
        unlink_objs(x, obj);
    }
    while ((x = list_peek(obj->readers))) {
        unlink_objs(obj, x);
    }
    return;
}


#ifndef NDEBUG
static int validate_obj_links(obj_t *obj)
{
/*  Validates the readers and writers lists are successfully linked
 *    to other objects.
 *  Returns 0 if the links are good; o/w, returns -1.
 */
    ListIterator i;
    obj_t *reader;
    obj_t *writer;
    int gotError = 0;

    assert (obj != NULL);

    i = list_iterator_create(obj->readers);
    while ((reader = list_next(i))) {
        if (!list_find_first(reader->writers, (ListFindF) find_obj, obj)) {
            DPRINTF((1, "[%s] writes not linked to [%s] reads.\n",
                obj->name, reader->name));
            gotError = 1;
        }
    }
    list_iterator_destroy(i);

    i = list_iterator_create(obj->writers);
    while ((writer = list_next(i))) {
        if (!list_find_first(writer->readers, (ListFindF) find_obj, obj)) {
            DPRINTF((1, "[%s] reads not linked to [%s] writes.\n",
                obj->name, writer->name));
            gotError = 1;
        }
    }
    list_iterator_destroy(i);

    return(gotError ? -1 : 0);
}
#endif /* !NDEBUG */


int shutdown_obj(obj_t *obj)
{
/*  Shuts down (and potentially re-opens) the specified obj.
 *  Returns -1 if the obj is ready to be removed from the master objs list
 *    and destroyed; o/w, returns 0.
 */
    int n;

    assert(obj != NULL);

    DPRINTF((20, "Entered shutdown_obj: [%s]\n", obj->name));

    /*  An inactive obj should not be destroyed.
     */
    if (obj->fd < 0) {
        log_msg(LOG_INFO, "Attempted to shutdown [%s] when fd=%d",
            obj->name, obj->fd);
        return(0);
    }
    /*  Close the existing connection.
     */
    tpoll_clear(tp_global, obj->fd, POLLIN | POLLOUT);
    if (close(obj->fd) < 0) {
        log_msg(LOG_WARNING, "Unable to close [%s] during shutdown: %s",
            obj->name, strerror(errno));
    }
    obj->fd = -1;
    /*
     *  FIXME:  The connection state should ideally be marked as DOWN here if
     *    applicable (eg, telnet & unixsock), perhaps via a close_foo_obj().
     *    However, reopen_obj() below calls open_foo_obj() which checks the
     *    state and calls disconnect_foo_obj() if the state was UP.  This
     *    should be refactored since it's confusing.
     */

    /*  Flush the obj's buffer.
     */
    x_pthread_mutex_lock(&obj->bufLock);
    n = num_bytes_buffered(obj);
    obj->bufInPtr = obj->bufOutPtr = obj->buf;
    obj->gotEOF = 0;
    x_pthread_mutex_unlock(&obj->bufLock);
    if (n > 0) {
        log_msg(LOG_WARNING,
            "Flushed %d byte%s of unwritten data from [%s]",
            n, (n == 1 ? "" : "s"), obj->name);
    }
    /*  Prepare this obj for destruction by unlinking it from all others.
     *    It will be removed from the master objs list by mux_io(),
     *    and the objs list destructor will destroy the obj.
     */
    if (is_client_obj(obj)) {
        unlink_obj(obj);
        return(-1);
    }
    /*  If a logfile obj is shut down, close the file but retain the obj;
     *    an attempt to reopen it will be made if the daemon is reconfigured.
     */
    if (is_logfile_obj(obj)) {
        return(0);
    }
    /*  If a console obj is shut down, close the existing connection
     *    and re-open a new one.
     */
    if (is_console_obj(obj)) {
        reopen_obj(obj);
        return(0);
    }
    log_err(0, "Unable to shutdown unrecognized object [%s] type=%d",
        obj->name, obj->type);
    return(-1);
}


int read_from_obj(obj_t *obj)
{
/*  Reads data from the obj's file descriptor and writes it out
 *    to the circular-buffer of each obj in its "readers" list.
 *  Returns the number of bytes read (>=0 on success),
 *    or -1 if the obj is ready to be destroyed.
 *
 *  An obj's circular-buffer is empty when (bufInPtr == bufOutPtr).
 *    Thus, it can hold at most (OBJ_BUF_SIZE - 1) bytes of data.
 *  But if the obj is a logfile, its data can grow as a result of the
 *    additional processing.  This routine's internal buffer is reduced
 *    somewhat to reduce the likelihood of log data being dropped.
 */
    unsigned char buf[(OBJ_BUF_SIZE / 2) - 1];
    int n;
    int isEmpty;
    ListIterator i;
    obj_t *reader;

    DPRINTF((20, "Entered read_from_obj: [%s]\n", obj->name));

    if (obj->fd < 0) {
        return(0);
    }
    /*  Do not read from a telnet obj that is not yet in the UP state.
     *  When the non-blocking connect completes, the fd becomes writable;
     *    when connection establishment fails, it becomes readable & writable.
     *  The completion of a PENDING connection is handled in write_to_obj().
     */
    if (is_telnet_obj(obj) && (obj->aux.telnet.state != CONMAN_TELNET_UP)) {
        return(0);
    }
again:
    if ((n = read(obj->fd, buf, sizeof(buf))) < 0) {
        if (errno == EINTR) {
            goto again;
        }
        if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
            return(0);
        }
        log_msg(LOG_INFO, "Unable to read from [%s]: %s",
            obj->name, strerror(errno));
        return(shutdown_obj(obj));
    }
    else if (n == 0) {
        DPRINTF((15, "Read EOF from [%s].\n", obj->name));
        if (obj->gotEOF) {
            log_msg(LOG_WARNING, "Read EOF from [%s] after gotEOF", obj->name);
        }
        obj->gotEOF = 1;
        tpoll_clear(tp_global, obj->fd, POLLIN);
        isEmpty = (obj->bufInPtr == obj->bufOutPtr);
        return(isEmpty ? shutdown_obj(obj) : 0);
    }
    else {
        DPRINTF((15, "Read %d bytes from [%s].\n", n, obj->name));
        if (is_client_obj(obj)) {
            x_pthread_mutex_lock(&obj->bufLock);
            time(&obj->aux.client.timeLastRead);
            if (obj->aux.client.timeLastRead == (time_t) -1) {
                log_err(errno, "time() failed");
            }
            x_pthread_mutex_unlock(&obj->bufLock);
            n = process_client_escapes(obj, buf, n);
        }
        else if (is_telnet_obj(obj)) {
            n = process_telnet_escapes(obj, buf, n);
        }
        /*  Ensure the buffer still contains data
         *    after the escape characters have been processed.
         */
        if (n > 0) {
            i = list_iterator_create(obj->readers);
            while ((reader = list_next(i))) {

                if (is_logfile_obj(reader)) {
                    write_log_data(reader, buf, n);
                }
                else {
                    write_obj_data(reader, buf, n, 0);
                }
            }
            list_iterator_destroy(i);
        }
    }
    return(n);
}


int write_obj_data(obj_t *obj, const void *src, int len, int isInfo)
{
/*  Writes the buffer (src) of length (len) into the object's (obj)
 *    circular-buffer.  If (isInfo) is true, the data is considered
 *    an informational message which a client may suppress.
 *  Returns the number of bytes written.
 *
 *  Note that this routine can write at most (OBJ_BUF_SIZE - 1) bytes
 *    of data into the object's circular-buffer.
 */
    int avail;
    int n, m;

    DPRINTF((20, "Entered write_obj_data: [%s]\n", obj->name));

    if (!src || len <= 0) {
        return(0);
    }
    /*  If the obj's gotEOF flag is set,
     *    no more data can be written into its buffer.
     */
    if (obj->gotEOF) {
        log_msg(LOG_INFO, "Attempted to write %d byte%s to [%s] after EOF",
            len, (len == 1 ? "" : "s"), obj->name);
        return(0);
    }
    /*  An obj's circular-buffer is empty when (bufInPtr == bufOutPtr).
     *    Thus, it can hold at most (OBJ_BUF_SIZE - 1) bytes of data.
     */
    if (len >= OBJ_BUF_SIZE) {
        len = OBJ_BUF_SIZE - 1;
    }
    x_pthread_mutex_lock(&obj->bufLock);

    /*  Do nothing if this is an informational message
     *    and the client has requested not to be bothered.
     */
    if (isInfo && is_client_obj(obj) && obj->aux.client.req->enableQuiet) {
        x_pthread_mutex_unlock(&obj->bufLock);
        return(0);
    }
    /*  Assert the buffer's input and output ptrs are valid upon entry.
     */
    assert(obj->bufInPtr >= obj->buf);
    assert(obj->bufInPtr < &obj->buf[OBJ_BUF_SIZE]);
    assert(obj->bufOutPtr >= obj->buf);
    assert(obj->bufOutPtr < &obj->buf[OBJ_BUF_SIZE]);

    n = len;

    /*  Calculate the number of bytes available before data is overwritten.
     *  Data in the circular-buffer will be overwritten if needed since
     *    this routine must not block.
     *  Since an obj's circular-buffer is empty when (bufInPtr == bufOutPtr),
     *    subtract one byte to account for this sentinel.
     */
    avail = OBJ_BUF_SIZE - 1 - num_bytes_buffered(obj);

    /*  Copy first chunk of data (ie, up to the end of the buffer).
     */
    m = MIN(len, &obj->buf[OBJ_BUF_SIZE] - obj->bufInPtr);
    if (m > 0) {
        memcpy(obj->bufInPtr, src, m);
        n -= m;
        src = (unsigned char *) src + m;
        obj->bufInPtr += m;
        /*
         *  Do the hokey-pokey and perform a circular-buffer wrap-around.
         */
        if (obj->bufInPtr == &obj->buf[OBJ_BUF_SIZE]) {
            obj->bufInPtr = obj->buf;
            obj->gotBufWrap = 1;
        }
    }
    /*  Copy second chunk of data (ie, from the beginning of the buffer).
     */
    if (n > 0) {
        memcpy(obj->bufInPtr, src, n);
        obj->bufInPtr += n;             /* Hokey-Pokey not needed here */
    }
    /*  Check to see if any data in circular-buffer was overwritten.
     */
    if (len > avail) {
        if (!is_client_obj(obj) || !obj->aux.client.gotSuspend) {
            log_msg(LOG_NOTICE, "Overwrote %d bytes for \"%s\"",
                len - avail, obj->name);
        }
        obj->bufOutPtr = obj->bufInPtr + 1;
        if (obj->bufOutPtr == &obj->buf[OBJ_BUF_SIZE]) {
            obj->bufOutPtr = obj->buf;
        }
    }
    /*  Notify tpoll that data is available for writing
     *    unless it is a client obj that is currently suspended.
     */
    if (!is_client_obj(obj) || !obj->aux.client.gotSuspend) {
        tpoll_set(tp_global, obj->fd, POLLOUT);
    }
    /*  Assert the buffer's input and output ptrs are valid upon exit.
     */
    assert(obj->bufInPtr >= obj->buf);
    assert(obj->bufInPtr < &obj->buf[OBJ_BUF_SIZE]);
    assert(obj->bufOutPtr >= obj->buf);
    assert(obj->bufOutPtr < &obj->buf[OBJ_BUF_SIZE]);

    x_pthread_mutex_unlock(&obj->bufLock);

    /*  If an informational message has been added to the log,
     *    re-initialize the console log's newline state.
     */
    if (isInfo && is_logfile_obj(obj)) {
        obj->aux.logfile.lineState = CONMAN_LOG_LINE_INIT;
    }
    return(len);
}


int write_to_obj(obj_t *obj)
{
/*  Writes data from the obj's circular-buffer out to its file descriptor.
 *  Returns 0 on success, or -1 if the obj is ready to be destroyed.
 */
    struct iovec iov[2];
    int iovcnt = 0;
    int isDead = 0;
    int n;

    DPRINTF((20, "Entered write_to_obj: [%s]\n", obj->name));

    if (obj->fd < 0) {
        return(0);
    }
    /*  The completion of a nonblocking connect() makes the socket writable,
     *    so complete the telnet obj non-blocking connect here if needed.
     */
    if (is_telnet_obj(obj) && (obj->aux.telnet.state == CONMAN_TELNET_PENDING))
    {
        open_telnet_obj(obj);
        return(0);
    }
    x_pthread_mutex_lock(&obj->bufLock);

    /*  Assert the buffer's input and output ptrs are valid upon entry.
     */
    assert(obj->bufInPtr >= obj->buf);
    assert(obj->bufInPtr < &obj->buf[OBJ_BUF_SIZE]);
    assert(obj->bufOutPtr >= obj->buf);
    assert(obj->bufOutPtr < &obj->buf[OBJ_BUF_SIZE]);

    /*  IOV for object buffer cases OIO (wrap-around pt1) & IO (no-wrap).
     */
    if (obj->bufOutPtr > obj->bufInPtr) {
        iov[0].iov_base = obj->bufOutPtr;
        iov[0].iov_len = &obj->buf[OBJ_BUF_SIZE] - obj->bufOutPtr;
        iovcnt = 1;
        /*
         *  IOV for object buffer case OIO (wrap-around pt2).
         */
        if (obj->bufInPtr > obj->buf) {
            iov[1].iov_base = obj->buf;
            iov[1].iov_len = obj->bufInPtr - obj->buf;
            iovcnt = 2;
        }
    }
    /*  IOV for object buffer cases IOI & OI.
     */
    else if (obj->bufInPtr > obj->bufOutPtr) {
        iov[0].iov_base = obj->bufOutPtr;
        iov[0].iov_len = obj->bufInPtr - obj->bufOutPtr;
        iovcnt = 1;
    }

    if (iovcnt > 0) {
again:
        n = writev(obj->fd, iov, iovcnt);
        if (n < 0) {
            if (errno == EINTR) {
                goto again;
            }
            if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
                /*
                 *  If an error occurs while writing to the obj's fd,
                 *    trigger a shutdown of the obj by setting 'isDead'.
                 */
                log_msg(LOG_INFO, "Unable to write to [%s]: %s",
                    obj->name, strerror(errno));
                isDead = 1;
            }
        }
        else if (n > 0) {
            DPRINTF((15, "Wrote %d bytes to [%s].\n", n, obj->name));
            obj->bufOutPtr += n;
            if (obj->bufOutPtr >= &obj->buf[OBJ_BUF_SIZE]) {
                obj->bufOutPtr -= OBJ_BUF_SIZE;
            }
        }
    }
    /*  If all buffered data has been written out to the fd...
     */
    if (obj->bufInPtr == obj->bufOutPtr) {
        /*
         *  If the gotEOF flag is set, no additional data can be written into
         *    the buffer.  As such, the object is ready for shutdown.
         */
        if (obj->gotEOF) {
            isDead = 1;
        }
        /*  Notify tpoll that all available data has been written.
         */
        tpoll_clear(tp_global, obj->fd, POLLOUT);
    }
    /*  Assert the buffer's input and output ptrs are valid upon exit.
     */
    assert(obj->bufInPtr >= obj->buf);
    assert(obj->bufInPtr < &obj->buf[OBJ_BUF_SIZE]);
    assert(obj->bufOutPtr >= obj->buf);
    assert(obj->bufOutPtr < &obj->buf[OBJ_BUF_SIZE]);

    x_pthread_mutex_unlock(&obj->bufLock);

    return(isDead ? shutdown_obj(obj) : 0);
}


static int num_bytes_buffered(obj_t *obj)
{
/*  Returns the number of bytes of buffered data in 'obj' waiting to be
 *    written out to the file descriptor.
 */
    int n;

    assert(obj != NULL);

    if (obj->bufInPtr >= obj->bufOutPtr) {
        n = obj->bufInPtr - obj->bufOutPtr;
    }
    else {
        n = (&obj->buf[OBJ_BUF_SIZE] - obj->bufOutPtr) +
            (obj->bufInPtr - obj->buf);
    }
    return(n);
}
