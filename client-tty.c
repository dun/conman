/******************************************************************************\
 *  $Id: client-tty.c,v 1.26 2001/08/02 23:37:23 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
\******************************************************************************/


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>
#include "common.h"
#include "client.h"
#include "errors.h"
#include "util.h"


static void exit_handler(int signum);
static void set_raw_tty_mode(int fd, struct termios *old);
static void restore_tty_mode(int fd, struct termios *new);
static int read_from_stdin(client_conf_t *conf);
static int write_to_stdout(client_conf_t *conf);
static int send_esc_seq(client_conf_t *conf, char c);
static int perform_close_esc(client_conf_t *conf, char c);
static int perform_info_esc(client_conf_t *conf, char c);
static int perform_help_esc(client_conf_t *conf, char c);
static int perform_suspend_esc(client_conf_t *conf, char c);
static void locally_echo_esc(char e, char c);
static void locally_display_status(client_conf_t *conf, char *msg);


static int done = 0;


void connect_console(client_conf_t *conf)
{
/*  Connect client to the remote console(s).
 */
    fd_set rset, rsetBak;
    int n;

    assert(conf->req->sd >= 0);
    assert((conf->req->command == CONNECT) || (conf->req->command == MONITOR));

    if (!isatty(STDIN_FILENO))
        err_msg(0, "Standard Input is not a terminal device.");
    if (!isatty(STDOUT_FILENO))
        err_msg(0, "Standard Output is not a terminal device.");

    Signal(SIGHUP, SIG_IGN);
    Signal(SIGINT, SIG_IGN);
    Signal(SIGQUIT, SIG_IGN);
    Signal(SIGPIPE, SIG_IGN);
    Signal(SIGTSTP, SIG_DFL);
    Signal(SIGTERM, exit_handler);

    set_raw_tty_mode(STDIN_FILENO, &conf->term);

    locally_display_status(conf, "opened");

    FD_ZERO(&rsetBak);
    FD_SET(STDIN_FILENO, &rsetBak);
    FD_SET(conf->req->sd, &rsetBak);

    while (!done) {
        rset = rsetBak;
        while ((n = select(conf->req->sd+1, &rset, NULL, NULL, NULL)) <= 0) {
            if (errno != EINTR)
                err_msg(errno, "select() failed");
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
        err_msg(errno, "close() failed on fd=%d", conf->req->sd);
    conf->req->sd = -1;

    if (!conf->closedByClient)
        locally_display_status(conf, "terminated by peer");

    restore_tty_mode(STDIN_FILENO, &conf->term);
    return;
}


static void exit_handler(int signum)
{
/*  Exit-handler to break out of while-loop in connect_console().
 */
    done = 1;
    return;
}


static void set_raw_tty_mode(int fd, struct termios *old)
{
    struct termios term;

    assert(fd >= 0);

    if (!isatty(fd))
        return;
    if (tcgetattr(fd, &term) < 0)
        err_msg(errno, "tcgetattr() failed on fd=%d", fd);
    if (old)
        *old = term;

    /*  disable SIGINT on break, CR-to-NL, input parity checking,
     *    stripping 8th bit off input chars, output flow ctrl
     */
    term.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

    /*  disable output processing
     */
    term.c_oflag &= ~(OPOST);

    /*  disable echo, canonical mode, extended input processing, signal chars
     */
    term.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    /*  read() does not return until data is present (may block indefinitely)
     */
    term.c_cc[VMIN] = 1;
    term.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &term) < 0)
        err_msg(errno, "tcgetattr() failed on fd=%d", fd);
    return;
}


static void restore_tty_mode(int fd, struct termios *new)
{
    assert(fd >= 0);
    assert(new);

    if (!isatty(fd))
        return;
    if (tcsetattr(fd, TCSANOW, new) < 0)
        err_msg(errno, "tcgetattr() failed on fd=%d", fd);
    return;
}


static int read_from_stdin(client_conf_t *conf)
{
/*  Read from stdin and write to socket connection.
 *  Returns 1 if the read was successful,
 *    or 0 if the connection has been closed.
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
            err_msg(errno, "read() failed on fd=%d", conf->req->sd);
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
            return(send_esc_seq(conf, c));
        case ESC_CHAR_CLOSE:
            return(perform_close_esc(conf, c));
        case ESC_CHAR_INFO:
            return(perform_info_esc(conf, c));
        case ESC_CHAR_HELP:
            return(perform_help_esc(conf, c));
        case ESC_CHAR_LOG:
            return(send_esc_seq(conf, c));
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
    assert((p > buf) && ((p - buf) <= sizeof(buf)));

    /*  Do not send chars across the socket if we are in MONITOR mode.
     *    The server would discard them anyways, but why waste resources.
     *  Besides, we're now practicing conservation here in California. ;)
     */
    if (conf->req->command == CONNECT) {
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
            err_msg(errno, "write() failed on fd=%d", conf->req->sd);
        }
    }
    return(1);
}


static int write_to_stdout(client_conf_t *conf)
{
/*  Read from socket connection and write to stdout.
 *  Returns the number of bytes written to stdout,
 *    or 0 if the socket connection has been closed.
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
            err_msg(errno, "read() failed on fd=%d", conf->req->sd);
    }
    if (n > 0) {
        if (write_n(STDOUT_FILENO, buf, n) < 0)
            err_msg(errno, "write() failed on fd=%d", STDOUT_FILENO);
        if (conf->logd >= 0)
            if (write_n(conf->logd, buf, n) < 0)
                err_msg(errno, "write() failed on fd=%d", conf->logd);
    }
    return(n);
}


static int send_esc_seq(client_conf_t *conf, char c)
{
/*  Transmit an escape sequence to the server.
 *  Returns 1 on success, or 0 if the socket connection has been closed.
 */
    unsigned char buf[2];

    buf[0] = ESC_CHAR;
    buf[1] = c;

    if (write_n(conf->req->sd, buf, sizeof(buf)) < 0) {
        if (errno == EPIPE)
            return(0);
        err_msg(errno, "write() failed on fd=%d", STDOUT_FILENO);
    }
    return(1);
}


static int perform_close_esc(client_conf_t *conf, char c)
{
/*  Perform a client-initiated close.
 *  Returns 1 on success.
 */
    locally_echo_esc(conf->escapeChar, c);

    locally_display_status(conf, "closed");

    if (shutdown(conf->req->sd, SHUT_WR) < 0)
        err_msg(errno, "shutdown() failed on fd=%d", conf->req->sd);

    conf->closedByClient = 1;
    return(1);
}


static int perform_info_esc(client_conf_t *conf, char c)
{
/*  Display information about the current connection.
 *  Returns 1 on success.
 */
    char *str;

    if (list_count(conf->req->consoles) == 1) {
        str = create_fmt_string("%sConnected to console [%s] on %s:%d%s",
            CONMAN_MSG_PREFIX, (char *) list_peek(conf->req->consoles),
            conf->req->host, conf->req->port, CONMAN_MSG_SUFFIX);
    }
    else {
        str = create_fmt_string("%sBroadcasting to %d consoles on %s:%d%s",
            CONMAN_MSG_PREFIX, list_count(conf->req->consoles),
            conf->req->host, conf->req->port, CONMAN_MSG_SUFFIX);
    }
    if (write_n(STDOUT_FILENO, str, strlen(str)) < 0)
        err_msg(errno, "write() failed on fd=%d", STDOUT_FILENO);
    free(str);
    return(1);
}


static int perform_help_esc(client_conf_t *conf, char c)
{
/*  Display the "escape sequence" help.
 *  Returns 1 on success.
 */
    char escChar[3];
    char escBreak[3];
    char escClose[3];
    char escHelp[3];
    char escInfo[3];
    char escLog[3];
    char escSuspend[3];
    char *str;

    locally_echo_esc(conf->escapeChar, c);

    write_esc_char(conf->escapeChar, escChar);
    write_esc_char(ESC_CHAR_BREAK, escBreak);
    write_esc_char(ESC_CHAR_CLOSE, escClose);
    write_esc_char(ESC_CHAR_HELP, escHelp);
    write_esc_char(ESC_CHAR_INFO, escInfo);
    write_esc_char(ESC_CHAR_LOG, escLog);
    write_esc_char(ESC_CHAR_SUSPEND, escSuspend);

    str = create_fmt_string(
        "\r\nSupported ConMan Escape Sequences:\r\n"
        "  %2s%-2s -  Display this help message.\r\n"
        "  %2s%-2s -  Terminate the connection.\r\n"
        "  %2s%-2s -  Send the escape character by typing it twice.\r\n"
        "  %2s%-2s -  Transmit a serial-break.\r\n"
        "  %2s%-2s -  Display connection information.\r\n"
        "  %2s%-2s -  Replay up to the last %d bytes of the log.\r\n"
        "  %2s%-2s -  Suspend the client.\r\n",
        escChar, escHelp, escChar, escClose, escChar, escChar,
        escChar, escBreak, escChar, escInfo, escChar, escLog,
        CONMAN_REPLAY_LEN, escChar, escSuspend);
    if (write_n(STDOUT_FILENO, str, strlen(str)) < 0)
        err_msg(errno, "write() failed on fd=%d", STDOUT_FILENO);
    free(str);
    return(1);
}


static int perform_suspend_esc(client_conf_t *conf, char c)
{
/*  Suspend the client.  While suspended, anything sent by the server
 *    will be buffered by the network layer until the client is resumed.
 *    Once the network buffers are full, the server should overwrite
 *    data to this client in its circular write buffer.
 *  Returns 1 on success, or 0 if the socket connection has been closed.
 */
    locally_echo_esc(conf->escapeChar, c);

    if (!send_esc_seq(conf, c))
        return(0);
    locally_display_status(conf, "suspended");
    restore_tty_mode(STDIN_FILENO, &conf->term);

    if (kill(getpid(), SIGTSTP) < 0)
        err_msg(errno, "Unable to suspend client (pid %d)", getpid());

    set_raw_tty_mode(STDIN_FILENO, &conf->term);
    locally_display_status(conf, "resumed");
    if (!send_esc_seq(conf, c))
        return(0);
    return(1);
}


static void locally_echo_esc(char e, char c)
{
/*  Locally echo an escape character sequence on stdout.
 */
    char buf[4];
    char *p = buf;

    p = write_esc_char(e, p);
    p = write_esc_char(c, p);

    assert((p - buf) <= sizeof(buf));

    if (write_n(STDOUT_FILENO, buf, p - buf) < 0)
        err_msg(errno, "write() failed on fd=%d", STDOUT_FILENO);
    return;
}


char * write_esc_char(char c, char *dst)
{
/*  Writes the escape character (c) into the buffer pointed to by (dst).
 *  Returns a ptr to the char following the escape char in the buffer.
 *  (cf. Stevens UNP p638).
 */
    assert(dst);

    c &= 0x7F;

    /*  Echo ASCII ctrl-chars as a caret followed by the uppercase char.
     */
    if (c < 0x20) {
        *dst++ = '^';
        *dst++ = c + '@';
    }
    else if (c == 0x7F) {		/* ASCII DEL char */
        *dst++ = '^';
        *dst++ = '?';
    }
    else {
        *dst++ = c;
    }

    *dst = '\0';			/* DO NOT advance ptr here */
    return(dst);
}


static void locally_display_status(client_conf_t *conf, char *msg)
{
/*  Displays (msg) regarding the state of the console connection.
 */
    char buf[MAX_LINE];
    char *ptr = buf;
    int len = sizeof(buf);
    int n;
    int overflow = 0;

    assert((conf->req->command == CONNECT) || (conf->req->command == MONITOR));

    if (list_count(conf->req->consoles) == 1) {
        n = snprintf(ptr, len, "%sConnection to console [%s] %s%s",
            CONMAN_MSG_PREFIX, (char *) list_peek(conf->req->consoles),
            msg, CONMAN_MSG_SUFFIX);
    }
    else {
        n = snprintf(ptr, len, "%sBroadcast to %d consoles %s%s",
            CONMAN_MSG_PREFIX, list_count(conf->req->consoles),
            msg, CONMAN_MSG_SUFFIX);
    }
    if (n < 0 || n >= len)
        overflow = 1;
    else
        ptr += n, len -= n;

    if (overflow) {
        ptr = buf + sizeof(buf) - 3;
        snprintf(ptr, 3, "\r\n");
    }
    if (write_n(STDOUT_FILENO, buf, strlen(buf)) < 0)
        err_msg(errno, "write() failed on fd=%d", STDOUT_FILENO);
    return;
}
