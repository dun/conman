/******************************************************************************\
 *  $Id: server-tty.c,v 1.4 2001/12/14 07:43:04 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
\******************************************************************************/


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include "errors.h"
#include "server.h"


#define DEFAULT_SERIAL_BPS	B9600
#define DEFAULT_SERIAL_DATABITS	8
#define DEFAULT_SERIAL_PARITY	0
#define DEFAULT_SERIAL_STOPBITS	1


typedef struct serial_opts {
    speed_t bps;			/* cf. bps_table[] below */
    int     databits;			/* 5-8 */
    int     parity;			/* 0=NONE, 1=ODD, 2=EVEN */
    int     stopbits;			/* 1-2 */
} serial_opts_t;

typedef struct bps_tag {
    speed_t bps;
    int     val;
} bps_tag_t;


static bps_tag_t bps_table[] = {	/* values are in increasing order */
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
    {B38400,  38400},			/* end of the line for POSIX.1 bps's */
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
    {0,       0}			/* sentinel denotes end of array */
};


static void parse_serial_opts(serial_opts_t *opts, char *console, char *str);
static int bps_to_int(speed_t bps);
static speed_t int_to_bps(int val);
static const char * parity_to_str(int parity);


void set_serial_opts(struct termios *tty, obj_t *serial, char *str)
{
/*  Sets serial device options specified by the string 'str' for the
 *    'tty' terminal settings associated with the 'serial' object.
 *  Updates the 'tty' struct as appropriate.
 */
    serial_opts_t opts;

    assert(tty != NULL);
    assert(serial != NULL);
    assert(is_serial_obj(serial));

    opts.bps = DEFAULT_SERIAL_BPS;
    opts.databits = DEFAULT_SERIAL_DATABITS;
    opts.parity = DEFAULT_SERIAL_PARITY;
    opts.stopbits = DEFAULT_SERIAL_STOPBITS;

    parse_serial_opts(&opts, serial->name, str);
    assert(opts.bps > 0);
    assert((opts.databits >= 5) && (opts.databits <= 8));
    assert((opts.parity >= 0) && (opts.parity <= 2));
    assert((opts.stopbits >= 1) && (opts.stopbits <= 2));

    if (cfsetispeed(tty, opts.bps) < 0)
        err_msg(errno, "Unable to set [%s] input baud rate to %d",
            serial->name, opts.bps);
    if (cfsetospeed(tty, opts.bps) < 0)
        err_msg(errno, "Unable to set [%s] output baud rate to %d",
            serial->name, opts.bps);

    tty->c_cflag &= ~CSIZE;
    if (opts.databits == 5) {
        tty->c_cflag |= CS5;
    }
    else if (opts.databits == 6) {
        tty->c_cflag |= CS6;
    }
    else if (opts.databits == 7) {
        tty->c_cflag |= CS7;
    }
    else /* (opts.databits == 8) */ {	/* safe default in case value is bad */
        tty->c_cflag |= CS8;
    }

    if (opts.parity == 1) {
        tty->c_cflag |= (PARENB | PARODD);
    }
    else if (opts.parity == 2) {
        tty->c_cflag |= PARENB;
        tty->c_cflag &= ~PARODD;
    }
    else /* (opts.parity == 0) */ {	/* safe default in case value is bad */
        tty->c_cflag &= ~(PARENB | PARODD);
    }

    if (opts.stopbits == 2) {
        tty->c_cflag |= CSTOPB;
    }
    else /* (opts.stopbits == 1) */ {	/* safe default in case value is bad */
        tty->c_cflag &= ~CSTOPB;
    }

    DPRINTF("Setting [%s] dev=%s to %d,%d%s%d.\n",
        serial->name, serial->aux.serial.dev, bps_to_int(opts.bps),
        opts.databits, parity_to_str(opts.parity), opts.stopbits);
    return;
}


static void parse_serial_opts(serial_opts_t *opts, char *console, char *str)
{
/*  Parses 'str' for serial device 'opts' associated with 'console'.
 *  The expected string is of the form "<bps>,<databits><parity><stopbits>".
 *  Updates the 'opts' struct as appropriate.
 */
    int i;
    int n;
    int bps;
    int databits;
    char parity;
    int stopbits;

    assert(opts != NULL);
    assert(console != NULL);
    if (!str || !*str)			/* no string or empty string */
        return;

    n = sscanf(str, "%d,%d%c%d", &bps, &databits, &parity, &stopbits);

    if (n >= 1) {
        i = int_to_bps(bps);
        if (i <= 0)
            fprintf(stderr, "ERROR: expected INTEGER > 0 for [%s]"
                " bps setting.\n", console);
        else
            opts->bps = i;
    }
    if (n >= 2) {
        if ((databits < 5) || (databits > 8))
            fprintf(stderr, "ERROR: expected INTEGER 5-8 for [%s]"
                " databits setting.\n", console);
        else
            opts->databits = databits;
    }
    if (n >= 3) {
        switch(parity) {
        case 'N':
        case 'n':
            opts->parity = 0;
            break;
        case 'O':
        case 'o':
            opts->parity = 1;
            break;
        case 'E':
        case 'e':
            opts->parity = 2;
            break;
        default:
            fprintf(stderr, "ERROR: expected (N|O|E) for [%s]"
                " parity setting.\n", console);
            break;
        }
    }
    if (n >= 4) {
        if ((stopbits < 1) || (stopbits > 2))
            fprintf(stderr, "ERROR: expected INTEGER 1-2 for [%s]"
                " stopbits setting.\n", console);
        else
            opts->stopbits = stopbits;
    }
    return;
}


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
