/******************************************************************************\
 *  server-obj.c  
 *    by Chris Dunlap <cdunlap@llnl.gov>
 *
 *  $Id: server-obj.c,v 1.4 2001/05/14 16:22:09 dun Exp $
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
#include <time.h>
#include <unistd.h>
#include "errors.h"
#include "list.h"
#include "server.h"
#include "util.h"


static obj_t * create_obj(List objs, char *name, enum obj_type type);
static void set_raw_console_mode(obj_t *obj);
static void restore_console_mode(obj_t *obj);
static void parse_buf_for_control(obj_t *obj, void *src, int *pLen);


obj_t * create_console_obj(List objs, char *name, char *dev,
    char *log, char *rst, int bps)
{
    obj_t *obj;

    assert(name && *name);
    assert(dev && *dev);

    /* FIX_ME: check if console dev is really a tty via isatty() */
    /* FIX_ME: check name, dev, & log for dopplegangers */
    /* FIX_ME: check if rst program exists */
    /* FIX_ME: config file needs directive to specify execute dir for rst */
    /* FIX_ME: check bps for valid baud rate (cf APUE p343-344) */

    obj = create_obj(objs, name, CONSOLE);
    obj->aux.console.dev = create_string(dev);
    if (log && *log)
        obj->aux.console.log = create_string(log);
    if (rst && *rst)
        obj->aux.console.rst = create_string(rst);
    obj->aux.console.bps = bps;
    return(obj);
}


obj_t * create_logfile_obj(List objs, char *name)
{
    obj_t *obj;

    assert(name);

    obj = create_obj(objs, name, LOGFILE);
    return(obj);
}


obj_t * create_socket_obj(List objs, char *user, char *host, int sd)
{
    char *name;
    obj_t *obj;

    assert(user);
    assert(host);
    assert(sd >= 0);

    name = create_fmt_string("%s@%s", user, host);
    obj = create_obj(objs, name, SOCKET);
    free(name);

    /*  Socket objs are created in the "active" state (ie, fd >= 0)
     *    since the connection has already been established.
     */
    set_descriptor_nonblocking(sd);
    obj->fd = sd;

    obj->aux.socket.gotIAC = 0;
    time(&obj->aux.socket.timeLastRead);
    if (obj->aux.socket.timeLastRead == ((time_t) -1))
        err_msg(errno, "time() failed -- What time is it?");
    return(obj);
}


static obj_t * create_obj(List objs, char *name, enum obj_type type)
{
    obj_t *obj;
    int rc;

    assert(name);
    assert(type == CONSOLE || type == LOGFILE || type == SOCKET);

    /*  FIX_ME? Ensure name not already in use by another obj of same type
     */
    /*  FIX_ME: Return NULL if out-of-memory
     */
    if (!(obj = malloc(sizeof(obj_t))))
        err_msg(0, "Out of memory");
    obj->name = create_string(name);
    obj->fd = -1;
    obj->gotEOF = 0;
    obj->bufInPtr = obj->bufOutPtr = obj->buf;
    if ((rc = pthread_mutex_init(&obj->bufLock, NULL)) != 0)
        err_msg(rc, "pthread_mutex_init() failed for [%s]", name);
    obj->writer = NULL;
    if (!(obj->readers = list_create(NULL)))
        err_msg(0, "Out of memory");
    obj->type = type;

    /*  Save ref to objs list so destroy_obj() can remove the obj.
     */
    obj->objs = objs;

    /*  Add obj to conf->objs list.
     */
    if (!list_append(objs, obj))
        err_msg(0, "Out of memory");

    DPRINTF("Created object [%s].\n", name);
    return(obj);   
}


void destroy_obj(obj_t *obj)
{
    int n;

    n = list_delete_all(obj->objs, (ListFindF) find_obj, obj);
    assert(n == 1);
    return;
}


void dealloc_obj(obj_t *obj)
{
/*  Note: Do NOT destroy objs list; it is just a ref to the actual objs list.
 */
    int rc;

    assert(obj->bufInPtr == obj->bufOutPtr);

    DPRINTF("Destroyed object [%s].\n", obj->name);

    if (obj->fd >= 0) {
        if (close(obj->fd) < 0)
            err_msg(errno, "close(%d) failed", obj->fd);
        obj->fd = -1;
    }
    if ((rc = pthread_mutex_destroy(&obj->bufLock)) != 0)
        err_msg(rc, "pthread_mutex_destroy() failed for [%s]", obj->name);
    list_destroy(obj->readers);

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
        break;
    case SOCKET:
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


int open_obj(obj_t *obj)
{
/*  Returns 1 if obj was successfully opened, 0 if obj was already open,
 *    and -1 if an error occurred.
 *  Note that SOCKET objs are created in the "open" state.
 */
    if (obj->fd >= 0)			/* obj already open */
        return(0);

    if (obj->type == CONSOLE) {
        if ((obj->fd = open(obj->aux.console.dev, O_RDWR | O_NONBLOCK)) < 0) {
            log_msg(0, "Unable to open console [%s]: %s", obj->name,
                strerror(errno));
            return(-1);
        }
        set_raw_console_mode(obj);
    }
    else if (obj->type == LOGFILE) {
        int flags = O_WRONLY | O_CREAT | O_APPEND | O_NONBLOCK;
        if ((obj->fd = open(obj->name, flags, S_IRUSR | S_IWUSR)) < 0) {
            log_msg(0, "Unable to open logfile [%s]: %s", obj->name,
                strerror(errno));
            return(-1);
        }
    }

    DPRINTF("Opened object [%s].\n", obj->name);
    return(1);
}


static void set_raw_console_mode(obj_t *obj)
{
/*  FIX_ME:  Can this be combined with client's set_raw_tty_mode() in util.c?
 */
    struct termios term;

    assert(obj->type == CONSOLE);
    assert(obj->fd >= 0);

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


void close_obj(obj_t *obj)
{
    /*  FIX_ME: Write msg to console logfile when object is closed. */

    unlink_obj(obj);

    DPRINTF("Closing object [%s].\n", obj->name);

    /*  If buffer contains data, set "gotEOF" so write_to_obj() will flush it.
     */
    if (obj->bufInPtr != obj->bufOutPtr) {
        obj->gotEOF = 1;
    }
    else {
        obj->gotEOF = 0;
        if (obj->fd >= 0) {
            if (obj->type == CONSOLE)
                restore_console_mode(obj);
            if (close(obj->fd) < 0)
                err_msg(errno, "close(%d) failed", obj->fd);
            obj->fd = -1;
        }
        if (obj->type == SOCKET) {
            destroy_obj(obj);
        }
    }
    return;
}


static void restore_console_mode(obj_t *obj)
{
/*  FIX_ME:  Can this be combined with client's restore_tty_mode() in util.c?
 */
    assert(obj->type == CONSOLE);
    assert(obj->fd >= 0);

    if (tcsetattr(obj->fd, TCSANOW, &obj->aux.console.term) < 0)
        err_msg(errno, "tcsetattr(%s) failed", obj->aux.console.dev);
    DPRINTF("Restored cooked terminal mode on [%s].\n", obj->name);
    return;
}


int compare_objs(obj_t *obj1, obj_t *obj2)
{
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
    return(obj == key);
}


int link_objs(obj_t *src, obj_t *dst)
{
    int rcSrc, rcDst;
    char *now, *str;

    /*  Ensure both objs are "active".
     */
    if (src->fd < 0) {
        if ((rcSrc = open_obj(src)) < 0)
            return(-1);
    }
    if (dst->fd < 0) {
        if ((rcDst = open_obj(dst)) < 0) {
            if (rcSrc == 1)
                close_obj(src);
            return(-1);
        }
    }

    /*  If the dst console is already in R/W use by another client, steal it.
     */
    if (dst->writer != NULL) {
        assert(src->type == SOCKET);
        assert(dst->type == CONSOLE);
        assert(dst->writer->type == SOCKET);
        now = create_time_string(0);
        str = create_fmt_string("\nConsole '%s' stolen by <%s> at %s.\r\n",
            dst->name, src->name, now);
        write_obj_data(dst->writer, str, strlen(str));
        free(now);
        free(str);
        close_obj(dst->writer);
    }

    /*  Create obj link where src writes to dst.
     */
    dst->writer = src;
    if (!list_append(src->readers, dst))
        err_msg(0, "Out of memory");

    DPRINTF("Linked object [%s] to [%s].\n", src->name, dst->name);
    return(0);
}


int unlink_obj(obj_t *obj)
{
    ListIterator i;
    obj_t *reader;

    DPRINTF("Unlinking object [%s].\n", obj->name);

    /*  Remove object link between my writer and me.
     */
    if (obj->writer != NULL) {
        if (!(i = list_iterator_create(obj->writer->readers)))
            err_msg(0, "Out of memory");
        while ((reader = list_next(i))) {
            if (reader == obj) {
                DPRINTF("Removing(1) [%s] from [%s] readers.\n",
                    obj->name, obj->writer->name);
                list_delete(i);
                if ((obj->writer->writer == NULL)
                  && list_is_empty(obj->writer->readers))
                    close_obj(obj->writer);
                obj->writer = NULL;
                break;
            }
        }
        list_iterator_destroy(i);
    }

    /*  Remove object link between each of my readers and me.
     */
    while ((reader = list_pop(obj->readers))) {
        if (reader->writer == obj) {
            DPRINTF("Removing(2) [%s] from [%s] readers.\n",
                reader->name, obj->name);
            reader->writer = NULL;
            if (list_is_empty(reader->readers))
                close_obj(reader);
        }
    }

    return(0);
}


void write_to_obj(obj_t *obj)
{
/*  Writes data from the obj's circular-buffer out to its file descriptor.
 */
    int rc;
    int avail;
    int n;

    assert(obj->fd >= 0);
    if (obj->fd < 0)
        return;

    if ((rc = pthread_mutex_lock(&obj->bufLock)) != 0)
        err_msg(rc, "pthread_mutex_lock() failed for [%s]", obj->name);

    assert(obj->bufInPtr >= obj->buf);
    assert(obj->bufInPtr < &obj->buf[MAX_BUF_SIZE]);
    assert(obj->bufOutPtr >= obj->buf);
    assert(obj->bufOutPtr < &obj->buf[MAX_BUF_SIZE]);

    /*  The number of available bytes to write out to the file descriptor
     *    does not take into account data that has wrapped-around in the
     *    circular-buffer.  This remaining data will be written on the
     *    next invocation of this routine.
     *  Note that if (bufInPtr == bufOutPtr), the obj's buffer is empty.
     */
    if (obj->bufInPtr >= obj->bufOutPtr)
        avail = obj->bufInPtr - obj->bufOutPtr;
    else
        avail = &obj->buf[MAX_BUF_SIZE] - obj->bufOutPtr;

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

            obj->bufOutPtr += n;
            /*
             *  Do the hokey-pokey and perform a circular-buffer wrap-around.
             */
            if (obj->bufOutPtr == &obj->buf[MAX_BUF_SIZE])
                obj->bufOutPtr = obj->buf;
        }
    }

    assert(obj->bufOutPtr >= obj->buf);
    assert(obj->bufOutPtr < &obj->buf[MAX_BUF_SIZE]);

    if ((rc = pthread_mutex_unlock(&obj->bufLock)) != 0)
        err_msg(rc, "pthread_mutex_unlock() failed for [%s]", obj->name);

    /*  If the gotEOF flag in enabled, no additional data can be
     *    written into the buffer.  And if (bufInPtr == bufOutPtr),
     *    all data in the buffer has been written out to its fd.
     *    Thus, the object is ready to be closed.
     */
    if (obj->gotEOF && (obj->bufInPtr == obj->bufOutPtr)) {
        close_obj(obj);
    }

    return;
}


void read_from_obj(obj_t *obj)
{
/*  Reads data from the obj's file descriptor and writes it out
 *    to the circular-buffer of each obj in its "readers" list.
 *  An obj's circular-buffer is empty when (bufInPtr == bufOutPtr).
 *    Thus, it can hold at most (MAX_BUF_SIZE - 1) bytes of data.
 *    This routine's internal buffer is set accordingly.
 */
    unsigned char buf[MAX_BUF_SIZE - 1];
    int n;
    ListIterator i;
    obj_t *reader;

    assert(obj->fd >= 0);
    if (obj->fd < 0)
        return;

again:
    if ((n = read(obj->fd, buf, sizeof(buf))) < 0) {
        if (errno == EINTR)
            goto again;
        else if ((errno != EAGAIN) && (errno != EWOULDBLOCK))
            err_msg(errno, "read error on fd=%d (%s)",
                obj->fd, obj->name);
    }
    else if (n == 0) {
        close_obj(obj);
    }
    else {
        if (obj->type == SOCKET) {
            if (time(&obj->aux.socket.timeLastRead) == ((time_t) -1))
                err_msg(errno, "time() failed -- What time is it?");
            parse_buf_for_control(obj, buf, &n);
        }
        /*  If the obj's gotEOF flag is enabled,
         *    no additional data can be written into its buffer.
         */
        if (!(i = list_iterator_create(obj->readers)))
            err_msg(0, "Out of memory");
        while ((reader = list_next(i)))
            if (!reader->gotEOF)
                write_obj_data(reader, buf, n);
        list_iterator_destroy(i);
    }

    return;
}


static void parse_buf_for_control(obj_t *obj, void *src, int *pLen)
{
    if (!src || *pLen <= 0)
        return;
    /*
     *  FIX_ME: NOT_IMPLEMENTED_YET
     */
    return;
}


int write_obj_data(obj_t *obj, void *src, int len)
{
/*  Writes the buffer (src) of length (len) into the object's
 *    circular-buffer.  Returns the number of bytes written.
 */
    int rc;
    int avail;
    int n, m;

    if (!src || len <= 0)
        return(0);

    /*  An obj's circular-buffer is empty when (bufInPtr == bufOutPtr).
     *    Thus, it can hold at most (MAX_BUF_SIZE - 1) bytes of data.
     */
    if (len >= MAX_BUF_SIZE)
        len = MAX_BUF_SIZE - 1;

    if ((rc = pthread_mutex_lock(&obj->bufLock)) != 0)
        err_msg(rc, "pthread_mutex_lock() failed for [%s]", obj->name);

    assert(obj->bufInPtr >= obj->buf);
    assert(obj->bufInPtr < &obj->buf[MAX_BUF_SIZE]);
    assert(obj->bufOutPtr >= obj->buf);
    assert(obj->bufOutPtr < &obj->buf[MAX_BUF_SIZE]);

    n = len;

    /*  Calculate the number of bytes available before data is overwritten.
     *  Data in the circular-buffer will be overwritten if needed since
     *    this routine must not block.
     */
    if (obj->bufOutPtr == obj->bufInPtr)
        avail = MAX_BUF_SIZE - 1;
    else if (obj->bufOutPtr > obj->bufInPtr)
        avail = obj->bufOutPtr - obj->bufInPtr;
    else
        avail = (&obj->buf[MAX_BUF_SIZE] - obj->bufInPtr) +
            (obj->bufOutPtr - obj->buf);

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
        obj->bufInPtr += n;		/* Hokey-Pokey not needed here. */
    }

    /*  Check to see if any data in circular-buffer was overwritten.
     */
    if (len > avail) {
        log_msg(10, "[%s] overwrote %d bytes", obj->name, len-avail);
        obj->bufOutPtr = obj->bufInPtr + 1;
        if (obj->bufOutPtr == &obj->buf[MAX_BUF_SIZE])
            obj->bufOutPtr = obj->buf;
    }

    assert(obj->bufInPtr >= obj->buf);
    assert(obj->bufInPtr < &obj->buf[MAX_BUF_SIZE]);
    assert(obj->bufOutPtr >= obj->buf);
    assert(obj->bufOutPtr < &obj->buf[MAX_BUF_SIZE]);

    if ((rc = pthread_mutex_unlock(&obj->bufLock)) != 0)
        err_msg(rc, "pthread_mutex_unlock() failed for [%s]", obj->name);

    return(len);
}
