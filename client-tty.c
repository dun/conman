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
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>
#include "client.h"
#include "common.h"
#include "log.h"
#include "util-file.h"
#include "util-str.h"
#include "util.h"


static void exit_handler(int signum);
static int read_from_stdin(client_conf_t *conf);
static int write_to_stdout(client_conf_t *conf);
static int send_esc_seq(client_conf_t *conf, char c);
static int perform_break_esc(client_conf_t *conf, char c);
static int perform_close_esc(client_conf_t *conf, char c);
static int perform_del_esc(client_conf_t *conf, char c);
static int perform_echo_esc(client_conf_t *conf, char c);
static int perform_force_esc(client_conf_t *conf, char c);
static int perform_help_esc(client_conf_t *conf, char c);
static int perform_info_esc(client_conf_t *conf, char c);
static int perform_join_esc(client_conf_t *conf, char c);
static int perform_log_replay_esc(client_conf_t *conf, char c);
static int perform_monitor_esc(client_conf_t *conf, char c);
static int perform_quiet_esc(client_conf_t *conf, char c);
static int perform_reset_esc(client_conf_t *conf, char c);
static int perform_suspend_esc(client_conf_t *conf, char c);
static void locally_echo_esc(char e, char c);
static void locally_display_status(client_conf_t *conf, char *msg);


static int done = 0;


void connect_console(client_conf_t *conf)
{
/*  Connects the client to the remote console(s).
 */
    struct termios tty;
    fd_set rset, rsetBak;
    int n;

    assert(conf->req->sd >= 0);
    assert((conf->req->command == CONMAN_CMD_CONNECT)
        || (conf->req->command == CONMAN_CMD_MONITOR));
    assert(list_count(conf->req->consoles) > 0);

    /*  If only one console was selected for a broadcast, then
     *    the session is placed into R/W mode instead of W/O mode.
     *    So update the req accordingly.
     */
    if (list_count(conf->req->consoles) == 1)
        conf->req->enableBroadcast = 0;

    if (!isatty(STDIN_FILENO))
        log_err(0, "Standard Input is not a terminal device");
    if (!isatty(STDOUT_FILENO))
        log_err(0, "Standard Output is not a terminal device");

    posix_signal(SIGHUP, SIG_IGN);
    posix_signal(SIGINT, SIG_IGN);
    posix_signal(SIGPIPE, SIG_IGN);
    posix_signal(SIGQUIT, SIG_IGN);
    posix_signal(SIGTERM, exit_handler);
    posix_signal(SIGTSTP, SIG_DFL);

    get_tty_mode(&conf->tty, STDIN_FILENO);
    get_tty_raw(&tty, STDIN_FILENO);
    set_tty_mode(&tty, STDIN_FILENO);

    locally_display_status(conf, "opened");

    FD_ZERO(&rsetBak);
    FD_SET(STDIN_FILENO, &rsetBak);
    FD_SET(conf->req->sd, &rsetBak);

    while (!done) {
        rset = rsetBak;
        while ((n = select(conf->req->sd+1, &rset, NULL, NULL, NULL)) <= 0) {
            if (errno != EINTR)
                log_err(errno, "Unable to multiplex I/O");
            else if (done)
                /* i need a */ break;
        }
        if (n <= 0)
            /* should i */ continue;

        if (FD_ISSET(STDIN_FILENO, &rset)) {
            if (!read_from_stdin(conf))
                done = 1;
        }
        if (FD_ISSET(conf->req->sd, &rset)) {
            if (!write_to_stdout(conf))
                done = 1;
        }
    }

    if (close(conf->req->sd) < 0)
        log_err(errno, "Unable to close connection to <%s:%d>",
            conf->req->host, conf->req->port);
    conf->req->sd = -1;

    if (!conf->isClosedByClient)
        locally_display_status(conf, "terminated by server");

    set_tty_mode(&conf->tty, STDIN_FILENO);
    return;
}


static void exit_handler(int signum)
{
/*  Exit-handler to break out of while-loop in connect_console().
 */
    done = 1;
    return;
}


static int read_from_stdin(client_conf_t *conf)
{
/*  Reads from stdin and writes to the socket connection.
 *  Returns 1 if the read was successful,
 *    or 0 if the connection is to be closed.
 *  Note that this routine can conceivably block in the write() to the socket.
 */
    static enum { CHR, EOL, ESC } mode = EOL;
    int n;
    unsigned char c;
    char esc = conf->escapeChar;
    unsigned char buf[4];
    unsigned char *p = buf;
    unsigned char *q, *r;

    while ((n = read(STDIN_FILENO, &c, 1)) < 0) {
        if (errno != EINTR)
            log_err(errno, "Unable to read from stdin");
    }
    if (n == 0)
        return(0);

    if ((mode != ESC) && (c == esc)) {
        mode = ESC;
        return(1);
    }
    if (mode == ESC) {
        mode = EOL;
        switch(c) {
        case ESC_CHAR_BREAK:
            return(perform_break_esc(conf, c));
        case ESC_CHAR_CLOSE:
            return(perform_close_esc(conf, c));
        case ESC_CHAR_DEL:              /* XXX: gnats:100 del char kludge */
            return(perform_del_esc(conf, c));
        case ESC_CHAR_ECHO:
            return(perform_echo_esc(conf, c));
        case ESC_CHAR_FORCE:
            return(perform_force_esc(conf, c));
        case ESC_CHAR_HELP:
            return(perform_help_esc(conf, c));
        case ESC_CHAR_INFO:
            return(perform_info_esc(conf, c));
        case ESC_CHAR_JOIN:
            return(perform_join_esc(conf, c));
        case ESC_CHAR_REPLAY:
            return(perform_log_replay_esc(conf, c));
        case ESC_CHAR_MONITOR:
            return(perform_monitor_esc(conf, c));
        case ESC_CHAR_QUIET:
            return(perform_quiet_esc(conf, c));
        case ESC_CHAR_RESET:
            return(perform_reset_esc(conf, c));
        case ESC_CHAR_SUSPEND:
            return(perform_suspend_esc(conf, c));
        }
        if (c != esc) {
            /*
             *  If the input was escape-someothercharacter, write both the
             *    escape character and the other character to the socket.
             *  Just write the escape character here, since the default case
             *    for writing the other character is a few lines further down.
             */
            *p++ = esc;
        }
    }
    if ((c == '\r') || (c == '\n'))
        mode = EOL;
    else
        mode = CHR;

    *p++ = c;
    assert((p > buf) && ((size_t) (p - buf) <= sizeof(buf)));

    /*  Do not send chars across the socket if we are in MONITOR mode.
     *    The server would discard them anyways, but why waste resources.
     *  Besides, we're now practicing conservation here in California. ;)
     */
    if (conf->req->command == CONMAN_CMD_CONNECT) {
        /*
         *  Perform character-stuffing of the escape-sequence character
         *    by doubling all occurrences of it.
         */
        for (q=p-1; q>=buf; q--) {
            if (*q == ESC_CHAR) {
                for (r=p-1, p++; r>=q; r--)
                    *(r+1) = *r;
            }
        }
        if (write_n(conf->req->sd, buf, p - buf) < 0) {
            if (errno == EPIPE)
                return(0);
            log_err(errno, "Unable to write to <%s:%d>",
                conf->req->host, conf->req->port);
        }
    }
    return(1);
}


static int write_to_stdout(client_conf_t *conf)
{
/*  Reads from the socket connection and writes to stdout.
 *  Returns the number of bytes written to stdout,
 *    or 0 if the socket connection is to be closed.
 */
    unsigned char buf[MAX_BUF_SIZE];
    int n;

    /*  Stdin has to be processed character-by-character to check for
     *    escape sequences.  For human input, this isn't too bad.
     */
    while ((n = read(conf->req->sd, buf, sizeof(buf))) < 0) {
        if (errno == EPIPE)
            return(0);
        if (errno != EINTR)
            log_err(errno, "Unable to read from <%s:%d>",
                conf->req->host, conf->req->port);
    }
    if (n > 0) {
        if (write_n(STDOUT_FILENO, buf, n) < 0)
            log_err(errno, "Unable to write to stdout");
        if (conf->logd >= 0)
            if (write_n(conf->logd, buf, n) < 0)
                log_err(errno, "Unable to write to \"%s\"", conf->log);
    }
    return(n);
}


static int send_esc_seq(client_conf_t *conf, char c)
{
/*  Transmits an escape sequence to the server.
 *  Returns 1 on success, or 0 if the socket connection is to be closed.
 */
    unsigned char buf[2];

    buf[0] = ESC_CHAR;
    buf[1] = c;

    if (write_n(conf->req->sd, buf, sizeof(buf)) < 0) {
        if (errno == EPIPE)
            return(0);
        log_err(errno, "Unable to write to <%s:%d>",
            conf->req->host, conf->req->port);
    }
    return(1);
}


static int perform_break_esc(client_conf_t *conf, char c)
{
/*  Transmits a serial-break to all writable consoles connected to the client.
 *  Returns 1 on success, or 0 if the socket connection is to be closed.
 */
    if (conf->req->command != CONMAN_CMD_CONNECT)
        return(1);

    return(send_esc_seq(conf, c));
}


static int perform_close_esc(client_conf_t *conf, char c)
{
/*  Performs a client-initiated close.
 *  Returns 1 on success, or 0 if the socket connection is to be closed.
 */
    locally_echo_esc(conf->escapeChar, c);

    locally_display_status(conf, "closed");

    if (shutdown(conf->req->sd, SHUT_WR) < 0) {
        log_err(errno, "Unable to shutdown connection to <%s:%d>",
            conf->req->host, conf->req->port);
    }
    conf->isClosedByClient = 1;
    return(1);
}


static int perform_del_esc(client_conf_t *conf, char c)
{
/*  Transmits a DEL char to all writable consoles connected to the client.
 *  This is somewhat necessary because some terminals remap the DEL key
 *    (0x7f) into an ANSI escape character sequence (0x1b5b337e).
 *  Returns 1 on success, or 0 if the socket connection is to be closed.
 *
 *  XXX: gnats:100 del char kludge
 */
    if (conf->req->command != CONMAN_CMD_CONNECT)
        return(1);

    return(send_esc_seq(conf, c));
}


static int perform_echo_esc(client_conf_t *conf, char c)
{
/*  Toggles whether the client echoes its standard input.
 *  Returns 1 on success, or 0 if the socket connection is to be closed.
 */
    struct termios tty;

    if (conf->req->command != CONMAN_CMD_CONNECT)
        return(1);
    get_tty_mode(&tty, STDIN_FILENO);
    tty.c_lflag ^= ECHO;
    set_tty_mode(&tty, STDIN_FILENO);
    conf->req->enableEcho ^= 1;
    return(1);
}


static int perform_force_esc(client_conf_t *conf, char c)
{
/*  Changes a R/O session into a R/W session by forcibly stealing the console
 *    from existing console writers.
 *  Returns 1 on success, or 0 if the socket connection is to be closed.
 */
    if (conf->req->command != CONMAN_CMD_MONITOR)
        return(1);
    assert(!conf->req->enableBroadcast);

    conf->req->command = CONMAN_CMD_CONNECT;
    conf->req->enableForce = 1;
    conf->req->enableJoin = 0;
    return(send_esc_seq(conf, c));
}


static int perform_help_esc(client_conf_t *conf, char c)
{
/*  Displays a dynamic help message regarding the available escape sequences.
 *  Returns 1 on success, or 0 if the socket connection is to be closed.
 */
    char buf[MAX_BUF_SIZE] = "";        /* init buf for appending with NUL */
    char esc[3];
    char tmp[3];
    int n;

    write_esc_char(conf->escapeChar, esc);
    (void) append_format_string(buf, sizeof(buf),
        "\r\nSupported ConMan Escape Sequences:\r\n");

    write_esc_char(ESC_CHAR_HELP, tmp);
    (void) append_format_string(buf, sizeof(buf),
        "  %2s%-2s -  Display this help message.\r\n", esc, tmp);

    write_esc_char(ESC_CHAR_CLOSE, tmp);
    (void) append_format_string(buf, sizeof(buf),
        "  %2s%-2s -  Terminate the connection.\r\n", esc, tmp);

    (void) append_format_string(buf, sizeof(buf),
        "  %2s%-2s -  Send the escape character.\r\n", esc, esc);

    if (conf->req->command == CONMAN_CMD_CONNECT) {
        write_esc_char(ESC_CHAR_BREAK, tmp);
        (void) append_format_string(buf, sizeof(buf),
            "  %2s%-2s -  Transmit a serial-break.\r\n", esc, tmp);
    }

    if (conf->req->command == CONMAN_CMD_CONNECT) {
        write_esc_char(ESC_CHAR_DEL, tmp);
        (void) append_format_string(buf, sizeof(buf),
            "  %2s%-2s -  Transmit a DEL character.\r\n", esc, tmp);
    }

    if (conf->req->command == CONMAN_CMD_CONNECT) {
        write_esc_char(ESC_CHAR_ECHO, tmp);
        (void) append_format_string(buf, sizeof(buf),
            "  %2s%-2s -  %s echoing of client input.\r\n", esc, tmp,
            conf->req->enableEcho ? "Disable" : "Enable");
    }

    if (conf->req->command == CONMAN_CMD_MONITOR) {
        write_esc_char(ESC_CHAR_FORCE, tmp);
        (void) append_format_string(buf, sizeof(buf),
            "  %2s%-2s -  Force write-privileges (console-stealing).\r\n",
            esc, tmp);
    }

    write_esc_char(ESC_CHAR_INFO, tmp);
    (void) append_format_string(buf, sizeof(buf),
        "  %2s%-2s -  Display connection information.\r\n", esc, tmp);

    if (conf->req->command == CONMAN_CMD_MONITOR) {
        write_esc_char(ESC_CHAR_JOIN, tmp);
        (void) append_format_string(buf, sizeof(buf),
            "  %2s%-2s -  Join write-privileges (console-sharing).\r\n",
            esc, tmp);
    }

    /*  FIXME: Only display this option if the console is being logged.
     */
    if (!conf->req->enableBroadcast) {
        write_esc_char(ESC_CHAR_REPLAY, tmp);
        (void) append_format_string(buf, sizeof(buf),
            "  %2s%-2s -  Replay up to the last %d bytes of the log.\r\n",
            esc, tmp, LOG_REPLAY_LEN);
    }

    if ((conf->req->command == CONMAN_CMD_CONNECT) &&
        (!conf->req->enableBroadcast)
       ) {
        write_esc_char(ESC_CHAR_MONITOR, tmp);
        (void) append_format_string(buf, sizeof(buf),
            "  %2s%-2s -  Monitor without write-privileges (read-only).\r\n",
            esc, tmp);
    }

    write_esc_char(ESC_CHAR_QUIET, tmp);
    if (conf->req->enableQuiet) {
        (void) append_format_string(buf, sizeof(buf), "  %2s%-2s -  "
            "Disable quiet-mode (display info msgs).\r\n", esc, tmp);
    }
    else {
        (void) append_format_string(buf, sizeof(buf), "  %2s%-2s -  "
            "Enable quiet-mode (suppress info msgs).\r\n", esc, tmp);
    }

    if ((conf->req->command == CONMAN_CMD_CONNECT) &&
        (conf->req->enableReset)
       ) {
        write_esc_char(ESC_CHAR_RESET, tmp);
        (void) append_format_string(buf, sizeof(buf), "  %2s%-2s -  "
            "Reset node%s associated with this console.\r\n", esc, tmp,
            (list_count(conf->req->consoles) == 1 ? "" : "s"));
    }

    /*  Store the retval of the last write into 'buf' to check for buffer
     *    truncation as well as the final string length to write via write_n().
     */
    write_esc_char(ESC_CHAR_SUSPEND, tmp);
    n = append_format_string(buf, sizeof(buf),
        "  %2s%-2s -  Suspend the client.\r\n", esc, tmp);

    /*  If writes into 'buf' have become truncated at any point, the final
     *    call to append_format_string() will still return -1.  If truncation
     *    has occurred, ensure the buffer is terminated with a CR/LF and write
     *    as much of the help message that the local buffer will allow.
     */
    if (n == -1) {
        const char * const end_str = "+\r\n";
        const size_t end_len = strlen(end_str) + 1;
        assert(sizeof(buf) >= end_len);
        strncpy(&buf[sizeof(buf) - end_len], end_str, end_len);
        n = sizeof(buf) - 1;
    }
    assert((size_t) n == strlen(buf));
    if (write_n(STDOUT_FILENO, buf, n) < 0) {
        log_err(errno, "Unable to write to stdout");
    }
    return(1);
}


static int perform_info_esc(client_conf_t *conf, char c)
{
/*  Displays information about the current connection.
 *  Returns 1 on success, or 0 if the socket connection is to be closed.
 */
    char *str;

    if (list_count(conf->req->consoles) == 1) {
        str = create_format_string(
            "%sConnected %s to console [%s] on <%s:%d>%s", CONMAN_MSG_PREFIX,
            (conf->req->command == CONMAN_CMD_MONITOR ? "R/O" : "R/W"),
            (char *) list_peek(conf->req->consoles),
            conf->req->host, conf->req->port, CONMAN_MSG_SUFFIX);
    }
    else {
        str = create_format_string(
            "%sBroadcasting to %d consoles on <%s:%d>%s",
            CONMAN_MSG_PREFIX, list_count(conf->req->consoles),
            conf->req->host, conf->req->port, CONMAN_MSG_SUFFIX);
    }
    if (write_n(STDOUT_FILENO, str, strlen(str)) < 0)
        log_err(errno, "Unable to write to stdout");
    free(str);
    return(1);
}


static int perform_join_esc(client_conf_t *conf, char c)
{
/*  Changes a R/O session into a R/W session by joining with existing
 *    console writers.
 *  Returns 1 on success, or 0 if the socket connection is to be closed.
 */
    if (conf->req->command != CONMAN_CMD_MONITOR)
        return(1);
    assert(!conf->req->enableBroadcast);

    conf->req->command = CONMAN_CMD_CONNECT;
    conf->req->enableForce = 0;
    conf->req->enableJoin = 1;
    return(send_esc_seq(conf, c));
}


static int perform_log_replay_esc(client_conf_t *conf, char c)
{
/*  Requests the server to replay the log of the connected console.
 *  Returns 1 on success, or 0 if the socket connection is to be closed.
 */
    if (conf->req->enableBroadcast)
        return(1);
    assert(list_count(conf->req->consoles) == 1);

    return(send_esc_seq(conf, c));
}


static int perform_monitor_esc(client_conf_t *conf, char c)
{
/*  Changes a R/W session into a R/O session by dropping write-privileges
 *    from the console.
 *  Returns 1 on success, or 0 if the socket connection is to be closed.
 */
    if (conf->req->command != CONMAN_CMD_CONNECT)
        return(1);
    if (conf->req->enableBroadcast)
        return(1);

    conf->req->command = CONMAN_CMD_MONITOR;
    conf->req->enableForce = 0;
    conf->req->enableJoin = 0;
    return(send_esc_seq(conf, c));
}


static int perform_quiet_esc(client_conf_t *conf, char c)
{
/*  Toggles whether the client connection is "quiet" (ie, whether or not
 *    informational messages will be displayed).
 *  Returns 1 on success, or 0 if the socket connection is to be closed.
 */
    conf->req->enableQuiet ^= 1;
    return(send_esc_seq(conf, c));
}


static int perform_reset_esc(client_conf_t *conf, char c)
{
/*  Transmits a reset request to all writable consoles connected to the client.
 *  Returns 1 on success, or 0 if the socket connection is to be closed.
 */
    if ((conf->req->command != CONMAN_CMD_CONNECT)
      || (!conf->req->enableReset))
        return(1);

    return(send_esc_seq(conf, c));
}


static int perform_suspend_esc(client_conf_t *conf, char c)
{
/*  Suspends the client.  While suspended, anything sent by the server
 *    will be buffered by the network layer until the client is resumed.
 *    Once the network buffers are full, the server should overwrite data
 *    to this client in its circular write buffer.
 *  Returns 1 on success, or 0 if the socket connection is to be closed.
 */
    struct termios tty;

    locally_echo_esc(conf->escapeChar, c);

    if (!send_esc_seq(conf, c))
        return(0);
    locally_display_status(conf, "suspended");
    set_tty_mode(&conf->tty, STDIN_FILENO);

    if (kill(getpid(), SIGTSTP) < 0)
        log_err(errno, "Unable to suspend client (pid %d)", (int) getpid());

    get_tty_raw(&tty, STDIN_FILENO);
    set_tty_mode(&tty, STDIN_FILENO);
    locally_display_status(conf, "resumed");
    if (!send_esc_seq(conf, c))
        return(0);
    return(1);
}


static void locally_echo_esc(char e, char c)
{
/*  Locally echoes an escape character sequence on stdout.
 */
    char buf[4];
    char *p = buf;

    p = write_esc_char(e, p);
    p = write_esc_char(c, p);

    assert((p > buf) && ((size_t) (p - buf) <= sizeof(buf)));

    if (write_n(STDOUT_FILENO, buf, p - buf) < 0)
        log_err(errno, "Unable to write to stdout");
    return;
}


char * write_esc_char(char c, char *dst)
{
/*  Writes the escape character (c) into the buffer pointed to by (dst).
 *  Returns a ptr to the char following the escape char in the buffer.
 *  (cf. Stevens UNP p638).
 */
    assert(dst != NULL);

    c &= 0x7F;

    /*  Echo ASCII ctrl-chars as a caret followed by the uppercase char.
     */
    if (c < 0x20) {
        *dst++ = '^';
        *dst++ = c + '@';
    }
    else if (c == 0x7F) {               /* ASCII DEL char */
        *dst++ = '^';
        *dst++ = '?';
    }
    else {
        *dst++ = c;
    }

    *dst = '\0';                        /* DO NOT advance ptr here */
    return(dst);
}


static void locally_display_status(client_conf_t *conf, char *msg)
{
/*  Displays (msg) regarding the state of the console connection.
 */
    char buf[MAX_LINE];
    int n;

    assert((conf->req->command == CONMAN_CMD_CONNECT)
        || (conf->req->command == CONMAN_CMD_MONITOR));

    if (list_count(conf->req->consoles) == 1) {
        n = snprintf(buf, sizeof(buf), "%sConnection to console [%s] %s%s",
            CONMAN_MSG_PREFIX, (char *) list_peek(conf->req->consoles),
            msg, CONMAN_MSG_SUFFIX);
    }
    else {
        n = snprintf(buf, sizeof(buf), "%sBroadcast to %d consoles %s%s",
            CONMAN_MSG_PREFIX, list_count(conf->req->consoles),
            msg, CONMAN_MSG_SUFFIX);
    }

    if ((n < 0) || ((size_t) n >= sizeof(buf)))
        strcpy(&buf[sizeof(buf) - 3], "\r\n");
    if (write_n(STDOUT_FILENO, buf, strlen(buf)) < 0)
        log_err(errno, "Unable to write to stdout");
    return;
}
