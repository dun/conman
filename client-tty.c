/******************************************************************************\
 *  client-tty.c
 *    by Chris Dunlap <cdunlap@llnl.gov>
 *
 *  $Id: client-tty.c,v 1.3 2001/05/11 22:49:00 dun Exp $
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


#define ESC_CLOSE_CHAR	'.'
#define ESC_HELP_CHAR	'?'


static void exit_handler(int signum);
static void set_raw_tty_mode(int fd, struct termios *old);
static void restore_tty_mode(int fd, struct termios *new);
static int read_from_stdin(client_conf_t *conf);
static int write_to_stdout(client_conf_t *conf);
static void perform_help_esc(client_conf_t *conf, char c);
static void perform_close_esc(client_conf_t *conf, char c);
static void locally_echo_esc(char e, char c);


static int done = 0;


void connect_console(client_conf_t *conf)
{
    struct termios bak;
    fd_set rset, rsetBak;
    int n;

    assert(conf->sd >= 0);

    Signal(SIGHUP, SIG_IGN);
    Signal(SIGINT, SIG_IGN);
    Signal(SIGQUIT, SIG_IGN);
    Signal(SIGPIPE, SIG_IGN);
    Signal(SIGTSTP, SIG_IGN);
    Signal(SIGTERM, exit_handler);

    set_raw_tty_mode(STDIN_FILENO, &bak);

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

    if (shutdown(conf->sd, SHUT_RDWR) < 0)
        err_msg(errno, "shutdown(%d) failed", conf->sd);
    conf->sd = -1;

    restore_tty_mode(STDIN_FILENO, &bak);
    return;
}


static void exit_handler(int signum)
{
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

    DPRINTF("Setting raw tty mode.\n");
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
    DPRINTF("Restored cooked tty mode.\n");
    return;
}


static int read_from_stdin(client_conf_t *conf)
{
/*  Read from stdin and write to socket connection.
 *  Returns 1 if the read was successful,
 *    or 0 if the connection has been closed.
 *  Note that this routine can block in the write() to the socket.
 */
    static enum { CHR, EOL, ESC } mode = EOL;
    char esc = conf->escapeChar;
    char c;
    int n;

    while ((n = read(STDIN_FILENO, &c, 1)) < 0) {
        if (errno != EINTR)
            err_msg(errno, "read(%d) failed", conf->sd);
    }

    if ((mode == EOL) && (c == esc)) {
        mode = ESC;
        return(1);
    }
    if (mode == ESC) {
        if (c == ESC_HELP_CHAR) {
            perform_help_esc(conf, c);
            mode = EOL;
            return(1);
        }
        if (c == ESC_CLOSE_CHAR) {
            perform_close_esc(conf, c);
            mode = EOL;
            return(0);			/* fake an EOF to close connection */
        }
        if (c != esc) {
            /*
             *  If the input was escape-someothercharacter, write both the
             *    escape character and the other character to the socket.
             */
            if (write_n(conf->sd, &esc, 1) < 0) {
                if (errno == EPIPE)
                    return(0);
                err_msg(errno, "write(%d) failed", conf->sd);
            }
        }
    }

    /*  FIX_ME: Verify EPIPE behavior.  Output "conn term'd by peer"?
     */

    if (write_n(conf->sd, &c, 1) < 0) {
        if (errno == EPIPE)
            return(0);
        err_msg(errno, "write(%d) failed", conf->sd);
    }
    if ((c == '\r') || (c == '\n'))
        mode = EOL;
    else
        mode = CHR;

    return(n);
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


static void perform_help_esc(client_conf_t *conf, char c)
{
/*  Display the "escape sequence" help.
 */
    char esc[3];
    char escClose[3];
    char escHelp[3];
    char *str;

    locally_echo_esc(conf->escapeChar, c);

    str = write_esc_char(conf->escapeChar, esc);
    assert((str - esc) <= sizeof(esc));
    str = write_esc_char(ESC_HELP_CHAR, escHelp);
    assert((str - escHelp) <= sizeof(escHelp));
    str = write_esc_char(ESC_CLOSE_CHAR, escClose);
    assert((str - escClose) <= sizeof(escClose));

    /*  Append CR/LFs since tty is in raw mode.
     */
    str = create_fmt_string(
        "Supported ConMan Escape Sequences:\r\n"
        "  %2s%-2s -  Display this help message.\r\n"
        "  %2s%-2s -  Terminate the connection.\r\n"
        "  %2s%-2s -  Send the escape character by typing it twice.\r\n"
        "(Note that escapes are only recognized immediately after newline)\r\n",
        esc, escHelp, esc, escClose, esc, esc);
    if (write_n(STDOUT_FILENO, str, strlen(str)) < 0)
        err_msg(errno, "write(%d) failed", STDOUT_FILENO);
    free(str);
    return;
}


static void perform_close_esc(client_conf_t *conf, char c)
{
/*  Perform a client-initiated close.
 *  Note that this routine does not actually shutdown the socket connection.
 *    Instead, read_from_stdin() returns 0, thereby faking an EOF from read().
 *    This causes the connect_console() while-loop to terminate, after which
 *    the socket connection is shutdown.
 */
    char *str;

    locally_echo_esc(conf->escapeChar, c);

    /*  Append a CR/LF since tty is in raw mode.
     */
    str = create_fmt_string("Connection to ConMan [%s:%d] closed.\r\n",
        conf->dhost, conf->dport);
    if (write_n(STDOUT_FILENO, str, strlen(str)) < 0)
        err_msg(errno, "write(%d) failed", STDOUT_FILENO);
    free(str);
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
