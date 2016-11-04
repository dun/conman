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
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include "common.h"
#include "list.h"
#include "log.h"
#include "server.h"
#include "tpoll.h"
#include "util-file.h"
#include "util-str.h"

extern tpoll_t tp_global;               /* defined in server.c */


typedef struct bps_tag {
    speed_t bps;
    int     val;
} bps_tag_t;


static bps_tag_t bps_table[] = {        /* values are in increasing order */
    {B50,     50},
    {B75,     75},
    {B110,    110},
    {B134,    134},
    {B150,    150},
    {B200,    200},
    {B300,    300},
    {B600,    600},
    {B1200,   1200},
    {B1800,   1800},
    {B2400,   2400},
    {B4800,   4800},
    {B9600,   9600},
    {B19200,  19200},
    {B38400,  38400},                   /* end of the line for POSIX.1 bps's */
#ifdef B57600
    {B57600,  57600},
#endif /* B57600 */
#ifdef B115200
    {B115200, 115200},
#endif /* B115200 */
#ifdef B230400
    {B230400, 230400},
#endif /* B230400 */
#ifdef B460800
    {B460800, 460800},
#endif /* B460800 */
    {0,       0}                        /* sentinel denotes end of array */
};


static speed_t int_to_bps(int val);
#ifndef NDEBUG
static int bps_to_int(speed_t bps);
static const char * parity_to_str(int parity);
#endif /* !NDEBUG */


int is_serial_dev(const char *dev, const char *cwd, char **path_ref)
{
    char         buf[PATH_MAX];
    int          n;
    struct stat  st;

    assert(dev != NULL);

    if ((dev[0] != '/') && (cwd != NULL)) {
        n = snprintf(buf, sizeof(buf), "%s/%s", cwd, dev);
        if ((n < 0) || ((size_t) n >= sizeof(buf))) {
            return(0);
        }
        dev = buf;
    }
    if (stat(dev, &st) < 0) {
        return(0);
    }
    if (!S_ISCHR(st.st_mode)) {
        return(0);
    }
    if (path_ref) {
        *path_ref = create_string(dev);
    }
    return(1);
}


int parse_serial_opts(
    seropt_t *opts, const char *str, char *errbuf, int errlen)
{
/*  Parses 'str' for serial device options 'opts'.
 *    The 'opts' struct should be initialized to a default value.
 *    The 'str' string is of the form "<bps>,<databits><parity><stopbits>".
 *  Returns 0 and updates the 'opts' struct on success; o/w, returns -1
 *    (writing an error message into 'errbuf' if defined).
 */
    int n;
    seropt_t optsTmp;
    int bpsTmp;
    char parityTmp;

    assert(opts != NULL);

    /*  By setting the tmp opts to the 'opts' that are passed in,
     *    we establish defaults for any values that are not changed by 'str'.
     */
    optsTmp = *opts;

    if ((str == NULL) || str[0] == '\0') {
        if ((errbuf != NULL) && (errlen > 0))
            snprintf(errbuf, errlen,
                "encountered empty options string");
        return(-1);
    }

    n = sscanf(str, "%d,%d%c%d", &bpsTmp, &optsTmp.databits,
        &parityTmp, &optsTmp.stopbits);

    if (n >= 1) {
        optsTmp.bps = int_to_bps(bpsTmp);
        if (optsTmp.bps <= 0) {
            if ((errbuf != NULL) && (errlen > 0))
                snprintf(errbuf, errlen,
                    "expected INTEGER >0 for bps setting");
            return(-1);
        }
    }
    if (n >= 2) {
        if ((optsTmp.databits < 5) || (optsTmp.databits > 8)) {
            if ((errbuf != NULL) && (errlen > 0))
                snprintf(errbuf, errlen,
                    "expected INTEGER 5-8 for databits setting");
            return(-1);
        }
    }
    if (n >= 3) {
        switch(parityTmp) {
        case 'N':
        case 'n':
            optsTmp.parity = 0;
            break;
        case 'O':
        case 'o':
            optsTmp.parity = 1;
            break;
        case 'E':
        case 'e':
            optsTmp.parity = 2;
            break;
        default:
            if ((errbuf != NULL) && (errlen > 0))
                snprintf(errbuf, errlen,
                    "expected (N|O|E) for parity setting");
            return(-1);
            break;
        }
    }
    if (n >= 4) {
        if ((optsTmp.stopbits < 1) || (optsTmp.stopbits > 2)) {
            if ((errbuf != NULL) && (errlen > 0))
                snprintf(errbuf, errlen,
                    "expected INTEGER 1-2 for stopbits setting");
            return(-1);
        }
    }

    *opts = optsTmp;
    return(0);
}


static speed_t int_to_bps(int val)
{
/*  Converts a numeric value 'val' into a bps speed_t,
 *    rounding down to the next bps value if necessary.
 */
    bps_tag_t *tag;
    speed_t bps = 0;

    for (tag=bps_table; tag->val > 0; tag++) {
        if (tag->val <= val)
            bps = tag->bps;
        else
            break;
    }
    return(bps);
}


#ifndef NDEBUG
static int bps_to_int(speed_t bps)
{
/*  Converts a 'bps' speed_t into its numeric value.
 *  Returns 0 if 'bps' does not correspond to any values in the table.
 */
    bps_tag_t *tag;

    for (tag=bps_table; tag->val > 0; tag++) {
        if (tag->bps == bps)
            return(tag->val);
    }
    return(0);
}
#endif /* !NDEBUG */


#ifndef NDEBUG
static const char * parity_to_str(int parity)
{
/*  Returns a constant string denoting the specified 'parity' value.
 */
    if (parity == 1)
        return("O");
    else if (parity == 2)
        return("E");
    else /* (parity == 0) */
        return("N");
}
#endif /* !NDEBUG */


void set_serial_opts(struct termios *tty, obj_t *serial, seropt_t *opts)
{
/*  Sets serial device options specified by 'opts' for the
 *   'tty' terminal settings associated with the 'serial' object.
 *  Updates the 'tty' struct as appropriate.
 */
    assert(tty != NULL);
    assert(serial != NULL);
    assert(is_serial_obj(serial));
    assert(opts != NULL);
    assert(opts->bps > 0);
    assert((opts->databits >= 5) && (opts->databits <= 8));
    assert((opts->parity >= 0) && (opts->parity <= 2));
    assert((opts->stopbits >= 1) && (opts->stopbits <= 2));

    DPRINTF((10, "Setting [%s] dev=%s to %d,%d%s%d.\n",
        serial->name, serial->aux.serial.dev, bps_to_int(opts->bps),
        opts->databits, parity_to_str(opts->parity), opts->stopbits));

    if (cfsetispeed(tty, opts->bps) < 0)
        log_err(errno, "Unable to set [%s] input baud rate to %d",
            serial->name, opts->bps);
    if (cfsetospeed(tty, opts->bps) < 0)
        log_err(errno, "Unable to set [%s] output baud rate to %d",
            serial->name, opts->bps);

    tty->c_cflag &= ~CSIZE;
    if (opts->databits == 5) {
        tty->c_cflag |= CS5;
    }
    else if (opts->databits == 6) {
        tty->c_cflag |= CS6;
    }
    else if (opts->databits == 7) {
        tty->c_cflag |= CS7;
    }
    else /* (opts->databits == 8) */ {  /* safe default in case value is bad */
        tty->c_cflag |= CS8;
    }

    if (opts->parity == 1) {
        tty->c_cflag |= (PARENB | PARODD);
    }
    else if (opts->parity == 2) {
        tty->c_cflag |= PARENB;
        tty->c_cflag &= ~PARODD;
    }
    else /* (opts->parity == 0) */ {    /* safe default in case value is bad */
        tty->c_cflag &= ~(PARENB | PARODD);
    }

    if (opts->stopbits == 2) {
        tty->c_cflag |= CSTOPB;
    }
    else /* (opts->stopbits == 1) */ {  /* safe default in case value is bad */
        tty->c_cflag &= ~CSTOPB;
    }

    return;
}


obj_t * create_serial_obj(server_conf_t *conf, char *name,
    char *dev, seropt_t *opts, char *errbuf, int errlen)
{
/*  Creates a new serial device object and adds it to the master objs list.
 *    Note: the console is open and set for non-blocking I/O.
 *  Returns the new object, or NULL on error.
 */
    ListIterator i;
    obj_t *serial;

    assert(conf != NULL);
    assert((name != NULL) && (name[0] != '\0'));
    assert((dev != NULL) && (dev[0] != '\0'));
    assert(opts != NULL);

    /*  Check for duplicate console and device names.
     *  While the write-lock will protect against two separate daemons
     *    using the same device, it will not protect against two console
     *    objects within the same daemon process using the same device.
     *    So that check is performed here.
     */
    i = list_iterator_create(conf->objs);
    while ((serial = list_next(i))) {
        if (is_console_obj(serial) && !strcmp(serial->name, name)) {
            if ((errbuf != NULL) && (errlen > 0)) {
                snprintf(errbuf, errlen,
                    "console [%s] specifies duplicate console name", name);
            }
            break;
        }
        if (is_serial_obj(serial) && !strcmp(serial->aux.serial.dev, dev)) {
            if ((errbuf != NULL) && (errlen > 0)) {
                snprintf(errbuf, errlen,
                    "console [%s] specifies duplicate device \"%s\"",
                    name, dev);
            }
            break;
        }
    }
    list_iterator_destroy(i);
    if (serial != NULL) {
        return(NULL);
    }
    serial = create_obj(conf, name, -1, CONMAN_OBJ_SERIAL);
    serial->aux.serial.dev = create_string(dev);
    serial->aux.serial.opts = *opts;
    serial->aux.serial.logfile = NULL;
    /*
     *  Add obj to the master conf->objs list.
     */
    list_append(conf->objs, serial);

    return(serial);
}


int open_serial_obj(obj_t *serial)
{
/*  (Re)opens the specified 'serial' obj.
 *  Returns 0 if the serial console is successfully opened; o/w, returns -1.
 *
 *  FIXME: Check to see if "downed" serial consoles are ever resurrected.
 */
    int fd;
    int flags;
    struct termios tty;

    assert(serial != NULL);
    assert(is_serial_obj(serial));

    if (serial->fd >= 0) {
        write_notify_msg(serial, LOG_INFO,
            "Console [%s] disconnected from \"%s\"",
            serial->name, serial->aux.serial.dev);
        tpoll_clear(tp_global, serial->fd, POLLIN | POLLOUT);
        set_tty_mode(&serial->aux.serial.tty, serial->fd);
        if (close(serial->fd) < 0)      /* log err and continue */
            log_msg(LOG_WARNING, "Unable to close [%s] device \"%s\": %s",
                serial->name, serial->aux.serial.dev, strerror(errno));
        serial->fd = -1;
    }
    flags = O_RDWR | O_NONBLOCK | O_NOCTTY;
    if ((fd = open(serial->aux.serial.dev, flags)) < 0) {
        log_msg(LOG_WARNING, "Unable to open [%s] device \"%s\": %s",
            serial->name, serial->aux.serial.dev, strerror(errno));
        goto err;
    }
    if (get_write_lock(fd) < 0) {
        log_msg(LOG_WARNING, "Unable to lock [%s] device \"%s\"",
            serial->name, serial->aux.serial.dev);
        goto err;
    }
    if (!isatty(fd)) {
        log_msg(LOG_WARNING, "[%s] device \"%s\" not a terminal",
            serial->name, serial->aux.serial.dev);
        goto err;
    }
    /*  According to the UNIX Programming FAQ v1.37
     *    <http://www.faqs.org/faqs/unix-faq/programmer/faq/>
     *    (Section 3.6: How to Handle a Serial Port or Modem),
     *    systems seem to differ as to whether a nonblocking
     *    open on a tty will affect subsequent read()s.
     *    Play it safe and be explicit!
     */
    set_fd_nonblocking(fd);
    set_fd_closed_on_exec(fd);
    /*
     *  Note that while the initial state of the console dev's termios
     *    are saved, the 'opts' settings are not.  This is because the
     *    settings do not change until the obj is destroyed, at which time
     *    the termios is reverted back to its initial state.
     *
     *  FIXME: Re-evaluate this thinking since a SIGHUP should attempt
     *         to resurrect "downed" serial objs.
     */
    get_tty_mode(&serial->aux.serial.tty, fd);
    get_tty_raw(&tty, fd);
    set_serial_opts(&tty, serial, &serial->aux.serial.opts);
    set_tty_mode(&tty, fd);
    serial->fd = fd;
    serial->gotEOF = 0;
    tpoll_set(tp_global, serial->fd, POLLIN);
    /*
     *  Success!
     */
    write_notify_msg(serial, LOG_INFO, "Console [%s] connected to \"%s\"",
        serial->name, serial->aux.serial.dev);
    DPRINTF((9, "Opened [%s] serial: fd=%d dev=%s bps=%d.\n",
        serial->name, serial->fd, serial->aux.serial.dev,
        bps_to_int(serial->aux.serial.opts.bps)));
    return(0);

err:
    if (fd >= 0) {
        (void) close(fd);
    }
    return(-1);
}
