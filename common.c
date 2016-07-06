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
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include "common.h"
#include "log.h"
#include "util-str.h"
#include "util.h"


const char *conman_license = \
    "ConMan: The Console Manager\n"                                           \
    "https://dun.github.io/conman/\n"                                         \
    "\n"                                                                      \
    "Written by Chris Dunlap <cdunlap@llnl.gov>.\n"                           \
    "Copyright (C) 2007-2016 Lawrence Livermore National Security, LLC.\n"    \
    "Copyright (C) 2001-2007 The Regents of the University of California.\n"  \
    "\n"                                                                      \
    "ConMan is free software: you can redistribute it and/or modify it\n"     \
    "under the terms of the GNU General Public License as published by\n"     \
    "the Free Software Foundation; either version 3 of the License, or\n"     \
    "(at your option) any later version.\n"                                   \
    "\n";


char *proto_strs[] = {
/*
 *  Keep strings in sync w/ common.h:proto_toks enum.
 *  These must be sorted in a case-insensitive manner.
 */
    "BROADCAST",
    "CODE",
    "CONNECT",
    "CONSOLE",
    "ERROR",
    "FORCE",
    "HELLO",
    "JOIN",
    "MESSAGE",
    "MONITOR",
    "OK",
    "OPTION",
    "QUERY",
    "QUIET",
    "REGEX",
    "RESET",
    "TTY",
    "USER",
    NULL
};


req_t * create_req(void)
{
/*  Creates and returns a request struct.
 */
    req_t *req;

    if (!(req = malloc(sizeof(req_t))))
        out_of_memory();
    req->sd = -1;
    req->user = NULL;
    req->tty = NULL;
    req->fqdn = NULL;
    req->host = NULL;
    req->ip = NULL;
    req->port = 0;
    req->consoles = list_create((ListDelF) destroy_string);
    req->command = CONMAN_CMD_NONE;
    req->enableBroadcast = 0;
    req->enableEcho = 0;
    req->enableForce = 0;
    req->enableJoin = 0;
    req->enableQuiet = 0;
    req->enableRegex = 0;
    req->enableReset = 0;
    return(req);
}


void destroy_req(req_t *req)
{
/*  Destroys a request struct.
 */
    if (!req)
        return;

    if (req->sd >= 0) {
        if (close(req->sd) < 0)
            log_err(errno, "close() failed on fd=%d", req->sd);
        req->sd = -1;
    }
    if (req->user)
        free(req->user);
    if (req->tty)
        free(req->tty);
    if (req->fqdn)
        free(req->fqdn);
    if (req->host)
        free(req->host);
    if (req->ip)
        free(req->ip);
    if (req->consoles)
        list_destroy(req->consoles);

    free(req);
    return;
}


void get_tty_mode(struct termios *tty, int fd)
{
/*  Gets the tty values associated with 'fd' and stores them in 'tty'.
 */
    assert(fd >= 0);
    assert(tty != NULL);

    if (!isatty(fd))
        return;
    if (tcgetattr(fd, tty) < 0)
        log_err(errno, "tcgetattr() failed on fd=%d", fd);
    return;
}


void set_tty_mode(struct termios *tty, int fd)
{
/*  Sets the tty values associated with 'fd' to those stored in 'tty'.
 *
 *  FIXME: tcsetattr() returns 0 (success) if it is able to perform ANY of the
 *    requested actions.  Therefore, after calling tcsetattr(), one must call
 *    tcgetattr() and compare the actual terminal's attributes to the desired
 *    attributes to detect any differences.  (APUE2 18.4)
 */
    assert(fd >= 0);
    assert(tty != NULL);

    if (!isatty(fd))
        return;
    if (tcsetattr(fd, TCSAFLUSH, tty) < 0)
        log_err(errno, "tcgetattr() failed on fd=%d", fd);
    return;
}


void get_tty_raw(struct termios *tty, int fd)
{
/*  Gets the tty values associated with 'fd' and stores them in 'tty',
 *    adjusting them to reflect the device is operating in "raw" mode.
 *  Note that the 'fd' device is not placed in raw mode by this call;
 *    to do so, invoke set_tty_mode() with the updated termios struct.
 */
    assert(tty != NULL);

    get_tty_mode(tty, fd);

    /*  Disable SIGINT on BREAK, CR-to-NL, input parity check,
     *    stripping 8th bit on input, and output flow control.
     */
    tty->c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

    /*  Disable output processing.
     */
    tty->c_oflag &= ~(OPOST);

    /*  Set 8 bits/char.
     */
    tty->c_cflag &= ~CSIZE;
    tty->c_cflag |= CS8;

    /*  Disable parity checking.
     */
    tty->c_cflag &= ~PARENB;

    /*  Ignore modem status lines for locally attached device.
     */
    tty->c_cflag |= CLOCAL;

    /*  Disable echo, canonical mode, extended input processing, signal chars.
     */
    tty->c_lflag &= ~(ECHO | ECHOCTL | ICANON | IEXTEN | ISIG);

    /*  read() does not return until data is present (may block indefinitely).
     */
    tty->c_cc[VMIN] = 1;
    tty->c_cc[VTIME] = 0;
    return;
}
