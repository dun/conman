/******************************************************************************\
 *  $Id: server-obj.c,v 1.12 2001/06/07 17:01:31 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
\******************************************************************************/


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include "common.h"
#include "errors.h"
#include "list.h"
#include "server.h"
#include "util.h"


static obj_t * create_obj(enum obj_type type, List objs, char *name, int fd);
static void set_raw_console_mode(obj_t *obj);
static void restore_console_mode(obj_t *obj);
static void unlink_objs_helper(obj_t *src, obj_t *dst);
static int process_escape_chars(obj_t *client, void *src, int len);
static void perform_serial_break(obj_t *client);


obj_t * create_console_obj(List objs, char *name, char *dev,
    char *log, char *rst, int bps)
{
/*  Creates a new console object and adds it to the master (objs) list.
 *    Note: the console is open and set for non-blocking I/O.
 *  Returns the new object, or NULL on error.
 */
    int fd;
    obj_t *obj;

    assert(objs);
    assert(name && *name);
    assert(dev && *dev);

    /* FIX_ME: check name, dev, & log for dopplegangers */
    /* FIX_ME: check if rst program exists */
    /* FIX_ME: config file needs directive to specify execute dir for rst */
    /* FIX_ME: check bps for valid baud rate (cf APUE p343-344) */

    if ((fd = open(dev, O_RDWR | O_NONBLOCK)) < 0) {
        log_msg(0, "Unable to open console %s: %s", name, strerror(errno));
        return(NULL);
    }
    if (!isatty(fd)) {
        log_msg(0, "Console %s is not a TTY device", name);
        if (close(fd) < 0)
            err_msg(errno, "close(%d) failed", fd);
        return(NULL);
    }
    obj = create_obj(CONSOLE, objs, name, fd);
    obj->aux.console.dev = create_string(dev);
    if (log && *log)
        obj->aux.console.log = create_string(log);
    if (rst && *rst)
        obj->aux.console.rst = create_string(rst);
    obj->aux.console.bps = bps;
    set_raw_console_mode(obj);

    return(obj);
}


obj_t * create_logfile_obj(List objs, char *logfile, char *console)
{
/*  Creates a new logfile object and adds it to the master (objs) list.
 *    Note: the logfile is open and set for non-blocking I/O.
 *  Returns the new object, or NULL on error.
 */
    int flags;
    int fd;
    obj_t *obj;
    char *now, *msg;

    assert(objs);
    assert(logfile && *logfile);
    assert(console && *console);

    flags = O_WRONLY | O_CREAT | O_APPEND | O_NONBLOCK;
    if ((fd = open(logfile, flags, S_IRUSR | S_IWUSR)) < 0) {
        log_msg(0, "Unable to open logfile %s: %s", logfile, strerror(errno));
        return(NULL);
    }

    obj = create_obj(LOGFILE, objs, logfile, fd);
    obj->aux.logfile.console = create_string(console);

    now = create_date_time_string(0);
    msg = create_fmt_string("\r\n%s Console %s log started at %s.\r\n",
        CONMAN_MSG_PREFIX, console, now);
    write_obj_data(obj, msg, strlen(msg));
    free(now);
    free(msg);

    return(obj);
}


obj_t * create_client_obj(List objs, req_t *req)
{
/*  Creates a new client object and adds it to the master (objs) list.
 *    Note: the socket is open and set for non-blocking I/O.
 *  Returns the new object.
 */
    char name[MAX_LINE];
    obj_t *obj;

    assert(objs);
    assert(req);
    assert(req->sd >= 0);
    assert(req->user && *req->user);
    assert(req->host && *req->host);

    set_descriptor_nonblocking(req->sd);

    snprintf(name, sizeof(name), "%s@%s:%d", req->user, req->host, req->port);
    name[sizeof(name) - 1] = '\0';
    obj = create_obj(CLIENT, objs, name, req->sd);

    obj->aux.client.req = req;
    obj->aux.client.gotEscape = 0;
    time(&obj->aux.client.timeLastRead);
    if (obj->aux.client.timeLastRead == ((time_t) -1))
        err_msg(errno, "time() failed -- What time is it?");

    return(obj);
}


static obj_t * create_obj(enum obj_type type, List objs, char *name, int fd)
{
/*  Creates an object of the specified (type) opened on (fd) and adds it
 *    to the master (objs) list.
 */
    obj_t *obj;
    int rc;

    assert(type == CONSOLE || type == LOGFILE || type == CLIENT);
    assert(objs);
    assert(name);
    assert(fd >= 0);

    /*  FIX_ME? Ensure name not already in use by another obj of same type
     */
    /*  FIX_ME? Return NULL if out-of-memory
     */
    if (!(obj = malloc(sizeof(obj_t))))
        err_msg(0, "Out of memory");
    obj->name = create_string(name);
    obj->fd = fd;
    obj->gotEOF = 0;
    obj->bufInPtr = obj->bufOutPtr = obj->buf;
    if ((rc = pthread_mutex_init(&obj->bufLock, NULL)) != 0)
        err_msg(rc, "pthread_mutex_init() failed for [%s]", name);
    if (!(obj->readers = list_create(NULL)))
        err_msg(0, "Out of memory");
    if (!(obj->writers = list_create(NULL)))
        err_msg(0, "Out of memory");
    obj->type = type;

    /*  Add obj to the master conf->objs list.
     */
    if (!list_append(objs, obj))
        err_msg(0, "Out of memory");

    DPRINTF("Created object [%s] on fd=%d.\n", obj->name, obj->fd);
    return(obj);   
}


void destroy_obj(obj_t *obj)
{
/*  Destroys the object, closing the fd and freeing resources as needed.
 *  Note: This routine is ONLY called via the objs list destructor.
 */
    int rc;

    if (!obj)
        return;

    DPRINTF("Destroyed object [%s].\n", obj->name);

    assert(obj->fd >= 0);
    assert(obj->bufInPtr == obj->bufOutPtr);

    if (obj->type == CONSOLE) {
        restore_console_mode(obj);
    }
    if (obj->fd >= 0) {
        if (close(obj->fd) < 0)
            err_msg(errno, "close(%d) failed", obj->fd);
        obj->fd = -1;
    }
    if ((rc = pthread_mutex_destroy(&obj->bufLock)) != 0)
        err_msg(rc, "pthread_mutex_destroy() failed for [%s]", obj->name);
    if (obj->readers)
        list_destroy(obj->readers);
    if (obj->writers)
        list_destroy(obj->writers);

    switch(obj->type) {
    case CONSOLE:
        if (obj->aux.console.dev)
            free(obj->aux.console.dev);
        if (obj->aux.console.log)
            free(obj->aux.console.log);
        if (obj->aux.console.rst)
            free(obj->aux.console.rst);
        break;
    case LOGFILE:
        if (obj->aux.logfile.console)
            free(obj->aux.logfile.console);
        break;
    case CLIENT:
        if (obj->aux.client.req) {
        /*
         *  Prevent destroy_req() from closing 'sd' a second time.
         */
            obj->aux.client.req->sd = -1;
            destroy_req(obj->aux.client.req);
        }
        break;
    default:
        err_msg(0, "Invalid object (%d) at %s:%d",
            obj->type, __FILE__, __LINE__);
        break;
    }

    if (obj->name)
        free(obj->name);

    free(obj);
    return;
}


static void set_raw_console_mode(obj_t *obj)
{
/*  FIX_ME: Can this be combined with client's set_raw_tty_mode() in util.c?
 */
    struct termios term;

    assert(obj->fd >= 0);
    assert(obj->type == CONSOLE);

    if (tcgetattr(obj->fd, &term) < 0)
        err_msg(errno, "tcgetattr(%s) failed", obj->aux.console.dev);
    obj->aux.console.term = term;

    /*  disable SIGINT on break, CR-to-NL, input parity checking,
     *    stripping 8th bit off input chars, output flow ctrl
     */
    term.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

    /*  disable output processing
     */
    term.c_oflag &= ~(OPOST);

    /*  clear size bits; disable parity checking
     */
    term.c_cflag &= ~(CSIZE | PARENB);

    /*  set 8 bits/char; ignore modem status lines
     */
    term.c_cflag |= (CS8 | CLOCAL);

    /*  disable echo, canonical mode, extended input processing, signal chars
     */
    term.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    /*  read() does not return until data is present (may block indefinitely)
     */
    term.c_cc[VMIN] = 1;
    term.c_cc[VTIME] = 0;

    DPRINTF("Setting raw terminal mode on [%s].\n", obj->name);
    if (tcsetattr(obj->fd, TCSANOW, &term) < 0)
        err_msg(errno, "tcsetattr(%s) failed", obj->aux.console.dev);
    return;
}


static void restore_console_mode(obj_t *obj)
{
/*  FIX_ME: Can this be combined with client's restore_tty_mode() in util.c?
 */
    assert(obj->fd >= 0);
    assert(obj->type == CONSOLE);

    if (tcsetattr(obj->fd, TCSANOW, &obj->aux.console.term) < 0)
        err_msg(errno, "tcsetattr(%s) failed", obj->aux.console.dev);
    DPRINTF("Restored cooked terminal mode on [%s].\n", obj->name);
    return;
}


int compare_objs(obj_t *obj1, obj_t *obj2)
{
/*  Used by list_sort() to compare the name of (obj1) to that of (obj2).
 */
    char *x, *y;

    assert(obj1);
    assert(obj2);

    x = obj1->name;
    y = obj2->name;
    while (*x && *x == *y)
        x++, y++;
    return(*x - *y);
}


int find_obj(obj_t *obj, obj_t *key)
{
/*  Used by list_find_first() and list_delete_all() to locate
 *    the object specified by (key) within the list.
 */
    return(obj == key);
}


void link_objs(obj_t *src, obj_t *dst)
{
/*  Creates a link so data read from (src) is copied to (dst).
 */
    ListIterator i;
    char *now;
    char *tty;
    obj_t *writer;
    int gotBcast, gotForce, gotJoin;
    char buf[MAX_LINE];

    assert(src->fd >= 0);
    assert(dst->fd >= 0);

    /*  If the dst console already has writers,
     *    display a warning if the request is to be joined
     *    or steal the console if the request is to be forced.
     */
    if ((dst->type == CONSOLE) && (!list_is_empty(dst->writers))) {

        assert(src->type == CLIENT);
        gotBcast = list_count(src->aux.client.req->consoles) > 1;
        gotForce = src->aux.client.req->enableForce;
        gotJoin = src->aux.client.req->enableJoin;
        assert(gotForce ^ gotJoin);

        now = create_time_string(0);
        tty = src->aux.client.req->tty;
        snprintf(buf, sizeof(buf),
            "\r\n%s Console %s %s%s by %s@%s%s%s at %s.\r\n",
            CONMAN_MSG_PREFIX, dst->name,
            (gotJoin ? "joined" : "stolen"), (gotBcast ? " for B/C" : ""),
            src->aux.client.req->user, src->aux.client.req->host,
            (tty ? " on " : ""), (tty ? tty : ""), now);
        free(now);
        buf[sizeof(buf) - 2] = '\n';
        buf[sizeof(buf) - 1] = '\0';
        write_obj_data(src, buf, strlen(buf));

        if (!(i = list_iterator_create(dst->writers)))
            err_msg(0, "Out of memory");
        while ((writer = list_next(i))) {
            assert(writer->type == CLIENT);
            write_obj_data(writer, buf, strlen(buf));
            if (gotForce)
                unlink_objs(dst, writer);
        }
        list_iterator_destroy(i);
    }

    /*  Create link from src reads to dst writes.
     */
    assert(!list_find_first(src->readers, (ListFindF) find_obj, dst));
    if (!list_append(src->readers, dst))
        err_msg(0, "Out of memory");
    assert(!list_find_first(dst->writers, (ListFindF) find_obj, src));
    if (!list_append(dst->writers, src))
        err_msg(0, "Out of memory");

    DPRINTF("Linked [%s] reads to [%s] writes.\n", src->name, dst->name);
    return;
}


void unlink_objs(obj_t *obj1, obj_t *obj2)
{
/*  Destroys all links between (obj1) and (obj2).
 */
    assert(obj1->fd >= 0);
    assert(obj2->fd >= 0);

    unlink_objs_helper(obj1, obj2);
    unlink_objs_helper(obj2, obj1);
    DPRINTF("Unlinked objects [%s] and [%s].\n", obj1->name, obj2->name);
    return;
}


static void unlink_objs_helper(obj_t *src, obj_t *dst)
{
    int n;
    ListIterator i;
    obj_t *writer;
    char *now;
    char *tty;
    char buf[MAX_LINE];

    if (list_find_first(src->readers, (ListFindF) find_obj, dst))
        DPRINTF("Removing [%s] from [%s] readers.\n", dst->name, src->name);
    list_delete_all(src->readers, (ListFindF) find_obj, dst);
    if (list_find_first(src->writers, (ListFindF) find_obj, dst))
        DPRINTF("Removing [%s] from [%s] writers.\n", dst->name, src->name);
    n = list_delete_all(src->writers, (ListFindF) find_obj, dst);
    if ((n > 0) && (src->type == CONSOLE) && (!list_is_empty(src->writers))) {

        assert(dst->type == CLIENT);
        now = create_time_string(0);
        tty = dst->aux.client.req->tty;
        snprintf(buf, sizeof(buf),
            "\r\n%s Console %s departed by %s@%s%s%s at %s.\r\n",
            CONMAN_MSG_PREFIX, src->name,
            dst->aux.client.req->user, dst->aux.client.req->host,
            (tty ? " on " : ""), (tty ? tty : ""), now);
        free(now);
        buf[sizeof(buf) - 2] = '\n';
        buf[sizeof(buf) - 1] = '\0';

        if (!(i = list_iterator_create(src->writers)))
            err_msg(0, "Out of memory");
        while ((writer = list_next(i))) {
            assert(writer->type == CLIENT);
            write_obj_data(writer, buf, strlen(buf));
        }
        list_iterator_destroy(i);
    }

    /*  If client has been unlinked from all readers and writers,
     *    set flag to ensure no additional data is written into the buffer.
     */
    if ((src->type == CLIENT)
      && list_is_empty(src->readers) && list_is_empty(src->writers))
        src->gotEOF = 1;

    return;
}


void shutdown_obj(obj_t *obj)
{
    ListIterator i;
    obj_t *reader;
    obj_t *writer;

    assert(obj->fd >= 0);

    if (!(i = list_iterator_create(obj->readers)))
        err_msg(0, "Out of memory");
    while ((reader = list_next(i)))
        unlink_objs(obj, reader);
    list_iterator_destroy(i);

    if (!(i = list_iterator_create(obj->writers)))
        err_msg(0, "Out of memory");
    while ((writer = list_next(i)))
        unlink_objs(obj, writer);
    list_iterator_destroy(i);

    DPRINTF("Shutdown object [%s].\n", obj->name);
    return;
}


void read_from_obj(obj_t *obj, fd_set *pWriteSet)
{
/*  Reads data from the obj's file descriptor and writes it out
 *    to the circular-buffer of each obj in its "readers" list.
 *  The ptr to select()'s write-set is an optimization used to
 *    "prime" the set for write_to_obj().  This allows data read
 *    to be written out to those objs not yet traversed during the
 *    current iteration, thereby reducing the latency.  Without it,
 *    these objs would be select()'d on the next iteration of the loop.
 *  An obj's circular-buffer is empty when (bufInPtr == bufOutPtr).
 *    Thus, it can hold at most (MAX_BUF_SIZE - 1) bytes of data.
 *    This routine's internal buffer is set accordingly.
 */
    unsigned char buf[MAX_BUF_SIZE - 1];
    int n;
    int rc;
    ListIterator i;
    obj_t *reader;

    assert(obj->fd >= 0);

again:
    if ((n = read(obj->fd, buf, sizeof(buf))) < 0) {
        if (errno == EINTR)
            goto again;
        else if ((errno != EAGAIN) && (errno != EWOULDBLOCK))
            err_msg(errno, "read error on fd=%d (%s)",
                obj->fd, obj->name);
    }
    else if (n == 0) {
        shutdown_obj(obj);
        FD_SET(obj->fd, pWriteSet);	/* ensure buffer is flushed if needed */
    }
    else {
        DPRINTF("Read %d bytes from [%s].\n", n, obj->name); /* xyzzy */
        if (obj->type == CLIENT) {

            if ((rc = pthread_mutex_lock(&obj->bufLock)) != 0)
                err_msg(rc, "pthread_mutex_lock() failed for [%s]", obj->name);
            if (time(&obj->aux.client.timeLastRead) == ((time_t) -1))
                err_msg(errno, "time() failed -- What time is it?");
            if ((rc = pthread_mutex_unlock(&obj->bufLock)) != 0)
                err_msg(rc, "pthread_mutex_unlock() failed for [%s]",
                    obj->name);

            n = process_escape_chars(obj, buf, n);
        }

        if (!(i = list_iterator_create(obj->readers)))
            err_msg(0, "Out of memory");
        while ((reader = list_next(i))) {
            /*
             *  If the obj's gotEOF flag is set,
             *    no more data can be written into its buffer.
             */
            if (!reader->gotEOF) {
                write_obj_data(reader, buf, n);
                FD_SET(reader->fd, pWriteSet);
            }
        }
        list_iterator_destroy(i);
    }
    return;
}


static int process_escape_chars(obj_t *client, void *src, int len)
{
/*  Processes the buffer (src) of length (len) received from
 *    the client for escape char sequences.
 *  Escape characters are removed from the buffer and processed.
 *  Returns the new length of the buffer.
 */
    u_char *last = (u_char *) src + len;
    u_char *p, *q;

    assert(client->type == CLIENT);
    assert(client->fd >= 0);

    if (!src || len <= 0)
        return(0);

    for (p=q=src; p<last; p++) {

        if (client->aux.client.gotEscape) {
            client->aux.client.gotEscape = 0;
            switch (*p) {
            case ESC_CHAR_BREAK:
                perform_serial_break(client);
                break;
            case CONMAN_ESC_CHAR:
                *q++ = *p;
                break;
            default:
                log_msg(10, "Received invalid escape char '%c' from %s.\n",
                    *p, client->name);
                break;
            }
        }
        else if (*p == CONMAN_ESC_CHAR) {
            client->aux.client.gotEscape = 1;
        }
        else {
            *q++ = *p;
        }
    }
    assert((q >= (u_char *) src) && (q <= p));
    len = q - (u_char *) src;
    return(len);
}


static void perform_serial_break(obj_t *client)
{
/*  Transmits a serial-break to each of the consoles written to by the client.
 */
    ListIterator i;
    obj_t *console;

    assert(client->type == CLIENT);
    if (!(i = list_iterator_create(client->readers)))
        err_msg(0, "Out of memory");
    while ((console = list_next(i))) {
        assert(console->type == CONSOLE);
        DPRINTF("Performing serial-break on console [%s].\n", console->name);
        if (tcsendbreak(console->fd, 0) < 0)
            err_msg(errno, "tcsendbreak() failed for console [%s]",
                console->name);
    }
    list_iterator_destroy(i);
    return;
}


int write_obj_data(obj_t *obj, void *src, int len)
{
/*  Writes the buffer (src) of length (len) into the object's (obj)
 *    circular-buffer.  Returns the number of bytes written.
 */
    int rc;
    int avail;
    int n, m;

    if (!src || len <= 0)
        return(0);

    /*  If the obj's gotEOF flag is set,
     *    no more data can be written into its buffer.
     */
    if (obj->gotEOF) {
        DPRINTF("Attempted to write into [%s] after EOF.\n", obj->name);
        return(0);
    }

    /*  An obj's circular-buffer is empty when (bufInPtr == bufOutPtr).
     *    Thus, it can hold at most (MAX_BUF_SIZE - 1) bytes of data.
     */
    if (len >= MAX_BUF_SIZE)
        len = MAX_BUF_SIZE - 1;

    if ((rc = pthread_mutex_lock(&obj->bufLock)) != 0)
        err_msg(rc, "pthread_mutex_lock() failed for [%s]", obj->name);

    /*  Assert the buffer's input and output ptrs are valid upon entry.
     */
    assert(obj->bufInPtr >= obj->buf);
    assert(obj->bufInPtr < &obj->buf[MAX_BUF_SIZE]);
    assert(obj->bufOutPtr >= obj->buf);
    assert(obj->bufOutPtr < &obj->buf[MAX_BUF_SIZE]);

    n = len;

    /*  Calculate the number of bytes available before data is overwritten.
     *  Data in the circular-buffer will be overwritten if needed since
     *    this routine must not block.
     */
    if (obj->bufOutPtr == obj->bufInPtr) {
        avail = MAX_BUF_SIZE - 1;
    }
    else if (obj->bufOutPtr > obj->bufInPtr) {
        avail = obj->bufOutPtr - obj->bufInPtr;
    }
    else {
        avail = (&obj->buf[MAX_BUF_SIZE] - obj->bufInPtr) +
            (obj->bufOutPtr - obj->buf);
    }

    /*  Copy first chunk of data (ie, up to the end of the buffer).
     */
    m = MIN(len, &obj->buf[MAX_BUF_SIZE] - obj->bufInPtr);
    if (m > 0) {
        memcpy(obj->bufInPtr, src, m);
        n -= m;
        src += m;
        obj->bufInPtr += m;
        /*
         *  Do the hokey-pokey and perform a circular-buffer wrap-around.
         */
        if (obj->bufInPtr == &obj->buf[MAX_BUF_SIZE])
            obj->bufInPtr = obj->buf;
    }

    /*  Copy second chunk of data (ie, from the beginning of the buffer).
     */
    if (n > 0) {
        memcpy(obj->bufInPtr, src, n);
        obj->bufInPtr += n;		/* Hokey-Pokey not needed here */
    }

    /*  Check to see if any data in circular-buffer was overwritten.
     */
    if (len > avail) {
        log_msg(10, "Buffer for %s overwrote %d bytes", obj->name, len-avail);
        obj->bufOutPtr = obj->bufInPtr + 1;
        if (obj->bufOutPtr == &obj->buf[MAX_BUF_SIZE])
            obj->bufOutPtr = obj->buf;
    }

    /*  Assert the buffer's input and output ptrs are valid upon exit.
     */
    assert(obj->bufInPtr >= obj->buf);
    assert(obj->bufInPtr < &obj->buf[MAX_BUF_SIZE]);
    assert(obj->bufOutPtr >= obj->buf);
    assert(obj->bufOutPtr < &obj->buf[MAX_BUF_SIZE]);

    if ((rc = pthread_mutex_unlock(&obj->bufLock)) != 0)
        err_msg(rc, "pthread_mutex_unlock() failed for [%s]", obj->name);

    return(len);
}


int write_to_obj(obj_t *obj)
{
/*  Writes data from the obj's circular-buffer out to its file descriptor.
 *  Returns -1 if obj buffer has been flushed and the obj is ready to remove
 *    from the objs list; o/w, returns 0.
 */
    int rc;
    int avail;
    int n;
    int flushed = 0;

    assert(obj->fd >= 0);

    if ((rc = pthread_mutex_lock(&obj->bufLock)) != 0)
        err_msg(rc, "pthread_mutex_lock() failed for [%s]", obj->name);

    /*  Assert the buffer's input and output ptrs are valid upon entry.
     */
    assert(obj->bufInPtr >= obj->buf);
    assert(obj->bufInPtr < &obj->buf[MAX_BUF_SIZE]);
    assert(obj->bufOutPtr >= obj->buf);
    assert(obj->bufOutPtr < &obj->buf[MAX_BUF_SIZE]);

    /*  The number of available bytes to write out to the file descriptor
     *    does not take into account data that has wrapped-around in the
     *    circular-buffer.  This remaining data will be written on the
     *    next invocation of this routine.  It's just simpler that way.
     *  Note that if (bufInPtr == bufOutPtr), the obj's buffer is empty.
     */
    if (obj->bufInPtr >= obj->bufOutPtr) {
        avail = obj->bufInPtr - obj->bufOutPtr;
    }
    else {
        avail = &obj->buf[MAX_BUF_SIZE] - obj->bufOutPtr;
    }

    if (avail > 0) {
again:
        if ((n = write(obj->fd, obj->bufOutPtr, avail)) < 0) {

            if (errno == EINTR) {
                goto again;
            }
            else if (errno == EPIPE) {
                obj->gotEOF = 1;
                obj->bufInPtr = obj->bufOutPtr = obj->buf;	/* flush buf */
            }
            else if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
                err_msg(errno, "write error on fd=%d (%s)", obj->fd, obj->name);
            }
        }
        else if (n > 0) {

            DPRINTF("Wrote %d bytes to [%s].\n", n, obj->name); /* xyzzy */
            obj->bufOutPtr += n;
            /*
             *  Do the hokey-pokey and perform a circular-buffer wrap-around.
             */
            if (obj->bufOutPtr == &obj->buf[MAX_BUF_SIZE])
                obj->bufOutPtr = obj->buf;
        }
    }

    /*  If the gotEOF flag in enabled, no additional data can be
     *    written into the buffer.  And if (bufInPtr == bufOutPtr),
     *    all data in the buffer has been written out to its fd.
     *    Thus, the object is ready to be closed, so return a code to
     *    notify mux_io() that the obj can be deleted from the objs list.
     */
    if (obj->gotEOF && (obj->bufInPtr == obj->bufOutPtr))
        flushed = 1;

    /*  Assert the buffer's input and output ptrs are valid upon exit.
     */
    assert(obj->bufInPtr >= obj->buf);
    assert(obj->bufInPtr < &obj->buf[MAX_BUF_SIZE]);
    assert(obj->bufOutPtr >= obj->buf);
    assert(obj->bufOutPtr < &obj->buf[MAX_BUF_SIZE]);

    if ((rc = pthread_mutex_unlock(&obj->bufLock)) != 0)
        err_msg(rc, "pthread_mutex_unlock() failed for [%s]", obj->name);

    return(flushed ? -1 : 0);
}
