/******************************************************************************\
 *  $Id: common.c,v 1.19 2001/10/08 04:02:37 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
\******************************************************************************/


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include "common.h"
#include "errors.h"
#include "util-str.h"


char *proto_strs[] = {
/*
 *  Keep strings in sync w/ common.h:proto_toks enum.
 */
    "OK",
    "ERROR",
    "BROADCAST",
    "CODE",
    "CONNECT",
    "CONSOLE",
    "FORCE",
    "HELLO",
    "JOIN",
    "MESSAGE",
    "MONITOR",
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
    req->command = NONE;
    req->enableBroadcast = 0;
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
            err_msg(errno, "close() failed on fd=%d", req->sd);
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
    assert(fd >= 0);
    assert(tty);

    if (!isatty(fd))
        return;
    if (tcgetattr(fd, tty) < 0)
        err_msg(errno, "tcgetattr() failed on fd=%d", fd);
    return;
}


void set_tty_mode(struct termios *tty, int fd)
{
    assert(fd >= 0);
    assert(tty);

    if (!isatty(fd))
        return;
    if (tcsetattr(fd, TCSAFLUSH, tty) < 0)
        err_msg(errno, "tcgetattr() failed on fd=%d", fd);
    return;
}


void get_tty_raw(struct termios *tty, int fd)
{
    assert(tty);

    get_tty_mode(tty, fd);

    tty->c_iflag = 0;
    tty->c_oflag = 0;

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
    tty->c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    /*  read() does not return until data is present (may block indefinitely).
     */
    tty->c_cc[VMIN] = 1;
    tty->c_cc[VTIME] = 0;
    return;
}
