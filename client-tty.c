/******************************************************************************\
 *  client-tty.c
 *    by Chris Dunlap <cdunlap@llnl.gov>
 *
 *  $Id: client-tty.c,v 1.1 2001/05/04 15:26:40 dun Exp $
\******************************************************************************/


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include "conman.h"
#include "client.h"
#include "errors.h"
#include "util.h"


static void set_raw_tty_mode(int fd, struct termios *bak);
static void restore_tty_mode(int fd, struct termios *new);
static int read_from_stdin(client_conf_t *conf);
static int write_to_stdout(client_conf_t *conf);
static void perform_help_esc(client_conf_t *conf, char c);
static void perform_close_esc(client_conf_t *conf, char c);
static void locally_echo_esc(char e, char c);


void connect_console(client_conf_t *conf)
{
    struct termios bak;
    fd_set rset, rsetBak;
    int done = 0;
    int n;

    assert(conf->sd >= 0);

    Signal(SIGHUP, SIG_IGN);
    Signal(SIGINT, SIG_IGN);
    Signal(SIGQUIT, SIG_IGN);
    Signal(SIGTERM, SIG_IGN);
    Signal(SIGPIPE, SIG_IGN);
    Signal(SIGTSTP, SIG_IGN);

    set_raw_tty_mode(STDIN_FILENO, &bak);

    FD_ZERO(&rsetBak);
    FD_SET(STDIN_FILENO, &rsetBak);
    FD_SET(conf->sd, &rsetBak);

    while (!done) {
        rset = rsetBak;
        while ((n = select(conf->sd+1, &rset, NULL, NULL, NULL)) < 0) {
            if (errno != EINTR)
                err_msg(errno, "select() failed");
        }
        if (n <= 0) {
            continue;
        }
        else if (FD_ISSET(STDIN_FILENO, &rset)) {
            if (read_from_stdin(conf) == 0)
                done = 1;
        }
        else if (FD_ISSET(conf->sd, &rset)) {
            if (write_to_stdout(conf) == 0)
                done = 1;
        }
    }

    if (close(conf->sd) < 0)
        err_msg(errno, "close(%d) failed", conf->sd);
    conf->sd = -1;

    restore_tty_mode(STDIN_FILENO, &bak);
    return;
}


static void set_raw_tty_mode(int fd, struct termios *bak)
{
    struct termios term;

    assert(fd >= 0);

    if (tcgetattr(fd, &term) < 0)
        err_msg(errno, "tcgetattr(%d) failed", fd);
    if (bak)
        *bak = term;

    /*  disable SIGINT on break, CR-to-NL, CR ignore, NL-to-CR, flow ctrl
     */
    term.c_iflag &= ~(BRKINT | ICRNL | IGNCR | INLCR | IXOFF | IXON);
    /*
     *  disable output processing
     */
    term.c_oflag &= ~(OPOST);
    /*
     *  disable echo, canonical mode, extended input processing, signal chars
     */
    term.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    /*
     *  read() does not return until data is present (may block indefinitely)
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
        if (c == '?') {
            perform_help_esc(conf, '?');
            mode = EOL;
            return(1);
        }
        if (c == '.') {
            perform_close_esc(conf, '.');
            mode = EOL;
            return(1);
        }
        if (c != esc) {
            /*
             *  If the input was escape-someothercharacter, write both the
             *    escape character and the other character to the socket.
             */
            if (write_n(conf->sd, &esc, 1) < 0)
                err_msg(errno, "write(%d) failed", conf->sd);
        }
    }

    /*  FIX_ME: What should happen if socket write()s gets EPIPE? */

    if (write_n(conf->sd, &c, 1) < 0)
        err_msg(errno, "write(%d) failed", conf->sd);
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

        /* FIX_ME: If logfile opt specified, write to logfile as well. */
    }
    return(n);
}


static void perform_help_esc(client_conf_t *conf, char c)
{
    int esc = conf->escapeChar;
    char *str;

    locally_echo_esc(esc, c);
    str = create_fmt_string(
        "Supported ConMan Escape Sequences:\n"
        "  %c? - Display this help message.\n"
        "  %c. - Terminate connection.\n"
        "  %c%c - Send the escape character by typing it twice.\n"
        "(Note that escapes are only recognized immediately after newline.)\n",
        esc, esc, esc, esc);
    if (write_n(STDOUT_FILENO, str, strlen(str)) < 0)
        err_msg(errno, "write(%d) failed", STDOUT_FILENO);
    free(str);
    return;
}


static void perform_close_esc(client_conf_t *conf, char c)
{
    int esc = conf->escapeChar;
    char *str;

    locally_echo_esc(esc, c);
    if (close(conf->sd) < 0)
        err_msg(errno, "close(%d) failed", conf->sd);
    conf->sd = -1;
    str = create_fmt_string("Connection to ConMan at %s closed.\n",
        conf->rhost);
    if (write_n(STDOUT_FILENO, str, strlen(str)) < 0)
        err_msg(errno, "write(%d) failed", STDOUT_FILENO);
    free(str);
    return;
}


static void locally_echo_esc(char e, char c)
{
/*  Locally echo an escape character on stdout (cf. Stevens UNP p638).
 */
    char buf[6];
    char *p = buf;

    /*  Echo the esc-char first.
     */
    *p++ = e;

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

    /*  Append a CR/LF since tty is in raw mode.
     */
    *p++ = '\r';
    *p++ = '\n';

    assert((p - buf) <= sizeof(buf));

    if (write_n(STDOUT_FILENO, buf, p - buf) < 0)
        err_msg(errno, "write(%d) failed", STDOUT_FILENO);
    return;
}
