/*****************************************************************************\
 *  $Id: server-serial.c,v 1.3 2002/03/14 03:37:00 dun Exp $
 *****************************************************************************
 *  Copyright (C) 2001-2002 The Regents of the University of California.
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *  UCRL-CODE-2002-009.
 *  
 *  This file is part of ConMan, a remote console management program.
 *  For details, see <http://www.llnl.gov/linux/conman.html>.
 *  
 *  ConMan was produced at the University of California, Lawrence Livermore
 *  National Laboratory (UC LLNL) under contract no. W-7405-ENG-48
 *  (Contract 48) between the U.S. Department of Energy (DOE) and The Regents
 *  of the University of California (University) for the operation of UC LLNL.
 *  The rights of the Federal Government are reserved under Contract 48
 *  subject to the restrictions agreed upon by the DOE and University as
 *  allowed under DOE Acquisition Letter 97-1.
 *  
 *  ConMan is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  ConMan is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with ConMan; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/


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


static int bps_to_int(speed_t bps);
static speed_t int_to_bps(int val);
static const char * parity_to_str(int parity);


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

    DPRINTF("Setting [%s] dev=%s to %d,%d%s%d.\n",
        serial->name, serial->aux.serial.dev, bps_to_int(opts->bps),
        opts->databits, parity_to_str(opts->parity), opts->stopbits);

    if (cfsetispeed(tty, opts->bps) < 0)
        err_msg(errno, "Unable to set [%s] input baud rate to %d",
            serial->name, opts->bps);
    if (cfsetospeed(tty, opts->bps) < 0)
        err_msg(errno, "Unable to set [%s] output baud rate to %d",
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
