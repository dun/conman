/******************************************************************************\
 *  $Id: server-obj.c,v 1.47 2001/09/26 21:14:20 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
\******************************************************************************/


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include "common.h"
#include "errors.h"
#include "list.h"
#include "server.h"
#include "tselect.h"
#include "util.h"
#include "util-file.h"
#include "util-net.h"
#include "util-str.h"
#include "wrapper.h"


static obj_t * create_obj(
    server_conf_t *conf, char *name, int fd, enum obj_type type);
static char * find_trailing_int_str(char *str);
static void unlink_objs_helper(obj_t *src, obj_t *dst);
static void notify_objs(List list1, List list2, char *msg);


static obj_t * create_obj(
    server_conf_t *conf, char *name, int fd, enum obj_type type)
{
/*  Creates an object of the specified (type) opened on (fd) and adds it
 *    to the master objs list.
 */
    obj_t *obj;

    assert(conf);
    assert(name);
    assert(type==CLIENT || type==LOGFILE || type==SERIAL || type==TELNET);

    if (!(obj = malloc(sizeof(obj_t))))
        out_of_memory();
    obj->name = create_string(name);
    obj->fd = fd;
    obj->bufInPtr = obj->bufOutPtr = obj->buf;
    x_pthread_mutex_init(&obj->bufLock, NULL);
    obj->readers = list_create(NULL);
    obj->writers = list_create(NULL);
    obj->type = type;
    obj->gotBufWrap = 0;
    obj->gotEOF = 0;

    /*  Add obj to the master conf->objs list.
     */
    list_append(conf->objs, obj);

    DPRINTF("Created object [%s] on fd=%d.\n", obj->name, obj->fd);
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

    assert(conf);
    assert(req);
    assert(req->sd >= 0);
    assert(req->user && *req->user);
    assert(req->host && *req->host);

    set_descriptor_nonblocking(req->sd);

    snprintf(name, sizeof(name), "%s@%s:%d", req->user, req->host, req->port);
    name[sizeof(name) - 1] = '\0';
    client = create_obj(conf, name, req->sd, CLIENT);

    client->aux.client.req = req;
    time(&client->aux.client.timeLastRead);
    if (client->aux.client.timeLastRead == (time_t) -1)
        err_msg(errno, "time() failed");
    client->aux.client.gotEscape = 0;
    client->aux.client.gotSuspend = 0;

    return(client);
}


obj_t * create_logfile_obj(server_conf_t *conf, char *name, obj_t *console)
{
/*  Creates a new logfile object and adds it to the master objs list.
 *    Note: the logfile is open and set for non-blocking I/O.
 *  Returns the new object, or NULL on error.
 */
    ListIterator i;
    obj_t *logfile;
    int flags;
    int fd = -1;
    char *now, *msg;

    assert(conf);
    assert(name && *name);
    assert(console);

    /*  Check for duplicate logfile names.
     *  While the write-lock will protect against two separate daemons
     *    using the same logfile, it will not protect against two logfile
     *    objects within the same daemon process using the same filename.
     *    So that check is performed here.
     */
    i = list_iterator_create(conf->objs);
    while ((logfile = list_next(i))) {
        if (!is_logfile_obj(logfile))
            continue;
        if (!strcmp(logfile->name, name)) {
            log_msg(0, "Ignoring duplicate logfile name \"%s\".", name);
            break;
        }
    }
    list_iterator_destroy(i);
    if (logfile != NULL)
        goto err;

    flags = O_WRONLY | O_CREAT | O_APPEND | O_NONBLOCK;
    if (conf->enableZeroLogs)
        flags |= O_TRUNC;
    if ((fd = open(name, flags, S_IRUSR | S_IWUSR)) < 0) {
        log_msg(0, "Unable to open logfile \"%s\": %s.", name, strerror(errno));
        goto err;
    }
    if (get_write_lock(fd) < 0) {
        log_msg(0, "Unable to lock \"%s\".", name);
        goto err;
    }

    logfile = create_obj(conf, name, fd, LOGFILE);
    logfile->aux.logfile.consoleName = create_string(console->name);
    if (is_serial_obj(console))
        console->aux.serial.logfile = logfile;
    else if (is_telnet_obj(console))
        console->aux.telnet.logfile = logfile;
    else
        assert(is_console_obj(console));

    now = create_long_time_string(0);
    msg = create_format_string("%sConsole [%s] log started at %s%s",
        CONMAN_MSG_PREFIX, console->name, now, CONMAN_MSG_SUFFIX);
    write_obj_data(logfile, msg, strlen(msg), 0);
    free(now);
    free(msg);

    return(logfile);

err:
    if (fd >= 0)
        if (close(fd) < 0)
            err_msg(errno, "close() failed on fd=%d", fd);
    return(NULL);
}


obj_t * create_serial_obj(
    server_conf_t *conf, char *name, char *dev, char *opts)
{
/*  Creates a new serial device object and adds it to the master objs list.
 *    Note: the console is open and set for non-blocking I/O.
 *  Returns the new object, or NULL on error.
 */
    ListIterator i;
    obj_t *serial;
    int fd = -1;
    struct termios tty;

    assert(conf);
    assert(name && *name);
    assert(dev && *dev);

    /*  Check for duplicate console and device names.
     *  While the write-lock will protect against two separate daemons
     *    using the same device, it will not protect against two console
     *    objects within the same daemon process using the same device.
     *    So that check is performed here.
     */
    i = list_iterator_create(conf->objs);
    while ((serial = list_next(i))) {
        if (is_console_obj(serial) && !strcmp(serial->name, name)) {
            log_msg(0, "Ignoring duplicate console name \"%s\".", name);
            break;
        }
        if (is_serial_obj(serial) && !strcmp(serial->aux.serial.dev, dev)) {
            log_msg(0, "Ignoring duplicate device name \"%s\".", dev);
            break;
        }
    }
    list_iterator_destroy(i);
    if (serial != NULL)
        goto err;

    if ((fd = open(dev, O_RDWR | O_NONBLOCK | O_NOCTTY)) < 0) {
        log_msg(0, "Unable to open console [%s]: %s.", name, strerror(errno));
        goto err;
    }
    if (get_write_lock(fd) < 0) {
        log_msg(0, "Unable to lock \"%s\".", dev);
        goto err;
    }
    if (!isatty(fd)) {
        log_msg(0, "Device \"%s\" is not a terminal.", dev);
        goto err;
    }
    /*  According to the UNIX Programming FAQ v1.37
     *    <http://www.faqs.org/faqs/unix-faq/programmer/faq/>
     *    (Section 3.6: How to Handle a Serial Port or Modem),
     *    systems seem to differ as to whether a nonblocking
     *    open on a tty will affect subsequent read()s.
     *    Play it safe and be explicit!
     */
    set_descriptor_nonblocking(fd);

    /*  Note that while the initial state of the console dev's termios
     *    are saved, the settings derived from the 'opts' string are not.
     *  This is because the settings do not change until the obj is destroyed,
     *    at which time the termios is reverted back to its initial state.
     */
    serial = create_obj(conf, name, fd, SERIAL);
    serial->aux.serial.dev = create_string(dev);
    serial->aux.serial.logfile = NULL;
    get_tty_mode(&serial->aux.serial.tty, fd);
    get_tty_raw(&tty, fd);
    set_serial_opts(&tty, serial, opts);
    set_tty_mode(&tty, fd);

    return(serial);

err:
    if (fd >= 0)
        if (close(fd) < 0)
            err_msg(errno, "close() failed on fd=%d", fd);
    return(NULL);
}


obj_t * create_telnet_obj(
    server_conf_t *conf, char *name, char *host, int port)
{
/*  Creates a new terminal server object and adds it to the master objs list.
 *  Note: a non-blocking connect is initiated for the remote host:port
 *    and later completed by mux_io().
 *  Returns the new object, or NULL on error.
 */
    ListIterator i;
    obj_t *telnet;
    struct sockaddr_in saddr;
    int n;
    char buf[MAX_LINE];

    assert(conf);
    assert(name && *name);
    assert(host && *host);

    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    if (port <= 0) {
        log_msg(0, "Invalid port number %d.", port);
        return(NULL);
    }
    saddr.sin_port = htons(port);
    if (host_name_to_addr4(host, &saddr.sin_addr) < 0) {
        log_msg(0, "Unable to resolve host \"%s\".", host);
        return(NULL);
    }

    /*  Check for duplicate console names and terminal server locations.
     */
    i = list_iterator_create(conf->objs);
    while ((telnet = list_next(i))) {
        if (is_console_obj(telnet) && !strcmp(telnet->name, name)) {
            log_msg(0, "Ignoring duplicate console name \"%s\".", name);
            break;
        }
        if (is_telnet_obj(telnet) && !memcmp(&saddr,
          &telnet->aux.telnet.saddr, sizeof(telnet->aux.telnet.saddr))) {
            log_msg(0, "Ignoring duplicate terminal server \"%s:%d\".",
                host, port);
            break;
        }
    }
    list_iterator_destroy(i);
    if (telnet != NULL)
        return(NULL);

    if (host_addr4_to_name(&saddr.sin_addr, buf, sizeof(buf))) {
        if ((host = strchr(buf, '.')))
            *host = '\0';
        host = buf;
    }

    telnet = create_obj(conf, name, -1, TELNET);
    telnet->aux.telnet.host = create_string(host);
    telnet->aux.telnet.port = port;
    telnet->aux.telnet.saddr = saddr;
    telnet->aux.telnet.logfile = NULL;
    telnet->aux.telnet.timer = -1;
    telnet->aux.telnet.delay = TELNET_MIN_TIMEOUT;
    telnet->aux.telnet.iac = -1;
    for (n=0; n<NTELOPTS; n++)
        telnet->aux.telnet.optState[n] = TELOPT_NO;
    telnet->aux.telnet.conState = TELCON_DOWN;
    /*
     *  Dup 'enableKeepAlive' to prevent passing 'conf' to connect_telnet_obj().
     */
    telnet->aux.telnet.enableKeepAlive = conf->enableKeepAlive;

    connect_telnet_obj(telnet);
    return(telnet);
}


int connect_telnet_obj(obj_t *telnet)
{
/*  Establishes a non-blocking connect with the specified (telnet) obj.
 *  Returns 0 if the connection is successfully completed; o/w, returns -1.
 */
    const int on = 1;
    const int n = sizeof(on);
    char *now;
    char buf[MAX_LINE];

    assert(telnet);
    assert(is_telnet_obj(telnet));
    assert(telnet->aux.telnet.conState != TELCON_UP);

    if (telnet->aux.telnet.timer >= 0) {
        untimeout(telnet->aux.telnet.timer);
        telnet->aux.telnet.timer = -1;
    }

    if (telnet->aux.telnet.conState == TELCON_DOWN) {
        /*
         *  Initiate a non-blocking connection attempt.
         */
        if ((telnet->fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
            err_msg(0, "Unable to create socket for [%s].", telnet->name);
        if (setsockopt(telnet->fd, SOL_SOCKET, SO_OOBINLINE, &on, n) < 0)
            err_msg(errno, "Unable to set OOBINLINE socket option");
        if (telnet->aux.telnet.enableKeepAlive) {
            if (setsockopt(telnet->fd, SOL_SOCKET, SO_KEEPALIVE, &on, n) < 0)
                err_msg(errno, "Unable to set KEEPALIVE socket option");
        }
        set_descriptor_nonblocking(telnet->fd);

        while (connect(telnet->fd,
          (struct sockaddr *) &telnet->aux.telnet.saddr,
          sizeof(telnet->aux.telnet.saddr)) < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EINPROGRESS) {
            /*
             *  NOTE: Bug exists in timeout of connect():
             *      server.c:335: Unable to multiplex I/O: Bad file descriptor.
             *
             *  telnet->aux.telnet.timer =
             *      timeout((CallBackF) disconnect_telnet_obj,
             *      telnet, TELNET_MIN_TIMEOUT * 1000);
             */
                telnet->aux.telnet.conState = TELCON_PENDING;
            }
            else
                disconnect_telnet_obj(telnet);
            return(-1);
        }
        /* Success!  Continue after if/else expr. */
    }
    else if (telnet->aux.telnet.conState == TELCON_PENDING) {
        /*
         *  Did the non-blocking connect complete successfully?
         *    (cf. Stevens UNPv1 15.3 p409)
         */
        int err = 0;
        int len = sizeof(err);
        int rc = getsockopt(telnet->fd, SOL_SOCKET, SO_ERROR, &err, &len);
        /*
         *  If an error occurred, Berkeley-derived implementations
         *    return 0 with the pending error in 'err'.  But Solaris
         *    returns -1 with the pending error in 'errno'.  Sigh...
         */
        if (rc < 0)
            err = errno;
        if (err) {
            disconnect_telnet_obj(telnet);
            return(-1);
        }
        /* Success!  Continue after if/else expr. */
    }
    else {
        err_msg(0, "Console [%s] is in an unknown telnet state %d.\n",
            telnet->aux.telnet.conState);
    }

    /*  Success!
     */
    log_msg(0, "Console [%s] connected to <%s:%d>.", telnet->name,
        telnet->aux.telnet.host, telnet->aux.telnet.port);

    /*  Notify linked objs when transitioning into an UP state.
     */
    now = create_short_time_string(0);
    snprintf(buf, sizeof(buf), "%sConsole [%s] connected to <%s:%d> at %s%s",
        CONMAN_MSG_PREFIX, telnet->name, telnet->aux.telnet.host,
        telnet->aux.telnet.port, now, CONMAN_MSG_SUFFIX);
    free(now);
    strcpy(&buf[sizeof(buf) - 3], "\r\n");
    notify_objs(telnet->readers, telnet->writers, buf);
    telnet->aux.telnet.conState = TELCON_UP;
    telnet->aux.telnet.delay = 0;

    send_telnet_cmd(telnet, DO, TELOPT_SGA);
    send_telnet_cmd(telnet, DO, TELOPT_ECHO);

    return(0);
}


void disconnect_telnet_obj(obj_t *telnet)
{
/*  Closes the existing connection with the specified (telnet) obj
 *    and sets a timer for establishing a new connection.
 */
    char buf[MAX_LINE];
    char *now;

    assert(telnet);
    assert(telnet->fd > 0);
    assert(is_telnet_obj(telnet));

    if (telnet->aux.telnet.timer >= 0) {
        untimeout(telnet->aux.telnet.timer);
        telnet->aux.telnet.timer = -1;
    }
    if (close(telnet->fd) < 0)
        err_msg(errno, "close() of <%s:%d> failed for [%s]",
            telnet->aux.telnet.host, telnet->aux.telnet.port, telnet->name);
    telnet->fd = -1;

    /*  Notify linked objs when transitioning from an UP state.
     */
    if (telnet->aux.telnet.conState == TELCON_UP) {
        log_msg(0, "Console [%s] disconnected from <%s:%d>.", telnet->name,
            telnet->aux.telnet.host, telnet->aux.telnet.port);
        now = create_short_time_string(0);
        snprintf(buf, sizeof(buf),
            "%sConsole [%s] disconnected from <%s:%d> at %s%s",
            CONMAN_MSG_PREFIX, telnet->name, telnet->aux.telnet.host,
            telnet->aux.telnet.port, now, CONMAN_MSG_SUFFIX);
        free(now);
        strcpy(&buf[sizeof(buf) - 3], "\r\n");
        notify_objs(telnet->readers, telnet->writers, buf);
    }

    /*  Set timer for establishing new connection using exponential backoff.
     */
    telnet->aux.telnet.conState = TELCON_DOWN;
    telnet->aux.telnet.timer = timeout((CallBackF) connect_telnet_obj,
        telnet, telnet->aux.telnet.delay * 1000);
    if (telnet->aux.telnet.delay == 0)
        telnet->aux.telnet.delay = TELNET_MIN_TIMEOUT;
    else if (telnet->aux.telnet.delay < TELNET_MAX_TIMEOUT)
        telnet->aux.telnet.delay =
            MIN(telnet->aux.telnet.delay * 2, TELNET_MAX_TIMEOUT);
    return;
}


void destroy_obj(obj_t *obj)
{
/*  Destroys the object, closing the fd and freeing resources as needed.
 *  Note: This routine is ONLY called via the objs list destructor.
 */
    if (!obj)
        return;

    DPRINTF("Destroyed object [%s].\n", obj->name);
/*
 *  FIX_ME? Ensure obj buf is flushed (if not suspended) before destruction.
 *
 *  assert(obj->bufInPtr == obj->bufOutPtr); */

    if (is_serial_obj(obj)) {
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
        if (tcflush(obj->fd, TCIOFLUSH) < 0)
            err_msg(errno, "Unable to flush tty device for console [%s]",
                obj->name);
        set_tty_mode(&obj->aux.serial.tty, obj->fd);
    }
    if (obj->fd >= 0) {
        if (close(obj->fd) < 0)
            err_msg(errno, "close() failed on fd=%d", obj->fd);
        obj->fd = -1;
    }
    x_pthread_mutex_destroy(&obj->bufLock);
    if (obj->readers)
        list_destroy(obj->readers);
    if (obj->writers)
        list_destroy(obj->writers);

    switch(obj->type) {
    case CLIENT:
        if (obj->aux.client.req) {
        /*
         *  Prevent destroy_req() from closing 'sd' a second time.
         */
            obj->aux.client.req->sd = -1;
            destroy_req(obj->aux.client.req);
        }
        break;
    case LOGFILE:
        if (obj->aux.logfile.consoleName)
            free(obj->aux.logfile.consoleName);
        break;
    case SERIAL:
        if (obj->aux.serial.dev)
            free(obj->aux.serial.dev);
        /* Do not destroy obj->aux.serial.logfile since it is only a ref. */
        break;
    case TELNET:
        if (obj->aux.telnet.host)
            free(obj->aux.telnet.host);
        /* Do not destroy obj->aux.telnet.logfile since it is only a ref. */
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


int compare_objs(obj_t *obj1, obj_t *obj2)
{
/*  Used by list_sort() to compare the name of (obj1) to that of (obj2).
 */
    char *s1, *s2;
    char *i1, *i2;

    assert(obj1);
    assert(obj2);
    assert(obj1->name);
    assert(obj2->name);

    s1 = obj1->name;
    s2 = obj2->name;
    i1 = find_trailing_int_str(s1);
    i2 = find_trailing_int_str(s2);

    while (*s1) {
        if ((s1 == i1) && (s2 == i2))
            return(atoi(i1) - atoi(i2));
        else if (*s1 == *s2)
            s1++, s2++;
        else
            break;
    }
    return(*s1 - *s2);
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
 */
    assert(obj);
    assert(key);

    return(obj == key);
}


void link_objs(obj_t *src, obj_t *dst)
{
/*  Creates a link so data read from (src) is written to (dst).
 */
    int gotBcast, gotForce, gotJoin;
    char *now;
    char *tty;
    char buf[MAX_LINE];
    ListIterator i;
    obj_t *writer;

    /*  If the dst console already has writers,
     *    display a warning if the request is to be joined
     *    or steal the console if the request is to be forced.
     */
    if (is_console_obj(dst) && !list_is_empty(dst->writers)) {

        assert(is_client_obj(src));
        gotBcast = list_count(src->aux.client.req->consoles) > 1;
        gotForce = src->aux.client.req->enableForce;
        gotJoin = src->aux.client.req->enableJoin;
        assert(gotForce ^ gotJoin);

        now = create_short_time_string(0);
        tty = src->aux.client.req->tty;
        snprintf(buf, sizeof(buf),
            "%sConsole [%s] %s%s by %s@%s%s%s at %s%s",
            CONMAN_MSG_PREFIX, dst->name,
            (gotJoin ? "joined" : "stolen"), (gotBcast ? " for B/C" : ""),
            src->aux.client.req->user, src->aux.client.req->host,
            (tty ? " on " : ""), (tty ? tty : ""), now, CONMAN_MSG_SUFFIX);
        free(now);
        strcpy(&buf[sizeof(buf) - 3], "\r\n");
        write_obj_data(src, buf, strlen(buf), 1);

        i = list_iterator_create(dst->writers);
        while ((writer = list_next(i))) {
            assert(is_client_obj(writer));
            write_obj_data(writer, buf, strlen(buf), 1);
            if (gotForce)
                unlink_objs(dst, writer);
        }
        list_iterator_destroy(i);
    }

    /*  Create link from src reads to dst writes.
     */
    assert(!list_find_first(src->readers, (ListFindF) find_obj, dst));
    list_append(src->readers, dst);
    assert(!list_find_first(dst->writers, (ListFindF) find_obj, src));
    list_append(dst->writers, src);

    DPRINTF("Linked [%s] reads to [%s] writes.\n", src->name, dst->name);
    return;
}


void unlink_objs(obj_t *obj1, obj_t *obj2)
{
/*  Destroys all links between (obj1) and (obj2).
 */
    unlink_objs_helper(obj1, obj2);
    unlink_objs_helper(obj2, obj1);
    DPRINTF("Unlinked objects [%s] and [%s].\n", obj1->name, obj2->name);
    return;
}


static void unlink_objs_helper(obj_t *src, obj_t *dst)
{
/*  DOCUMENT_ME!
 */
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

    if ((n > 0) && is_console_obj(src) && !list_is_empty(src->writers)) {

        assert(is_client_obj(dst));
        now = create_short_time_string(0);
        tty = dst->aux.client.req->tty;
        snprintf(buf, sizeof(buf),
            "%sConsole [%s] departed by %s@%s%s%s at %s%s", CONMAN_MSG_PREFIX,
            src->name, dst->aux.client.req->user, dst->aux.client.req->host,
            (tty ? " on " : ""), (tty ? tty : ""), now, CONMAN_MSG_SUFFIX);
        free(now);
        strcpy(&buf[sizeof(buf) - 3], "\r\n");

        i = list_iterator_create(src->writers);
        while ((writer = list_next(i))) {
            assert(is_client_obj(writer));
            write_obj_data(writer, buf, strlen(buf), 1);
        }
        list_iterator_destroy(i);
    }

    /*  If client has been unlinked from all readers and writers,
     *    set flag to ensure no additional data is written into the buffer.
     */
    if ( is_client_obj(src)
      && list_is_empty(src->readers)
      && list_is_empty(src->writers) )
        src->gotEOF = 1;

    return;
}


void shutdown_obj(obj_t *obj)
{
/*  DOCUMENT_ME!
 */
    ListIterator i;
    obj_t *reader;
    obj_t *writer;

    assert(obj->fd >= 0);

    /*  If a telent obj is shut down, shutdown the existing connection
     *    and attempt to establish a new connection.
     */
    if (is_telnet_obj(obj)) {
        disconnect_telnet_obj(obj);
        return;
    }

    i = list_iterator_create(obj->readers);
    while ((reader = list_next(i)))
        unlink_objs(obj, reader);
    list_iterator_destroy(i);

    i = list_iterator_create(obj->writers);
    while ((writer = list_next(i)))
        unlink_objs(obj, writer);
    list_iterator_destroy(i);

    DPRINTF("Shutdown object [%s].\n", obj->name);
    return;
}


void notify_objs(List list1, List list2, char *msg)
{
/*  DOCUMENT_ME!
 */
    ListIterator i;
    obj_t *obj;

    if (!list1)
        list1 = list2;
    if (!list1)
        return;
    if (!msg || !strlen(msg))
        return;

    i = list_iterator_create(list1);
    while ((obj = list_next(i)))
        write_obj_data(obj, msg, strlen(msg), 1);
    list_iterator_destroy(i);

    if (list2) {
        i = list_iterator_create(list2);
        while ((obj = list_next(i))) {
            if (!list_find_first(list1, (ListFindF) find_obj, obj))
                write_obj_data(obj, msg, strlen(msg), 1);
        }
        list_iterator_destroy(i);
    }

    return;
}


int read_from_obj(obj_t *obj, fd_set *pWriteSet)
{
/*  Reads data from the obj's file descriptor and writes it out
 *    to the circular-buffer of each obj in its "readers" list.
 *  Returns >=0 on success, or -1 if the obj is ready to be destroyed.
 *
 *  The ptr to select()'s write-set is an optimization used to "prime"
 *    the set for write_to_obj().  This allows data read to be written out
 *    to those objs not yet traversed during the current list iteration,
 *    thereby reducing the latency.  Without it, these objs would be
 *    select()'d on the next list iteration in mux_io()'s outer-loop.
 *  An obj's circular-buffer is empty when (bufInPtr == bufOutPtr).
 *    Thus, it can hold at most (MAX_BUF_SIZE - 1) bytes of data.
 *    This routine's internal buffer is set accordingly.
 */
    unsigned char buf[MAX_BUF_SIZE - 1];
    int n;
    ListIterator i;
    obj_t *reader;

    assert(obj->fd >= 0);

    if (is_telnet_obj(obj) && obj->aux.telnet.conState != TELCON_UP)
        return(0);

again:
    if ((n = read(obj->fd, buf, sizeof(buf))) < 0) {
        if (errno == EINTR) {
            goto again;
        }
        else if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
            log_msg(20, "Unable to read from %s: %s.",
                obj->name, strerror(errno));
            shutdown_obj(obj);
            obj->bufInPtr = obj->bufOutPtr = obj->buf;
            return(-1);
        }
    }
    else if (n == 0) {
        shutdown_obj(obj);
        if (obj->fd < 0)		/* telnet obj has been disconnected */
            return(-1);
        FD_SET(obj->fd, pWriteSet);	/* ensure buffer is flushed if needed */
    }
    else {
        DPRINTF("Read %d bytes from [%s].\n", n, obj->name); /* xyzzy */

        if (is_client_obj(obj)) {
            x_pthread_mutex_lock(&obj->bufLock);
            time(&obj->aux.client.timeLastRead);
            if (obj->aux.client.timeLastRead == (time_t) -1)
                err_msg(errno, "time() failed");
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
                /*
                 *  If the obj's gotEOF flag is set,
                 *    no more data can be written into its buffer.
                 */
                if (!reader->gotEOF) {
                    if (write_obj_data(reader, buf, n, 0) > 0)
                        FD_SET(reader->fd, pWriteSet);
                }
            }
            list_iterator_destroy(i);
        }
    }
    return(n);
}


int write_obj_data(obj_t *obj, void *src, int len, int isInfo)
{
/*  Writes the buffer (src) of length (len) into the object's (obj)
 *    circular-buffer.  If (isInfo) is true, the data is considered
 *    an informational message which a client may suppress.
 *  Returns the number of bytes written.
 *  Note that this routine can write at most (MAX_BUF_SIZE - 1) bytes
 *    of data into the object's circular-buffer.
 */
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

    /*  If the obj is a disconnected telnet connection,
     *    data will simply be discarded so perform a no-op here.
     */
    if (is_telnet_obj(obj) && obj->aux.telnet.conState != TELCON_UP) {
        DPRINTF("Attempted to write to disconnected [%s].\n", obj->name);
        return(0);
    }

    /*  An obj's circular-buffer is empty when (bufInPtr == bufOutPtr).
     *    Thus, it can hold at most (MAX_BUF_SIZE - 1) bytes of data.
     */
    if (len >= MAX_BUF_SIZE)
        len = MAX_BUF_SIZE - 1;

    x_pthread_mutex_lock(&obj->bufLock);

    /*  Do nothing if this is an informational message
     *    and the client has requested not to be bothered.
     */
    if (isInfo && is_client_obj(obj) && obj->aux.client.req->enableQuiet) {
        len = 0;
        goto end;
    }

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
     *  Since an obj's circular-buffer is empty when (bufInPtr == bufOutPtr),
     *    subtract one byte from 'avail' to account for this sentinel.
     */
    if (obj->bufOutPtr == obj->bufInPtr) {
        avail = MAX_BUF_SIZE - 1;
    }
    else if (obj->bufOutPtr > obj->bufInPtr) {
        avail = obj->bufOutPtr - obj->bufInPtr - 1;
    }
    else {
        avail = (&obj->buf[MAX_BUF_SIZE] - obj->bufInPtr) +
            (obj->bufOutPtr - obj->buf) - 1;
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
        if (obj->bufInPtr == &obj->buf[MAX_BUF_SIZE]) {
            obj->bufInPtr = obj->buf;
            obj->gotBufWrap = 1;
        }
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
        if (!is_client_obj(obj) || !obj->aux.client.gotSuspend)
            log_msg(10, "Overwrote %d bytes in buffer for %s.",
                len-avail, obj->name);
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

end:
    x_pthread_mutex_unlock(&obj->bufLock);
    return(len);
}


int write_to_obj(obj_t *obj)
{
/*  Writes data from the obj's circular-buffer out to its file descriptor.
 *  Returns 0 on success, or -1 if the obj is ready to be destroyed.
 */
    int avail;
    int n;
    int isDead = 0;

    assert(obj->fd >= 0);

    x_pthread_mutex_lock(&obj->bufLock);

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
     *  If a client is suspended, no data is written out to its fd.
     *  If a telnet connection goes down, the buffer is cleared.
     *  Note that if (bufInPtr == bufOutPtr), the obj's buffer is empty.
     */
    if (is_client_obj(obj) && obj->aux.client.gotSuspend) {
        avail = 0;
    }
    else if (is_telnet_obj(obj) && obj->aux.telnet.conState != TELCON_UP) {
        avail = 0;
        obj->bufInPtr = obj->bufOutPtr = obj->buf;
    }
    else if (obj->bufInPtr >= obj->bufOutPtr) {
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
            else if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
                log_msg(20, "Unable to write to %s: %s.",
                    obj->name, strerror(errno));
                obj->gotEOF = 1;
                obj->bufInPtr = obj->bufOutPtr = obj->buf;
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
        isDead = 1;

    /*  Assert the buffer's input and output ptrs are valid upon exit.
     */
    assert(obj->bufInPtr >= obj->buf);
    assert(obj->bufInPtr < &obj->buf[MAX_BUF_SIZE]);
    assert(obj->bufOutPtr >= obj->buf);
    assert(obj->bufOutPtr < &obj->buf[MAX_BUF_SIZE]);

    x_pthread_mutex_unlock(&obj->bufLock);
    return(isDead ? -1 : 0);
}
