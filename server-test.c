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
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "list.h"
#include "log.h"
#include "server.h"
#include "tpoll.h"
#include "util-file.h"
#include "util-str.h"
#include "util.h"

extern tpoll_t tp_global;               /* defined in server.c */


#define TEST_CONSOLE_DEFAULT_BYTES              1024
#define TEST_CONSOLE_DEFAULT_DELAY_MSECS        100
#define TEST_CONSOLE_FIRST_CHAR                 0x20
#define TEST_CONSOLE_LAST_CHAR                  0x7E


static int process_test_opt(
    test_opt_t *opts, const char *str, char *errbuf, int errlen);


int is_test_dev(const char *dev)
{
/*  Returns 1 if 'dev' specifies a test console device; o/w, returns 0.
 */
    return(!strcasecmp(dev, "test:"));
}


int init_test_opts(test_opt_t *opts)
{
/*  Initializes 'opts' to the default values.
 *  Returns 0 on success, -1 on error.
 */
    if (opts == NULL) {
        return(-1);
    }
    opts->numBytes = TEST_CONSOLE_DEFAULT_BYTES;
    opts->msecMax = -1;
    opts->msecMin = -1;
    opts->probability = 100;
    return(0);
}


int parse_test_opts(
    test_opt_t *opts, const char *str, char *errbuf, int errlen)
{
/*  Parses string 'str' for test console device options 'opts'.
 *    The string 'str' is broken up into comma-delimited tokens; as such,
 *    token values for a given test device option cannot contain commas.
 *    The 'opts' should be initialized to a default value beforehand.
 *  Returns 0 and updates the 'opts' struct on success; o/w, returns -1
 *    (writing an error message into buffer 'errbuf' of length 'errlen').
 */
    test_opt_t          opts_tmp;
    char                buf[MAX_LINE];
    char               *tok;
    const char * const  separators = ",";

    if (opts == NULL) {
        log_err(0, "parse_test_opts: opts ptr is NULL");
    }
    opts_tmp = *opts;

    if (str == NULL) {
        if ((errbuf != NULL) && (errlen > 0)) {
            snprintf(errbuf, errlen, "testopts string is NULL");
        }
        return(-1);
    }
    if (strlcpy(buf, str, sizeof(buf)) >= sizeof(buf)) {
        if ((errbuf != NULL) && (errlen > 0)) {
            snprintf(errbuf, errlen,
                "testopts string exceeds %lu-byte maximum",
                (unsigned long) sizeof(buf) - 1);
        }
        return(-1);
    }
    tok = strtok(buf, separators);
    while (tok != NULL) {
        if (process_test_opt(&opts_tmp, tok, errbuf, errlen) < 0) {
            return(-1);
        }
        tok = strtok(NULL, separators);
    }
    *opts = opts_tmp;
    return(0);
}


static int process_test_opt(
    test_opt_t *opts, const char *str, char *errbuf, int errlen)
{
/*  Parses string 'str' for a single test console device option.
 *    The string 'str' is of the form "X:VALUE", where "X" is a single-char key
 *    tag specifying the option type and "VALUE" is its corresponding value.
 *  Returns 0 and updates the 'opts' struct on success; o/w, returns -1
 *    (writing an error message into buffer 'errbuf' of length 'errlen').
 */
    char        c;
    const char *p;
    long        l;
    char       *endp;

    assert(opts != NULL);
    assert(str != NULL);

    if ((strspn(str, "BbMmNnPp") != 1) || (str[1] != ':')) {
        if ((errbuf != NULL) && (errlen > 0)) {
            snprintf(errbuf, errlen, "invalid testopts value \"%s\"", str);
        }
        return(-1);
    }
    c = toupper((int) str[0]);
    p = str + 2;
    l = strtol(p, &endp, 0);
    if ((*endp != '\0') || (errno == ERANGE) || (l < 0) || (l > INT_MAX)) {
        if ((errbuf != NULL) && (errlen > 0)) {
            snprintf(errbuf, errlen, "invalid testopts value \"%s\"", str);
        }
        return(-1);
    }
    switch (c) {
        case 'B':
            opts->numBytes = l;
            break;
        case 'M':
            opts->msecMax = l;
            break;
        case 'N':
            opts->msecMin = l;
            break;
        case 'P':
            opts->probability = MIN(l,100);
            break;
        default:
            /*  This case should never happen since the tag has already been
             *    validated above via strspn().
             */
            log_err(0, "invalid testopts tag '%c'", c);
            break;
    }
    return(0);
}


obj_t * create_test_obj(server_conf_t *conf, char *name,
    test_opt_t *opts, char *errbuf, int errlen)
{
/*  Creates a new test console device and adds it to the master objs list.
 *  Returns the new object, or NULL on error.
 */
    ListIterator i;
    obj_t *test;

    assert(conf != NULL);
    assert((name != NULL) && (name[0] != '\0'));
    assert(opts != NULL);

    /*  Check for duplicate console names.
     */
    i = list_iterator_create(conf->objs);
    while ((test = list_next(i))) {
        if (is_console_obj(test) && !strcmp(test->name, name)) {
            if ((errbuf != NULL) && (errlen > 0)) {
                snprintf(errbuf, errlen,
                    "console [%s] specifies duplicate console name", name);
            }
            break;
        }
    }
    list_iterator_destroy(i);
    if (test != NULL) {
        return(NULL);
    }
    test = create_obj(conf, name, -1, CONMAN_OBJ_TEST);
    test->aux.test.opts = *opts;
    test->aux.test.logfile = NULL;
    test->aux.test.timer = -1;
    test->aux.test.numLeft = 0;
    test->aux.test.lastChar = TEST_CONSOLE_FIRST_CHAR;
    /*
     *  Add obj to the master conf->objs list.
     */
    list_append(conf->objs, test);

    return(test);
}


int open_test_obj(obj_t *test)
{
/*  (Re)opens the specified 'test' obj.
 *  Returns 0 if the test console device is successfully opened;
 *    o/w, returns -1.
 */
    test_obj_t *auxp;
    test_opt_t *opts;

    assert(test != NULL);
    assert(is_test_obj(test));

    auxp = &test->aux.test;
    opts = &test->aux.test.opts;

    if (auxp->timer >= 0) {
        (void) tpoll_timeout_cancel(tp_global, auxp->timer);
        auxp->timer = -1;
    }
    if (test->fd >= 0) {
        tpoll_clear(tp_global, test->fd, POLLOUT);
        if (close(test->fd) < 0) {
            log_msg(LOG_WARNING,
                "Unable to close test [%s]: %s", test->name, strerror(errno));
        }
        test->fd = -1;
    }
    test->fd = open("/dev/null", O_WRONLY | O_NONBLOCK);
    if (test->fd < 0) {
        log_msg(LOG_WARNING,
            "Unable to open test [%s]: %s", test->name, strerror(errno));
        return(-1);
    }
    set_fd_nonblocking(test->fd);
    set_fd_closed_on_exec(test->fd);

    /*  Schedule immediate timer to perform initial read once in mux_io().
     */
    auxp->timer = tpoll_timeout_relative(tp_global,
        (callback_f) read_test_obj, test, 0);

    (void) opts;                /* suppress unused-but-set-variable warning */
    DPRINTF((9, "Opened [%s] test: bytes=%d max=%d min=%d prob=%d.\n",
        test->name, opts->numBytes, opts->msecMax, opts->msecMin,
        opts->probability));
    return(0);
}


int read_test_obj(obj_t *test)
{
/*  Simulates a read from the 'test' console device, and writes it out to the
 *    circular-buffer of each 'reader' obj.  If the current read does not fit
 *    within the local buffer, a timer with a delay of 0 will be scheduled to
 *    continue reading from where it left off; otherwise, a timer will be
 *    scheduled to start reading a new burst within the specified min & max.
 *  Returns the number of bytes read.
 */
    test_obj_t *auxp;
    test_opt_t *opts;
    unsigned char buf[(OBJ_BUF_SIZE / 2) - 1];
    int n = 0;
    int m;
    ListIterator i;
    obj_t *reader;
    int delay;
    int interval;

    assert(test != NULL);
    assert(is_test_obj(test));

    auxp = &test->aux.test;
    opts = &test->aux.test.opts;

    if (auxp->timer >= 0) {
        (void) tpoll_timeout_cancel(tp_global, auxp->timer);
        auxp->timer = -1;
    }
    /*  Pseudorandomly perform a read at the start of a new burst.
     *  Not truly uniform, but close enough here for integers in [0,100].
     */
    if ((auxp->numLeft > 0) || (opts->probability > rand() % 100)) {

        if (auxp->numLeft == 0) {
            auxp->numLeft = opts->numBytes;
        }
        n = MIN(auxp->numLeft, (int) sizeof(buf));

        for (m = 0; m < n; m++) {
            buf[m] = ++auxp->lastChar;
            if (auxp->lastChar == TEST_CONSOLE_LAST_CHAR) {
                auxp->lastChar = TEST_CONSOLE_FIRST_CHAR;
            }
        }
        auxp->numLeft -= n;

        i = list_iterator_create(test->readers);
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
    /*  Schedule the next timer.
     */
    if (auxp->numLeft > 0) {
        delay = 0;
    }
    else if (opts->msecMax < 0) {
        delay = TEST_CONSOLE_DEFAULT_DELAY_MSECS;
    }
    else if ((opts->msecMin < 0) || (opts->msecMin >= opts->msecMax)) {
        delay = opts->msecMax;
    }
    else {
        interval = opts->msecMax - opts->msecMin + 1;
        delay = opts->msecMin + (rand() % interval);
    }
    auxp->timer = tpoll_timeout_relative(tp_global,
        (callback_f) read_test_obj, test, delay);

    return(n);
}
