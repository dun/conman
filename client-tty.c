/******************************************************************************\
 *  $Id: client-tty.c,v 1.11 2001/05/24 20:56:08 dun Exp $
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
#include "conman.h"
#include "client.h"
#include "errors.h"
#include "util.h"


#define ESC_CHAR_CLOSE		'.'
#define ESC_CHAR_HELP		'?'
#define ESC_CHAR_SUSPEND	'Z'


static void exit_handler(int signum);
static void set_raw_tty_mode(int fd, struct termios *old);
static void restore_tty_mode(int fd, struct termios *new);
static int read_from_stdin(client_conf_t *conf);
static int write_to_stdout(client_conf_t *conf);
static void perform_close_esc(client_conf_t *conf, char c);
static void perform_help_esc(client_conf_t *conf, char c);
static void perform_suspend_esc(client_conf_t *conf, char c);
static void locally_echo_esc(char e, char c);
static void display_connection_msg(client_conf_t *conf, char *msg);


static int done = 0;


void connect_console(client_conf_t *conf)
{
/*  Connect client to the remote console(s).
 */
    fd_set rset, rsetBak;
    int n;

    assert(conf->sd >= 0);
    assert((conf->command == CONNECT) || (conf->command == MONITOR));

    Signal(SIGHUP, SIG_IGN);
    Signal(SIGINT, SIG_IGN);
    Signal(SIGQUIT, SIG_IGN);
    Signal(SIGPIPE, SIG_IGN);
    Signal(SIGTSTP, SIG_DFL);
    Signal(SIGTERM, exit_handler);

    set_raw_tty_mode(STDIN_FILENO, &conf->term);

    display_connection_msg(conf, "opened");

    FD_ZERO(&rsetBak);
    FD_SET(STDIN_FILENO, &rsetBak);
    FD_SET(conf->sd, &rsetBak);

    while (!done) {
        rset = rsetBak;
        while ((n = select(conf->sd+1, &rset, NULL, NULL, NULL)) <= 0) {
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
        if (FD_ISSET(conf->sd, &rset)) {
            if (!write_to_stdout(conf))
                done = 1;
        }
    }

    if (close(conf->sd) < 0)
        err_msg(errno, "close(%d) failed", conf->sd);
    conf->sd = -1;

    if (!conf->closedByClient)
        display_connection_msg(conf, "terminated by peer");

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

    if (tcgetattr(fd, &term) < 0)
        err_msg(errno, "tcgetattr(%d) failed", fd);
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
        err_msg(errno, "tcgetattr(%d) failed", fd);
    return;
}


static void restore_tty_mode(int fd, struct termios *new)
{
    assert(fd >= 0);
    assert(new);

    if (tcsetattr(fd, TCSANOW, new) < 0)
        err_msg(errno, "tcgetattr(%d) failed", fd);
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
    char esc = conf->escapeChar;
    char c;
    int n;
    char buf[2];
    char *p = buf;

    while ((n = read(STDIN_FILENO, &c, 1)) < 0) {
        if (errno != EINTR)
            err_msg(errno, "read(%d) failed", conf->sd);
    }
    if (n == 0)
        return(0);

    if ((mode == EOL) && (c == esc)) {
        mode = ESC;
        return(1);
    }
    if (mode == ESC) {
        if (c == ESC_CHAR_CLOSE) {
            perform_close_esc(conf, c);
            mode = EOL;
            return(1);
        }
        if (c == ESC_CHAR_HELP) {
            perform_help_esc(conf, c);
            mode = EOL;
            return(1);
        }
        if (c == ESC_CHAR_SUSPEND) {
            perform_suspend_esc(conf, c);
            mode = EOL;
            return(1);
        }
        if (c != esc) {
            /*
             *  If the input was escape-someothercharacter, write both the
             *    escape character and the other character to the socket.
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
    if (conf->command == CONNECT) {
        if (write_n(conf->sd, buf, p - buf) < 0) {
            if (errno == EPIPE)
                return(0);
            err_msg(errno, "write(%d) failed", conf->sd);
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
    char buf[MAX_BUF_SIZE];
    int n;

    /*  Stdin has to be processed character-by-character to check for
     *    escape sequences.  For human input, this isn't too bad.
     */
    while ((n = read(conf->sd, buf, sizeof(buf))) < 0) {
        if (errno != EINTR)
            err_msg(errno, "read(%d) failed", conf->sd);
    }
    if (n > 0) {
        if (write_n(STDOUT_FILENO, buf, n) < 0)
            err_msg(errno, "write(%d) failed", STDOUT_FILENO);
        if (conf->ld >= 0)
            if (write_n(conf->ld, buf, n) < 0)
                err_msg(errno, "write(%d) failed", conf->ld);
    }
    return(n);
}


static void perform_close_esc(client_conf_t *conf, char c)
{
/*  Perform a client-initiated close.
 */
    locally_echo_esc(conf->escapeChar, c);

    display_connection_msg(conf, "closed");

    if (shutdown(conf->sd, SHUT_WR) < 0)
        err_msg(errno, "shutdown(%d) failed", conf->sd);

    conf->closedByClient = 1;
    return;
}


static void perform_help_esc(client_conf_t *conf, char c)
{
/*  Display the "escape sequence" help.
 */
    char escChar[3];
    char escClose[3];
    char escHelp[3];
    char escSuspend[3];
    char *str;

    locally_echo_esc(conf->escapeChar, c);

    str = write_esc_char(conf->escapeChar, escChar);
    assert((str - escChar) <= sizeof(escChar));
    str = write_esc_char(ESC_CHAR_CLOSE, escClose);
    assert((str - escClose) <= sizeof(escClose));
    str = write_esc_char(ESC_CHAR_HELP, escHelp);
    assert((str - escHelp) <= sizeof(escHelp));
    str = write_esc_char(ESC_CHAR_SUSPEND, escSuspend);
    assert((str - escSuspend) <= sizeof(escSuspend));

    str = create_fmt_string(
        "Supported ConMan Escape Sequences:\r\n"
        "  %2s%-2s -  Display this help message.\r\n"
        "  %2s%-2s -  Terminate the connection.\r\n"
        "  %2s%-2s -  Send the escape character by typing it twice.\r\n"
        "  %2s%-2s -  Suspend the client.\r\n"
        "(Note that escapes are only recognized immediately after newline)\r\n",
        escChar, escHelp, escChar, escClose, escChar, escChar,
        escChar, escSuspend);
    if (write_n(STDOUT_FILENO, str, strlen(str)) < 0)
        err_msg(errno, "write(%d) failed", STDOUT_FILENO);
    free(str);
    return;
}


static void perform_suspend_esc(client_conf_t *conf, char c)
{
/*  Suspend the client.  While suspended, anything sent by the server
 *    will be buffered by the network layer until the client is resumed.
 *    Once the network buffers are full, the server should overwrite
 *    data to this client in its circular write buffer.
 */
    locally_echo_esc(conf->escapeChar, c);

    display_connection_msg(conf, "suspended");
    restore_tty_mode(STDIN_FILENO, &conf->term);

    if (kill(getpid(), SIGTSTP) < 0)
        err_msg(errno, "Unable to suspend client (pid %d)", getpid());

    set_raw_tty_mode(STDIN_FILENO, &conf->term);
    display_connection_msg(conf, "resumed");

    return;
}


static void locally_echo_esc(char e, char c)
{
/*  Locally echo an escape character sequence on stdout.
 */
    char buf[6];
    char *p = buf;

    p = write_esc_char(e, p);
    p = write_esc_char(c, p);

    /*  Append a CR/LF since tty is in raw mode.
     */
    *p++ = '\r';
    *p++ = '\n';

    assert((p - buf) <= sizeof(buf));

    if (write_n(STDOUT_FILENO, buf, p - buf) < 0)
        err_msg(errno, "write(%d) failed", STDOUT_FILENO);
    return;
}


char * write_esc_char(char c, char *p)
{
/*  Writes the escape character (c) into the buffer pointed to by (p).
 *  Returns a ptr to the char following the escape char in the buffer.
 *  (cf. Stevens UNP p638).
 */
    assert(p);

    c &= 0177;

    /*  Echo ASCII ctrl-chars as a caret followed by the uppercase char.
     */
    if (c < 040) {
        *p++ = '^';
        *p++ = c + '@';
    }
    else if (c == 0177) {		/* ASCII DEL char */
        *p++ = '^';
        *p++ = '?';
    }
    else {
        *p++ = c;
    }

    *p = '\0';				/* DO NOT advance ptr here */
    return(p);
}


static void display_connection_msg(client_conf_t *conf, char *msg)
{
/*  Displays (msg) regarding the state of the console connection.
 */
    char buf[MAX_LINE];
    char *ptr = buf;
    int len = sizeof(buf);
    int n;
    int overflow = 0;

    assert ((conf->command == CONNECT) || (conf->command == MONITOR));

    if (!strcmp(msg, "terminated by peer")) {
        n = snprintf(ptr, len, "\r\n");
        if (n < 0 || n >= len)
            overflow = 1;
        else
            ptr += n, len -= n;
    }
    if (!overflow) {

        if (list_count(conf->consoles) == 1) {
            n = snprintf(ptr, len, "%s Connection to console [%s] %s.\r\n",
                CONMAN_MSG_PREFIX, (char *) list_peek(conf->consoles), msg);
        }
        else {
            n = snprintf(ptr, len, "%s Broadcast to %d consoles %s.\r\n",
                CONMAN_MSG_PREFIX, list_count(conf->consoles), msg);
        }
        if (n < 0 || n >= len)
            overflow = 1;
        else
            ptr += n, len -= n;
    }

    if (overflow) {
        ptr = buf + sizeof(buf) - 3;
        snprintf(ptr, 3, "\r\n");
    }
    if (write_n(STDOUT_FILENO, buf, strlen(buf)) < 0)
        err_msg(errno, "write(%d) failed", STDOUT_FILENO);
    return;
}
